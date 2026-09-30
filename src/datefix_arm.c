/*
 * datefix_arm.c - native ARM part of DateFix (Palm OS 5)
 *
 * Palm OS 5 implements the date functions natively. Every caller, 68k
 * applications through their PACE shims as well as native modules (status
 * bar, Datebook internals), reaches them through the export table of the
 * Boot module: LDR R12,[R9,#-8]; LDR PC,[R12,#4*index]. DateFix replaces
 * those table entries with the functions below.
 *
 * The code runs from a locked resource: no globals, no data sections, no
 * absolute addresses. The only data - the epoch offset and the original
 * entry points of the wrapped functions - lives in DfConfig() and is
 * written by the 68k installer with DmWrite before the table is patched.
 *
 * The 68k side finds the table index of each function by decoding the
 * PACE shim behind its trap (see datefix.c), so no ROM specific numbers
 * are compiled in.
 */

#include "calendar.h"

typedef UInt8 Char;

// -------------------------------------------------------------------------
// configuration (written by the 68k installer with DmWrite)
// -------------------------------------------------------------------------

#define CFG_ORIG_DATE_TO_ASCII           0   // original entry points of the
#define CFG_ORIG_DATE_TO_DOWDM_FORMAT    1   // wrapped formatting functions
#define CFG_ORIG_DATE_TEMPLATE_TO_ASCII  2
#define CFG_OFFSET_YEARS                 3   // start year - 1904
#define CFG_OFFSET_DAYS                  4   // days 1904-01-01 .. start year
#define CFG_STATE                        5   // UInt32 in the dynamic heap:
                                             // formatting calls in progress
#define CFG_ORIG_SELECT_DAY              6   // date picker wrapper
#define CFG_ORIG_STR_I_TO_A              7   // number formatting in the picker
#define CFG_COUNT                        8

/**
 * Returns the address of the table that follows its two instructions;
 * datefix.c finds the table at the symbol offset + 8 (armc_offsets.h).
 */
UInt32 *DfConfig(void);

__asm__ (
  "  .text                    \n"
  "  .align 2                 \n"
  "  .global DfConfig         \n"
  "DfConfig:                  \n"
  "  adr   r0, 1f             \n"
  "  bx    lr                 \n"
  "1:                         \n"
  "  .word 0, 0, 0, 0, 0, 0, 0, 0 \n");

typedef void   DateToAsciiProc(UInt8, UInt8, UInt16, UInt32, Char *);
typedef UInt16 DateTemplateToAsciiProc(const Char *, UInt8, UInt8, UInt16,
                                       Char *, Int16);

/**
 * The formatting functions of the system compute the weekday name through
 * the (patched) DayOfWeek. While a wrapper below runs, the year they pass
 * on is already the real one and must not be moved a second time. The
 * counter lives in the dynamic heap: the code resource itself is in the
 * write-protected storage heap.
 */
static volatile UInt32 *
FormattingDepth(void)
{
  return (volatile UInt32 *)DfConfig()[CFG_STATE];
}

static Int16
WeekdayOffsetYears(void)
{
  return (*FormattingDepth() != 0) ? 0 : (Int16)DfConfig()[CFG_OFFSET_YEARS];
}

// state words in the dynamic heap: [0] formatting calls in progress,
// [1] date picker (SelectDay) in progress
static volatile UInt32 *
PickerScope(void)
{
  return (volatile UInt32 *)DfConfig()[CFG_STATE] + 1;
}

// Trace of the last date function calls, for diagnostics (68k side:
// "Show Trace"). State block words: [5] number of entries written,
// [6..41] ring of 12 entries (tag, argument, result), [42..59] copy of the
// 6 newest entries taken when the date picker was called, [60] its size.
#define TRACE_HEAD   5
#define TRACE_RING   6
#define TRACE_SIZE   12
#define TRACE_SNAP   42
#define TRACE_COUNT  60

#define TAG_SECONDS  1          // TimSecondsToDateTime: seconds -> y/m/d
#define TAG_DAYOFWEEK 2         // DayOfWeek: y/m/d -> weekday
#define TAG_DAYOFMONTH 3        // DayOfMonth: y/m/d -> day of month

static void
TraceAdd(UInt32 tag, UInt32 arg, UInt32 result)
{
  volatile UInt32 *state = (volatile UInt32 *)DfConfig()[CFG_STATE];
  UInt32 head = state[TRACE_HEAD];
  volatile UInt32 *entry = state + TRACE_RING + (head % TRACE_SIZE) * 3;

  entry[0] = tag;
  entry[1] = arg;
  entry[2] = result;
  state[TRACE_HEAD] = head + 1;
}

