/*
 * clockcheck.c - the real date of the clock, computed by DateFix itself
 *
 * The clock check in DateFix's main form compares what Palm OS shows
 * (through the patched system functions) with this independent result:
 * the calendar arithmetic of calendar.c on the raw clock value. Kept apart
 * from datefix.c because calendar.h brings its own fixed-size types.
 */

#include "calendar.h"
#include "clockcheck.h"

void
ClockCheckRealDate(UInt32 seconds, UInt16 startYear, Int16 *year, Int16 *month,
                   Int16 *day, Int16 *weekDay)
{
  UInt32 days = seconds / 86400UL + CalOffsetDays(startYear);
  Int32  y, m, d;

  CalCivilFrom1904(days, &y, &m, &d);
  *year    = (Int16)y;
  *month   = (Int16)m;
  *day     = (Int16)d;
  *weekDay = CalWeekDay(days);
}
