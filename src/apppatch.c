/*
 * apppatch.c - see apppatch.h
 */

#include "apppatch.h"

#ifdef HOST_TEST
#define sysFileCApplication 0
#endif

// Date Book+ 3.0H (Handspring Visor ROM, database "DateBk3h", creator 'HsDB').
// Found with tools/yearfinder, confirmed in the emulator:
//  code 3: the year view's title
//  code 4: the year in the titles of the week and two-week view (four calls
//          of the function that draws "year / two-digit year")
static const AppPatchSite kDateBook3h[] =
{
  { 3, 35926, 0x1312 },
  { 4, 34960, 0x61da },
  { 4, 34960, 0x61f8 },
  { 4, 34960, 0x6240 },
  { 4, 34960, 0x627c },
};

// tools/probes/yearprobe.c, the test fixture for the mechanism on every Palm OS
static const AppPatchSite kYearProbe[] =
{
  { 1, 868, 0x00F0 },
};

const AppPatchApp kAppPatches[] =
{
  { "DateBk3h", 'HsDB', kDateBook3h, sizeof(kDateBook3h) / sizeof(kDateBook3h[0]) },
  // the same code under its own name and creator: a copy for a device that has
  // Date Book+ in ROM, where the ROM version cannot be patched and wins the
  // launch of the original creator (tools/ramcopy)
  { "DateBk3x", 'HsDR', kDateBook3h, sizeof(kDateBook3h) / sizeof(kDateBook3h[0]) },
  { "YearProbe", 'YrPb', kYearProbe, sizeof(kYearProbe) / sizeof(kYearProbe[0]) },
};

const UInt16 kNumAppPatches = sizeof(kAppPatches) / sizeof(kAppPatches[0]);

UInt16
AppPatchInspect(const UInt8 *code, UInt32 size, const AppPatchSite *site,
                UInt16 *year)
{
  const UInt8 *p;
  UInt16 w;

  if (size != site->size || (UInt32)site->offset + 4 > size)
    return SITE_OTHER;
  p = code + site->offset;
  if (p[0] != 0x06 || (p[1] & 0xF8) != 0x40)     // addi.w #imm,Dn
    return SITE_OTHER;
  w = ((UInt16)p[2] << 8) | p[3];
  if (w == 1904)
  {
    *year = w;
    return SITE_ORIGINAL;
  }
  if (w > 1904 && w <= 1972 && (w - 1904) % 4 == 0)
  {
    *year = w;
    return SITE_PATCHED;
  }
  return SITE_OTHER;
}

#ifndef HOST_TEST

void
AppPatchSet(UInt16 year, AppPatchStats *stats)
{
  UInt16 a, s, card, index;
  LocalID id;
  DmSearchStateType state;
  Boolean first;
  DmOpenRef db;
  MemHandle h;
  UInt8 *code;
  UInt16 current, kind;
  UInt8 word[2];

  stats->patched = stats->unchanged = stats->other = stats->locked = 0;
  word[0] = year >> 8;
  word[1] = year & 0xFF;

  for (a = 0; a < kNumAppPatches; a++)
  {
    const AppPatchApp *app = &kAppPatches[a];

    for (first = true; ; first = false)
    {
      if (DmGetNextDatabaseByTypeCreator(first, &state, sysFileTApplication,
                                         app->creator, false, &card, &id) != errNone
          || !id)
        break;

      db = DmOpenDatabase(card, id, dmModeReadWrite);
      if (!db)                                     // in ROM or in use
      {
        stats->locked++;
        continue;
      }
      for (s = 0; s < app->numSites; s++)
      {
        const AppPatchSite *site = &app->sites[s];

        index = DmFindResource(db, 'code', site->resource, NULL);
        if (index == 0xFFFF) { stats->other++; continue; }
        h = DmGetResourceIndex(db, index);
        if (!h) { stats->other++; continue; }
        code = MemHandleLock(h);
        kind = AppPatchInspect(code, MemHandleSize(h), site, &current);
        if (kind == SITE_OTHER)
          stats->other++;
        else if (current == year)
          stats->unchanged++;
        else
        {
          DmWrite(code, site->offset + 2, word, 2);
          stats->patched++;
        }
        MemHandleUnlock(h);
        DmReleaseResource(h);
      }
      DmCloseDatabase(db);
    }
  }
}

#endif