static UInt32
PackYMD(Int32 year, Int32 month, Int32 day)
{
  return (UInt32)year * 512 + (UInt32)month * 32 + (UInt32)day;
}

/**
 * The year shown to the user for a year an application passes in: the
 * internal years 1904..2031 are the real years S..S+127. Inside a
 * formatting function the year is already the real one: the original
 * DateToDOWDMFormat, for one, formats through DateTemplateToAscii, which
 * runs through the patched table as well.
 */
static UInt16
RealYear(UInt16 year)
{
  // The date picker names its weekday header from a fixed reference date
  // it takes for a Sunday - with the stock (internal) calendar; the grid
  // below uses the patched DayOfWeek. So inside the picker the formatting
  // functions keep the internal year (and, through FormattingDepth, the
  // internal weekday).
  if ((*FormattingDepth() != 0) || (*PickerScope() != 0))
    return year;
  if ((year >= CAL_FIRST_YEAR) && (year <= CAL_FIRST_YEAR + 127))
    year += (UInt16)DfConfig()[CFG_OFFSET_YEARS];
  else if (year < 100)
    // Applications such as Date Book+ pass only the last two digits of the
    // year; real = internal + offset, so the last two digits follow
    // exactly, whatever the century: (y + offset) mod 100.
    year = (UInt16)((year + DfConfig()[CFG_OFFSET_YEARS]) % 100);
  return year;
}

// -------------------------------------------------------------------------
// replacements (native Palm OS 5 calling convention, little-endian data)
//
// Everything that only counts days - DateToDays, DateDaysToDate,
// DateAdjust, TimAdjust, TimDateTimeToSeconds, DaysInMonth - stays the
// system's: internal and real years have the same leap years.
// -------------------------------------------------------------------------

void
DfSecondsToDateTime(UInt32 seconds, CalDateTime *dateTimeP)
{
  CalSecondsToDateTime(seconds, DfConfig()[CFG_OFFSET_DAYS], dateTimeP);
  TraceAdd(TAG_SECONDS, seconds,
           PackYMD(dateTimeP->year, dateTimeP->month, dateTimeP->day));
}

Int16
DfDayOfWeek(Int16 month, Int16 day, Int16 year)
{
  Int16 result = CalDayOfWeek(month, day, year, WeekdayOffsetYears());

  TraceAdd(TAG_DAYOFWEEK, PackYMD(year, month, day), (UInt32)result);
  return result;
}

Int16
DfDayOfMonth(Int16 month, Int16 day, Int16 year)
{
  Int16 result = CalDayOfMonth(month, day, year, WeekdayOffsetYears());

  TraceAdd(TAG_DAYOFMONTH, PackYMD(year, month, day), (UInt32)result);
  return result;
}

// the formatting functions stay the system's (locale, weekday names);
// they get the real year and compute the real weekday themselves

void
DfDateToAscii(UInt8 months, UInt8 days, UInt16 years, UInt32 dateFormat,
              Char *stringP)
{
  DateToAsciiProc *orig =
    (DateToAsciiProc *)DfConfig()[CFG_ORIG_DATE_TO_ASCII];

  UInt16 year = RealYear(years);

  (*FormattingDepth())++;
  orig(months, days, year, dateFormat, stringP);
  (*FormattingDepth())--;
}

void
DfDateToDOWDMFormat(UInt8 months, UInt8 days, UInt16 years, UInt32 dateFormat,
                    Char *stringP)
{
  DateToAsciiProc *orig =
    (DateToAsciiProc *)DfConfig()[CFG_ORIG_DATE_TO_DOWDM_FORMAT];

  UInt16 year = RealYear(years);

  (*FormattingDepth())++;
  orig(months, days, year, dateFormat, stringP);
  (*FormattingDepth())--;
}

UInt16
DfDateTemplateToAscii(const Char *templateP, UInt8 months, UInt8 days,
                      UInt16 years, Char *stringP, Int16 stringLen)
{
  DateTemplateToAsciiProc *orig =
    (DateTemplateToAsciiProc *)DfConfig()[CFG_ORIG_DATE_TEMPLATE_TO_ASCII];

  UInt16 year = RealYear(years);
  UInt16 result;

  (*FormattingDepth())++;
  result = orig(templateP, months, days, year, stringP, stringLen);
  (*FormattingDepth())--;
  return result;
}

// The date picker (SelectDay, a native UI function) draws its year with
// StrIToA from the internal year. While it runs, StrIToA shows a year of the
// window as the real one; the picker itself keeps working with internal
// years, so what it returns is what applications store.

