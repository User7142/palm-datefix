/*
 * convert.h - moves stored dates to another epoch
 *
 * Data written with the epoch 1904 keeps its dates when DateFix moves the
 * epoch to S: a DateType year then counts from S, so every stored year
 * has to be lowered by S - 1904 once (and raised again when DateFix is
 * disabled). Months and days stay: S - 1904 is a multiple of 4, so both
 * epochs have the same leap years.
 *
 * The functions work on a copy of a packed record (big-endian, as stored
 * on the Palm) and know the record formats of the built-in applications
 * (Datebook/Calendar, To Do/Tasks, Contacts, Expense). They use no
 * globals and no Palm OS calls, so the host tests run the same code.
 */

#ifndef CONVERT_H
#define CONVERT_H

#ifndef HOST_TEST
#include <PalmOS.h>
#else
#include "types.h"
typedef unsigned char Boolean;
#define true  1
#define false 0
#endif

typedef struct
{
  UInt32 dates;           // dates moved
  UInt32 clamped;         // before the new start: set to its first year
  UInt32 overflow;        // after the new end: cannot be moved
  UInt32 limited;         // after the new end: set to its last day (limitOverflow)
  UInt32 malformed;       // records whose layout did not parse
  UInt32 headerClamped;   // database dates before the new start
  Boolean limitOverflow;  // input: dates after the new end become its last
                          // day (31 Dec of the last year) instead of a refusal
} ConvStats;

// record formats
#define CONV_NONE         0
#define CONV_DATEBOOK     1     // Datebook 'date' and Calendar 'PDat'
#define CONV_TODO         2     // classic To Do 'todo'
#define CONV_TASKS        3     // Tasks 'PTod'
#define CONV_CONTACTS     4     // Contacts 'PAdd' (birthday, anniversary)
#define CONV_EXPENSE      5     // Expense 'exps'

// the format of a database, CONV_NONE if its records hold no known dates
UInt16 ConvFormat(UInt32 type, UInt32 creator);

/*
 * Moves the dates of one packed record by deltaYears (negative: towards
 * the later epoch). With apply false the record is only checked and the
 * stats say what would happen. Dates after the end of the window are
 * never written unless stats->limitOverflow is set, which moves them to the
 * last day of the window (stats->limited); a caller refuses the conversion
 * or asks the user if stats->overflow > 0.
 *
 * Returns true if the record parsed.
 */
Boolean ConvRecord(UInt16 format, UInt8 *rec, UInt32 size, Int16 deltaYears,
                   Boolean apply, ConvStats *stats);

/*
 * A database date (seconds since the epoch) moved by deltaDays; 0 means
 * "never" and stays 0. A date before the new start becomes its first
 * second (headerClamped), one past the end of the counter stays.
 */
UInt32 ConvSeconds(UInt32 seconds, Int32 deltaDays, ConvStats *stats);

#endif
