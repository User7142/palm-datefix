/*
 * apppatch.h - per-application patches for years that an application draws itself
 *
 * Why: DateFix moves the epoch, so an application sees "internal" years
 * 1904..2031. Everything that goes through the system's date functions is
 * converted by DateFix. An application that computes the year itself
 * (DateType.year + 1904, then StrIToA or StrPrintF) shows the internal
 * year, e.g. 1998 for 2026. That cannot be fixed in the system: a hook on
 * StrIToA cannot tell a year from a day, and the application may also cut
 * the year to two digits and pad it itself.
 *
 * What: at those places the application adds the constant firstYear (1904,
 * `addi.w #1904,d0`). Replacing the constant by the start year makes the
 * application compute the real year, and everything it does with it
 * afterwards (two digits, padding) stays right. DateFix writes the new
 * value into the stored code resource of the application (a write to
 * storage RAM through DmWrite), and writes 1904 back when it is disabled.
 *
 * Safety: a site is only touched if the code resource has exactly the size
 * of the version the table was made for and the instruction there is the
 * expected one. Applications in ROM cannot be written and keep their years.
 * The sites are found with tools/yearfinder.
 */

#ifndef APPPATCH_H
#define APPPATCH_H

#ifndef HOST_TEST
#include <PalmOS.h>
#else
#include "types.h"
#ifndef true
typedef unsigned char Boolean;
#define true  1
#define false 0
#endif
#endif

typedef struct
{
  UInt16 resource;      // 'code' resource id
  UInt32 size;          // exact size of that resource in the version of the table
  UInt16 offset;        // of the instruction; its immediate word follows the opcode
  UInt16 opcode;        // expected opcode word ...
  UInt16 opcodeMask;    // ... compared under this mask
  UInt16 constant;      // the year in the original code
  Int16  direction;     // +1: constant + offset, -1: constant - offset (see below)
} AppPatchSite;

/*
 * Two kinds of year constants:
 * - firstYear 1904, added to a DateType year to get the year to show: it has
 *   to become 1904 + offset = the start year (direction +1);
 * - a *real* year the application converts into clock seconds, e.g. the Unix
 *   epoch 1970 (TimeCopy): the same moment is the internal year 1970 - offset
 *   (direction -1).
 * offset = start year - 1904, a multiple of four (0..68).
 */
#define SITE_FIRST_YEAR(res, size, off) { res, size, off, 0x0640, 0xFFF8, 1904, 1 }  // addi.w #1904,Dn

typedef struct
{
  const char         *name;       // application, for the report
  UInt32              creator;    // creator id of the 'appl' database
  const AppPatchSite *sites;
  UInt16              numSites;
} AppPatchApp;

// what a site contains
#define SITE_ORIGINAL   0       // the original constant
#define SITE_PATCHED    1       // moved by a possible offset
#define SITE_OTHER      2       // other version or other code: not touched

/**
 * Looks at one site of a code resource.
 *
 * @param offset  set to the offset the constant there is moved by (0 for
 *                SITE_ORIGINAL)
 */
UInt16 AppPatchInspect(const UInt8 *code, UInt32 size, const AppPatchSite *site,
                       UInt16 *offset);

/**
 * The constant a site gets for an offset; false if that is no valid year
 * (a real year before the start of the window, e.g. 1970 with start year 1972).
 */
Boolean AppPatchValue(const AppPatchSite *site, UInt16 offset, UInt16 *value);

extern const AppPatchApp kAppPatches[];
extern const UInt16      kNumAppPatches;

#ifndef HOST_TEST
typedef struct
{
  UInt16 patched;       // sites written
  UInt16 unchanged;     // already as wanted
  UInt16 other;         // other version of the application, not touched
  UInt16 locked;        // database could not be opened for writing (ROM)
} AppPatchStats;

/**
 * Moves the year constants of every known site of the installed applications
 * to the epoch of startYear: the start year to enable, 1904 to take the
 * patches out.
 * Idempotent; run it whenever DateFix starts, an application may have been
 * installed again since.
 */
void AppPatchSet(UInt16 startYear, AppPatchStats *stats);
#endif

#endif
