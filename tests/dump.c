/* prints the calendar results for tests/check.py */
#include <stdio.h>
#include "calendar.h"

int main(void)
{
  Int32 start;
  UInt32 d, s, offset;
  CalDateTime dt;

  for (start = 1900; start <= 1976; start++)
    printf("V %ld %d\n", (long)start, CalValidStartYear(start));

  for (start = CAL_MIN_START_YEAR; start <= CAL_MAX_START_YEAR; start += 4)
  {
    offset = CalOffsetDays(start);
    printf("O %ld %lu\n", (long)start, (unsigned long)offset);

    /* every internal day 1904-01-01 .. 2031-12-31 as a clock value */
    for (d = 0; d < 46752UL; d++)
    {
      s = d * 86400UL + 12UL * 3600 + 34 * 60 + 56;
      CalSecondsToDateTime(s, offset, &dt);
      printf("S %ld %lu %d-%d-%d %d:%d:%d %d\n", (long)start, (unsigned long)d,
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.weekDay);
    }
    CalSecondsToDateTime(0xFFFFFFFFUL, offset, &dt);
    printf("M %ld %d-%d-%d %d:%d:%d\n", (long)start, dt.year, dt.month, dt.day,
           dt.hour, dt.minute, dt.second);

    /* weekday / day of month for internal years as applications pass them */
    for (d = 1904; d <= 2031; d++)
    {
      int m;
      for (m = 1; m <= 12; m++)
        printf("W %ld %lu %d %d %d\n", (long)start, (unsigned long)d, m,
               CalDayOfWeek(m, 13, (Int16)d, (Int16)(start - 1904)),
               CalDayOfMonth(m, 28, (Int16)d, (Int16)(start - 1904)));
    }
  }
  return 0;
}
