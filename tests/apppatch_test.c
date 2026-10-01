/*
 * apppatch_test.c - the checks that decide whether a site may be written,
 * and the value it gets, for both kinds of year constants
 */
#include <stdio.h>
#include <string.h>
#include "apppatch.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void put(UInt8 *p, UInt16 op, UInt16 imm)
{
  p[0] = op >> 8; p[1] = op & 0xFF; p[2] = imm >> 8; p[3] = imm & 0xFF;
}

int main(void)
{
  static UInt8 code[64];
  const AppPatchSite first = SITE_FIRST_YEAR(3, sizeof(code), 10);
  const AppPatchSite unix = { 1, sizeof(code), 20, 0x3D7C, 0xFFFF, 1970, -1 };
  UInt16 off = 99, v = 0;

  memset(code, 0x4E, sizeof(code));
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_OTHER);      // other code

  // firstYear: addi.w #1904,Dn
  put(code + 10, 0x0640, 1904);
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_ORIGINAL && off == 0);
  put(code + 10, 0x0640, 1932);
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_PATCHED && off == 28);
  put(code + 10, 0x0640, 1933);                                                // not a start year
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_OTHER);
  put(code + 10, 0x0640, 1972);
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_PATCHED && off == 68);
  put(code + 10, 0x0640, 1976);                                                // beyond the range
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_OTHER);
  put(code + 10, 0x0643, 1904);                                                // d3 is as good
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_ORIGINAL);
  put(code + 10, 0x0648, 1904);                                                // another opcode
  CHECK(AppPatchInspect(code, sizeof(code), &first, &off) == SITE_OTHER);
  put(code + 10, 0x0640, 1904);
  CHECK(AppPatchInspect(code, sizeof(code) - 1, &first, &off) == SITE_OTHER);  // other size = other version
  CHECK(AppPatchValue(&first, 28, &v) && v == 1932);
  CHECK(AppPatchValue(&first, 0, &v) && v == 1904);

  // a real year: move.w #1970,-4(a6), moved down
  put(code + 20, 0x3D7C, 1970);
  CHECK(AppPatchInspect(code, sizeof(code), &unix, &off) == SITE_ORIGINAL && off == 0);
  CHECK(AppPatchValue(&unix, 28, &v) && v == 1942);                            // start 1932
  CHECK(AppPatchValue(&unix, 36, &v) && v == 1934);                            // start 1940
  CHECK(AppPatchValue(&unix, 64, &v) && v == 1906);                            // start 1968
  CHECK(!AppPatchValue(&unix, 68, &v));                                        // start 1972: 1970 is before it
  put(code + 20, 0x3D7C, 1942);
  CHECK(AppPatchInspect(code, sizeof(code), &unix, &off) == SITE_PATCHED && off == 28);
  put(code + 20, 0x3D7C, 1998);                                                // moved the wrong way
  CHECK(AppPatchInspect(code, sizeof(code), &unix, &off) == SITE_OTHER);
  put(code + 20, 0x3D7D, 1970);                                                // other opcode
  CHECK(AppPatchInspect(code, sizeof(code), &unix, &off) == SITE_OTHER);

  {
    const AppPatchSite edge = SITE_FIRST_YEAR(3, 12, 10);                     // offset + 4 beyond the end
    CHECK(AppPatchInspect(code, 12, &edge, &off) == SITE_OTHER);
  }

  CHECK(kNumAppPatches >= 1 && kAppPatches[0].numSites >= 1);
  printf("apppatch: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
  return failures != 0;
}
