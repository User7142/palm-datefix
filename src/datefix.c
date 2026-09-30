/*
 * datefix.c - DateFix for Palm OS 5: a selectable epoch instead of 1904
 *
 * Palm OS counts dates from 1904: the clock in 32-bit seconds, DateType in
 * a 7-bit year, so the calendar ends on 2031-12-31. DateFix moves the
 * epoch to a start year S (1904..1972, steps of 4): clock and stored dates
 * count from S, applications keep working with internal years 1904..2031,
 * which are the real years S..S+127 (see docs/plan-epoch.md).
 *
 * Internal and real years have the same leap years, so only the weekday
 * and the year shown to the user differ. DateFix replaces those functions
 * in the export table of the Boot module with the native code in the
 * 'armc' resource (datefix_arm.c):
 *
 *   trap  ->  PACE shim (Thumb)  ->  veneer  ->  Boot export table  ->  code
 *
 * Patching the table reaches every caller - 68k applications and native
 * modules alike. The table index of each function is not compiled in: it
 * is decoded from the shim and the veneer at run time, and nothing is
 * patched unless every instruction matches the expected pattern.
 *
 * The patch lasts until the next reset; DateFix installs it again on
 * sysAppLaunchCmdSystemReset while it is enabled. That launch has no
 * globals: everything it reaches must not use initialised data
 * (tools/check_reset_path.py checks this on every build).
 */

#include <PalmOS.h>
#include <VFSMgr.h>
#include "datefix.h"
#include "convert.h"
#include "selectday.h"
#include "clockcheck.h"
#include "m68k.h"
#include "armc_offsets.h"

// ---------------------------------------------------------------------------
// Palm OS 5 declarations missing in the 4.0 SDK
// ---------------------------------------------------------------------------

#define sysTrapPceNativeCall  0xA45A
typedef UInt32 NativeFuncType(const void *emulStateP, void *userData68KP,
                              void *call68KFuncP);
UInt32 PceNativeCall(NativeFuncType *nativeFuncP, void *userDataP)
  SYS_TRAP(sysTrapPceNativeCall);

// ---------------------------------------------------------------------------
// epoch
// ---------------------------------------------------------------------------

#define EPOCH_YEAR          1904        // Palm OS
#define MIN_START_YEAR      1904
#define MAX_START_YEAR      1972        // window ends 2099, before 2100
#define DEFAULT_START_YEAR  1940
#define DAYS_PER_4_YEARS    1461UL      // no century year in 1904..2099
#define SECONDS_PER_DAY     86400UL
#define WINDOW_DAYS         46752UL     // 128 years
#define WEEKDAY_1904        5           // 1904-01-01 was a Friday

static Boolean
ValidStartYear(UInt16 year)
{
  return (year >= MIN_START_YEAR) && (year <= MAX_START_YEAR) &&
         (((year - EPOCH_YEAR) % 4) == 0);
}

/**
 * Days from 1904-01-01 to startYear-01-01. Exact because every 4-year
 * block between 1904 and 2099 has one leap year.
 */
static UInt32
OffsetDays(UInt16 startYear)
{
  return (UInt32)((startYear - EPOCH_YEAR) / 4) * DAYS_PER_4_YEARS;
}

// ---------------------------------------------------------------------------
// the functions DateFix replaces
// ---------------------------------------------------------------------------

typedef struct
{
  UInt16 trap;
  UInt32 codeOffset;            // replacement in the armc resource
  Int16  origSlot;              // >= 0: wrapper, original goes to this slot
} Target;

#define TARGET_COUNT  8                 // 6 date functions + 2 for the picker
#define REQUIRED_TARGETS 6              // the picker entries are optional

/**
 * A switch instead of a table: this also runs on sysAppLaunchCmdSystemReset,
 * where an application has no globals - and prc-tools keeps initialised
 * data, even const, in the globals.
 */
static void
GetTarget(UInt16 i, Target *target)
{
  target->origSlot = -1;
  switch (i)
  {
    case 0:  target->trap = 0xA0FC; target->codeOffset = OFS_DfSecondsToDateTime; break;
    case 1:  target->trap = 0xA25F; target->codeOffset = OFS_DfDayOfWeek;         break;
    case 2:  target->trap = 0xA261; target->codeOffset = OFS_DfDayOfMonth;        break;
    case 3:  target->trap = 0xA266; target->codeOffset = OFS_DfDateToAscii;
             target->origSlot = 0;                                                break;
    case 4:  target->trap = 0xA267; target->codeOffset = OFS_DfDateToDOWDMFormat;
             target->origSlot = 1;                                                break;
    case 5:  target->trap = 0xA3CD; target->codeOffset = OFS_DfDateTemplateToAscii;
             target->origSlot = 2;                                                break;
    case 6:  target->trap = 0xA2D0; target->codeOffset = OFS_DfSelectDayNative;
             target->origSlot = 6;                                                break;
    default: target->trap = 0xA0C9; target->codeOffset = OFS_DfStrIToA;
             target->origSlot = 7;                                                break;
  }
}

// configuration words in the armc resource (see DfConfig in datefix_arm.c)
#define CFG_OFFSET_YEARS    3
#define CFG_OFFSET_DAYS     4
#define CFG_STATE           5

// request for DfInstall (datefix_arm.c) - big-endian, as the 68k writes it
#define DF_MAX_ENTRIES   16
#define DF_CMD_READ      1
#define DF_CMD_WRITE     2

typedef struct
{
  UInt32 command;
  UInt32 count;
  struct
  {
    UInt32 module;
    UInt32 index;
    UInt32 value;
  } entry[DF_MAX_ENTRIES];
} DfRequest;

// ---------------------------------------------------------------------------
// preferences: the epoch the clock and the data are in
// ---------------------------------------------------------------------------

typedef struct
{
  UInt16  version;
  UInt16  startYear;            // enabled: the epoch in use, else the choice
  Boolean enabled;
  UInt8   converting;           // a data conversion did not finish
} Prefs;

static void
LoadPrefs(Prefs *prefs)
{
  UInt16 size = sizeof(Prefs);

  if ((PrefGetAppPreferences(appCreator, appPrefID, prefs, &size, true)
       == noPreferenceFound) || (size != sizeof(Prefs)) ||
      (prefs->version != appPrefVersion) || !ValidStartYear(prefs->startYear))
  {
    prefs->version   = appPrefVersion;
    prefs->startYear = DEFAULT_START_YEAR;
    prefs->enabled    = false;
    prefs->converting = false;
  }
}

static void
SavePrefs(const Prefs *prefs)
{
  PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion,
                        prefs, sizeof(Prefs), true);
}

// the clock before "New Year", so "Back" can return to the real time
typedef struct
{
  UInt32 before;                // clock value before "New Year"
  UInt32 set;                   // clock value "New Year" set
} TestClock;

