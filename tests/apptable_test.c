/*
 * apptable_test.c - reads the table that tools/apptable.py built from
 * apps/apps.txt (argv[1]) and checks it against the sites DateFix had as a
 * C array before; then damages the table in every way the check must catch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apptable.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static int same(const AppPatchSite *a, UInt16 res, UInt32 size, UInt16 off,
                UInt16 op, UInt16 mask, UInt16 c, Int16 dir)
{
  return a->resource == res && a->size == size && a->offset == off && a->opcode == op
      && a->opcodeMask == mask && a->constant == c && a->direction == dir;
}

static int valid(const UInt8 *t, UInt32 n)
{
  UInt32 v;
  UInt16 apps;
  return AppTableCheck(t, n, &v, &apps);
}

int main(int argc, char **argv)
{
  static UInt8 t[4096], bad[4096];
  UInt32 n, version = 0, pos;
  UInt16 apps = 0, i;
  AppTableApp app[4];
  AppPatchSite site;
  FILE *f;

  if (argc != 2 || !(f = fopen(argv[1], "rb")))
  {
    printf("usage: apptable_test table.bin\n");
    return 2;
  }
  n = fread(t, 1, sizeof(t), f);
  fclose(f);

  // the table from apps/apps.txt
  CHECK(AppTableCheck(t, n, &version, &apps));
  CHECK(version == 2026100201UL);
  CHECK(apps == 4);
  for (i = 0, pos = appTableFirstApp; i < apps && i < 4; i++)
    pos = AppTableReadApp(t, pos, &app[i]);
  CHECK(pos == n);

  CHECK(app[0].creator == 'HsDB' && strcmp(app[0].name, "DateBk3h") == 0 && app[0].numSites == 5);
  AppTableReadSite(t, &app[0], 0, &site);
  CHECK(same(&site, 3, 35926, 0x1312, 0x0640, 0xFFF8, 1904, 1));
  AppTableReadSite(t, &app[0], 4, &site);
  CHECK(same(&site, 4, 34960, 0x627c, 0x0640, 0xFFF8, 1904, 1));
  CHECK(app[1].creator == 'HsDR' && strcmp(app[1].name, "DateBk3x") == 0 && app[1].numSites == 5);
  AppTableReadSite(t, &app[1], 2, &site);                       // the alias has the same sites
  CHECK(same(&site, 4, 34960, 0x61f8, 0x0640, 0xFFF8, 1904, 1));
  CHECK(app[2].creator == 'TiCo' && strcmp(app[2].name, "TimeCopy") == 0 && app[2].numSites == 1);
  AppTableReadSite(t, &app[2], 0, &site);
  CHECK(same(&site, 1, 8448, 0x185e, 0x3D7C, 0xFFFF, 1970, -1));
  CHECK(app[3].creator == 'YrPb' && app[3].numSites == 1);
  AppTableReadSite(t, &app[3], 0, &site);
  CHECK(same(&site, 1, 868, 0x00F0, 0x0640, 0xFFF8, 1904, 1));

  // damaged tables
  CHECK(!AppTableCheck(NULL, 0, &version, &apps));
  CHECK(!valid(t, 11));                                         // shorter than the header
  CHECK(!valid(t, n - 1));                                      // last site cut off
  memcpy(bad, t, n); bad[n] = 0;
  CHECK(!valid(bad, n + 1));                                    // something behind the table
  memcpy(bad, t, n); bad[0] = 'X';
  CHECK(!valid(bad, n));                                        // magic
  memcpy(bad, t, n); bad[5] = 2;
  CHECK(!valid(bad, n));                                        // format
  memcpy(bad, t, n); bad[11] = 5;
  CHECK(!valid(bad, n));                                        // more applications than there are
  memcpy(bad, t, n); bad[11] = 3;
  CHECK(!valid(bad, n));                                        // fewer: bytes left over
  memcpy(bad, t, n); memset(bad + appTableFirstApp + 4, 'A', appTableNameLen);
  CHECK(!valid(bad, n));                                        // name not terminated
  memcpy(bad, t, n); bad[appTableFirstApp + 4] = 0;
  CHECK(!valid(bad, n));                                        // empty name
  memcpy(bad, t, n); bad[appTableFirstApp + 4 + appTableNameLen] = 0xFF;
  CHECK(!valid(bad, n));                                        // number of sites far too large
  memcpy(bad, t, n); bad[appTableFirstApp + 4 + appTableNameLen + 1] = 0;
  CHECK(!valid(bad, n));                                        // zero sites

  printf("apptable: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
  return failures != 0;
}
