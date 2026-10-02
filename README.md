# DateFix

**Keep your Palm working after 31 December 2031.**

Palm OS stops its calendar on **31 December 2031, 23:59:59**. After that the clock resets, the Calendar cannot go forward, and appointments in 2032 crash applications. DateFix moves the start of Palm OS's date counting from 1904 to a year you choose (default **1932**), so a Palm keeps a real, working calendar and clock for another 128 years (**1932 – 2059**).

> **Status: beta.** Verified on a Tungsten T3, a Palm m515 and in emulators; see [Tested devices](#tested-devices) and [Known limitations](#known-limitations). Please read [Before you start](#before-you-start).

**Download:** [DateFix-2.0.0-beta.5.prc](https://github.com/User7142/palm-datefix/releases/latest) &nbsp;|&nbsp; **Article** with photos, the measurements and every dead end: [DateFix: Keeping a Palm Alive After December 31st, 2031](https://palm2000.com/articles/49)

## The idea, and credit

Palm OS stores a date in a 7-bit year (1904 + 0…127) and counts the clock in seconds since 1 January 1904. The hardware clock keeps running past 2031, only the conversion to a date stops.

The basic trick is not new: run the Palm's clock some decades in the past and add the offset whenever a year is shown. [NaivePalmdayMitigation](https://github.com/Tavisco/NaivePalmdayMitigation) by Tavisco (2024, Palm OS 3.5 - 4.x, for HackMaster/X-Master) does it with an offset of 56 years, by hand. DateFix takes the same idea and automates and extends it:

- it **moves the clock itself**, as if the Palm had been shipped with the counter starting in 1932, so the clock stays below the limit for 128 years and **survives a reset**;
- the stored dates (appointments, tasks, birthdays, expenses) are **converted once**, with a backup;
- it patches more of the date API (the weekday and all the year formatting functions, the date picker) and runs on **Palm OS 5** as well as 3.5 - 4.x;
- applications see an ordinary, gap-free calendar of "internal" years 1904…2031; only what differs from reality is patched: the **weekday** and the **year shown to the user**.

The limits are the same as for any such fix: the packed date keeps its 7-bit year (the window is 128 years), and applications that draw the year themselves, instead of calling the system, need patches of their own (see [Known limitations](#known-limitations)).

The start year can be any of 1904, 1908, … 1972 (steps of four, so leap years stay aligned). The default 1932 gives 1932 – 2059. It is 28 years after 1904, and 28 years are 10,227 days, exactly 1,461 weeks (the leap-year pattern of 1901 – 2099 repeats every 28 years), so every date has the same weekday internally and in reality, and DateFix's weekday patches become no-ops. An application doing its own day arithmetic is then right without any patch; only the year it shows remains. 1960 (offset 56, like Tavisco's) has the same property.

## Before you start

- **Make a HotSync backup** and, if the device has a card slot, **insert an SD card**: DateFix copies the databases it is about to change to `/PALM/DateFix` on the card first.
- Enabling **converts your stored dates** (every year moves by *start year − 1904*). Dates before the start year cannot be represented and are set to its first year (DateFix tells you how many). **Birthdays or appointments before 1932 are affected.**
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

## Applications that draw the year themselves

Some applications do not ask the system to format the year, they compute `year + 1904` and draw the result themselves (Date Book+: week, two-week and year view). They show the internal year. DateFix has a small table of such applications (**so far Date Book+ 3.0H, and TimeCopy 1.4**, whose HotSync sets the clock from the desktop's Unix time): it writes the start year into the year constant of their stored code, so they compute the real year. The sites were found with [`tools/yearfinder`](tools/README.md); *Test* shows `apps: n` (sites patched), `other: n` (another version of a known application, not touched) and `locked: n` (database that cannot be written). *Disable* takes the patch out again.

The table is data, not code: [`apps/apps.txt`](apps/apps.txt) lists every application and site, and the build turns it into a resource of DateFix. `make apps` turns the same file into **`DateFixApps.pdb`**: installed like an application, it replaces the built-in table if its version is higher, so a new application version needs no new DateFix. A damaged or older `DateFixApps` is ignored. *Options → Show Trace* shows which table is in use (`Table 2026100201 (built in)` or `(DateFixApps)`). New sites come from `tools/yearfinder --table NAME CREATOR`, which prints them as lines for `apps.txt` (check each one in the application before adding it).

**An application in ROM cannot be patched**, and a RAM copy of the same name and creator does not take over from it (tried on a Handspring Visor). The way around is a copy under its **own creator**: [`tools/ramcopy/make_ram_copy.py`](tools/ramcopy/make_ram_copy.py) makes it from the application's `.prc` (name `DateBk3x`, creator `HsDR`, launcher name "DB+ (RAM)"; the table already has an entry for it). It shows up as a second icon, runs from RAM, patched, and uses the same appointments. The hardware button and alarms still start the ROM version. **DateFix does not contain Date Book+ and this repository has no copy of it**; you need the `.prc` of version 3.0H yourself. Applications and versions that are not in the table are not changed.

## Tested devices

| Device | Palm OS | Result |
|---|---|---|
| Tungsten T3 | 5.2.1 | works: rollover 2031 → 2032, Calendar both ways, date picker, reset |
| Palm m515 | 4.1 | works: conversion, soft reset keeps the patch |
| Tungsten E2 (emulator) | 5.4 | works |
| Palm m515 (emulator) | 4.1 | works |
| Handspring Visor | 3.5.2H3 | Date Book works; Date Book+ 3.0H: the ROM version shows the internal year in week, two-week and year view; a RAM copy under its own creator (launcher entry "DB+ (RAM)", see `tools/ramcopy`) shows the right year in every view (confirmed by the owner of the device) |

**Help wanted:** T5, TX, LifeDrive, Zire, Clié and everything else is *expected* to work (the export table entries are located by decoding the ROM, nothing is hard-coded; DateFix patches nothing if it cannot find them), but it is not confirmed. If you try it, please report: device, Palm OS version, the self test line, the clock check line and *Show Trace*.

## Known limitations

- **Applications that draw the year themselves** (not through the system's date functions) show the *internal* year, unless DateFix knows them. DateFix has a small table of such applications and versions (so far Date Book+ 3.0H, found with `tools/yearfinder`): it writes the start year into the year constant of their stored code, so the week, two-week and year view show the real year (checked on a Palm m515 in the emulator; `Test` shows `apps: n`). *Disable* takes it out. An application in ROM (the Visor's own Date Book+) cannot be changed and keeps showing the internal year, as does every application and version that is not in the table. A RAM copy of the same name and creator does not take over from the ROM version (Visor, 2026-10-01); a copy under its own creator does (`tools/ramcopy`), but then Date Book+ has to be started from its own launcher icon. Date Book+ 3.0H (Handspring build) does not run on Palm OS 5 at all. The standard Date Book is right in every view.
- **HotSync with a desktop** transfers the internal dates. Conduits and the desktop software know nothing about the moved epoch.
- The window is **128 years** (the year field has 7 bits). Dates outside it cannot be stored.
- Databases of other applications (third-party calendars, Note Pad, Voice Memo) are not converted; their record formats are not known. Date Book+ 3.0H keeps appointments, floating events, journal entries, templates and To Dos in the standard Date Book and To Do databases, which *are* converted; its own database `Datebk3HDB` stayed empty in every test (see [fix log](docs/fix-log.md), section 27).
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
make apps       # build/DateFixApps.pdb, the application table on its own
make test       # host tests: calendar arithmetic, record conversion, application patches and table
```

The version is in `VERSION`; *Options → About* shows it. Every push runs the host tests and builds `DateFix.prc` on GitHub Actions with the same, pinned toolchain ([workflow](.github/workflows/build.yml)); the `DateFix.prc` of a release is the one built there. The launcher icons in `src/icon/` are generated by `tools/icon/` and committed, so building needs no extra tools. `tools/emulator/` builds CloudpilotEmu with a writable real-time clock (upstream ignores clock writes), the test emulator used for this project.

## Project layout

| | |
|---|---|
| `src/` | the application (`datefix.c`), the native ARM code for Palm OS 5 (`datefix_arm.c`), the 68k wrappers for Palm OS 3.5 - 4 (`m68k.c`), the date picker (`selectday.c`), record conversion (`convert.c`), calendar arithmetic (`calendar.c`) |
| `apps/` | the application table: applications that draw the year themselves and where DateFix patches them |
| `tests/` | host tests: the arithmetic for every day and start year, the record conversion, the application patches and table |
| `tools/` | build checks, icon generator, the test emulator patch, diagnostic probes (see [tools/README.md](tools/README.md)) |
| `docs/` | [fix log](docs/fix-log.md) (the whole investigation), [design](docs/plan-epoch.md) |
| [ROADMAP.md](ROADMAP.md) | what is open |

## Reporting a problem

Open an issue: there are forms for a **device report** (DateFix on a device that is not in the table above, working or not) and a **bug report**. They ask for device and Palm OS version, DateFix version (*Options → About*), the **Test** line, the **Clock check** lines and *Show Trace* (which also names the application table). Do not attach your database files.

## License

MIT, see [LICENSE](LICENSE). Palm OS is a trademark of its owners; this project is not affiliated with them.
