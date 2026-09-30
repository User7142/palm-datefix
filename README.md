# DateFix

**Keep your Palm working after 31 December 2031.**

Palm OS stops its calendar on **31 December 2031, 23:59:59**. After that the clock resets, the Calendar cannot go forward, and appointments in 2032 crash applications. DateFix moves the start of Palm OS's date counting from 1904 to a year you choose (default **1940**), so a Palm keeps a real, working calendar and clock for another 128 years (**1940 – 2067**).

> **Status: beta.** Verified on a Tungsten T3, a Palm m515 and in emulators; see [Tested devices](#tested-devices) and [Known limitations](#known-limitations). Please read [Before you start](#before-you-start).

**Download:** [DateFix-2.0.0-beta.1.prc](https://github.com/User7142/palm-datefix/releases/latest) &nbsp;|&nbsp; **Article** with photos, the measurements and every dead end: [DateFix: Keeping a Palm Alive After December 31st, 2031](https://palm2000.com/articles/49)

## What makes it different

Palm OS stores a date in a 7-bit year (1904 + 0…127) and counts the clock in seconds since 1 January 1904. The hardware clock keeps running past 2031, only the conversion to a date stops. A fix that merely changes what is *displayed* leaves every application that does its own date arithmetic broken.

DateFix changes the *epoch* instead, as if the Palm had been shipped with the counter starting in 1940:

- the clock counts from the start year, so it stays below the limit for 128 years and **survives a reset**;
- stored dates (appointments, tasks, birthdays, expenses) are **converted once**, with a backup;
- applications see an ordinary, gap-free calendar of "internal" years 1904…2031. Sorting, date comparisons, repeating events and alarms work without touching any application;
- only what differs from reality is patched: the **weekday** and the **year shown to the user**.

The start year can be any of 1904, 1908, … 1972 (steps of four, so leap years stay aligned). The default 1940 gives 1940 – 2067.

## Before you start

- **Make a HotSync backup** and, if the device has a card slot, **insert an SD card**: DateFix copies the databases it is about to change to `/PALM/DateFix` on the card first.
- Enabling **converts your stored dates** (every year moves by *start year − 1904*). Dates before the start year cannot be represented and are set to its first year (DateFix tells you how many). **Birthdays or appointments before 1940 are affected.**
- **Emergency exit:** a soft reset while holding the navigator **up** button (a "no notify" reset) starts the Palm without DateFix. Then open DateFix and tap **Disable**, or delete it.
- Palm OS **below 3.5** is not supported. DateFix checks this *before* it converts anything.

## Usage

1. Install `DateFix.prc` (HotSync or card).
2. Start DateFix, choose the **start year** and tap **Enable**. DateFix counts the dates to convert and asks; tap **Convert**.
3. **Test** runs a self test. The **Clock check** under the buttons shows what Palm OS displays next to DateFix's own calculation, with `OK` or `MISMATCH`.
4. DateFix installs itself again after every reset while it is enabled. You can leave it alone from now on.

| Button / menu | What it does |
|---|---|
| **Enable** / **Disable** | Enable: convert data, install the patch, move the clock. Disable: the same in reverse. Disable is refused while the clock is after 2031; entries after 2031 are set to 31 Dec 2031 *after you confirm*. |
| **Test** | Self test of the patched functions. |
| **New Year** / **Back** | Sets the clock to 31 Dec 2031 23:59:50 to watch the rollover; **Back** returns to the real time. |
| *Options → Prepare Update* | Takes the patch out without converting anything back, so HotSync can replace DateFix with a new version. The new version switches itself on again after the HotSync. **Use this instead of Disable for updates.** |
| *Options → Show Trace* | Shows what applications passed to the date functions. For bug reports. |

## What is converted

Stored dates of the built-in applications are moved with the epoch, one record at a time, in a copy that is written back in one piece (records are not marked as changed, so the next HotSync does not re-send them):

| Application | Converted |
|---|---|
| Date Book / Calendar | date, repeat end date, exceptions |
| To Do / Tasks | due date, completion date, repeat start and end |
| Address / Contacts | birthday and anniversary |
| Expense | date |
| all databases in RAM | creation, modification and backup date |

The original databases are **not** touched on the card; `/PALM/DateFix` holds copies made *before* converting. A reset in the middle of a conversion is reported at the next start.

## Tested devices

| Device | Palm OS | Result |
|---|---|---|
| Tungsten T3 | 5.2.1 | works: rollover 2031 → 2032, Calendar both ways, date picker, reset |
| Palm m515 | 4.1 | works: conversion, soft reset keeps the patch |
| Tungsten E2 (emulator) | 5.4 | works |
| Palm m515 (emulator) | 4.1 | works |
| Handspring Visor | 3.5.2H3 | Date Book works; Date Book+ partly (see limitations) |

**Help wanted:** T5, TX, LifeDrive, Zire, Clié and everything else is *expected* to work (the export table entries are located by decoding the ROM, nothing is hard-coded; DateFix patches nothing if it cannot find them), but it is not confirmed. If you try it, please report: device, Palm OS version, the self test line, the clock check line and *Show Trace*.

## Known limitations

- **Applications that draw the year themselves** (not through the system's date functions) show the *internal* year. Date Book+ (Handspring Visor, Pimlico DateBk3): day and month view are right, **week, two-week and year view show the internal year** (e.g. 1996 instead of 2032). The plan is a table of call sites per application version; the standard Date Book is right in every view.
- **HotSync with a desktop** transfers the internal dates. Conduits and the desktop software know nothing about the moved epoch.
- The window is **128 years** (the year field has 7 bits). Dates outside it cannot be stored.
- Databases of other applications (Date Book+'s own database `Datebk3HDB`, third-party calendars) are not converted; their record formats are not known.
- Palm OS 3.5 and 4.x: the date picker is replaced by DateFix's own (`SelectDay` is patched through the trap table). On Palm OS 5 the system picker is patched instead (see [fix log](docs/fix-log.md), sections 11 and 15).
- Not supported: Palm OS below 3.5 (Palm III with 3.0/3.3, Palm V with 3.1).

## How it works

On **Palm OS 5** the system is native ARM code and 68k applications run in the PACE emulator. A 68k trap leads to a small stub, through a shim, to a **veneer that reads the function from the export table of a system module** in RAM. Every caller - 68k applications and native modules - goes through that table, so DateFix replaces the entries (`TimSecondsToDateTime`, `DayOfWeek`, `DayOfMonth`, `DateToAscii`, `DateToDOWDMFormat`, `DateTemplateToAscii`, and for the date picker `SelectDay` and `StrIToA` while it runs) with its own position-independent ARM code. The table index of every function is decoded from the shim at run time; nothing is patched unless every instruction matches.

On **Palm OS 3.5 – 4.x** the functions are 68k code and their traps are replaced directly with `SysSetTrapAddress`.

Internal and real years have the same leap years (the offset is a multiple of four and the window stays below 2100), so month lengths and day counts need no change. The [article](https://palm2000.com/articles/49) explains it step by step, with photos; the [fix log](docs/fix-log.md) documents the whole investigation, including dead ends; [docs/plan-epoch.md](docs/plan-epoch.md) the design.

## Building

Requires the palmdev-macos toolchain (prc-tools-remix, arm-palmos-gcc 3.3.1) and pilrc 3.2.

```sh
make            # build/DateFix.prc
make test       # host tests: calendar arithmetic (every day, all start years) and record conversion
```

The version is in `VERSION`; *Options → About* shows it. The launcher icons in `src/icon/` are generated by `tools/icon/` and committed, so building needs no extra tools. `tools/emulator/` builds CloudpilotEmu with a writable real-time clock (upstream ignores clock writes), the test emulator used for this project.

## Project layout

| | |
|---|---|
| `src/` | the application (`datefix.c`), the native ARM code for Palm OS 5 (`datefix_arm.c`), the 68k wrappers for Palm OS 3.5 - 4 (`m68k.c`), the date picker (`selectday.c`), record conversion (`convert.c`), calendar arithmetic (`calendar.c`) |
| `tests/` | host tests: the arithmetic for every day and start year, the record conversion |
| `tools/` | build checks, icon generator, the test emulator patch, diagnostic probes (see [tools/README.md](tools/README.md)) |
| `docs/` | [fix log](docs/fix-log.md) (the whole investigation), [design](docs/plan-epoch.md) |
| [ROADMAP.md](ROADMAP.md) | what is open |

## Reporting a problem

Open an issue with: device and Palm OS version, DateFix version (*Options → About*), what you did, and - if possible - the **Test** line, the **Clock check** lines and *Show Trace* from DateFix. Do not attach your database files.

## License

MIT, see [LICENSE](LICENSE). Palm OS is a trademark of its owners; this project is not affiliated with them.
