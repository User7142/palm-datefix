# DateFix 2: a real epoch

Version 1 kept every stored value and only changed how the system reads it (7-bit year 0 = 2032). The test on the Tungsten T3 showed where that stops working:

| Finding on the T3 | Cause |
|---|---|
| Calendar cannot go back before 2032-01-01, nor forward past 2031-12-31 until the clock itself passes it | Datebook compares `DateType` values as numbers: 2032 (year 0) is "smaller" than 1940 |
| Calendar: *DateDay.c, Line:1666, Record not on day* when creating an entry in 2032 | same – the application's own date logic |
| Tasks: due date picker shows 1904 for 2032, dates above 2032 impossible | applications compute full years as `DateType` year + 1904 |
| Reset with the clock in 2032 → 2002 | the clock value is above `maxSeconds`; the ROM discards it while booting, before DateFix runs (observed; the T3 ROM itself was not examined) |
| Alarm on 2032-01-01 fires | works: alarms only use seconds |

## The model

As if the Palm had been shipped with an epoch of **1 January *S*** instead of 1904 (*S* = start year, 1904…1972 in steps of 4, default 1932; 1940 in 2.0.0-beta.1):

- The clock counts seconds since *S*-01-01 (stored value = real time − *D* days, *D* = days from 1904-01-01 to *S*-01-01).
- A stored `DateType` year counts from *S* (2032 with *S* = 1940 is 92).
- Applications therefore see a normal, gap-free calendar of internal years 1904…2031 – *S*…*S*+127 in reality. Sorting, comparisons, navigation and alarms work without touching any application, and the clock stays below `maxSeconds` until the end of the window, so a reset keeps it.

Because *S* − 1904 is a multiple of 4 and the window never reaches 2100, internal and real years have exactly the same leap years: month lengths, day counts and date arithmetic of the internal calendar are correct as they are. Only two things differ from reality, and those are what DateFix patches:

1. **The weekday** – internal 1996-01-01 is a Monday, real 2032-01-01 a Thursday: `TimSecondsToDateTime` (weekDay), `DayOfWeek`, `DayOfMonth`.
2. **Year numbers shown to the user** – `DateToAscii`, `DateToDOWDMFormat`, `DateTemplateToAscii` get the real year; the date picker (`SelectDay`) shows and accepts real years.

## Data conversion

Existing data was written with *S* = 1904 and has to be moved by *S* − 1904 years once (and back when DateFix is disabled or *S* changes):

- the clock,
- `DateType`/seconds fields of every application whose record format is known – first the documented palmOne PIM databases (Calendar, Tasks, Contacts birthdays), then further time-dependent applications,
- creation/modification/backup dates of all databases.

Before converting, DateFix copies every affected database to the SD card (`/PALM/DateFix/backup/`). Dates before *S* cannot be represented and are reported, not silently changed. Databases with an unknown format are listed as such.

Limitations that remain by design: applications that print years themselves (instead of through the system) show the internal year; data exchanged with a desktop (HotSync conduits) carries internal dates.

## Stages (all done unless marked)

1. Epoch core: patches above, start year setting, clock conversion.
2. Conversion framework with SD backup; palmOne Calendar, Tasks, Contacts; Expense; database dates.
3. Date picker with real years (Palm OS 5 through the export tables, Palm OS 3.5 - 4 through the trap table).
4. Palm OS 3.5 - 4.x: the same model with 68k trap patches.
5. *Open:* further applications whose record formats are known (Note Pad, Voice Memo, third-party calendars) and a per-version patch table for applications that draw the year themselves (see ROADMAP.md).