static Boolean
LoadTestClock(TestClock *test)
{
  UInt16 size = sizeof(TestClock);

  return (PrefGetAppPreferences(appCreator, testPrefID, test, &size, false)
          != noPreferenceFound) && (size == sizeof(TestClock)) &&
         (test->set != 0);
}

static void
ClearTestClock(void)
{
  TestClock test;

  MemSet(&test, sizeof(test), 0);
  PrefSetAppPreferences(appCreator, testPrefID, appPrefVersion,
                        &test, sizeof(test), false);
}

// ---------------------------------------------------------------------------
// decoding the PACE shim and the veneer
// ---------------------------------------------------------------------------

static UInt16
ReadLE16(const UInt8 *p)
{
  return (UInt16)(p[0] | (p[1] << 8));
}

static UInt32
ReadLE32(const UInt8 *p)
{
  return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) |
         ((UInt32)p[3] << 24);
}

/**
 * Checks for the veneer LDR R12,[R9,#-4*(m+1)]; LDR PC,[R12,#4*i].
 */
static Boolean
DecodeVeneer(const UInt8 *p, UInt32 *module, UInt32 *index)
{
  UInt32 w0 = ReadLE32(p);
  UInt32 w1 = ReadLE32(p + 4);
  UInt32 m;

  if ((w0 & 0xFFFFF000UL) != 0xE519C000UL) return false;   // ldr ip,[r9,#-x]
  if ((w1 & 0xFFFFF000UL) != 0xE59CF000UL) return false;   // ldr pc,[ip,#y]
  if ((w0 & 3) || (w1 & 3)) return false;

  m = (w0 & 0xFFF) / 4;
  if ((m < 1) || (m > 3)) return false;                     // DAL, Boot, UI

  *module = m - 1;
  *index  = (w1 & 0xFFF) / 4;
  return true;
}

/**
 * Counts the veneers called by a Thumb function (blx to an ARM veneer) and
 * returns the last one found. A `bl` to another Thumb function is followed
 * for `depth` more levels: some shims (SelectDay) marshal their arguments
 * in a shared core that makes the call.
 */
static UInt16
ScanForVeneers(const UInt8 *pc, UInt16 depth, UInt32 *module, UInt32 *index)
{
  UInt32 target, m, i;
  UInt16 hi, lo, n, found = 0;
  Int32  offset;

  for (n = 0; n < 64; n++, pc += 2)
  {
    hi = ReadLE16(pc);

    // end of the function: bx rX / pop {.., pc}
    if (((hi & 0xFF87) == 0x4700) || ((hi & 0xFF00) == 0xBD00))
      break;

    // bl / blx pair
    if ((hi & 0xF800) != 0xF000) continue;
    lo = ReadLE16(pc + 2);
    if ((lo & 0xE800) != 0xE800) continue;

    offset = ((Int32)(hi & 0x7FF) << 12) | ((Int32)(lo & 0x7FF) << 1);
    if (offset & 0x400000L) offset -= 0x800000L;
    target = (UInt32)pc + 4 + offset;
    pc += 2;
    n++;

    if ((lo & 0xF800) == 0xE800)                            // blx: ARM veneer
    {
      target &= ~3UL;
      if (DecodeVeneer((const UInt8 *)target, &m, &i))
      {
        *module = m;
        *index  = i;
        found++;
      }
    }
    else if (depth > 0)                                     // bl: Thumb core
      found += ScanForVeneers((const UInt8 *)target, depth - 1, module, index);
  }
  return found;
}

/**
 * Finds the export table entry behind a trap: the trap points to a PACE
 * stub (TRAP #15, 0x07FE, native address), the native address to a Thumb
 * shim that marshals the 68k arguments and calls exactly one veneer.
 */
static Boolean
FindExport(UInt16 trap, UInt32 *module, UInt32 *index)
{
  const UInt8 *stub = (const UInt8 *)SysGetTrapAddress(trap);
  UInt32       shim;
  UInt16       found;

  if ((stub == NULL) ||
      (stub[0] != 0x4E) || (stub[1] != 0x4F) ||
      (stub[2] != 0x07) || (stub[3] != 0xFE))
    return false;

  shim = ReadLE32(stub + 4);
  if ((shim & 1) == 0) return false;                        // Thumb expected

  // the shim itself first; only if it calls no veneer, its shared core
  found = ScanForVeneers((const UInt8 *)(shim & ~1UL), 0, module, index);
  if (found == 0)
    found = ScanForVeneers((const UInt8 *)(shim & ~1UL), 1, module, index);
  return found == 1;
}

// ---------------------------------------------------------------------------
// installing the patch
// ---------------------------------------------------------------------------

static Boolean
IsPalmOS5(void)
{
  UInt32 romVersion = 0;

  FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
  return (romVersion >= sysMakeROMVersion(5, 0, 0, sysROMStageDevelopment, 0));
}

static UInt8 *
InstalledCode(void)
{
  UInt32 code;

  if (FtrGet(appCreator, ftrInstalled, &code) != errNone) return NULL;
  return (UInt8 *)code;
}

/**
 * Palm OS 3.5 .. 4.x: the date functions are 68k code and their traps are
 * replaced directly (m68k.c). Below 3.5 there is no DateTemplateToAscii and
 * the date picker is another trap: not supported yet.
 */
static Boolean
IsPalmOS35to4(void)
{
  UInt32 romVersion = 0;

  FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
  return (romVersion >= sysMakeROMVersion(3, 5, 0, sysROMStageDevelopment, 0)) &&
         (romVersion < sysMakeROMVersion(5, 0, 0, sysROMStageDevelopment, 0));
}

static M68kConfig *
Config68k(void)
{
  UInt32 p = 0;

  if (FtrGet(appCreator, ftrM68kConfig, &p) != errNone) return NULL;
  return (M68kConfig *)p;
}

/**
 * Is a patch in place, on either kind of device?
 */
static Boolean
IsActive(void)
{
  return (InstalledCode() != NULL) || (Config68k() != NULL);
}

static UInt32
CallInstaller(UInt8 *code, DfRequest *req)
{
  return PceNativeCall((NativeFuncType *)(code + OFS_DfInstall), req);
}

/**
 * Writes one configuration word of the native code (little-endian).
 */
static void
WriteConfig(UInt8 *code, UInt16 index, UInt32 value)
{
  UInt8 le[4];

  le[0] = (UInt8)value;
  le[1] = (UInt8)(value >> 8);
  le[2] = (UInt8)(value >> 16);
  le[3] = (UInt8)(value >> 24);
  DmWrite(code, OFS_CONFIG + 4 * index, le, 4);
}

/**
 * Reads one configuration word of the native code (little-endian).
 */
static UInt32
ReadConfig(const UInt8 *code, UInt16 index)
{
  return ReadLE32(code + OFS_CONFIG + 4 * index);
}

