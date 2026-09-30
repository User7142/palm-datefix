/*
 * convert.c - moves stored dates to another epoch (see convert.h)
 *
 * Record layouts: palmOne PIM SDK (DateDB.h, ToDoDB.h, AddressDB.h and
 * "Accessing PIM Databases", AN-4) and the Palm OS 3.5 sample sources of
 * Datebook, To Do and Expense. All values are big-endian; bit fields are
 * allocated from the most significant bit, as the 68k compilers do.
 */

#include "convert.h"

#define NO_DATE            0xFFFF    // "no due date", "repeat forever"

#define BIRTHDAY_DATE_BIT  (39 - 28)  // Contacts field 39 in the 2nd flag word
#define BIRTHDAY_MASK_BIT  (40 - 28)
#define BIRTHDAY_PRESET_BIT (41 - 28)
#define CONTACTS_STRINGS   39         // name .. note
#define CONTACTS_BITS2     28         // first field in the 2nd flag word
#define CONTACTS_HEADER    17         // options 8, flags 8, company offset 1
#define RELEASE2_BLOB      0x42643031UL  // 'Bd01': dirty, anniversary, tone

static UInt16
Get16(const UInt8 *p)
{
  return (UInt16)((p[0] << 8) | p[1]);
}

static UInt32
Get32(const UInt8 *p)
{
  return ((UInt32)p[0] << 24) | ((UInt32)p[1] << 16) |
         ((UInt32)p[2] << 8) | p[3];
}

/**
 * Moves one DateType (7-bit year since the epoch, month, day). Dates
 * before the new start keep month and day in its first year; dates after
 * the new end are left alone and counted, the caller refuses them.
 */
static void
MoveDate(UInt8 *p, Int16 deltaYears, Boolean apply, ConvStats *stats)
{
  UInt16 value = Get16(p);
  Int16  year;

  if ((value == NO_DATE) || (((value >> 5) & 0x0F) == 0))
    return;                                     // no date stored

  year = (Int16)(value >> 9) + deltaYears;
  if (year > 127)
  {
    if (!stats->limitOverflow)
    {
      stats->overflow++;
      return;
    }
    stats->limited++;
    stats->dates++;
    if (apply)
    {
      value = (UInt16)((127 << 9) | (12 << 5) | 31);     // 31 Dec, last year
      p[0] = (UInt8)(value >> 8);
      p[1] = (UInt8)value;
    }
    return;
  }
  if (year < 0)
  {
    stats->clamped++;
    year = 0;
  }
  stats->dates++;
  if (apply)
  {
    value = (UInt16)((year << 9) | (value & 0x01FF));
    p[0] = (UInt8)(value >> 8);
    p[1] = (UInt8)value;
  }
}

/**
 * Datebook and Calendar: when (start, end, date), flags, then optional
 * alarm, repeat info (with its end date) and the exception dates. The
 * strings and the Calendar blobs (time zone, meeting) that follow hold no
 * dates.
 */
static Boolean
ConvDatebook(UInt8 *rec, UInt32 size, Int16 delta, Boolean apply,
             ConvStats *stats)
{
  UInt16 flags, count;
  UInt32 p = 8;

  if (size < 8) return false;
  MoveDate(rec + 4, delta, apply, stats);
  flags = Get16(rec + 6);

  if (flags & 0x4000)                           // alarm: advance, unit
    p += 2;
  if (flags & 0x2000)                           // repeat: type, reserved,
  {                                             // end date, frequency, ...
    if (p + 8 > size) return false;
    MoveDate(rec + p + 2, delta, apply, stats);
    p += 8;
  }
  if (flags & 0x0800)                           // exceptions: count, dates
  {
    if (p + 2 > size) return false;
    count = Get16(rec + p);
    p += 2;
    if (p + 2UL * count > size) return false;
    for (; count > 0; count--, p += 2)
      MoveDate(rec + p, delta, apply, stats);
  }
  return true;
}

/**
 * Tasks: data flags, record flags, priority, then optional due date,
 * completion date, alarm (time, advance days) and repeat (start date,
 * repeat info with its end date).
 */
