/*
 * m68k.c - DateFix's date functions for Palm OS 3.x/4.x (see m68k.h)
 *
 * The same model as the native code for Palm OS 5 (datefix_arm.c): the
 * clock and stored dates count from a start year, applications see the
 * internal years 1904..2031 (= the real years S..S+127); only the weekday
 * and the year shown to the user differ, and those are what these
 * functions provide.
 */

#define DATEFIX_PALMOS_TYPES
#include "m68k.h"
#include "calendar.h"

#define CAL_LAST_YEAR  (CAL_FIRST_YEAR + 127)

static M68kConfig *
Config(void)
{
  UInt32 p = 0;

  FtrGet('DtFx', ftrM68kConfig, &p);
  return (M68kConfig *)p;
}

/**
 * While a formatting function runs, the year it passes on is already the
 * real one - the original DateToDOWDMFormat, for one, may format through
 * DateTemplateToAscii and DayOfWeek (patched as well).
 */
static UInt16
RealYear(const M68kConfig *c, UInt16 year)
{
  if (c->depth != 0) return year;
  if ((year >= CAL_FIRST_YEAR) && (year <= CAL_LAST_YEAR))
    year += (UInt16)c->offsetYears;
  else if (year < 100)
    // Applications like Date Book+ pass only the last two digits of the
    // year; real = internal + offset, so the last two digits follow
    // exactly, whatever the century: (y + offset) mod 100.
    year = (UInt16)((year + c->offsetYears) % 100);
  return year;
}

/**
 * Keeps the arguments of the newest formatting calls (Options -> Show
 * Trace): tag 1 = DateToAscii, 2 = DateToDOWDMFormat, 3 = DateTemplateToAscii.
 */
static void
Trace(M68kConfig *c, UInt16 tag, UInt16 month, UInt16 day, UInt16 year,
      UInt16 shown)
{
  UInt16 *e = c->trace[c->traceCount & 7];

  if (!c->recording) return;                    // DateFix's own redraws
  e[0] = tag; e[1] = month; e[2] = day; e[3] = year; e[4] = shown;
  c->traceCount++;
}

static Int16
WeekdayOffsetYears(const M68kConfig *c)
{
  return (c->depth != 0) ? 0 : (Int16)c->offsetYears;
}

void
M68kSecondsToDateTime(UInt32 seconds, DateTimePtr dateTimeP)
{
  M68kConfig *c = Config();

  CalSecondsToDateTime(seconds, c->offsetDays, (CalDateTime *)dateTimeP);
}

Int16
M68kDayOfWeek(Int16 month, Int16 day, Int16 year)
{
  M68kConfig *c = Config();

  return CalDayOfWeek(month, day, year, WeekdayOffsetYears(c));
}

Int16
M68kDayOfMonth(Int16 month, Int16 day, Int16 year)
{
  M68kConfig *c = Config();

  return CalDayOfMonth(month, day, year, WeekdayOffsetYears(c));
}

typedef void DateToAsciiProc(UInt8, UInt8, UInt16, DateFormatType, Char *);
typedef UInt16 DateTemplateProc(const Char *, UInt8, UInt8, UInt16, Char *,
                                Int16);

void
M68kDateToAscii(UInt8 months, UInt8 days, UInt16 years,
                DateFormatType dateFormat, Char *pString)
{
  M68kConfig *c = Config();
  UInt16      year = RealYear(c, years);

  Trace(c, 1, months, days, years, year);
  c->depth++;
  ((DateToAsciiProc *)c->origDateToAscii)(months, days, year, dateFormat,
                                          pString);
  c->depth--;
}

void
M68kDateToDOWDMFormat(UInt8 months, UInt8 days, UInt16 years,
                      DateFormatType dateFormat, Char *pString)
{
  M68kConfig *c = Config();
  UInt16      year = RealYear(c, years);

  Trace(c, 2, months, days, years, year);
  c->depth++;
  ((DateToAsciiProc *)c->origDateToDOWDMFormat)(months, days, year,
                                                dateFormat, pString);
  c->depth--;
}

UInt16
M68kDateTemplateToAscii(const Char *templateP, UInt8 months, UInt8 days,
                        UInt16 years, Char *stringP, Int16 stringLen)
{
  M68kConfig *c = Config();
  UInt16      year = RealYear(c, years);
  UInt16      result;

  Trace(c, 3, months, days, years, year);
  c->depth++;
  result = ((DateTemplateProc *)c->origDateTemplateToAscii)(
             templateP, months, days, year, stringP, stringLen);
  c->depth--;
  return result;
}