/**
 * Puts DfSelectDay over the SelectDay trap. The code resource it lives in
 * stays locked until Uninstall (or the next reset) - the picker runs in
 * other applications long after DateFix itself has quit.
 *
 * Palm OS 5 refuses this trap (sysErrNotAllowed: PACE only lets a few
 * traps be replaced), so there the system picker stays - see
 * docs/fix-log.md. Palm OS 3.x/4.x take it.
 */
static void
InstallPicker(void)
{
  MemHandle codeH;
  DmOpenRef db;
  UInt32    orig;

  if (FtrGet(appCreator, ftrPickerCode, &orig) == errNone) return;

  db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                   dmModeReadOnly);
  if (db == NULL) return;
  codeH = DmGet1Resource(sysResTAppCode, 1);
  if (codeH != NULL)
  {
    MemHandleLock(codeH);
    orig = (UInt32)SysGetTrapAddress(sysTrapSelectDay);
    if (SysSetTrapAddress(sysTrapSelectDay, (void *)DfSelectDay) == errNone)
    {
      FtrSet(appCreator, ftrOrigSelectDay, orig);
      FtrSet(appCreator, ftrPickerCode, (UInt32)codeH);
    }
    else
      MemHandleUnlock(codeH);
  }
  DmCloseDatabase(db);
}

static void
UninstallPicker(void)
{
  UInt32 codeH, orig;

  if (FtrGet(appCreator, ftrPickerCode, &codeH) != errNone) return;
  FtrGet(appCreator, ftrOrigSelectDay, &orig);
  SysSetTrapAddress(sysTrapSelectDay, (void *)orig);
  MemHandleUnlock((MemHandle)codeH);
  FtrUnregister(appCreator, ftrPickerCode);
  FtrUnregister(appCreator, ftrOrigSelectDay);
}

/**
 * The 68k route: the traps of the six date functions point to m68k.c. The
 * originals the formatting functions call are kept in the configuration
 * chunk (dynamic heap, owned by the system: it outlives this application)
 * and in features, so Uninstall can put the traps back.
 */
static Boolean
Install68k(UInt16 startYear, Char *message)
{
  static const UInt16 traps[6] = { 0xA0FC, 0xA25F, 0xA261, 0xA266, 0xA267, 0xA3CD };
  MemHandle   codeH;
  DmOpenRef   db;
  M68kConfig *c;
  UInt32      orig[6];
  UInt16      i;

  if (Config68k() != NULL) return true;

  db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                   dmModeReadOnly);
  if (db == NULL)
  {
    if (message != NULL) StrCopy(message, "DateFix database not found");
    return false;
  }
  codeH = DmGet1Resource(sysResTAppCode, 1);
  if (codeH == NULL)
  {
    DmCloseDatabase(db);
    if (message != NULL) StrCopy(message, "Code resource not found");
    return false;
  }
  c = (M68kConfig *)MemPtrNew(sizeof(M68kConfig));
  if (c == NULL)
  {
    DmCloseDatabase(db);
    if (message != NULL) StrCopy(message, "Not enough memory");
    return false;
  }
  MemHandleLock(codeH);                         // until Uninstall or a reset
  MemPtrSetOwner(c, 0);

  for (i = 0; i < 6; i++)
    orig[i] = (UInt32)SysGetTrapAddress(traps[i]);
  MemSet(c, sizeof(M68kConfig), 0);
  c->offsetYears             = startYear - EPOCH_YEAR;
  c->offsetDays              = OffsetDays(startYear);
  c->origDateToAscii         = (void *)orig[3];
  c->origDateToDOWDMFormat   = (void *)orig[4];
  c->origDateTemplateToAscii = (void *)orig[5];
  c->recording               = true;

  FtrSet(appCreator, ftrM68kConfig, (UInt32)c);       // before the traps
  for (i = 0; i < 6; i++)
    FtrSet(appCreator, ftrOrigBase + i, orig[i]);

  SysSetTrapAddress(traps[0], (void *)M68kSecondsToDateTime);
  SysSetTrapAddress(traps[1], (void *)M68kDayOfWeek);
  SysSetTrapAddress(traps[2], (void *)M68kDayOfMonth);
  SysSetTrapAddress(traps[3], (void *)M68kDateToAscii);
  SysSetTrapAddress(traps[4], (void *)M68kDateToDOWDMFormat);
  SysSetTrapAddress(traps[5], (void *)M68kDateTemplateToAscii);

  FtrSet(appCreator, ftrCode68k, (UInt32)codeH);
  FtrSet(appCreator, ftrOffsetYears, startYear - EPOCH_YEAR);
  InstallPicker();
  {
    LocalID dbID;
    UInt16  card;

    DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &card, NULL);
    DmDatabaseProtect(card, dbID, true);          // as on Palm OS 5
  }
  DmCloseDatabase(db);
  return true;
}

static void
Uninstall68k(void)
{
  static const UInt16 traps[6] = { 0xA0FC, 0xA25F, 0xA261, 0xA266, 0xA267, 0xA3CD };
  M68kConfig *c = Config68k();
  UInt32      orig, codeH;
  UInt16      i;

  if (c == NULL) return;
  UninstallPicker();
  {
    DmOpenRef db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                               dmModeReadOnly);
    LocalID   dbID;
    UInt16    card;

    if (db != NULL)
    {
      DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &card, NULL);
      DmCloseDatabase(db);
      DmDatabaseProtect(card, dbID, false);
    }
  }
  for (i = 0; i < 6; i++)
    if (FtrGet(appCreator, ftrOrigBase + i, &orig) == errNone)
    {
      SysSetTrapAddress(traps[i], (void *)orig);
      FtrUnregister(appCreator, ftrOrigBase + i);
    }
  FtrUnregister(appCreator, ftrM68kConfig);
  MemPtrFree(c);
  if (FtrGet(appCreator, ftrCode68k, &codeH) == errNone)
  {
    MemHandleUnlock((MemHandle)codeH);
    FtrUnregister(appCreator, ftrCode68k);
  }
  FtrUnregister(appCreator, ftrOffsetYears);
}

/**
 * Can the patch be installed on this device? Checked BEFORE anything is
 * converted: Enable converts the stored dates first, and a device where
 * the install then fails (Palm OS 4 without the native code, an unknown
 * ROM layout) must not have seen a conversion at all - dates before the
 * start year would be clamped for good.
 */
static Boolean
CanInstall(Char *message)
{
  UInt32 module, index;
  UInt16 i;
  Target target;

  if (IsActive()) return true;
  if (IsPalmOS35to4()) return true;             // the 68k route needs no scan
  if (!IsPalmOS5())
  {
    StrCopy(message, "Palm OS 3.5 or later required");
    return false;
  }
  for (i = 0; i < REQUIRED_TARGETS; i++)
  {
    GetTarget(i, &target);
    if (!FindExport(target.trap, &module, &index))
    {
      StrPrintF(message, "Unknown ROM layout: trap %x", target.trap);
      return false;
    }
  }
  return true;
}

