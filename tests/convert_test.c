/*
 * convert_test.c - host tests of the record conversion (src/convert.c)
 *
 * Builds packed records as the applications store them, moves them to the
 * epoch 1940 (-36 years) and back, and checks every date field.
 */
#include <stdio.h>
#include <string.h>
#include "convert.h"

static int failures;

#define CHECK(cond, what) \
  do { if (!(cond)) { failures++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static void put16(UInt8 *p, UInt16 v) { p[0] = v >> 8; p[1] = (UInt8)v; }
static void put32(UInt8 *p, UInt32 v) { put16(p, v >> 16); put16(p + 2, (UInt16)v); }
static UInt16 get16(const UInt8 *p) { return (UInt16)((p[0] << 8) | p[1]); }

// DateType of a real year (epoch 1904)
static UInt16 date(int year, int month, int day)
{
  return (UInt16)(((year - 1904) << 9) | (month << 5) | day);
}

static int year_of(const UInt8 *p) { return (get16(p) >> 9) + 1904; }

static void
roundtrip(UInt16 format, UInt8 *rec, UInt32 size, const char *what)
{
  UInt8 copy[512];
  ConvStats st;

  memcpy(copy, rec, size);
  memset(&st, 0, sizeof st);
  CHECK(ConvRecord(format, rec, size, -36, true, &st), what);
  CHECK(st.overflow == 0 && st.malformed == 0, what);
  memset(&st, 0, sizeof st);
  CHECK(ConvRecord(format, rec, size, 36, true, &st), what);
  CHECK(memcmp(copy, rec, size) == 0, what);
  CHECK(ConvRecord(format, rec, size, -36, true, &st), what);   // leave moved
}

static void
test_datebook(void)
{
  UInt8 r[64];
  UInt32 n = 0;
  ConvStats st;

  memset(r, 0, sizeof r);
  put16(r + 0, 0x0900); put16(r + 2, 0x0A00);        // 9:00 - 10:00
  put16(r + 4, date(2026, 9, 30));
  put16(r + 6, 0x4000 | 0x2000 | 0x0800 | 0x0400);   // alarm, repeat, exc, desc
  n = 8;
  r[n] = 5; r[n + 1] = 0; n += 2;                    // alarm 5 minutes
  r[n] = 2; put16(r + n + 2, date(2031, 12, 31)); r[n + 4] = 1; n += 8;
  put16(r + n, 2); put16(r + n + 2, date(2027, 1, 5));
  put16(r + n + 4, date(2028, 2, 29)); n += 6;
  memcpy(r + n, "Meeting", 8); n += 8;

  roundtrip(CONV_DATEBOOK, r, n, "datebook roundtrip");
  CHECK(year_of(r + 4) == 2026 - 36, "datebook date");
  CHECK(get16(r + 4) == date(1990, 9, 30), "datebook month/day");
  CHECK(get16(r + 12) == date(1995, 12, 31), "datebook repeat end");
  CHECK(get16(r + 20) == date(1991, 1, 5), "datebook exception 1");
  CHECK(get16(r + 22) == date(1992, 2, 29), "datebook exception 2 (leap day)");
  CHECK(strcmp((char *)r + 24, "Meeting") == 0, "datebook text untouched");

  // repeat forever stays forever
  put16(r + 12, 0xFFFF);
  memset(&st, 0, sizeof st);
  ConvRecord(CONV_DATEBOOK, r, n, 36, true, &st);
  CHECK(get16(r + 12) == 0xFFFF, "datebook repeat forever");

  // truncated record
  memset(&st, 0, sizeof st);
  CHECK(!ConvRecord(CONV_DATEBOOK, r, 15, -36, false, &st), "datebook truncated");
  CHECK(st.malformed == 1, "datebook malformed counted");
}

static void
test_tasks(void)
{
  UInt8 r[64];
  UInt32 n;

  memset(r, 0, sizeof r);
  put16(r, 0x8000 | 0x4000 | 0x2000 | 0x1000 | 0x0800);
  n = 6;
  put16(r + n, date(2031, 1, 2)); n += 2;            // due
  put16(r + n, date(2031, 12, 30)); n += 2;          // completed
  put16(r + n, 0x0800); put16(r + n + 2, 1); n += 4; // alarm 8:00, 1 day
  put16(r + n, date(2025, 3, 1));                    // repeat start
  r[n + 2] = 1; put16(r + n + 4, date(2030, 1, 1)); n += 10;
  memcpy(r + n, "Tax", 4); n += 4;

  roundtrip(CONV_TASKS, r, n, "tasks roundtrip");
  CHECK(get16(r + 6) == date(1995, 1, 2), "tasks due");
  CHECK(get16(r + 8) == date(1995, 12, 30), "tasks completion");
  CHECK(get16(r + 10) == 0x0800, "tasks alarm time untouched");
  CHECK(get16(r + 14) == date(1989, 3, 1), "tasks repeat start");
  CHECK(get16(r + 18) == date(1994, 1, 1), "tasks repeat end");
}

static void
test_contacts(void)
{
  UInt8 r[128];
  UInt32 n = 17, flags1 = 0, flags2 = 0;
  ConvStats st;

  memset(r, 0, sizeof r);
  flags1 |= 1 << 0;  memcpy(r + n, "Smith", 6); n += 6;     // name
  flags1 |= 1 << 1;  memcpy(r + n, "Anna", 5);  n += 5;     // firstName
  flags2 |= 1 << (38 - 28); memcpy(r + n, "", 1); n += 1;   // note
  flags2 |= 1 << 11; put16(r + n, date(1975, 4, 12)); n += 2;
  flags2 |= 1 << 12; put16(r + n, 0x0001); n += 2;          // alarm
  flags2 |= 1 << 13; r[n++] = 3;                            // 3 days ahead
  put32(r + n, 0x42643030UL); put16(r + n + 4, 4);          // picture blob
  memset(r + n + 6, 0xAB, 4); n += 10;
  put32(r + n, 0x42643031UL); put16(r + n + 4, 9);          // Bd01
  put16(r + n + 6, 0); put16(r + n + 8, date(2001, 6, 30));
  n += 15;
  put32(r + 8, flags1); put32(r + 12, flags2);

  roundtrip(CONV_CONTACTS, r, n, "contacts roundtrip");
  CHECK(get16(r + 29) == date(1939, 4, 12), "contacts birthday");
  CHECK(get16(r + 52) == date(1965, 6, 30), "contacts anniversary");

  // born before the start year: month and day kept in the first year
  put16(r + 29, date(1935, 4, 12));
  memset(&st, 0, sizeof st);
  ConvRecord(CONV_CONTACTS, r, n, -36, true, &st);
  CHECK(st.clamped == 1 && get16(r + 29) == date(1904, 4, 12), "contacts clamp");

  // no birthday stored (month 0) stays untouched
  put16(r + 29, 0);
  ConvRecord(CONV_CONTACTS, r, n, -36, true, &st);
  CHECK(get16(r + 29) == 0, "contacts no birthday");
}

static void
test_simple(void)
{
  UInt8 r[8] = { 0 };
  ConvStats st;

  put16(r, date(2029, 11, 11)); r[2] = 1;
  roundtrip(CONV_TODO, r, 5, "todo roundtrip");
  CHECK(get16(r) == date(1993, 11, 11), "todo due");
  put16(r, 0xFFFF);
  ConvRecord(CONV_TODO, r, 5, -36, true, &st);
  CHECK(get16(r) == 0xFFFF, "todo no due date");

  put16(r, date(2030, 5, 5));
  roundtrip(CONV_EXPENSE, r, 8, "expense roundtrip");
  CHECK(get16(r) == date(1994, 5, 5), "expense date");

  // back to 1904: dates after 2031 cannot be stored and are refused
  put16(r, date(2040 - 36, 1, 1));              // real 2040 in epoch 1940
  memset(&st, 0, sizeof st);
  ConvRecord(CONV_EXPENSE, r, 8, 36, false, &st);
  CHECK(st.overflow == 1 && get16(r) == date(2004, 1, 1), "overflow counted");

  // the user chose to keep it: set to the last day of the window
  memset(&st, 0, sizeof st);
  st.limitOverflow = 1;
  ConvRecord(CONV_EXPENSE, r, 8, 36, true, &st);
  CHECK(st.overflow == 0 && st.limited == 1 && get16(r) == date(2031, 12, 31),
        "overflow limited to the last day");

  CHECK(ConvFormat(0x44415441UL, 0x50446174UL) == CONV_DATEBOOK, "format PDat");
  CHECK(ConvFormat(0x44415441UL, 0x6D656D6FUL) == CONV_NONE, "format memo");
  CHECK(ConvFormat(0x7070726FUL, 0x50416464UL) == CONV_NONE, "format not DATA");
}

static void
test_seconds(void)
{
  ConvStats st;
  Int32 d = 13149;                              // 1904-01-01 .. 1940-01-01
  UInt32 s2026 = 3842121600UL;                  // 2025-10-01 epoch 1904

  memset(&st, 0, sizeof st);
  CHECK(ConvSeconds(0, -d, &st) == 0, "seconds never");
  CHECK(ConvSeconds(s2026, -d, &st) == s2026 - 13149UL * 86400, "seconds move");
  CHECK(ConvSeconds(ConvSeconds(s2026, -d, &st), d, &st) == s2026, "seconds back");
  CHECK(ConvSeconds(86400, -d, &st) == 1 && st.headerClamped == 1, "seconds clamp");
  CHECK(ConvSeconds(0xFFFFFF00UL, d, &st) == 0xFFFFFF00UL && st.overflow == 0,
        "seconds past the counter stay");
}

int main(void)
{
  test_datebook();
  test_tasks();
  test_contacts();
  test_simple();
  test_seconds();
  printf("convert: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
  return failures != 0;
}
