/*
 * shimprobe.c - follows every trap DateFix patches on Palm OS 5 the way
 * DateFix does (src/datefix.c, FindExport): 68k stub, Thumb shim, the
 * veneer it calls (blx, or bl to a BX PC interworking stub, which may B to
 * the veneer) and the export table entry it names. For a device where
 * DateFix reports "Unknown ROM layout".
 *
 * Screen: per trap "m<module> #<index>" when found as DateFix would find
 *         it, otherwise FAIL with the first word of the first callee.
 *         x = blx calls, l = bl calls in the shim.
 * File:   /PALM/DateFix/shimdump.bin on the first volume (LifeDrive: the
 *         internal drive, Drive Mode): stub, shim and every callee as
 *         'BLK ' address(BE32) length(BE32) bytes, for the desktop. Closed
 *         after every trap, so a crash keeps what was written.
 * Diagnostic tool for the fix log, not part of DateFix.
 */
#include <PalmOS.h>
#include <VFSMgr.h>

#define SHIM_BYTES    256                     // SCAN_HALFWORDS in datefix.c
#define CALLEE_BYTES  128
#define DUMP_PATH     "/PALM/DateFix/shimdump.bin"

static const UInt16 traps[] = { 0xA0FC, 0xA25F, 0xA261, 0xA266, 0xA267, 0xA3CD,
                                0xA2D0, 0xA0C9 };

static FileRef file;
static UInt16  blocks;
static UInt32  firstCallee;