/**
 * Installs the patch for the epoch of startYear.
 *
 * @param message receives a short explanation if it fails (may be NULL).
 * @return true if DateFix is (now) installed.
 */
static Boolean
Install(UInt16 startYear, Char *message)
{
  DfRequest  req;
  MemHandle  codeH;
  DmOpenRef  db;
  UInt8     *code;
  UInt16     i, card;
  LocalID    dbID;
  UInt32     module, index;
  UInt32    *state;
  Target     target;

  if (IsActive()) return true;

  if (IsPalmOS35to4())
    return Install68k(startYear, message);

  if (!IsPalmOS5())
  {
    if (message != NULL) StrCopy(message, "Palm OS 3.5 or later required");
    return false;
  }

  // where do the functions live on this ROM?
  MemSet(&req, sizeof(req), 0);
  req.count = TARGET_COUNT;
  for (i = 0; i < TARGET_COUNT; i++)
  {
    GetTarget(i, &target);
    if (!FindExport(target.trap, &module, &index))
    {
      if (i >= REQUIRED_TARGETS)
      {
        FtrSet(appCreator, ftrPickerMissing, target.trap);
        req.count = REQUIRED_TARGETS;           // no date picker fix here
        break;
      }
      if (message != NULL)
        StrPrintF(message, "Unknown ROM layout: trap %x", target.trap);
      return false;
    }
    req.entry[i].module = module;
    req.entry[i].index  = index;
  }

  // the native code stays locked in place until the next reset
  db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                   dmModeReadOnly);
  if (db == NULL)
  {
    if (message != NULL) StrCopy(message, "DateFix database not found");
    return false;
  }
  codeH = DmGet1Resource(armcResType, armcResID);
  code  = (codeH != NULL) ? (UInt8 *)MemHandleLock(codeH) : NULL;
  DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &card, NULL);
  DmCloseDatabase(db);
  if ((code == NULL) || ((UInt32)code & 3))
  {
    if (code != NULL) MemHandleUnlock(codeH);
    if (message != NULL) StrCopy(message, "Native code not usable");
    return false;
  }

  // current entries = the originals
  req.command = DF_CMD_READ;
  CallInstaller(code, &req);

  // writable state of the native code: in the dynamic heap, owned by the
  // system so it outlives this application (until the next reset)
  state = (UInt32 *)MemPtrNew(64 * sizeof(UInt32));
  if (state == NULL)
  {
    MemHandleUnlock(codeH);
    if (message != NULL) StrCopy(message, "Not enough memory");
    return false;
  }
  state[0] = 0;                                 // formatting calls
  state[1] = 0;                                 // date picker
  MemSet(state + 2, 62 * sizeof(UInt32), 0);    // picker log, call trace
  MemPtrSetOwner(state, 0);

  // configuration: the epoch and the originals the wrappers call
  WriteConfig(code, CFG_STATE, (UInt32)state);
  WriteConfig(code, CFG_OFFSET_YEARS, startYear - EPOCH_YEAR);
  WriteConfig(code, CFG_OFFSET_DAYS, OffsetDays(startYear));
  for (i = 0; i < req.count; i++)
  {
    FtrSet(appCreator, ftrOrigBase + i, req.entry[i].value);
    GetTarget(i, &target);
    if (target.origSlot >= 0)
      WriteConfig(code, target.origSlot, req.entry[i].value);
  }

  // switch the table over to DateFix
  req.command = DF_CMD_WRITE;
  for (i = 0; i < req.count; i++)
  {
    GetTarget(i, &target);
    req.entry[i].value = (UInt32)code + target.codeOffset;
  }
  if (CallInstaller(code, &req) != 0)
  {
    // put back what the table had
    for (i = 0; i < req.count; i++)
      FtrGet(appCreator, ftrOrigBase + i, &req.entry[i].value);
    CallInstaller(code, &req);
    MemHandleUnlock(codeH);
    if (message != NULL) StrCopy(message, "Export table is read-only");
    return false;
  }

  FtrSet(appCreator, ftrInstalled, (UInt32)code);
  FtrSet(appCreator, ftrTargetCount, req.count);
  FtrSet(appCreator, ftrOffsetYears, startYear - EPOCH_YEAR);
  InstallPicker();
  DmDatabaseProtect(card, dbID, true);
  return true;
}

/**
 * Restores the original entries and releases the native code.
 */
static void
Uninstall(void)
{
  DfRequest req;
  UInt8    *code = InstalledCode();
  UInt32    module, index;
  UInt16    i, card;
  LocalID   dbID;
  DmOpenRef db;
  Target    target;

  if (code == NULL)
  {
    Uninstall68k();                             // the 68k route, if that one
    return;
  }

  MemSet(&req, sizeof(req), 0);
  req.command = DF_CMD_WRITE;
  req.count   = REQUIRED_TARGETS;
  if (FtrGet(appCreator, ftrTargetCount, &module) == errNone)
    req.count = module;
  for (i = 0; i < req.count; i++)
  {
    GetTarget(i, &target);
    FindExport(target.trap, &module, &index);
    req.entry[i].module = module;
    req.entry[i].index  = index;
    FtrGet(appCreator, ftrOrigBase + i, &req.entry[i].value);
  }
  CallInstaller(code, &req);

  UninstallPicker();
  MemPtrFree((MemPtr)ReadConfig(code, CFG_STATE));
  MemPtrUnlock(code);
  FtrUnregister(appCreator, ftrInstalled);
  FtrUnregister(appCreator, ftrTargetCount);

  db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                   dmModeReadOnly);
  if (db != NULL)
  {
    DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &card, NULL);
    DmCloseDatabase(db);
    DmDatabaseProtect(card, dbID, false);
  }
}

// ---------------------------------------------------------------------------
// moving between epochs
// ---------------------------------------------------------------------------

/**
 * Moves the clock from the epoch of fromYear to the epoch of toYear: the
 * same moment, counted from another start.
 *
 * @return false (clock unchanged) if the moment is not in the new window.
 */
static Boolean
MoveClock(UInt16 fromYear, UInt16 toYear, Char *message)
{
  UInt32 now   = TimGetSeconds();
  UInt32 delta;

  if (toYear >= fromYear)
  {
    delta = (OffsetDays(toYear) - OffsetDays(fromYear)) * SECONDS_PER_DAY;
    if (now < delta)
    {
      StrPrintF(message, "The date is before %d", toYear);
      return false;
    }
    TimSetSeconds(now - delta);
  }
  else
  {
    delta = (OffsetDays(fromYear) - OffsetDays(toYear)) * SECONDS_PER_DAY;
    if (now > WINDOW_DAYS * SECONDS_PER_DAY - 1 - delta)
    {
      StrPrintF(message, "The date is after %d", toYear + 127);
      return false;
    }
    TimSetSeconds(now + delta);
  }
  return true;
}

