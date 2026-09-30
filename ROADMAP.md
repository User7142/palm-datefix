# Roadmap

What is not done yet, roughly in order.

## Needs a device or a report

- **Scenario tests**: a reminder set in 2031 for 2032, reading an event from 2025 when the clock is in 2033, the World Clock, time-dependent games. The arithmetic behind all of them is covered by the host tests and the emulators; nobody has run them on a device.
- **Other Palm OS 5 devices** (T5, TX, LifeDrive, Zire 31/72, Clié NX/UX): expected to work, not confirmed. See *Tested devices* in the README.
- **Rollover on an m515** with the clock running (the emulator's Dragonball clock cannot be set like the Tungsten E2's).
- **Backup on a real card** (`/PALM/DateFix`) on both Palm OS generations.

## Known gaps

- **Applications that draw the year themselves.** Date Book+ (Handspring Visor, Pimlico DateBk3): week, two-week and year view show the internal year; day and month view are right. Planned: a patch table per application version - a `StrIToA` wrapper (68k route) that looks at the caller's address inside the running application's code resource and converts only at known call sites (see docs/fix-log.md, section 18.1). The call sites are found by recording the distinct callers of `StrIToA` per view; entries are meant to be contributed.
- **Other databases**: Date Book+'s own database `Datebk3HDB`, Note Pad, Voice Memo and third-party calendars are not converted (record formats unknown).
- **Palm OS below 3.5** (Palm III 3.0/3.3, Palm V 3.1): no `DateTemplateToAscii`, different date picker.
- **Desktop HotSync** transfers internal dates.

## Ideas

- A conduit or a desktop tool that translates the internal dates of a backup.
- Start year other than multiples of four by handling the leap-year pattern (needs a window that avoids 2100).
