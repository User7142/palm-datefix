# Roadmap

What is not done yet, roughly in order.

## Needs a device or a report

- **Scenario tests on a real device.** In the Tungsten E2 emulator (Palm OS 5.4, start year 1932) these ran: a reminder set for 2032 that fires at the 2031 / 2032 rollover, an event from 2025 read with the clock in 2033, the World Clock in 2033 (docs/fix-log.md, section 22). Not run on a device, and not run at all: time-dependent games.
- **Other Palm OS 5 devices** (T5, TX, LifeDrive, Zire 31/72, Clié NX/UX): expected to work, not confirmed. See *Tested devices* in the README.
- **Rollover on an m515** with the clock running (the emulator's Dragonball clock cannot be set like the Tungsten E2's).
- **Backup on a real card** (`/PALM/DateFix`) on both Palm OS generations.

## Known gaps

- **Applications that draw the year themselves.** DateFix patches the year constant of the applications in its table (so far Date Book+ 3.0H, fix log sections 21, 24); every other application and version shows the internal year until its sites are found with `tools/yearfinder` and added. The 32 sites of Date Book+ that the scanner could not classify are unchecked. A ROM application needs a RAM copy under its own creator (`tools/ramcopy`); the hardware button and alarms still start the ROM version. Planned: more versions (Pimlico DateBk3/4/5, Palm OS 5 builds); the table is data now (`apps/apps.txt`, `DateFixApps.pdb`), so they need no new DateFix build.
- **Other databases**: Note Pad, Voice Memo and third-party calendars are not converted (record formats unknown). Date Book+ 3.0H needs nothing extra: its entries are in the standard databases; what it keeps in `Datebk3HDB` (empty so far) is open – e.g. archived items were not tried.
- **Palm OS below 3.5** (Palm III 3.0/3.3, Palm V 3.1): no `DateTemplateToAscii`, different date picker.
- **Desktop HotSync** transfers internal dates.

## Ideas

- A conduit or a desktop tool that translates the internal dates of a backup.
- Start year other than multiples of four by handling the leap-year pattern (needs a window that avoids 2100).