// ---------------------------------------------------------------------------
// data conversion: stored dates follow the epoch
// ---------------------------------------------------------------------------

#define BACKUP_DIR  "/PALM/DateFix"

// MemHeapFlags: heap is read-only (ROM); documented, but only in MemoryPrv.h
#define memHeapFlagReadOnly  0x0001

/**
 * Days between the epochs of two start years (positive: to is later).
 */
static Int32
EpochDays(UInt16 fromYear, UInt16 toYear)
{
  return (Int32)OffsetDays(toYear) - (Int32)OffsetDays(fromYear);
}

/**
 * Databases in ROM cannot be changed and keep their dates.
 */
static Boolean
InRom(LocalID id)
{
  MemPtr p = MemLocalIDToGlobal(id, 0);
  UInt16 heap;

  if (MemLocalIDKind(id) == memIDHandle)
    heap = MemHandleHeapID((MemHandle)p);
  else
    heap = MemPtrHeapID(p);
  return (MemHeapFlags(heap) & memHeapFlagReadOnly) != 0;
}

/**
 * Moves the dates in the records of one database. Records are converted
 * in a copy and written back in one piece; they are not marked dirty, so
 * a HotSync does not send them to the desktop just for the epoch change.
 */
static void
ConvertRecords(LocalID id, UInt16 format, Int16 deltaYears, Boolean apply,
               ConvStats *stats)
{
  DmOpenRef db;
  MemHandle h;
  UInt8    *src, *copy, *dst;
  UInt32    size;
  UInt16    i, n;
  Boolean   changed;

  db = DmOpenDatabase(0, id, (apply ? dmModeReadWrite : dmModeReadOnly) |
                             dmModeShowSecret);
  if (!db)
  {
    stats->malformed++;
    return;
  }

  n = DmNumRecords(db);
  for (i = 0; i < n; i++)
  {
    h = DmQueryRecord(db, i);
    if (!h) continue;                           // deleted, no data
    size = MemHandleSize(h);
    copy = MemPtrNew(size);
    if (!copy)
    {
      stats->malformed++;
      continue;
    }
    src = MemHandleLock(h);
    MemMove(copy, src, size);
    ConvRecord(format, copy, size, deltaYears, apply, stats);
    changed = MemCmp(copy, src, size) != 0;
    MemHandleUnlock(h);

    if (apply && changed)
    {
      h = DmGetRecord(db, i);
      if (h)
      {
        dst = MemHandleLock(h);
        DmWrite(dst, 0, copy, size);
        MemHandleUnlock(h);
        DmReleaseRecord(db, i, false);
      }
    }
    MemPtrFree(copy);
  }
  DmCloseDatabase(db);
}

// the first database with dates beyond the window, for the message
static Char gOverflowDb[dmDBNameLength];

/**
 * Moves every stored date in RAM from the epoch of fromYear to the epoch
 * of toYear: the records of the known applications and the creation,
 * modification and backup dates of all databases. With apply false only
 * the stats are computed.
 */
static void
ConvertData(UInt16 fromYear, UInt16 toYear, Boolean apply, Boolean limitOverflow,
            ConvStats *stats)
{
  Int32   deltaDays  = -EpochDays(fromYear, toYear);
  Int16   deltaYears = (Int16)fromYear - (Int16)toYear;
  UInt16  i, n, attr, format;
  UInt32  created, modified, backedUp, type, creator, before;
  LocalID id;
  Char    name[dmDBNameLength];

  MemSet(stats, sizeof(ConvStats), 0);
  stats->limitOverflow = limitOverflow;
  gOverflowDb[0] = chrNull;
  n = DmNumDatabases(0);
  for (i = 0; i < n; i++)
  {
    id = DmGetDatabase(0, i);
    if (!id || InRom(id)) continue;
    if (DmDatabaseInfo(0, id, name, &attr, NULL, &created, &modified,
                       &backedUp, NULL, NULL, NULL, &type, &creator))
      continue;

    format = (attr & dmHdrAttrResDB) ? CONV_NONE : ConvFormat(type, creator);
    if (format != CONV_NONE)
    {
      before = stats->overflow + stats->limited;
      ConvertRecords(id, format, deltaYears, apply, stats);
      if ((gOverflowDb[0] == chrNull) && (stats->overflow + stats->limited > before))
        StrCopy(gOverflowDb, name);             // the first database that has some
    }

    created  = ConvSeconds(created, deltaDays, stats);
    modified = ConvSeconds(modified, deltaDays, stats);
    backedUp = ConvSeconds(backedUp, deltaDays, stats);
    if (apply)
      DmSetDatabaseInfo(0, id, NULL, NULL, NULL, &created, &modified,
                        &backedUp, NULL, NULL, NULL, NULL, NULL);
  }
}

/**
 * Copies the databases whose records DateFix changes to the first
 * expansion card (BACKUP_DIR), before converting them.
 *
 * @return the number of databases saved, -1 without a card.
 */
static Int16
BackupData(void)
{
  UInt32  version, iterator = vfsIteratorStart, type, creator;
  UInt16  volume, i, n, attr;
  Int16   saved = 0;
  LocalID id;
  Char    name[dmDBNameLength];
  Char    path[sizeof(BACKUP_DIR) + dmDBNameLength + 5];

  // VFSExportDatabaseToFile came with VFS Manager 2 (Palm OS 5); the
  // expansion slot of a Palm OS 4 device has an older one without it
  if (FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version) || (version < vfsMgrVersionNum) ||
      VFSVolumeEnumerate(&volume, &iterator) || (volume == vfsInvalidVolRef))
    return -1;

  VFSDirCreate(volume, "/PALM");
  VFSDirCreate(volume, BACKUP_DIR);

  n = DmNumDatabases(0);
  for (i = 0; i < n; i++)
  {
    id = DmGetDatabase(0, i);
    if (!id || InRom(id)) continue;
    if (DmDatabaseInfo(0, id, name, &attr, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, &type, &creator) ||
        (attr & dmHdrAttrResDB) || (ConvFormat(type, creator) == CONV_NONE))
      continue;

    StrPrintF(path, "%s/%s.pdb", BACKUP_DIR, name);
    VFSFileDelete(volume, path);                // the previous backup
    if (VFSExportDatabaseToFile(volume, path, 0, id) == errNone)
      saved++;
  }
  return saved;
}

/**
 * Asks before converting and reports the result.
 *
 * @return false if the user cancelled or a date cannot be stored.
 */