static Boolean
ConvTasks(UInt8 *rec, UInt32 size, Int16 delta, Boolean apply,
          ConvStats *stats)
{
  UInt16 flags;
  UInt32 p = 6;

  if (size < 6) return false;
  flags = Get16(rec);

  if (flags & 0x8000)                           // due date
  {
    if (p + 2 > size) return false;
    MoveDate(rec + p, delta, apply, stats);
    p += 2;
  }
  if (flags & 0x4000)                           // completion date
  {
    if (p + 2 > size) return false;
    MoveDate(rec + p, delta, apply, stats);
    p += 2;
  }
  if (flags & 0x2000)                           // alarm time, advance days
    p += 4;
  if (flags & 0x1000)                           // repeat
  {
    if (p + 10 > size) return false;
    MoveDate(rec + p, delta, apply, stats);     // start date
    MoveDate(rec + p + 4, delta, apply, stats); // repeat end date
  }
  return true;
}

static Boolean
ContactsFlag(const UInt8 *rec, UInt16 field)
{
  if (field < CONTACTS_BITS2)
    return (Get32(rec + 8) >> field) & 1;
  return (Get32(rec + 12) >> (field - CONTACTS_BITS2)) & 1;
}

/**
 * Contacts: options, flags, company offset, the strings whose flags are
 * set, then birthday date, birthday flags, advance days, then blobs; the
 * 'Bd01' blob holds the anniversary after its dirty word.
 */
static Boolean
ConvContacts(UInt8 *rec, UInt32 size, Int16 delta, Boolean apply,
             ConvStats *stats)
{
  UInt32 p = CONTACTS_HEADER, content;
  UInt16 field, blobSize;

  if (size < CONTACTS_HEADER) return false;

  for (field = 0; field < CONTACTS_STRINGS; field++)
    if (ContactsFlag(rec, field))
    {
      while ((p < size) && rec[p]) p++;
      if (p >= size) return false;
      p++;
    }

  if (ContactsFlag(rec, CONTACTS_BITS2 + BIRTHDAY_DATE_BIT))
  {
    if (p + 2 > size) return false;
    MoveDate(rec + p, delta, apply, stats);
    p += 2;
  }
  if (ContactsFlag(rec, CONTACTS_BITS2 + BIRTHDAY_MASK_BIT)) p += 2;
  if (ContactsFlag(rec, CONTACTS_BITS2 + BIRTHDAY_PRESET_BIT)) p += 1;

  while (p + 6 <= size)
  {
    blobSize = Get16(rec + p + 4);
    content  = p + 6;
    if (content + blobSize > size) return false;
    if ((Get32(rec + p) == RELEASE2_BLOB) && (blobSize >= 4))
      MoveDate(rec + content + 2, delta, apply, stats);
    p = content + blobSize;
  }
  return p <= size;
}

UInt16
ConvFormat(UInt32 type, UInt32 creator)
{
  if (type != 0x44415441UL)                     // 'DATA'
    return CONV_NONE;
  switch (creator)
  {
    case 0x64617465UL:                          // 'date'
    case 0x50446174UL: return CONV_DATEBOOK;    // 'PDat'
    case 0x746F646FUL: return CONV_TODO;        // 'todo'
    case 0x50546F64UL: return CONV_TASKS;       // 'PTod'
    case 0x50416464UL: return CONV_CONTACTS;    // 'PAdd'
    case 0x65787073UL: return CONV_EXPENSE;     // 'exps'
    default:           return CONV_NONE;
  }
}

Boolean
ConvRecord(UInt16 format, UInt8 *rec, UInt32 size, Int16 deltaYears,
           Boolean apply, ConvStats *stats)
{
  Boolean ok;

  switch (format)
  {
    case CONV_DATEBOOK:
         ok = ConvDatebook(rec, size, deltaYears, apply, stats);
         break;
    case CONV_TASKS:
         ok = ConvTasks(rec, size, deltaYears, apply, stats);
         break;
    case CONV_CONTACTS:
         ok = ConvContacts(rec, size, deltaYears, apply, stats);
         break;
    case CONV_TODO:                             // due date, priority, text
    case CONV_EXPENSE:                          // date, type, payment, ...
         ok = (size >= 3);
         if (ok) MoveDate(rec, deltaYears, apply, stats);
         break;
    default:
         ok = true;
         break;
  }
  if (!ok) stats->malformed++;
  return ok;
}

UInt32
ConvSeconds(UInt32 seconds, Int32 deltaDays, ConvStats *stats)
{
  UInt32 shift;

  if (seconds == 0) return 0;                   // never (e.g. backed up)
  if (deltaDays < 0)
  {
    shift = (UInt32)(-deltaDays) * 86400UL;
    if (seconds <= shift)
    {
      stats->headerClamped++;
      return 1;                                 // first second of the window
    }
    return seconds - shift;
  }
  shift = (UInt32)deltaDays * 86400UL;
  if (seconds > 0xFFFFFFFFUL - shift)
    return seconds;
  return seconds + shift;
}
