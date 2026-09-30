/*
 * m68k.h - the date functions of DateFix for Palm OS 3.x/4.x (68k)
 *
 * Below Palm OS 5 the system is 68k code and the trap table can be patched
 * directly: SysSetTrapAddress puts the functions of m68k.c in place of the
 * ROM's. They run inside every application, with that application's A5:
 * no globals, no string literals - the configuration lives in a chunk of
 * the dynamic heap that a feature points to.
 */

#ifndef M68K_H
#define M68K_H

#include <PalmOS.h>

#define ftrM68kConfig        20      // pointer to the M68kConfig chunk

typedef struct
{
  UInt32  offsetYears;          // start year - 1904
  UInt32  offsetDays;           // days 1904-01-01 .. start year
  UInt32  depth;                // formatting calls in progress
  void   *origDateToAscii;
  void   *origDateToDOWDMFormat;
  void   *origDateTemplateToAscii;
  UInt16  recording;            // other applications' calls are recorded
  UInt16  traceCount;           // formatting calls recorded so far
  UInt16  trace[8][5];          // the newest 8: tag, month, day, year, result year
} M68kConfig;

void   M68kSecondsToDateTime(UInt32 seconds, DateTimePtr dateTimeP);
Int16  M68kDayOfWeek(Int16 month, Int16 day, Int16 year);
Int16  M68kDayOfMonth(Int16 month, Int16 day, Int16 year);
void   M68kDateToAscii(UInt8 months, UInt8 days, UInt16 years,
                       DateFormatType dateFormat, Char *pString);
void   M68kDateToDOWDMFormat(UInt8 months, UInt8 days, UInt16 years,
                             DateFormatType dateFormat, Char *pString);
UInt16 M68kDateTemplateToAscii(const Char *templateP, UInt8 months,
                               UInt8 days, UInt16 years, Char *stringP,
                               Int16 stringLen);

#endif