static Boolean
ConvertWithBackup(UInt16 fromYear, UInt16 toYear, Char *message)
{
  ConvStats stats;
  Prefs     prefs;
  Char      text[200];
  Int16     saved;
  Boolean   limit = false;

  ConvertData(fromYear, toYear, false, false, &stats);
  if (stats.overflow > 0)
  {
    // Entries beyond the window (e.g. in 2032 when going back to 1904)
    // cannot be kept: ask whether they may be set to the last day of it.
    StrPrintF(text, "%ld dates in %s are after 31 Dec %d and cannot be kept "
              "with the start year %d. Set them to 31 Dec %d and go on?",
              stats.overflow, gOverflowDb, toYear + 127, toYear, toYear + 127);
    if (FrmCustomAlert(limitAlert, text, "", "") != 0)
    {
      message[0] = chrNull;                     // the user said no: no alert
      return false;
    }
    limit = true;
    ConvertData(fromYear, toYear, false, true, &stats);
  }

  StrPrintF(text, "%ld dates in Calendar, Tasks, Contacts and Expense "
            "move from the start year %d to %d.", stats.dates, fromYear, toYear);
  if (stats.clamped > 0)
    StrPrintF(text + StrLen(text), " %ld dates before %d are set to %d.",
              stats.clamped, toYear, toYear);
  if (FrmCustomAlert(convertAlert, text, "", "") != 0)
  {
    message[0] = chrNull;                       // cancelled: no alert
    return false;
  }

  saved = BackupData();
  LoadPrefs(&prefs);
  prefs.converting = true;
  SavePrefs(&prefs);

  ConvertData(fromYear, toYear, true, limit, &stats);

  LoadPrefs(&prefs);
  prefs.converting = false;
  SavePrefs(&prefs);

  if (saved < 0)
    StrPrintF(text, "%ld dates moved. No card: no backup.", stats.dates);
  else
    StrPrintF(text, "%ld dates moved. %d databases saved to "
              BACKUP_DIR " on the card first.", stats.dates, saved);
  if (stats.malformed > 0)
    StrPrintF(text + StrLen(text), " %ld records could not be read "
              "and were left unchanged.", stats.malformed);
  if (stats.limited > 0)
    StrPrintF(text + StrLen(text), " %ld dates were set to 31 Dec %d.",
              stats.limited, toYear + 127);
  FrmCustomAlert(resultAlert, text, "", "");
  return true;
}

/**
 * Enables DateFix with the epoch of startYear: data, patch, then clock -
 * applications that recompute their alarms on the time change already
 * find the new dates and the patched weekday.
 */
static Boolean
Enable(UInt16 startYear, Char *message)
{
  Prefs prefs;

  LoadPrefs(&prefs);
  if (prefs.enabled) return true;

  if (!CanInstall(message))
    return false;
  if (TimGetSeconds() < (UInt32)EpochDays(EPOCH_YEAR, startYear) * SECONDS_PER_DAY)
  {
    StrPrintF(message, "The date is before %d", startYear);
    return false;
  }
  if (!ConvertWithBackup(EPOCH_YEAR, startYear, message))
    return false;
  if (!Install(startYear, message))
  {
    ConvStats stats;                            // undo, nothing to report
    ConvertData(startYear, EPOCH_YEAR, true, true, &stats);
    return false;
  }
  MoveClock(EPOCH_YEAR, startYear, message);

  prefs.startYear = startYear;
  prefs.enabled   = true;
  SavePrefs(&prefs);
  ClearTestClock();                             // other epoch now
  return true;
}

/**
 * Disables DateFix: data, patch and clock back to the Palm OS epoch.
 * Refused if the clock or a stored date is after 2031.
 */
static Boolean
Disable(Char *message)
{
  Prefs  prefs;
  UInt32 delta;

  LoadPrefs(&prefs);
  if (!prefs.enabled) return true;

  delta = (UInt32)EpochDays(EPOCH_YEAR, prefs.startYear) * SECONDS_PER_DAY;
  if (TimGetSeconds() > WINDOW_DAYS * SECONDS_PER_DAY - 1 - delta)
  {
    StrCopy(message, "The date is after 2031. Use Prepare Update for a new version.");
    return false;
  }
  if (!ConvertWithBackup(prefs.startYear, EPOCH_YEAR, message))
    return false;
  Uninstall();
  MoveClock(prefs.startYear, EPOCH_YEAR, message);

  prefs.enabled = false;
  SavePrefs(&prefs);
  ClearTestClock();
  return true;
}

// ---------------------------------------------------------------------------
// self test (through the traps, i.e. through the patched table)
// ---------------------------------------------------------------------------

/**
 * Checks the patched functions for internal year 1996 (92 years after
 * 1904) = real year startYear + 92.
 *
 * @return the number of failed checks.
 */
static UInt16
SelfTest(UInt16 startYear, Char *report)
{
  DateTimeType dt;
  DateType     date;
  static Char  str[longDateStrLength + 1];
  Char         year[8];
  UInt16       failed = 0;
  UInt32       days1996 = 92 / 4 * DAYS_PER_4_YEARS;         // 1904 .. 1996
  Int16        weekday;

  weekday = (Int16)((OffsetDays(startYear) + days1996 + WEEKDAY_1904) % 7);
  StrIToA(year, startYear + 92);
  report[0] = chrNull;

#define CHECK(cond, text) \
  if (!(cond)) { if (failed++ == 0) StrCopy(report, text); }

  // clock: internal date, real weekday
  TimSecondsToDateTime(days1996 * SECONDS_PER_DAY + 3600, &dt);
  CHECK((dt.year == 1996) && (dt.month == 1) && (dt.day == 1) &&
        (dt.hour == 1), "TimSecondsToDateTime date");
  CHECK(dt.weekDay == weekday, "TimSecondsToDateTime weekday");

  CHECK(DayOfWeek(1, 1, 1996) == weekday, "DayOfWeek");
  CHECK(DayOfMonth(1, 1, 1996) == weekday, "DayOfMonth");

  // day arithmetic is the system's and must stay monotonic
  date.year = 2031 - EPOCH_YEAR; date.month = 12; date.day = 31;
  DateAdjust(&date, -1);
  CHECK((date.year == 127) && (date.day == 30), "DateAdjust");

  // "today" the way applications ask for it: DateSecondsToDate(TimGetSeconds())
  {
    static Char todayText[40];
    DateType today;
    DateTimeType now;

    TimSecondsToDateTime(TimGetSeconds(), &now);
    DateSecondsToDate(TimGetSeconds(), &today);
    StrPrintF(todayText, "DateSecondsToDate %d-%d-%d",
              today.year + EPOCH_YEAR, today.month, today.day);
    CHECK((today.year + EPOCH_YEAR == now.year) && (today.month == now.month) &&
          (today.day == now.day), todayText);
  }

  // years shown to the user are real years
  DateToAscii(1, 1, 1996, dfDMYLong, str);
  CHECK((StrLen(str) >= 4) &&
        (StrCompare(str + StrLen(str) - 4, year) == 0), str);
  DateTemplateToAscii("^4l", 1, 1, 1996, str, sizeof(str) - 1);
  CHECK(StrCompare(str, year) == 0, str);
  // formats through DateTemplateToAscii: the year must move only once
  DateToDOWDMFormat(1, 1, 1996, dfYMDLongWithDot, str);
  CHECK(StrStr(str, year) != NULL, str);

  // the date picker is DateFix's where the system lets it be replaced
  {
    UInt32 codeH;

    if (FtrGet(appCreator, ftrPickerCode, &codeH) == errNone)
      CHECK(SysGetTrapAddress(sysTrapSelectDay) == (void *)DfSelectDay,
            "SelectDay trap");
  }

#undef CHECK
  return failed;
}

