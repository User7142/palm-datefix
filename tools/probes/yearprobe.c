/*
 * yearprobe.c - an application that draws the year itself, the way Date Book+
 * does: DateType.year + 1904, then StrPrintF. Test subject for DateFix's
 * per-application patches (src/apppatch.c): the year it shows must be the
 * real one, not the internal one, once DateFix patched its code.
 */
#include <PalmOS.h>

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
  EventType event;
  DateType  today;
  Char      line[48];
  Int16     year;

  if (cmd != sysAppLaunchCmdNormalLaunch) return 0;

  DateSecondsToDate(TimGetSeconds(), &today);
  year = today.year + 1904;                       // the line DateFix patches

  WinEraseWindow();
  FntSetFont(stdFont);
  WinDrawChars("YearProbe", 9, 4, 4);
  StrPrintF(line, "Year: %d", year);
  WinDrawChars(line, StrLen(line), 4, 24);
  StrPrintF(line, "Two digits: %d", year % 100);
  WinDrawChars(line, StrLen(line), 4, 38);
  StrPrintF(line, "Internal: %d", today.year);
  WinDrawChars(line, StrLen(line), 4, 52);

  do
  {
    EvtGetEvent(&event, evtWaitForever);
    if (!SysHandleEvent(&event))
      ;
  }
  while (event.eType != appStopEvent && event.eType != penDownEvent);
  return 0;
}
