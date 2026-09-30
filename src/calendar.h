/*
 * calendar.h - date arithmetic for a Palm OS epoch of 1 January S
 *
 * DateFix moves the Palm OS epoch from 1904 to a start year S (1904..1972,
 * S - 1904 a multiple of 4): the clock counts seconds since S-01-01 and a
 * stored DateType year counts from S. Applications keep working with
 * "internal" years 1904..2031, which are the real years S..S+127.
 *
 * Internal and real years have the same leap years (the offset is a
 * multiple of 4 and the window never reaches 2100), so month lengths and
 * day counts of the internal calendar are correct; only the weekday and
 * the year shown to the user differ. That is what these functions compute.
 *
 * Pure arithmetic: no globals, no tables, no Palm OS calls - the same code
 * runs as position-independent native ARM code and in the host unit tests.
 */

#ifndef CALENDAR_H
#define CALENDAR_H

#include "types.h"

#define CAL_FIRST_YEAR        1904      // Palm OS epoch
#define CAL_MAX_SECONDS       0xF0C3EFFFUL   // 2031-12-31 23:59:59 internal

#define CAL_MIN_START_YEAR    1904
#define CAL_MAX_START_YEAR    1972      // window ends 2099, before 2100

typedef struct
{
  Int16 second;
  Int16 minute;
  Int16 hour;
  Int16 day;
  Int16 month;
  Int16 year;
  Int16 weekDay;            // days since Sunday
} CalDateTime;              // layout of Palm OS DateTimeType

// is S a valid start year?
int    CalValidStartYear(Int32 startYear);

// days from 1904-01-01 to startYear-01-01 (the epoch offset in days)
UInt32 CalOffsetDays(Int32 startYear);

// days since 1904-01-01 (negative before) of a real Gregorian date
Int32  CalDaysFrom1904(Int32 year, Int32 month, Int32 day);
void   CalCivilFrom1904(UInt32 days, Int32 *year, Int32 *month, Int32 *day);

// day of the week (0 = Sunday) of a real day number since 1904-01-01
Int16  CalWeekDay(UInt32 days);

// TimSecondsToDateTime: internal date and time, real weekday
void   CalSecondsToDateTime(UInt32 seconds, UInt32 offsetDays, CalDateTime *dt);

// DayOfWeek / DayOfMonth for an internal year
Int16  CalDayOfWeek(Int16 month, Int16 day, Int16 year, Int16 offsetYears);
Int16  CalDayOfMonth(Int16 month, Int16 day, Int16 year, Int16 offsetYears);

#endif
