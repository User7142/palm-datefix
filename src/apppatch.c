/*
 * apppatch.c - see apppatch.h
 */

#include "apppatch.h"

#ifdef HOST_TEST
#define sysFileCApplication 0
#endif

// Date Book+ 3.0H (Handspring Visor ROM, database "DateBk3h", creator 'HsDB').
// Found with tools/yearfinder, confirmed in the emulator and on a Visor:
//  code 3: the year view's title
//  code 4: the year in the titles of the week and two-week view (four calls
//          of the function that draws "year / two-digit year")
static const AppPatchSite kDateBook3h[] =
{
  SITE_FIRST_YEAR(3, 35926, 0x1312),
  SITE_FIRST_YEAR(4, 34960, 0x61da),
  SITE_FIRST_YEAR(4, 34960, 0x61f8),
  SITE_FIRST_YEAR(4, 34960, 0x6240),
  SITE_FIRST_YEAR(4, 34960, 0x627c),
};

// TimeCopy 1.4 (creator 'TiCo'): its conduit sends the desktop's time as
// Unix seconds; the application adds TimDateTimeToSeconds(1 Jan 1970), which
// with a moved epoch is 1970 internal = 1970 + offset real. The year in the
// DateTimeType it builds (`move.w #1970,-4(a6)`) has to be the internal year
// of the real 1970.
static const AppPatchSite kTimeCopy[] =
{
  { 1, 8448, 0x185e, 0x3D7C, 0xFFFF, 1970, -1 },
};

// tools/probes/yearprobe.c, the test fixture for the mechanism on every Palm OS
static const AppPatchSite kYearProbe[] =
{
  SITE_FIRST_YEAR(1, 868, 0x00F0),
};

#define APP(name, creator, sites) { name, creator, sites, sizeof(sites) / sizeof(sites[0]) }

const AppPatchApp kAppPatches[] =
{
  APP("DateBk3h", 'HsDB', kDateBook3h),
  // the same code under its own name and creator: a copy for a device that has
  // Date Book+ in ROM, where the ROM version cannot be patched and wins the
  // launch of the original creator (tools/ramcopy)
  APP("DateBk3x", 'HsDR', kDateBook3h),
  APP("TimeCopy", 'TiCo', kTimeCopy),
  APP("YearProbe", 'YrPb', kYearProbe),
};

const UInt16 kNumAppPatches = sizeof(kAppPatches) / sizeof(kAppPatches[0]);

UInt16
AppPatchInspect(const UInt8 *code, UInt32 size, const AppPatchSite *site,
                UInt16 *offset)
{
  const UInt8 *p;
  UInt16 op, w;
  Int16 d;

  if (size != site->size || (UInt32)site->offset + 4 > size)
    return SITE_OTHER;
  p = code + site->offset;
  op = ((UInt16)p[0] << 8) | p[1];
  if ((op & site->opcodeMask) != site->opcode)
    return SITE_OTHER;
  w = ((UInt16)p[2] << 8) | p[3];
  d = ((Int16)w - (Int16)site->constant) * site->direction;
  if (d == 0)
  {
    *offset = 0;
    return SITE_ORIGINAL;
  }
  if (d > 0 && d <= 1972 - 1904 && d % 4 == 0)
  {
    *offset = d;
    return SITE_PATCHED;
  }
  return SITE_OTHER;
}

Boolean
AppPatchValue(const AppPatchSite *site, UInt16 offset, UInt16 *value)
{
  Int16 v = (Int16)site->constant + site->direction * (Int16)offset;

  if (v < 1904)                                   // TimDateTimeToSeconds starts in 1904
    return false;
  *value = v;
  return true;
}

#ifndef HOST_TEST

void
AppPatchSet(UInt16 startYear, AppPatchStats *stats)
{
  UInt16 a, s, card, index;
  LocalID id;
  DmSearchStateType state;
  Boolean first;
  DmOpenRef db;
  MemHandle h;
  UInt8 *code;
  UInt16 current, kind, value, offset = startYear - 1904;
  UInt8 word[2];

  stats->patched = stats->unchanged = stats->other = stats->locked = 0;

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
        if (kind == SITE_OTHER || !AppPatchValue(site, offset, &value))
          stats->other++;
        else if (current == offset)
          stats->unchanged++;
        else
        {
          word[0] = value >> 8;
          word[1] = value & 0xFF;
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
