/*
 * apptable.c - see apptable.h
 */

#include "apptable.h"

#define APP_HEADER   (4 + appTableNameLen + 2)   // creator, name, number of sites
#define SITE_SIZE    16
#define FORMAT       1

static UInt16
Get16(const UInt8 *p)
{
  return ((UInt16)p[0] << 8) | p[1];
}

static UInt32
Get32(const UInt8 *p)
{
  return ((UInt32)Get16(p) << 16) | Get16(p + 2);
}

Boolean
AppTableCheck(const UInt8 *table, UInt32 size, UInt32 *version, UInt16 *numApps)
{
  UInt32 pos = appTableFirstApp;
  UInt16 a, n, sites, k;

  if (!table || size < appTableFirstApp
      || table[0] != 'D' || table[1] != 'F' || table[2] != 'A' || table[3] != 'T'
      || Get16(table + 4) != FORMAT)
    return false;
  n = Get16(table + 10);
  for (a = 0; a < n; a++)
  {
    if (size - pos < APP_HEADER)
      return false;
    for (k = 0; k < appTableNameLen && table[pos + 4 + k]; k++)
      ;
    if (k == 0 || k == appTableNameLen)           // empty or not terminated
      return false;
    sites = Get16(table + pos + 4 + appTableNameLen);
    pos += APP_HEADER;
    if (sites == 0 || (size - pos) / SITE_SIZE < sites)
      return false;
    pos += (UInt32)sites * SITE_SIZE;
  }
  if (pos != size)                                 // nothing may follow
    return false;
  *version = Get32(table + 6);
  *numApps = n;
  return true;
}

UInt32
AppTableReadApp(const UInt8 *table, UInt32 pos, AppTableApp *app)
{
  UInt16 k;

  app->creator = Get32(table + pos);
  for (k = 0; k < appTableNameLen; k++)
    app->name[k] = table[pos + 4 + k];
  app->name[appTableNameLen - 1] = 0;
  app->numSites = Get16(table + pos + 4 + appTableNameLen);
  app->sitesPos = pos + APP_HEADER;
  return app->sitesPos + (UInt32)app->numSites * SITE_SIZE;
}

void
AppTableReadSite(const UInt8 *table, const AppTableApp *app, UInt16 i, AppPatchSite *site)
{
  const UInt8 *p = table + app->sitesPos + (UInt32)i * SITE_SIZE;

  site->resource   = Get16(p);
  site->size       = Get32(p + 2);
  site->offset     = Get16(p + 6);
  site->opcode     = Get16(p + 8);
  site->opcodeMask = Get16(p + 10);
  site->constant   = Get16(p + 12);
  site->direction  = (Int16)Get16(p + 14);
}
