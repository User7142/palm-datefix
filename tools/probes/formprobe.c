/*
 * formprobe.c - lists the objects of the system SelectDay form (10200):
 * index, type, id, bounds and label. Diagnostic tool for the fix log.
 */
#include <PalmOS.h>

/* lists forms 10000..10400 in all resource databases: db, id, #objects, title */
static void ListForms(void)
{
  UInt16    card = 0, i, n, id, idx;
  UInt16    attr;
  LocalID   db;
  DmOpenRef ref;
  Char      name[32], str[48];
  Int16     y = 0;
  MemHandle h;
  UInt8    *p;

  FntSetFont(stdFont);
  n = DmNumDatabases(card);
  for (i = 0; i < n; i++)
  {
    db = DmGetDatabase(card, i);
    if (DmDatabaseInfo(card, db, name, &attr, 0,0,0,0,0,0,0,0,0) != errNone) continue;
    if (!(attr & dmHdrAttrResDB)) continue;
    ref = DmOpenDatabase(card, db, dmModeReadOnly);
    if (ref == NULL) continue;
    for (id = 10000; id < 10400; id++)
    {
      idx = DmFindResource(ref, 'tFRM', id, NULL);
      if (idx == 0xFFFF) continue;
      h = DmGetResourceIndex(ref, idx);
      p = (UInt8 *)MemHandleLock(h);
      /* FormType header: window (40 bytes) ... numObjects at offset 0x2E in 68k layout */
      StrPrintF(str, "%s %d n%d", name, id, (p[0x2E] << 8) | p[0x2F]);
      MemHandleUnlock(h);
      str[30] = 0;
      WinDrawChars(str, StrLen(str), 0, y);
      y += 10;
      if (y > 150) break;
    }
    DmCloseDatabase(ref);
    if (y > 150) break;
  }
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  FormType     *frm;
  EventType     event;
  RectangleType r;
  Char          str[64];
  const Char   *label;
  UInt16        i, n, id;
  FormObjectKind kind;
  Int16         y = 0;

  if (cmd != sysAppLaunchCmdNormalLaunch) return 0;
  WinEraseWindow();
  ListForms();
  goto done;
done:
  do { EvtGetEvent(&event, evtWaitForever); SysHandleEvent(&event); }
  while (event.eType != penDownEvent && event.eType != appStopEvent);
  return 0;
}