typedef UInt32 SelectDayProc(UInt32, void *, void *, void *, const Char *);
typedef Char  *StrIToAProc(Char *, Int32);

// The shim hands the picker native (little-endian) Int16 values behind its
// pointers and copies them back to the 68k side afterwards; packed as
// year * 512 + month * 32 + day for the diagnostics.
static UInt32
PackDate(const void *month, const void *day, const void *year)
{
  const UInt8 *m = (const UInt8 *)month, *d = (const UInt8 *)day,
              *y = (const UInt8 *)year;

  return (UInt32)(y[0] | (y[1] << 8)) * 512 + (UInt32)(m[0] | (m[1] << 8)) * 32 +
         (UInt32)(d[0] | (d[1] << 8));
}

UInt32
DfSelectDayNative(UInt32 selectDayBy, void *month, void *day, void *year,
                  const Char *title)
{
  SelectDayProc *orig = (SelectDayProc *)DfConfig()[CFG_ORIG_SELECT_DAY];
  volatile UInt32 *log = PickerScope() + 1;       // [2] in, [3] out, [4] calls
  volatile UInt32 *state = (volatile UInt32 *)DfConfig()[CFG_STATE];
  UInt32 result, head = state[TRACE_HEAD], n, k;

  // the calls that led to this picker call
  n = (head < 6) ? head : 6;
  for (k = 0; k < n; k++)
  {
    volatile UInt32 *from = state + TRACE_RING + ((head - 1 - k) % TRACE_SIZE) * 3;
    volatile UInt32 *to = state + TRACE_SNAP + k * 3;

    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
  }
  state[TRACE_COUNT] = n;

  log[0] = PackDate(month, day, year);
  (*PickerScope())++;
  result = orig(selectDayBy, month, day, year, title);
  (*PickerScope())--;
  log[1] = PackDate(month, day, year);
  log[2]++;
  return result;
}

Char *
DfStrIToA(Char *s, Int32 value)
{
  StrIToAProc *orig = (StrIToAProc *)DfConfig()[CFG_ORIG_STR_I_TO_A];

  if ((*PickerScope() != 0) && (value >= CAL_FIRST_YEAR) &&
      (value <= CAL_FIRST_YEAR + 127))
    value += (Int32)DfConfig()[CFG_OFFSET_YEARS];
  return orig(s, value);
}

// -------------------------------------------------------------------------
// installer (called by the 68k side through PceNativeCall)
// -------------------------------------------------------------------------

#define DF_MAX_ENTRIES   16

// request from the 68k side: big-endian UInt32 fields, only 2-byte aligned
// (68k stack) - read and written byte by byte, never as ARM words
#define REQ_COMMAND      0
#define REQ_COUNT        4
#define REQ_ENTRY        8     // module, index, value: 12 bytes per entry
#define REQ_ENTRY_SIZE   12

#define DF_CMD_READ      1     // report the current entries
#define DF_CMD_WRITE     2     // write the given entries, report the old ones

#define DF_OK            0
#define DF_ERR_VERIFY    1     // an entry did not keep the written value

static UInt32
GetBE32(const UInt8 *p)
{
  return ((UInt32)p[0] << 24) | ((UInt32)p[1] << 16) | ((UInt32)p[2] << 8) |
         (UInt32)p[3];
}

static void
PutBE32(UInt8 *p, UInt32 v)
{
  p[0] = (UInt8)(v >> 24);
  p[1] = (UInt8)(v >> 16);
  p[2] = (UInt8)(v >> 8);
  p[3] = (UInt8)v;
}

UInt32
DfInstall(const void *emulStateP, void *userData68KP, void *call68KFuncP)
{
  register UInt32 **tables __asm__("r9");
  UInt8    *req = (UInt8 *)userData68KP;
  UInt8    *entry;
  UInt32    i, count, command, module, index, old, value;
  volatile UInt32 *table;
  UInt32    result = DF_OK;

  command = GetBE32(req + REQ_COMMAND);
  count   = GetBE32(req + REQ_COUNT);
  if (count > DF_MAX_ENTRIES) count = DF_MAX_ENTRIES;

  for (i = 0; i < count; i++)
  {
    entry  = req + REQ_ENTRY + i * REQ_ENTRY_SIZE;
    module = GetBE32(entry);
    index  = GetBE32(entry + 4);
    table  = *(volatile UInt32 **)((UInt8 *)tables - 4 * (module + 1));
    old    = table[index];

    if (command == DF_CMD_WRITE)
    {
      value        = GetBE32(entry + 8);
      table[index] = value;
      if (table[index] != value)
        result = DF_ERR_VERIFY;
    }
    PutBE32(entry + 8, old);
  }

  return result;
}
