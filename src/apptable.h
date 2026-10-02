/*
 * apptable.h - the application table as data (built by tools/apptable.py)
 *
 * Why: the sites of the applications that draw the year themselves used to be
 * a C array, so every new application version needed a new DateFix build.
 * Now they are data, from one source (apps/apps.txt): a resource built into
 * DateFix.prc, and optionally a database DateFixApps (type 'DFat', creator
 * 'DtFx') that a user installs like an application. DateFix uses whichever
 * valid table has the higher version, so a newer table needs no new DateFix.
 *
 * The table comes from a file a user may have installed, so it is checked
 * completely (AppTableCheck) before anything is read from it, and read byte
 * by byte (the 68k faults on word access at an odd address).
 *
 * Layout: see tools/apptable.py.
 */

#ifndef APPTABLE_H
#define APPTABLE_H

#include "apppatch.h"

#define appTableResType   'DFat'    // in DateFix.prc
#define appTableResID     1000
#define appTableDBType    'DFat'    // DateFixApps.pdb, creator appCreator
#define appTableNameLen   32

typedef struct
{
  UInt32 creator;
  char   name[appTableNameLen];     // NUL-terminated
  UInt16 numSites;
  UInt32 sitesPos;                  // where its sites start in the table
} AppTableApp;

/**
 * Checks magic, format and that every application and site lies inside the
 * table and the table ends exactly after the last one.
 * Only a table that passed may be read with the functions below.
 */
Boolean AppTableCheck(const UInt8 *table, UInt32 size, UInt32 *version, UInt16 *numApps);

/** Position of the first application. */
#define appTableFirstApp  12

/**
 * Reads the application at pos into app.
 * @return position of the next application
 */
UInt32 AppTableReadApp(const UInt8 *table, UInt32 pos, AppTableApp *app);

/** Reads site i of app. */
void AppTableReadSite(const UInt8 *table, const AppTableApp *app, UInt16 i, AppPatchSite *site);

#endif
