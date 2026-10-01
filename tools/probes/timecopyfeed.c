/*
 * timecopyfeed.c - writes the database that the TimeCopy conduit writes during
 * a HotSync (type 'Time', creator 'TiCo', version 2, record 0: Unix time,
 * time zone in minutes, DST minutes, a word, two flags), so that TimeCopy can
 * set the clock in an emulator without a Windows desktop. Test fixture for
 * the TimeCopy entry of DateFix's per-application patches (src/apppatch.c).
 * The time is fixed: 2026-10-01 16:00:00 UTC. Afterwards TimeCopy gets the
 * launch code a HotSync sends (sysAppLaunchCmdSyncNotify); it then sets its
 * alarm and adjusts the clock as after a real HotSync.
 */
#include <PalmOS.h>

#define FEED_UNIX 1790870400UL

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  EventType  event;
  DmOpenRef  db;
  LocalID    id;
  MemHandle  h;
  UInt16     index = 0, version = 2;
  UInt8      rec[20];
  Char       line[40];
  const Char *msg = "Feed written: 2026-10-01 16:00 UTC";

  if (cmd != sysAppLaunchCmdNormalLaunch) return 0;

  db = DmOpenDatabaseByTypeCreator('Time', 'TiCo', dmModeReadOnly);
  if (db)
  {
    UInt16 card;
    DmOpenDatabaseInfo(db, &id, 0, 0, &card, 0);
    DmCloseDatabase(db);
    DmDeleteDatabase(card, id);
  }
  MemSet(rec, sizeof(rec), 0);
  rec[0] = (UInt8)(FEED_UNIX >> 24); rec[1] = (UInt8)(FEED_UNIX >> 16);
  rec[2] = (UInt8)(FEED_UNIX >> 8);  rec[3] = (UInt8)FEED_UNIX;
  {
    Err err = DmCreateDatabase(0, "TimeCopyData", 'TiCo', 'Time', false);

    if (err != errNone)
      StrPrintF(line, "create: error %x", err), msg = line;
    else if (!(id = DmFindDatabase(0, "TimeCopyData")))
      msg = "find failed";
    else if ((err = DmSetDatabaseInfo(0, id, 0, 0, &version, 0, 0, 0, 0, 0, 0, 0, 0)) != errNone)
      StrPrintF(line, "setinfo: error %x", err), msg = line;
    else if (!(db = DmOpenDatabase(0, id, dmModeReadWrite)))
      StrPrintF(line, "open: error %x", DmGetLastErr()), msg = line;
    else if (!(h = DmNewRecord(db, &index, sizeof(rec))))
      StrPrintF(line, "record: error %x", DmGetLastErr()), msg = line, DmCloseDatabase(db);
    else
    {
      DmWrite(MemHandleLock(h), 0, rec, sizeof(rec));
      MemHandleUnlock(h);
      DmReleaseRecord(db, index, true);
      DmCloseDatabase(db);
      if ((id = DmFindDatabase(0, "TimeCopy")) != 0)
      {
        UInt32 result;
        SysAppLaunch(0, id, 0, sysAppLaunchCmdSyncNotify, NULL, &result);
        msg = "Feed written, TimeCopy notified";
      }
    }
  }

  WinEraseWindow();
  WinDrawChars(msg, StrLen(msg), 4, 20);
  do
    EvtGetEvent(&event, evtWaitForever);
  while (event.eType != appStopEvent && event.eType != penDownEvent);
  return 0;
}
