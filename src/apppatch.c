/*
 * apppatch.c - see apppatch.h
 */

#include "apppatch.h"
#ifndef HOST_TEST
#include "apptable.h"
#include "datefix.h"            // appCreator: DateFixApps belongs to DateFix
#endif

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

/* An application table in use: the built-in resource or the database. */
typedef struct
{
  MemHandle    handle;
  DmOpenRef    db;              // DateFixApps, NULL for the resource
  const UInt8 *bytes;
  UInt32       size;
  UInt32       version;
} Table;

static void
TableClose(Table *t)
{
  if (t->handle)
  {
    MemHandleUnlock(t->handle);
    if (!t->db)
      DmReleaseResource(t->handle);
  }
  if (t->db)
    DmCloseDatabase(t->db);
  t->handle = NULL;
  t->db = NULL;
}

/* Locks h and keeps it if it holds a valid table. */
static Boolean
TableTake(Table *t, MemHandle h, DmOpenRef db)
{
  UInt16 apps;

  t->handle = h;
  t->db = db;
  t->bytes = MemHandleLock(h);
  t->size = MemHandleSize(h);
  if (AppTableCheck(t->bytes, t->size, &t->version, &apps))
    return true;
  TableClose(t);
  return false;
}

/*
 * Opens the valid table with the higher version: the one built into DateFix
 * or DateFixApps, if the user installed it. Runs in DateFix's own launch:
 * DmGet1Resource finds DateFix's resource.
 */
static Boolean
TableOpen(Table *use, Boolean *fromDb)
{
  Table builtin = { 0 }, extra = { 0 };
  Boolean haveBuiltin = false, haveExtra = false;
  DmSearchStateType state;
  UInt16 card;
  LocalID id;
  DmOpenRef db;
  MemHandle h;

  h = DmGet1Resource(appTableResType, appTableResID);
  if (h)
    haveBuiltin = TableTake(&builtin, h, NULL);

  if (DmGetNextDatabaseByTypeCreator(true, &state, appTableDBType, appCreator,
                                     true, &card, &id) == errNone && id)
  {
    db = DmOpenDatabase(card, id, dmModeReadOnly);
    if (db)
    {
      h = DmNumRecords(db) == 1 ? DmQueryRecord(db, 0) : NULL;
      if (h)
        haveExtra = TableTake(&extra, h, db);
      else
        DmCloseDatabase(db);
    }
  }

  if (haveExtra && (!haveBuiltin || extra.version > builtin.version))
  {
    if (haveBuiltin) TableClose(&builtin);
    *use = extra;
    *fromDb = true;
    return true;
  }
  if (haveExtra) TableClose(&extra);
  *use = builtin;
  *fromDb = false;
  return haveBuiltin;
}

Boolean
AppPatchTableInfo(UInt32 *version, Boolean *fromDb)
{
  Table t;

  if (!TableOpen(&t, fromDb))
    return false;
  *version = t.version;
  TableClose(&t);
  return true;
}

void
AppPatchSet(UInt16 startYear, AppPatchStats *stats)
{
  UInt16 a, s, card, index, numApps;
  LocalID id;
  DmSearchStateType state;
  Boolean first;
  DmOpenRef db;
  MemHandle h;
  UInt8 *code;
  UInt16 current, kind, value, offset = startYear - 1904;
  UInt8 word[2];
  Table table;
  UInt32 pos, version;
  AppTableApp app;
  AppPatchSite site;

  stats->patched = stats->unchanged = stats->other = stats->locked = 0;
  stats->table = 0;
  stats->tableFromDb = false;
  if (!TableOpen(&table, &stats->tableFromDb))
    return;
  AppTableCheck(table.bytes, table.size, &version, &numApps);
  stats->table = version;

  for (a = 0, pos = appTableFirstApp; a < numApps; a++)
  {
    pos = AppTableReadApp(table.bytes, pos, &app);

    for (first = true; ; first = false)
    {
      if (DmGetNextDatabaseByTypeCreator(first, &state, sysFileTApplication,
                                         app.creator, false, &card, &id) != errNone
          || !id)
        break;

      db = DmOpenDatabase(card, id, dmModeReadWrite);
      if (!db)                                     // in ROM or in use
      {
        stats->locked++;
        continue;
      }
      for (s = 0; s < app.numSites; s++)
      {
        AppTableReadSite(table.bytes, &app, s, &site);
        index = DmFindResource(db, 'code', site.resource, NULL);
        if (index == 0xFFFF) { stats->other++; continue; }
        h = DmGetResourceIndex(db, index);
        if (!h) { stats->other++; continue; }
        code = MemHandleLock(h);
        kind = AppPatchInspect(code, MemHandleSize(h), &site, &current);
        if (kind == SITE_OTHER || !AppPatchValue(&site, offset, &value))
          stats->other++;
        else if (current == offset)
          stats->unchanged++;
        else
        {
          word[0] = value >> 8;
          word[1] = value & 0xFF;
          DmWrite(code, site.offset + 2, word, 2);
          stats->patched++;
        }
        MemHandleUnlock(h);
        DmReleaseResource(h);
      }
      DmCloseDatabase(db);
    }
  }
  TableClose(&table);
}

#endif