static UInt32 LE32(const UInt8 *p)
{
  return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

static UInt16 LE16(const UInt8 *p)
{
  return (UInt16)(p[0] | (p[1] << 8));
}

static void Put(UInt8 *p, UInt32 v)
{
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void Block(UInt32 address, UInt32 length)
{
  UInt8 head[12];

  if (file == 0) return;
  head[0] = 'B'; head[1] = 'L'; head[2] = 'K'; head[3] = ' ';
  Put(head + 4, address);
  Put(head + 8, length);
  VFSFileWrite(file, 12, head, NULL);
  VFSFileWrite(file, length, (const void *)address, NULL);
  blocks++;
}

/* DecodeVeneer in src/datefix.c */
static Boolean Veneer(const UInt8 *p, UInt32 *module, UInt32 *index)
{
  UInt32 w0 = LE32(p), w1 = LE32(p + 4), m;

  if ((w0 & 0xFFFFF000UL) != 0xE519C000UL) return false;
  if ((w1 & 0xFFFFF000UL) != 0xE59CF000UL) return false;
  if ((w0 & 3) || (w1 & 3)) return false;
  m = (w0 & 0xFFF) / 4;
  if ((m < 1) || (m > 3)) return false;
  *module = m - 1;
  *index  = (w1 & 0xFFF) / 4;
  return true;
}

/* DecodeThumbStub in src/datefix.c; dumps the veneer a B leads to */
static Boolean ThumbStub(const UInt8 *p, Boolean dump, UInt32 *module,
                         UInt32 *index)
{
  UInt32 w;
  Int32  offset;

  if (((UInt32)p & 3) || (LE16(p) != 0x4778) || (LE16(p + 2) != 0x46C0))
    return false;
  p += 4;
  w = LE32(p);
  if ((w & 0xFF000000UL) == 0xEA000000UL)               // b veneer
  {
    offset = (Int32)(w & 0x00FFFFFFUL);
    if (offset & 0x00800000L) offset -= 0x01000000L;
    p += 8 + 4 * offset;
    if (dump) Block((UInt32)p, 16);
  }
  return Veneer(p, module, index);
}

/*
 * ScanForVeneers in src/datefix.c, plus: counts blx/bl, remembers the
 * first callee and, with `dump`, writes every callee to the file. Stops
 * at the first return - behind it is data.
 */
static UInt16 Scan(UInt32 code, UInt16 depth, Boolean dump, UInt32 *module,
                   UInt32 *index, UInt16 *arm, UInt16 *thumb)
{
  const UInt8 *pc = (const UInt8 *)code;
  UInt16 hi, lo, n, found = 0;
  UInt32 target, m, i;
  Int32  offset;

  for (n = 0; n + 4 <= SHIM_BYTES; n += 2, pc += 2)
  {
    hi = LE16(pc);
    if (((hi & 0xFF87) == 0x4700) || ((hi & 0xFF00) == 0xBD00)) break;
    if ((hi & 0xF800) != 0xF000) continue;
    lo = LE16(pc + 2);
    if ((lo & 0xE800) != 0xE800) continue;

    offset = ((Int32)(hi & 0x7FF) << 12) | ((Int32)(lo & 0x7FF) << 1);
    if (offset & 0x400000L) offset -= 0x800000L;
    target = (UInt32)pc + 4 + offset;
    pc += 2;
    n += 2;

    if ((lo & 0xF800) == 0xE800)                        // blx: ARM veneer
    {
      target &= ~3UL;
      (*arm)++;
      if (firstCallee == 0) firstCallee = target;
      if (dump) Block(target, CALLEE_BYTES);
      if (Veneer((const UInt8 *)target, &m, &i))
      {
        *module = m; *index = i; found++;
      }
    }
    else                                                // bl: Thumb
    {
      (*thumb)++;
      if (firstCallee == 0) firstCallee = target;
      if (dump) Block(target, depth ? SHIM_BYTES : CALLEE_BYTES);
      if (ThumbStub((const UInt8 *)target, dump, &m, &i))
      {
        *module = m; *index = i; found++;
      }
      else if (depth)
        found += Scan(target, depth - 1, dump, module, index, arm, thumb);
    }
  }
  return found;
}

static void Line(Int16 *y, const Char *s)
{
  WinDrawChars(s, StrLen(s), 0, *y);
  *y += 11;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  EventType    event;
  Char         str[64];
  UInt32       rom = 0, company = 0, device = 0, version, iterator = vfsIteratorStart;
  UInt32       module = 0, index = 0, m, i, shim;
  UInt16       t, volume = 0, found, arm, thumb, a, b;
  Int16        y = 0;
  const UInt8 *stub;
  Err          err = 1;

  if (cmd != sysAppLaunchCmdNormalLaunch) return 0;
  WinEraseWindow();

  FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
  FtrGet(sysFtrCreator, sysFtrNumOEMCompanyID, &company);
  FtrGet(sysFtrCreator, sysFtrNumOEMDeviceID, &device);
  StrPrintF(str, "ROM %lx  %c%c%c%c %c%c%c%c", rom,
            (Char)(company >> 24), (Char)(company >> 16), (Char)(company >> 8), (Char)company,
            (Char)(device >> 24), (Char)(device >> 16), (Char)(device >> 8), (Char)device);
  Line(&y, str);

  file = 0;
  if (!FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version) &&
      !VFSVolumeEnumerate(&volume, &iterator) && (volume != vfsInvalidVolRef))
  {
    VFSDirCreate(volume, "/PALM");
    VFSDirCreate(volume, "/PALM/DateFix");
    VFSFileDelete(volume, DUMP_PATH);
    err = VFSFileCreate(volume, DUMP_PATH);
    if (!err) err = VFSFileOpen(volume, DUMP_PATH, vfsModeWrite, &file);
    if (err) file = 0;
  }

  for (t = 0; t < sizeof(traps) / sizeof(traps[0]); t++)
  {
    stub = (const UInt8 *)SysGetTrapAddress(traps[t]);
    if (stub == NULL)
    {
      StrPrintF(str, "%x: no address", traps[t]);
      Line(&y, str);
      continue;
    }
    Block((UInt32)stub, 16);
    if ((stub[0] != 0x4E) || (stub[1] != 0x4F) ||
        (stub[2] != 0x07) || (stub[3] != 0xFE))
    {
      StrPrintF(str, "%x no stub: %lx %lx", traps[t], (UInt32)stub, LE32(stub));
      Line(&y, str);
      Block((UInt32)stub, SHIM_BYTES);                  // 68k code: a patch?
      continue;
    }
    shim = LE32(stub + 4);
    Block(shim & ~1UL, SHIM_BYTES);

    // as DateFix: the shim alone first, then one level of Thumb cores
    arm = thumb = 0;
    firstCallee = 0;
    found = Scan(shim & ~1UL, 0, false, &module, &index, &arm, &thumb);
    if (found == 0)
    {
      arm = thumb = 0;
      found = Scan(shim & ~1UL, 1, true, &module, &index, &arm, &thumb);
    }
    else
    {
      a = b = 0;
      Scan(shim & ~1UL, 1, true, &m, &i, &a, &b);         // dump only
    }

    if (found == 1)
      StrPrintF(str, "%x x%u l%u m%lu #%lu ok", traps[t], arm, thumb, module, index);
    else
      StrPrintF(str, "%x x%u l%u v%u FAIL %lx", traps[t], arm, thumb, found,
                LE32((const UInt8 *)(firstCallee & ~1UL)));
    Line(&y, str);

    // what is written so far survives a crash; a seek to the end of a file
    // reports vfsErrFileEOF
    if (file != 0)
    {
      VFSFileClose(file);
      err = VFSFileOpen(volume, DUMP_PATH, vfsModeWrite, &file);
      if (!err)
      {
        err = VFSFileSeek(file, vfsOriginEnd, 0);
        if (err == vfsErrFileEOF) err = errNone;
      }
      if (err) file = 0;
    }
  }

  if (file != 0)
  {
    VFSFileClose(file);
    StrPrintF(str, "File: %s (%u)", DUMP_PATH, blocks);
  }
  else
    StrPrintF(str, "No file written (err %x)", err);
  Line(&y, str);
  Line(&y, "Tap to quit");

  do { EvtGetEvent(&event, evtWaitForever); SysHandleEvent(&event); }
  while (event.eType != penDownEvent && event.eType != appStopEvent);
  return 0;
}