// ---------------------------------------------------------------------------
// user interface
// ---------------------------------------------------------------------------

static Char gStatus[120];
static Char gState[40];
static Char gYear[8];

/**
 * The clock check: what Palm OS shows (through the patched functions)
 * next to what DateFix computes on its own from the raw clock, and the
 * raw clock itself. Redrawn every half second while the main form is open.
 */
static void
Pad2(Char *s, Int16 n)
{
  s[0] = (Char)('0' + n / 10);
  s[1] = (Char)('0' + n % 10);
  s[2] = chrNull;
}

static void
DrawLine(Int16 y, const Char *text)
{
  RectangleType r;

  RctSetRectangle(&r, 0, y, 160, 11);
  WinEraseRectangle(&r, 0);
  WinDrawChars(text, StrLen(text), 4, y);
}

static UInt32
SwapWord(UInt32 w)
{
  return (w >> 24) | ((w >> 8) & 0xFF00UL) | ((w << 8) & 0xFF0000UL) | (w << 24);
}

static void
DrawClockCheck(void)
{
  static const Char *days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  DateTimeType dt;
  Prefs        prefs;
  UInt32       now = TimGetSeconds();
  UInt16       epoch;
  Int16        y, m, d, wd;
  Boolean      same;
  Char         line[64], date[longDateStrLength + 1], hh[3], mm[3], ss[3];

  LoadPrefs(&prefs);
  epoch = prefs.enabled ? prefs.startYear : EPOCH_YEAR;

  // 1. as applications see it: system functions, patched while active
  TimSecondsToDateTime(now, &dt);
  DateToDOWDMFormat((UInt8)dt.month, (UInt8)dt.day, (UInt16)dt.year,
                    dfDMYLong, date);
  Pad2(hh, dt.hour); Pad2(mm, dt.minute); Pad2(ss, dt.second);
  StrPrintF(line, "Palm OS: %s %s:%s:%s", date, hh, mm, ss);
  DrawLine(127, line);

  // 2. DateFix's own arithmetic on the raw clock
  ClockCheckRealDate(now, epoch, &y, &m, &d, &wd);
  same = (dt.year + (epoch - EPOCH_YEAR) == y) && (dt.month == m) &&
         (dt.day == d) && (dt.weekDay == wd);
  Pad2(mm, m); Pad2(ss, d);
  StrPrintF(line, "DateFix: %s %d-%s-%s  %s", days[wd], y, mm, ss,
            same ? "OK" : "MISMATCH");
  DrawLine(138, line);

  // 3. what the last date picker call got and returned (year-month-day)
  {
    UInt8  *code = InstalledCode();
    UInt32 *log, calls, in, out;

    if (code != NULL)
    {
      // written by the ARM code: little-endian words
      log = (UInt32 *)ReadConfig(code, CFG_STATE) + 2;
      calls = SwapWord(log[2]);
      in    = SwapWord(log[0]);
      out   = SwapWord(log[1]);
      if (calls != 0)
      {
        StrPrintF(line, "Picker: %ld-%ld-%ld > %ld-%ld-%ld", (Int32)(in / 512),
                  (Int32)(in / 32 % 16), (Int32)(in % 32), (Int32)(out / 512),
                  (Int32)(out / 32 % 16), (Int32)(out % 32));
        DrawLine(149, line);
      }
    }
  }
}

static void
MainFormUpdate(FormType *frm)
{
  ControlType *toggle, *trigger;
  ListType    *list;
  Prefs        prefs;

  LoadPrefs(&prefs);

  toggle = (ControlType *)FrmGetObjectPtr(frm,
             FrmGetObjectIndex(frm, mainFormToggle));
  CtlSetLabel(toggle, prefs.enabled ? "Disable" : "Enable");
  {
    TestClock test;
    CtlSetLabel((ControlType *)FrmGetObjectPtr(frm,
                  FrmGetObjectIndex(frm, mainFormRollover)),
                LoadTestClock(&test) ? "Back" : "New Year");
  }

  StrIToA(gYear, prefs.startYear);
  trigger = (ControlType *)FrmGetObjectPtr(frm,
              FrmGetObjectIndex(frm, mainFormYearTrigger));
  CtlSetLabel(trigger, gYear);
  CtlSetEnabled(trigger, !prefs.enabled);
  list = (ListType *)FrmGetObjectPtr(frm,
           FrmGetObjectIndex(frm, mainFormYearList));
  LstSetSelection(list, (prefs.startYear - MIN_START_YEAR) / 4);

  if (prefs.enabled && !IsActive())
    StrCopy(gState, "Paused for update");
  else if (prefs.enabled)
    StrPrintF(gState, "Active: %d - %d", prefs.startYear, prefs.startYear + 127);
  else
    StrCopy(gState, "Not active (1904 - 2031)");
  FrmCopyLabel(frm, mainFormStatus, gState);

  FrmDrawForm(frm);
  if (gStatus[0])
    WinDrawChars(gStatus, StrLen(gStatus), 4, 104);
  DrawClockCheck();
}

/**
 * The date function calls that led to the last date picker call (newest
 * first), as recorded by the native code: what the calling application
 * asked for and got.
 */
