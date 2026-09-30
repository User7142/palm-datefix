# Fix log

A chronological record of how DateFix was built: what was measured, what was concluded, what went wrong and how it was fixed. Each step corresponds to one or more commits.

Test devices:

- **Tungsten T3**, Palm OS 5.2.1, with palmOne's "T3 DIA Compatibility Update"
- **CloudpilotEmu** with the "Tungsten E3" ROM (Dmitry Grinberg's 320×480 build of the Tungsten E2, Palm OS 5.4)

---

## 1. The problem, measured

`DateTimeType` has a 16-bit year, but `DateType` stores the year in 7 bits counted from 1904 (1904…2031), and the clock counts seconds since 1904-01-01 in a `UInt32`. The SDK defines `maxSeconds = 0xF0C3EFFF` = 2031-12-31 23:59:59.

A small probe (`ClockProbe`, not part of this repository) on the T3, clock set to 2031-12-31 23:59:

| `TimGetSeconds()` | `maxSeconds +` | `TimSecondsToDateTime()` |
|---|---|---|
| `F0C3EFF7` | −8 | 2031-12-31 23:59:51 |
| `F0C3EFFF` | 0 | 2031-12-31 23:59:59 |
| `F0C3F002` | +3 | 2031-12-31 23:59:59 |
| `F0C3F00E` | +15 | 2031-12-31 23:59:59 |

**The clock keeps counting; only the conversion clamps.** A fix therefore only has to replace conversion functions – no stored data and no clock value has to change.

In the emulator (`DateProbe`): every value above `maxSeconds` up to `0xFFFFFFFF` converts to 2031-12-31 23:59:59, `TimDateTimeToSeconds(2032-01-01)` returns `0xF0C3F000` (linear, no clamp), and the date dialog (`SelectDay`) cannot go past 2031 (photo of the T3 "Datum einstellen" dialog stuck at December 2031).

## 2. Choosing the window: 1940–2067

The 7-bit year can only name 128 years. Moving the window keeps all values of the most likely existing data (1940…2031) bit for bit and gives the unused values 0…35 (1904…1939) the years 2032…2067. The user chose 1940–2067; dates before 1940 are not needed.

- **Days:** 1904 → 1940 = 13,149 days; 128 years = 46,752 days. 1904–1939 and 2032–2067 have the same leap years (every fourth year, no century in between), so month lengths are identical and only weekdays shift.
- **Seconds:** 1904 → 1940 = 1,136,073,600 s. A clock value below that is read as wrapped past 2³² = 49,710 days + 23,296 s (2040-02-06 06:28:16). The seconds window thus covers 1940-01-01 … 2076-02-06.
- **Years passed as numbers:** applications compute `date.year + firstYear`, which gives 1904…1939 for a 2032…2067 date. Every function taking a full year maps 1904…1939 to 2032…2067.

The arithmetic (`src/calendar.c`) uses H. Hinnant's days-from-civil algorithm, counted from 1600-03-01 so all intermediate values are positive, and has no tables, globals or OS calls. `make test` checks it on the host against Python's `datetime`: every day from 1940-01-01 to 2076-02-05 as a clock value and back, every possible `DateType` value, and weekday / day-of-month / days-in-month for 1890–2070. Result: 0 errors.

## 3. How Palm OS 5 reaches the date functions

On Palm OS 5 the system is native ARM code; 68k applications run in the PACE emulator. `SysGetTrapAddress` of a date trap returns a small 68k stub:

```
4E4F 07FE  <32-bit native address, little-endian>     TRAP #15 / call native
```

The native address is a Thumb shim that reads the 68k arguments, calls the real function, and converts the result back. Disassembled from the E3 ROM (`TimSecondsToDateTime`, trap `A0FC`):

```
2042c1be  push  {r0-r5, r7, lr}
          bl    read 68k argument 0 (seconds)
          bl    read 68k argument 1 (pointer)
          blx   0x20450d60                 ; the function
          bl    copy DateTimeType back to 68k
```

and `0x20450d60` is a veneer into a module export table:

```
20450d60  ldr  ip, [r9, #-8]      ; export table of module 1 (Boot)
20450d64  ldr  pc, [ip, #0x92c]   ; entry 587
```

This is the Palm OS 5 cross-module call convention: `r9` points below a list of export tables (`[r9-4]` DAL, `[r9-8]` Boot, `[r9-12]` UI). **Every** call into Boot from another module goes through that table – 68k applications via their shims, and native modules such as the status bar. The tables live in RAM (`0x1FF0xxxx` on the E3) and are writable; this is how Palm OS 5 system hacks work.

The date functions found this way (identical on the E3 5.4 and the T3 5.2.1 ROM):

| Trap | Function | Boot export | Replacement |
|---|---|---|---|
| A0FC | TimSecondsToDateTime | #587 | window arithmetic |
| A0FD | TimDateTimeToSeconds | #582 | window arithmetic |
| A0FE | TimAdjust | #581 | window arithmetic |
| A262 | DateDaysToDate | #52 | window arithmetic |
| A263 | DateToDays | #56 | window arithmetic |
| A264 | DateAdjust | #51 | window arithmetic |
| A25F | DayOfWeek | #59 | window arithmetic |
| A261 | DayOfMonth | #58 | window arithmetic |
| A260 | DaysInMonth | #60 | window arithmetic |
| A266 | DateToAscii | #55 | year mapped, original called |
| A267 | DateToDOWDMFormat | #57 | year mapped, original called |
| A3CD | DateTemplateToAscii | #54 | year mapped, original called |

`DateSecondsToDate` (A265) has no export of its own: its shim calls `TimSecondsToDateTime` (#587) and packs the result – covered by the entry above.

The formatting functions depend on the locale (month names, formats), so DateFix keeps them and only moves the year argument into the window before calling the original.

## 4. Design

- **No ROM-specific numbers.** `FindExport()` (`src/datefix.c`) follows trap → stub → shim → veneer at run time and accepts a function only if the stub bytes, the Thumb `BLX` and both veneer instructions match exactly, and only if the shim calls exactly one veneer. If any function cannot be found, nothing is patched.
- **Native code in a locked resource.** The replacements are the `armc` resource. It must work at whatever address the resource ends up: the build links it at two addresses and requires identical binaries, and `tools/check_sections.py` rejects anything besides `.text`.
- **Originals for the wrappers** live in a four-word table inside the code (`DfOrigTable`, reached PC-relative); the 68k side fills it with `DmWrite` before switching the export table over.
- **Lifetime.** Export tables are rebuilt on reset. DateFix reinstalls on `sysAppLaunchCmdSystemReset` while enabled, keeps the resource locked, protects its database against deletion while active, and stores the original entries in features (which also end with the reset).
- **Fail-safe.** "Enabled" is only saved after a successful install. A no-notify reset (navigator up) skips DateFix entirely.

## 5. Problems on the way

### 5.1 Fatal Exception when enabling (emulator)

Enabling, and even the read-only *Patch table* view, ended in a *Fatal Exception*. Narrowed down with on-screen trace lines:

1. all 12 functions were found (68k side fine),
2. a trivial native function returned `0x1234` (calling native code fine),
3. the installer crashed even with an empty request.

The installer read the request from the 68k stack with 32-bit `ldr`. 68k data is only 2-byte aligned, and Palm OS 5 raises a fatal exception on unaligned word loads (an earlier probe had worked only because its buffer happened to be 4-byte aligned). The request is now read and written byte by byte, explicitly big-endian (`GetBE32` / `PutBE32` in `src/datefix_arm.c`).

Also found while reading the generated code:

- The write verification (`table[index] != value` right after `table[index] = value`) had been optimised away; the table pointer is `volatile` now.
- The APCS frame (`stmdb …, pc` / `ldmdb fp, {…, sp}`) turned out not to be the cause but is unnecessary for this code; it is built with `-mno-apcs-frame -fomit-frame-pointer`.

### 5.2 Self test "DateToAscii 2045"

The first self test failed only here. Not a DateFix error: `dfYMDWithDashes` prints a two-digit year ("45-06-15"). The test now checks a long format ending in "2045" and the short format starting with "45".

### 5.3 The emulator's clock follows the host

`TimSetSeconds` in CloudpilotEmu is overridden by the host clock within seconds, so the transition past 2031 can only be watched on a real device.

## 6. Verification on the Tungsten T3 (Palm OS 5.2.1)

- *Patch table*: all 12 functions found, same export numbers as on the E3 (Boot code at `0x2008xxxx`/`0x2009xxxx`).
- Self test passed.
- "New Year" (clock set to 2031-12-31 23:59:29): the status bar went from 23:59 to **0:00** and kept running; the status popup (native) showed **"1. Jan 2032"**; ClockProbe read `F0C3F000` = 2032-1-1 0:00:00, then 0:00:06.
- In the Calendar (emulator), the week arrow moves from 2031-12-31 to **Wed 7 Jan 2032** with the correct weekday.

## 7. Crash on reset while enabled

A reset with DateFix enabled ended in a *Fatal Exception* while the Palm was booting (found in the emulator before it happened on the T3; the no-notify / "No extensions" boot recovered it).

On `sysAppLaunchCmdSystemReset` an application runs **without globals**: register A5 points nowhere. The function table `targets[]` was `static const`, but it contains pointers (the function names), and prc-tools keeps initialised data – even `const` – in the globals. `Install()` read it via `lea %a5@(-240)` and crashed.

- The table is now a `switch` in `GetTarget()` that fills a stack variable with immediate values; the names are gone (the patch table view shows trap numbers).
- The reset launch passes no message buffer to `Install()`.
- `tools/check_reset_path.py` runs on every build: it follows all calls from `PilotMain` (except the UI) in the 68k disassembly and fails on any A5-relative access. Built against the old source it reports exactly `Install: lea %a5@(-240)`.

Verified in the emulator: enable, normal reset, boots cleanly, DateFix active again, self test passed.

## 8. A test emulator with a real clock

CloudpilotEmu (Tungsten E3 ROM) cannot test anything that sets the clock: its PXA real-time clock returns the host time and ignores writes to the counter register (`RCNR`). `TimSetSeconds` therefore had no lasting effect, and a DateFix with a moved epoch saw the host date as its internal date.

`tools/emulator/` patches the emulator: the counter keeps an offset to the host time, set when the OS writes it, used for reads and for the alarm, and stored in the session (savestate chunk version 2). `build.sh` builds the web app with the same Emscripten version as CloudpilotEmu's CI (6.0.0) and serves it locally.

Verified with the patched emulator: clock set to 2031-12-31 23:59:55, ten seconds later it reads `maxSeconds + 5` – the same behaviour as measured on the Tungsten T3.

## 9. Stored dates move with the epoch

With the epoch at *S*, a stored `DateType` year counts from *S*. Data written before DateFix counts from 1904, so without a conversion every entry would appear *S* − 1904 years later. `src/convert.c` knows the record formats that hold dates (palmOne PIM SDK `DateDB.h`, `ToDoDB.h`, `AddressDB.h`, application note AN-4 *Accessing PIM Databases*):

| Database | Dates |
|---|---|
| Datebook `date`, Calendar `PDat` | date, repeat end, every exception (fixed fields before the strings; blobs hold none) |
| To Do `todo` | due date |
| Tasks `PTod` | due, completion, repeat start, repeat end |
| Contacts `PAdd` | birthday (after the strings), anniversary (blob `Bd01`, after its dirty word) |
| Expense `exps` | record date |
| every RAM database | creation, modification, backup date (seconds) |

Only the year changes: *S* − 1904 is a multiple of 4, so month and day stay valid (29 February included). "No date" (`0xFFFF`, month 0) stays. A date before *S* keeps month and day in the year *S* (counted and reported: birthdays before 1940 with *S* = 1940). A date after *S* + 127 cannot be stored in the 1904 epoch: **Disable** then refuses and names the number.

Flow on Enable/Disable: count (dry run) → ask → copy the affected databases to `/PALM/DateFix` on the first card (`VFSExportDatabaseToFile`; without a card the dialog says to HotSync first) → convert (every record in a copy, written back in one `DmWrite`, not marked dirty so a HotSync does not push the epoch change to the desktop) → install/uninstall → move the clock. A flag in the preferences marks a running conversion; after a reset in the middle the next launch says so and points to the backup. Enable now installs the patch *before* moving the clock: applications that recompute their alarms on the time change already get the converted data and the real weekday.

Host test (`make test`, `tests/convert_test.c`): records built byte by byte as the applications store them, every field checked, round trip, malformed/clamped/overflow counters.

Emulator (Tungsten E3, 1904 epoch): weekly Calendar event on Wed 2026-09-30 10:00 with a 5-minute alarm, ending Wed 2026-12-16, and an Expense entry of the same day. After Enable (1940): agenda, day view, event details and repeat end show the same real dates and weekdays; Disable restores the original values; Enable again – same result.

## 10. The year shifted twice in the event details

The event details showed **"Sat 9/30/62"** for Wed 2026-09-30. The original `DateToDOWDMFormat` formats through `DateTemplateToAscii`, and that call goes through the patched export table as well: the wrapper added the offset a second time (2026 + 36 = 2062, a Saturday). The weekday already had this guard (section 5), the year did not. `RealYear()` now leaves the year alone while a formatting call is running; the self test also checks `DateToDOWDMFormat`.

## 11. The date picker (open)

With DateFix active the system's `SelectDay` shows the internal year (1990 for 2026) and labels its weekday columns with arithmetic of its own: the header read *W T F S S M T* while the days sat in the right columns. It cannot be fixed from outside; it has to be replaced.

`src/selectday.c` is a complete replacement (same interface and semantics, `selectDayByDay/Week/Month`, real years, header and grid both from the patched `DayOfWeek`, locale month and day names, no globals, checked by `tools/check_reset_path.py`). It is installed over the 68k trap with `SysSetTrapAddress`.

**Palm OS 5 refuses that**: `SysSetTrapAddress(sysTrapSelectDay, …)` returns `sysErrNotAllowed` – PACE lets only a few traps be replaced. DateFix therefore installs the picker only where the call succeeds (Palm OS 3.x/4.x, not yet tested) and keeps the system picker on Palm OS 5. Next approach for Palm OS 5: the 68k trap entry points to a PACE stub (`4E4F 07FE` + native address, section 3); pointing that native address at a small ARM shim that calls the 68k picker through PACE's `call68KFuncP` would reach every 68k caller.

### 11.1 Attempt: overwrite the PACE stub (not shipped)

`SysGetTrapAddress(sysTrapSelectDay)` returns the PACE stub (`4E4F 07FE` + native address, 8 bytes in RAM at `0x006F20FC` on the E3). Idea: overwrite those 8 bytes with `JMP DfSelectDay` (`4EF9` + address) and a `NOP`, framed by `MemSemaphoreReserve(true)` / `MemSemaphoreRelease(true)`, original bytes kept in features – PACE would run the stub as 68k code and every 68k caller would land in the picker with a normal trap stack.

Result in the emulator: after Enable the Calendar still opened the system picker, and shortly afterwards CloudpilotEmu reported *Timeout while saving state* repeatedly – the emulated device stopped responding (it did not happen with any other build). The cause is not known yet (candidates: `MemSemaphoreReserve` under PACE, the stub region not being plain RAM, PACE caching decoded stubs). A change that can freeze a device is not shipped: the code was reverted, the attempt is kept as `docs/attempts/selectday-pace-stub.diff`. Next step: a probe that only reads the stub region's heap/flags and writes one byte to a copy, to find out what PACE does with it, before touching the live stub again.

## 12. Clock check in the main form (2.0d4)

The separate ClockProbe showed the raw clock and the *internal* date – after New Year's Eve 2031 "1996-1-1", correct for the epoch 1940 but confusing. DateFix's main form now shows a live clock check (every half second): what Palm OS shows through the (patched) system functions, what DateFix computes on its own from the raw clock value with the calendar arithmetic of `calendar.c` (compiled for 68k as well, `src/clockcheck.c`), `OK`/`MISMATCH`, and the raw clock with the internal date. "New Year" now sets 23:59:50.

Emulator (E3, start year 1940): 2031-12-31 23:59:51 → "Wed 31 Dec 2031 / Wed 2031-12-31 OK / 1995-12-31 internal", 00:00:02 → "Thu 1 Jan 2032 / Thu 2032-01-01 OK / 1996-1-1 internal". The 5-minute alarm of the weekly Calendar event (converted there and back several times) fired on time at 9:55 with "Wednesday, 9/30/26".

## 13. Updating DateFix while it is active (2.0d5)

While DateFix is active it protects its own database (the patched export table points into its code), so HotSync cannot replace it (`dlpRespErrAlreadyExists`, seen on the T3). Disable was the only way out – it converts all data back, and it is refused after 2031.

- **Options → Prepare Update** takes the patch out and lifts the protection, but keeps the converted data, the clock and the "enabled" preference. The new version switches itself on again on `sysAppLaunchCmdSyncNotify` (sent to an application right after a HotSync installed it), and on its next normal launch.
- **New Year → Back**: "New Year" remembers the clock; the button then reads "Back" and returns to that time plus the seconds that have passed. Enable/Disable forget it (other epoch).
- The status line says "Paused for update" while the patch is out; the clock check shows MISMATCH then.

Emulator: Prepare Update → MISMATCH (Palm OS shows the internal year), new build installed over it, launched → active again, OK. New Year → Back returned to the previous time. The SyncNotify path needs a real HotSync (not tested yet).

### 11.2 The date picker through the export tables (2.0d6, works)

Seen on the T3 (2.0d5): with the clock on 2032-01-01, Calendar → *Go To* opened on "1996", the internal year. The trap route is closed on Palm OS 5, but the mechanism that patches the date functions works for the picker too:

- The picker is native (UI export, trap `A2D0`). DateFix replaces that export with `DfSelectDayNative`, which only raises a *picker scope* counter (second word of the state block in the dynamic heap) around the original call.
- The picker draws its year with `StrIToA` (a Boot export, trap `A0C9`, called across modules, so it goes through the table). `DfStrIToA` adds the epoch offset to a value between 1904 and 2031 while the scope counter is set. The picker itself keeps working with internal years, so what it returns is what applications store.
- Its weekday header is named from a fixed reference date that the picker takes for a Sunday, using the *internal* calendar; the grid uses `DayOfWeek`, which DateFix patches. With the year shifted, the header started on Wednesday (the shift is 13149 days = 3 weekdays). Inside the scope the formatting wrappers therefore leave the year alone (`RealYear` checks the scope) – header and grid agree.
- `SelectDay`'s shim reaches its veneer through a shared Thumb core (one `bl` deeper), so `FindExport` now follows one `bl` when the shim itself calls no veneer. The six original functions are still found the way they were (depth 0 first). The two picker entries are optional: if either cannot be found, only the six are patched and the *Test* line says which trap was missing.
- `ftrTargetCount` records how many entries were patched, so Uninstall restores exactly those.

Emulator (E3, epoch 1940; its clock stands in 2062 after the conversion): *Go To* shows "2062" instead of 2026 and the header reads S M T W T F S with the 30th under S and the 1st under F; the year arrow, the month buttons and a tap on a day return the right date to the Calendar (Sep 9, 2063 with Sunday highlighted). Test line: "Self test passed, date picker fixed". The T3 must confirm (same export layout expected).

The 68k picker of §11 (`selectday.c`) stays for Palm OS 3.x/4.x, where the trap route works.

### 11.3 Tasks: due date picker opens on 1940 (2.0d7: diagnostics)

Seen on the T3 (2.0d6, clock 2032-01-01): Tasks → entry → due date → the picker opens on "Jan 1940", i.e. it was called with the internal year 1904 (+36). Calendar's picker opens on today. The Tasks application (not in the emulator's ROM) presumably passes a zero date for "no due date" instead of today. What it really passes was not known, so the main form now shows the last picker call – the date handed in and the date returned, both internal, in the fourth line of the clock check: `Picker: 2026-9-30 > 2026-9-6` (emulator, Calendar). To be read on the T3 right after opening the Tasks picker and cancelling it.

Implementation notes: the shim passes the picker native (little-endian) Int16 values behind its pointers; the log words are little-endian ARM words and are swapped on the 68k side.

### 11.4 Tasks: what the T3 does (2.0d9: Show Trace)

2.0d8 on the T3 (clock 2031-01-02 after a reset): *Self test passed, picker ok* - `DateSecondsToDate` returns the right "today" there. The picker line still said `1904-1-1 > 1904-1-1` when Tasks opened its due date picker, so Tasks hands the picker Jan 1 1904 on its own; DateFix's date functions are not what produces it.

To see what Tasks does before that call, the native code now keeps the last 12 calls of `TimSecondsToDateTime`, `DayOfWeek` and `DayOfMonth` (argument and result) and copies the six newest when the picker is called. *Options -> Show Trace* lists them, newest first (`Sec <seconds> > y-m-d`, `DoW y-m-d > weekday`, `DoM ...`). On the emulator's Calendar: five `Sec ... > 1996-1-1` - the Calendar asks the time functions for "today" and gets the internal date. If the Tasks trace shows no `Sec` call, Tasks builds its default without the system's time functions.

### 11.5 Tasks: resolved - not a DateFix bug

Tasks opens the due date picker on "Jan 1940" for an *old* task without a concrete due date, but on today's date for a *new* task (confirmed on the T3, 2.0d9, Trace: the application gets the right "today" from `TimSecondsToDateTime`, `Sec ... > 1990-9-30`). A record without a due date hands the picker the zero date, which is the first day of the window: 1940-01-01 with the epoch 1940 (1904-01-01 in the internal calendar). Nothing to fix; picking a date or creating the task new sets a normal date. Show Trace and the picker line stay in the app as diagnostics.

## 14. Enable checks the device before it converts anything (2.0d10)

On a Palm m515 (Palm OS 4.1) *Enable* converted the stored dates first and only then found that the patch cannot be installed ("Palm OS 5 required"); the undo converted them back, but dates before the start year (birthdays before 1940 …) had been clamped to it for good. `CanInstall()` now runs first: Palm OS 5 and every required export found. Only then the dates are counted, the user is asked and the databases are converted. On a device where DateFix cannot work, nothing is touched.

Emulator: Disable → Enable cycle with converted data works as before.

## 15. Palm OS 3.5 .. 4.x: the 68k route (2.0d11, m515 in the emulator)

Below Palm OS 5 the date functions are 68k code and their traps are replaced directly (`SysSetTrapAddress`): `src/m68k.c` holds the six wrappers (seconds→date with the real weekday, `DayOfWeek`, `DayOfMonth`, and the three formatting functions with the same depth guard as the native code), `Install68k()` the installation. The configuration (offsets, guard, the original entry points) lives in a dynamic-heap chunk owned by the system and referenced by a feature, because these functions run in other applications (no globals, no literals: `tools/check_reset_path.py` follows them). `Uninstall68k()` puts the traps back, frees the chunk and unlocks the code. The database is protected while active, like on Palm OS 5. The SD backup needs VFS Manager 2 (`vfsMgrVersionNum`); on older expansion support the dialog says to HotSync first. CanInstall() accepts 3.5 … 4.x without a scan; below 3.5 (no `DateTemplateToAscii`) it still refuses.

Emulator: CloudpilotEmu with the **Palm m515** ROM (`Palm-m515-4.1-en.rom`, from PalmDB – a device the author owns; 4.1, Dragonball VZ). A weekly Datebook event with an alarm and an end date, created in the 1904 epoch, was converted (2 dates, no card = no backup), DateFix enabled: Self test passed, clock check "Wed 30 Sep 2026 … OK" with the internal date 1990-9-30, Datebook shows the event on the same day with the alarm and repeat symbols. The emulator's Dragonball RTC is not patched like the PXA RTC of the E3, so the New Year rollover itself is a device test.

The date picker is the 68k `selectday.c`: `Install68k()` calls `InstallPicker()`, and on Palm OS 4 the trap route works (unlike on Palm OS 5). Go To shows the real year (2026 for the internal 1990), the year arrow goes on to 2032, and September 2032 has the right weekdays (30 Sep 2032 = Thursday); a tap on the day returns it to the Datebook ("Sep 30, 32", Thursday highlighted). The weekly event (Wed 30 Sep 2026 10:00, ending 16 Dec 2026) is on Dec 16 and gone on Dec 23: the converted repeat end is right.

Still open on Palm OS 4: Tasks/To Do and Contacts on the device, the clock rollover on the m515 (the Dragonball RTC of the emulator cannot be set like the E3's), the alarm, the SD backup path on a real card.

## 16. Launcher icon family (2.0d13)

A calendar page with the year 2032 (blue header, silver binder rings), in every size the launcher knows:

| Where | What |
|---|---|
| Palm OS 3.5 .. 4, 160x160 | 32x22 and 15x9 in 1 bpp (drawn by hand, `tools/make_icon.py`), 4 bpp grey and 8 bpp colour (derived from the vector drawing) |
| Palm OS 5, double density (320x320, 320x480) | 64x44 and 30x18 in 8 bpp and 16 bpp colour; the large one with a green "works beyond 2031" badge |

`tools/icon/render.swift` draws the vector version with CoreGraphics (shadow, gradients, a real typeface at double density, pixel digits at low density); `tools/icon/convert.py` writes the BMPs (`src/icon/`, committed, so building needs no Swift). `ICONFAMILYEX` / `SMALLICONFAMILYEX` in `datefix.rcp`; pilrc 3.2 accepts 1/4/8/16 bpp and densities, not 2 bpp (Palm OS falls back to the 1 bpp icon on 2-bit grey screens).

Emulator: m515 (OS 4.1): colour icon in the launcher after switching the category; E3 (OS 5.4, double density): the 64x44 colour icon with the badge after a soft reset - the Palm OS 5 launcher caches the icon of an installed application until the next reset.

Update 2.0d14 (after the T3 and m515 photos): at double density the year moved right (x 12.5 instead of 7 in the 64x44 grid) so the "2" no longer touches the page border; the badge covers the last "2" by a few pixels. The low-density sizes got the badge as hand-placed pixel art (7x7 disc, white check, white halo clearing the page lines; `pixelBadge` in render.swift, `badge()` in make_icon.py) - green in colour, black with a white check in 1 bpp.

Update 2.0d15: at double density the year is smaller (14 instead of 17 pt) and ends before the badge instead of under it - the proportions of the Palm OS 4 icon (digits about 60 % of the page width, badge beside them).

Update 2.0d16: the year is centred on the page at double density (x from the measured text width, baseline a little above the middle of the white field); the badge touches the last digit at the corner.

## 17. Disable with entries beyond the old window (2.0d17, 2.0d18)

Seen on the T3 (2.0d16): *Disable* showed "2 dates are after 2031 and cannot be stored with the start year 1904" and offered no way on - the dialog had only OK. Going back to the 1904 epoch moves every stored year 36 years earlier in the calendar, so an entry in 2032 or later (an appointment, a task, a birthday with a year) has no place in it.

2.0d17 asks instead of refusing: *"N dates in <database> are after 31 Dec 2031 and cannot be kept with the start year 1904. Set them to 31 Dec 2031 and go on?"* with "Set & go on" / "Cancel"; on OK those dates become 31 December of the last year (`ConvStats.limitOverflow`, host-tested: `make test`), the result message says how many. Cancel leaves everything as it was (no error alert). The two limits are different things: the **clock** must lie in the old window (else "The date is after 2031. Use Prepare Update for a new version."), entries beyond it may be sacrificed after asking. Prepare Update stays the way to replace DateFix without converting anything back.

Emulator note: the m515/E3 sessions share neither the clock nor a writable RTC like the E3's patched one, so the test state (clock in the 1940 window, an entry in 2032) could not be rebuilt reliably there; the dialog itself is exercised by the host test only. The T3 must confirm.

2.0d18: the *Clock check* block moved down one line so the status line ("Self test passed, picker ok") has its own row.

## 18. Date Book+ (Handspring Visor, 2.0d21)

Seen on a Handspring Visor (Palm OS 3.5.2H3): the built-in Date Book showed "Jan 2032", **Date Book+** ("Kalender+", Pimlico DateBk3 in the Visor ROM, `DateBk3h` / creator `HsDB`) showed "1. Jan 96".

Reproduced in the m515 emulator with the application taken from the Visor Deluxe ROM (`~/tools/palm-apps/DateBk3h-from-ROM.prc`, rebuilt from the ROM's resource database: localIDs minus the ROM base 0x10C00000, sizes from the chunk headers; the PalmDB download is gone). The 68k route records the newest formatting calls (*Show Trace*, `M68kConfig.trace`; recording is off while DateFix itself runs so its redraws do not fill the ring): Date Book+ calls `DateToAscii` / `DateTemplateToAscii` with **26**, not 2026 - it hands the system only the last two digits of the year. DateFix mapped only 1904 … 2031, so "26" passed unchanged.

The fix is exact, no guessing: real = internal + offset, so the last two digits of the real year are `(y + offset) mod 100` whatever the century (26 + 36 -> 62, 96 + 36 -> 32). `RealYear()` does that for values below 100, in the native code (Palm OS 5) and in `m68k.c` (3.5 .. 4). Emulator m515: "Sep 30, 26" became "Sep 30, 62"; the weekday highlight was right before and after.

The standard Datebook database is converted like before. The extra database of Date Book+ (`Datebk3HDB`) is not converted (format unknown); entries created in Date Book+ live in the standard database as far as known - to be confirmed on the Visor.

### 18.1 Which Date Book+ views are right (Visor photos and emulator)

| View | Title | Via |
|---|---|---|
| Day | "1. Jan 32" / "Sep 30, 62" | `DateToAscii` (two-digit year) - fixed in 2.0d21 |
| Month | "Januar 2032" / "September 2062" | `DateToAscii` (full year) - right |
| Week | "Dez 95 - Jan 96" / "Sep 26" | the application draws the year itself (`StrIToA`/`StrPrintF` of the two-digit year) - **shows the internal year** |
| Two weeks | "Sep-Oct 26" | same |
| Year | "1996" / "2026" | the application draws the four-digit year itself - **internal year** |

The week and year views never call a date formatting function (checked with *Show Trace*), so there is nothing DateFix can convert at the API level; a global hook on `StrIToA` would rewrite every number between 1904 and 2031 (and cannot tell the two-digit year 26 from day 26).

**Decision (2026-09-30): a patch table per application version, after the first release.** For a known version (first: Date Book+ 3.0H, code resources 1-4 of `DateBk3h`) DateFix would know the exact call sites that draw a year (caller address inside the application's code resource -> offset) and convert only there: no side effects, but work per application and version; the table is meant to be extended by the community. Sketch: a `StrIToA` wrapper (68k route) that looks at `__builtin_return_address(0)`, finds the code resource of the running application (`SysCurAppDatabase`) and compares offset and value with the table; the call sites are found by recording distinct callers of `StrIToA` per view (value, caller, count) in the trace ring. Until then: documented limitation, the standard Date Book is right in every view.
