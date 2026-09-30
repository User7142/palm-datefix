/*
 * exportprobe.c - shows where a trap ends up on Palm OS 5: PACE stub,
 * Thumb shim, veneer (module/index) and the current export table entry.
 * Diagnostic tool for the fix log, not part of DateFix.
 */
#include <PalmOS.h>

#define sysTrapPceNativeCall 0xA45A
typedef UInt32 NativeFuncType(const void *, void *, void *);
UInt32 PceNativeCall(NativeFuncType *nativeFuncP, void *userDataP)
  SYS_TRAP(sysTrapPceNativeCall);

static const UInt16 traps[] = { 0xA2D0, 0xA25A };

static UInt32 LE32(const UInt8 *p)
{
  return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

/* request for the ARM side: module, index -> value (byte-wise BE) */
static UInt8 req[12];

static void Put(UInt8 *p, UInt32 v)
{
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static UInt32 Get(const UInt8 *p)
{
  return ((UInt32)p[0] << 24) | ((UInt32)p[1] << 16) | ((UInt32)p[2] << 8) | p[3];
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  MemHandle   h;
  NativeFuncType *arm;
  EventType   event;
  Char        str[64];
  UInt16      t, n, hi, lo;
  Int16       y = 0;
  const UInt8 *stub, *pc;
  UInt32      shim, target, w0, w1;
  Int32       off;

  if (cmd != sysAppLaunchCmdNormalLaunch) return 0;
  h = DmGetResource('armc', 1000);
  arm = (NativeFuncType *)MemHandleLock(h);
  WinEraseWindow();

  for (t = 0; t < sizeof(traps) / sizeof(traps[0]); t++)
  {
    stub = (const UInt8 *)SysGetTrapAddress(traps[t]);
    shim = LE32(stub + 4);
    StrPrintF(str, "%x stub %lx shim %lx", traps[t], (UInt32)stub, shim);
    WinDrawChars(str, StrLen(str), 0, y); y += 11;
    pc = (const UInt8 *)(shim & ~1UL);
    for (n = 0; n < 200; n++, pc += 2)
    {
      hi = pc[0] | (pc[1] << 8);
      if (((hi & 0xFF87) == 0x4700) || ((hi & 0xFF00) == 0xBD00)) break;
      if ((hi & 0xF800) != 0xF000) continue;
      lo = pc[2] | (pc[3] << 8);
      if ((lo & 0xF800) != 0xE800) continue;
      off = ((Int32)(hi & 0x7FF) << 12) | ((Int32)(lo & 0x7FF) << 1);
      if (off & 0x400000L) off -= 0x800000L;
      target = ((UInt32)pc + 4 + off) & ~3UL;
      w0 = LE32((const UInt8 *)target);
      w1 = LE32((const UInt8 *)target + 4);
      if (((w0 & 0xFFFFF000UL) == 0xE519C000UL) && ((w1 & 0xFFFFF000UL) == 0xE59CF000UL))
      {
        Put(req, (w0 & 0xFFF) / 4 - 1);
        Put(req + 4, (w1 & 0xFFF) / 4);
        PceNativeCall(arm, req);
        StrPrintF(str, " m%ld #%ld -> %lx", Get(req), Get(req + 4), Get(req + 8));
        WinDrawChars(str, StrLen(str), 0, y); y += 11;
      }
      pc += 2; n++;
    }
  }

  do { EvtGetEvent(&event, evtWaitForever); SysHandleEvent(&event); }
  while (event.eType != penDownEvent && event.eType != appStopEvent);
  MemHandleUnlock(h);
  return 0;
}