static void
ShowTrace(void)
{
  static Char text[240];
  UInt8  *code = InstalledCode();
  UInt32 *state, n, k, tag, arg, res;

  StrCopy(text, "No picker call yet.");
  if (Config68k() != NULL)
  {
    // Palm OS 3.5 .. 4: the newest formatting calls (what the application
    // passed, what DateFix handed on)
    M68kConfig *c = Config68k();
    UInt16      k, n = (c->traceCount < 6) ? c->traceCount : 6;
    static const Char *names[4] = { "?", "D2A", "DOW", "TPL" };

    text[0] = chrNull;
    for (k = 0; k < n; k++)
    {
      UInt16 *e = c->trace[(c->traceCount - 1 - k) & 7];

      StrPrintF(text + StrLen(text), "%s %d-%d-%d > %d\n", names[e[0] & 3],
                e[3], e[1], e[2], e[4]);
    }
    if (n == 0) StrCopy(text, "No formatting call yet.");
  }
  else if (code != NULL)
  {
    state = (UInt32 *)ReadConfig(code, CFG_STATE);
    n = SwapWord(state[60]);
    text[0] = chrNull;
    for (k = 0; k < n; k++)
    {
      tag = SwapWord(state[42 + k * 3]);
      arg = SwapWord(state[42 + k * 3 + 1]);
      res = SwapWord(state[42 + k * 3 + 2]);
      if (tag == 1)
        StrPrintF(text + StrLen(text), "Sec %lx > %ld-%ld-%ld\n", arg,
                  (Int32)(res / 512), (Int32)(res / 32 % 16), (Int32)(res % 32));
      else
        StrPrintF(text + StrLen(text), "%s %ld-%ld-%ld > %ld\n",
                  (tag == 2) ? "DoW" : "DoM", (Int32)(arg / 512),
                  (Int32)(arg / 32 % 16), (Int32)(arg % 32), (Int32)res);
    }
    if (n == 0)
      StrCopy(text, "No date call before the last picker call.");
  }
  FrmCustomAlert(traceAlert, text, "", "");
}

static Boolean
MainFormHandleEvent(EventType *event)
{
  FormType *frm = FrmGetActiveForm();
  Char      message[80];
  Prefs     prefs;
  UInt16    failed;

  switch (event->eType)
  {
    case frmOpenEvent:
         MainFormUpdate(frm);
         return true;

    case popSelectEvent:
         if (event->data.popSelect.listID == mainFormYearList)
         {
           LoadPrefs(&prefs);
           prefs.startYear = MIN_START_YEAR + 4 * event->data.popSelect.selection;
           SavePrefs(&prefs);
           gStatus[0] = chrNull;
           MainFormUpdate(frm);
           return true;
         }
         break;

    case ctlSelectEvent:
         LoadPrefs(&prefs);
         switch (event->data.ctlSelect.controlID)
         {
           case mainFormToggle:
                gStatus[0] = chrNull;
                message[0] = chrNull;
                if (!(prefs.enabled ? Disable(message)
                                    : Enable(prefs.startYear, message)) &&
                    (message[0] != chrNull))
                  FrmCustomAlert(errorAlert,
                                 prefs.enabled ? "DateFix cannot be disabled:"
                                               : "DateFix cannot be enabled:",
                                 message, "");
                MainFormUpdate(frm);
                return true;

           case mainFormTest:
                if (!prefs.enabled)
                  StrCopy(gStatus, "Enable DateFix first");
                else
                {
                  failed = SelfTest(prefs.startYear, message);
                  if (failed == 0)
                  {
                    UInt32 count = 0;

                    StrCopy(gStatus, "Self test passed");
                    FtrGet(appCreator, ftrTargetCount, &count);
                    if (count > REQUIRED_TARGETS)
                      StrCat(gStatus, ", picker ok");
                    else if (FtrGet(appCreator, ftrPickerMissing, &count) == errNone)
                      StrPrintF(gStatus + StrLen(gStatus), ", picker: no %lx", count);
                  }
                  else
                    StrPrintF(gStatus, "%d failed: %s", failed, message);
                }
                MainFormUpdate(frm);
                return true;

           case mainFormRollover:
                // watch the clock pass New Year's Eve 2031, then go back
                {
                  TestClock test;

                  if (LoadTestClock(&test))
                  {
                    TimSetSeconds(test.before + (TimGetSeconds() - test.set));
                    ClearTestClock();
                    StrCopy(gStatus, "Clock back to the real time");
                  }
                  else if (!prefs.enabled)
                    StrCopy(gStatus, "Enable DateFix first");
                  else
                  {
                    test.before = TimGetSeconds();
                    test.set = (WINDOW_DAYS - OffsetDays(prefs.startYear)) *
                               SECONDS_PER_DAY - 10;
                    TimSetSeconds(test.set);
                    PrefSetAppPreferences(appCreator, testPrefID,
                                          appPrefVersion, &test,
                                          sizeof(test), false);
                    StrCopy(gStatus, "Clock set to 2031-12-31 23:59:50");
                  }
                }
                MainFormUpdate(frm);
                return true;
         }
         break;

    case menuEvent:
         if (event->data.menu.itemID == mainMenuTrace)
         {
           ShowTrace();
           return true;
         }
         if (event->data.menu.itemID == mainMenuUpdate)
         {
           // take the patch out, keep data, clock and "enabled":
           // the new version installs itself after the HotSync
           LoadPrefs(&prefs);
           if (!prefs.enabled)
             StrCopy(gStatus, "Not active: just HotSync");
           else
           {
             Uninstall();
             FrmAlert(updateAlert);
             StrCopy(gStatus, "Paused for the update");
           }
           MainFormUpdate(frm);
           return true;
         }
         if (event->data.menu.itemID == mainMenuAbout)
         {
           FrmAlert(aboutAlert);
           return true;
         }
         break;

    default:
         break;
  }
  return false;
}

static void
EventLoop(void)
{
  EventType event;
  FormType *frm;
  UInt16    err;

  do
  {
    // half-second ticks keep the clock check running
    EvtGetEvent(&event, SysTicksPerSecond() / 2);
    if ((event.eType == nilEvent) && (FrmGetActiveFormID() == mainForm))
    {
      DrawClockCheck();
      continue;
    }
    if (SysHandleEvent(&event)) continue;
    if (MenuHandleEvent(0, &event, &err)) continue;

    if (event.eType == frmLoadEvent)
    {
      frm = FrmInitForm(event.data.frmLoad.formID);
      FrmSetActiveForm(frm);
      FrmSetEventHandler(frm, MainFormHandleEvent);
      continue;
    }
    FrmDispatchEvent(&event);
  }
  while (event.eType != appStopEvent);
}

UInt32
PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  Prefs prefs;

  switch (cmd)
  {
    case sysAppLaunchCmdSystemReset:
    case sysAppLaunchCmdSyncNotify:
         // after a reset, and right after a HotSync installed a new
         // version (Prepare Update); no globals here: no messages
         // (literals are globals, too)
         LoadPrefs(&prefs);
         if (prefs.enabled)
           Install(prefs.startYear, NULL);
         break;

    case sysAppLaunchCmdNormalLaunch:
         if (Config68k() != NULL)
           Config68k()->recording = false;       // keep what the others did
         LoadPrefs(&prefs);
         if (prefs.converting)
           FrmAlert(interruptedAlert);
         if (prefs.enabled)
           Install(prefs.startYear, NULL);        // paused for an update
         FrmGotoForm(mainForm);
         EventLoop();
         FrmCloseAllForms();
         if (Config68k() != NULL)
         {
           Config68k()->traceCount = 0;          // from now on: the next application
           Config68k()->recording  = true;
         }
         break;

    default:
         break;
  }
  return 0;
}
