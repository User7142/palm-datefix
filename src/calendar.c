/*
 * calendar.c - date arithmetic for a Palm OS epoch of 1 January S
 *
 * Day numbers use the proleptic Gregorian calendar via the "days from
 * civil" algorithm (H. Hinnant), counted from 1600-03-01 so every
 * intermediate value stays positive for the years that matter here.
 */

#include "calendar.h"

// 1600-03-01 (start of a 400 year cycle, after its leap day) .. 1904-01-01
#define DAYS_1600_TO_1904   110973L
// 1600-03-01 was a Wednesday
#define WEEKDAY_1600        3
#define DAYS_PER_400_YEARS  146097L

int
CalValidStartYear(Int32 startYear)
{
  return (startYear >= CAL_MIN_START_YEAR) &&
         (startYear <= CAL_MAX_START_YEAR) &&
         (((startYear - CAL_FIRST_YEAR) % 4) == 0);
}

/**
 * Days since 1600-03-01 for a date after 1600-03-01.
 */
static Int32
DaysFrom1600(Int32 year, Int32 month, Int32 day)
{
  Int32 era, yoe, doy, doe;

  year -= 1600;
  if (month <= 2) year--;
  era = year / 400;
  yoe = year - era * 400;                                    // 0..399
  doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               // 0..146096
  return era * DAYS_PER_400_YEARS + doe;
}

/**
 * Days since 1904-01-01 (negative before). Years before 1601 are moved
 * by whole 400 year cycles, which repeat the calendar exactly.
 */
Int32
CalDaysFrom1904(Int32 year, Int32 month, Int32 day)
{
  Int32 cycles = 0;

  while (year < 1601)
  {
    year += 400;
    cycles++;
  }
  return DaysFrom1600(year, month, day) - DAYS_1600_TO_1904
         - cycles * DAYS_PER_400_YEARS;
}

UInt32
CalOffsetDays(Int32 startYear)
{
  return (UInt32)CalDaysFrom1904(startYear, 1, 1);
}

/**
 * Date of a day number (days since 1904-01-01).
 */
void
CalCivilFrom1904(UInt32 days, Int32 *year, Int32 *month, Int32 *day)
{
  UInt32 z, era, doe, yoe, doy, mp;

  z   = days + DAYS_1600_TO_1904;
  era = z / DAYS_PER_400_YEARS;
  doe = z - era * DAYS_PER_400_YEARS;                          // 0..146096
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // 0..399
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // 0..365
  mp  = (5 * doy + 2) / 153;                                   // 0..11

  *day   = (Int32)(doy - (153 * mp + 2) / 5 + 1);
  *month = (Int32)(mp < 10 ? mp + 3 : mp - 9);
  *year  = (Int32)(yoe + era * 400) + 1600 + (*month <= 2 ? 1 : 0);
}

Int16
CalWeekDay(UInt32 days)
{
  return (Int16)((days + DAYS_1600_TO_1904 + WEEKDAY_1600) % 7);
}

/**
 * TimSecondsToDateTime: the internal date and time (what applications
 * store), the weekday of the real date. Clamps at the end of the internal
 * calendar like Palm OS does.
 */
void
CalSecondsToDateTime(UInt32 seconds, UInt32 offsetDays, CalDateTime *dt)
{
  UInt32 days, rest;
  Int32  year, month, day;

  if (seconds > CAL_MAX_SECONDS)
    seconds = CAL_MAX_SECONDS;

  days = seconds / 86400UL;
  rest = seconds % 86400UL;

  CalCivilFrom1904(days, &year, &month, &day);
  dt->year    = (Int16)year;
  dt->month   = (Int16)month;
  dt->day     = (Int16)day;
  dt->hour    = (Int16)(rest / 3600);
  dt->minute  = (Int16)((rest / 60) % 60);
  dt->second  = (Int16)(rest % 60);
  dt->weekDay = CalWeekDay(days + offsetDays);
}

/**
 * DayOfWeek(month, day, year) with an internal year, 0 = Sunday.
 */
Int16
CalDayOfWeek(Int16 month, Int16 day, Int16 year, Int16 offsetYears)
{
  Int32 days = CalDaysFrom1904((Int32)year + offsetYears, month, day);

  // move negative day numbers into range by whole 400 year cycles
  while (days < 0)
    days += DAYS_PER_400_YEARS;
  return CalWeekDay((UInt32)days);
}

/**
 * DayOfMonth(month, day, year) with an internal year: which weekday of the
 * month (dom1stSun .. dom4thSat, domLastSun .. for the fifth occurrence).
 */
Int16
CalDayOfMonth(Int16 month, Int16 day, Int16 year, Int16 offsetYears)
{
  return (Int16)(((day - 1) / 7) * 7 +
                 CalDayOfWeek(month, day, year, offsetYears));
}
