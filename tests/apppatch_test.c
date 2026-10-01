/*
 * apppatch_test.c - the checks that decide whether a site may be written
 */
#include <stdio.h>
#include <string.h>
#include "apppatch.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main(void)
{
  static UInt8 code[64];
  const AppPatchSite site = { 3, sizeof(code), 10 };
  UInt16 year = 0;

  memset(code, 0x4E, sizeof(code));
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_OTHER);      // other code

  code[10] = 0x06; code[11] = 0x40; code[12] = 0x07; code[13] = 0x70;          // addi.w #1904,d0
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_ORIGINAL && year == 1904);

  code[12] = 0x07; code[13] = 0x8C;                                            // 1932
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_PATCHED && year == 1932);

  code[13] = 0x8D;                                                             // 1933: not a start year
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_OTHER);
  code[12] = 0x07; code[13] = 0xB4;                                            // 1972
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_PATCHED && year == 1972);
  code[13] = 0xB8;                                                             // 1976: beyond the range
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_OTHER);

  code[12] = 0x07; code[13] = 0x70;
  CHECK(AppPatchInspect(code, sizeof(code) - 1, &site, &year) == SITE_OTHER);  // other size = other version
  code[11] = 0x43;                                                             // addi.w #1904,d3 is as good
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_ORIGINAL && year == 1904);
  code[11] = 0x48;                                                             // another opcode
  CHECK(AppPatchInspect(code, sizeof(code), &site, &year) == SITE_OTHER);
  code[11] = 0x40;

  {
    const AppPatchSite edge = { 3, 12, 10 };                                   // offset + 4 beyond the end
    CHECK(AppPatchInspect(code, 12, &edge, &year) == SITE_OTHER);
  }

  CHECK(kNumAppPatches >= 1 && kAppPatches[0].numSites >= 1);
  printf("apppatch: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
  return failures != 0;
}
