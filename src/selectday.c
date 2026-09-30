/*
 * selectday.c - the date picker (SelectDay) for the moved epoch
 *
 * The system's date picker shows the year it works with, 1904..2031, and
 * labels its weekday columns with arithmetic of its own; with DateFix
 * active it showed "1990" for 2026 and a header shifted by three days.
 * DfSelectDay replaces it through the 68k trap table (SysSetTrapAddress),
 * which every 68k application calls through, on Palm OS 3.x..5.x alike.
 *
 * It works with the same internal dates as its callers and shows the
 * real year (internal + S - 1904); the weekday of every column comes from
 * DayOfWeek, which DateFix patches, so grid and header always agree.
 *
 * The function runs inside other applications: no globals, no string
 * literals (prc-tools keeps both in the globals, A5 belongs to the
 * caller). tools/check_reset_path.py checks this on every build.
 */

#include <PalmOS.h>
#include "datefix.h"
#include "selectday.h"

#define FIRST_YEAR   1904
#define LAST_YEAR    2031

#define GRID_COLS    7
#define GRID_ROWS    6

typedef struct
{
  Int16  month, day, year;              // shown month, chosen day
  Int16  offsetYears;                   // real = internal + offsetYears
  UInt16 weekStart;                     // 0 = Sunday
  Char   monthNames[12][8];             // labels of the month buttons
} PickerState;

static void
SetTemplate(Char *t, Char field)
{
  t[0] = '^'; t[1] = field; t[2] = 's'; t[3] = chrNull;
}

static Int16
DaysInMonthOf(Int16 month, Int16 year)
{
  return DaysInMonth(month, year);
}

/**
 * Column (0..6) of a weekday for the user's first day of the week.
 */
static Int16
Column(Int16 weekday, UInt16 weekStart)
{
  return (Int16)((weekday + 7 - weekStart) % 7);
}

static void
DrawYear(FormType *frm, const PickerState *st)
{
  RectangleType r;
  Char          text[8];
  FontID        font = FntSetFont(boldFont);
  Int16         width;

  FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, sdYearGadget), &r);
  WinEraseRectangle(&r, 0);
  StrIToA(text, st->year + st->offsetYears);
  width = FntCharsWidth(text, StrLen(text));
  WinDrawChars(text, StrLen(text), r.topLeft.x + (r.extent.x - width) / 2,
               r.topLeft.y);
  FntSetFont(font);
}

/**
 * The month grid: a header with the weekday initials, then the days.
 * Every weekday is DayOfWeek's, the header is labelled with days of the
 * shown month, so both use the same (patched) calendar.
 */
static void
DrawGrid(FormType *frm, const PickerState *st)
{
  RectangleType r, cell;
  Char          t[4], name[16], text[4];
  Int16         first, days, d, col, row, cw, rh, w, x, y;
  FontID        font;

  FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, sdGridGadget), &r);
  WinEraseRectangle(&r, 0);
  cw = r.extent.x / GRID_COLS;
  rh = r.extent.y / (GRID_ROWS + 1);

  first = DayOfWeek(st->month, 1, st->year);
  days  = DaysInMonthOf(st->month, st->year);

  // header: the first seven days of the month name their own columns
  font = FntSetFont(boldFont);
  SetTemplate(t, '1');
  for (d = 1; d <= 7; d++)
  {
    DateTemplateToAscii(t, (UInt8)st->month, (UInt8)d, (UInt16)st->year,
                        name, sizeof(name) - 1);
    col = Column(DayOfWeek(st->month, d, st->year), st->weekStart);
    w   = FntCharWidth(name[0]);
    WinDrawChars(name, 1, r.topLeft.x + col * cw + (cw - w) / 2, r.topLeft.y);
  }
  FntSetFont(stdFont);

  for (d = 1; d <= days; d++)
  {
    col = Column((Int16)((first + d - 1) % 7), st->weekStart);
    row = (Int16)((Column(first, st->weekStart) + d - 1) / 7);
    StrIToA(text, d);
    w = FntCharsWidth(text, StrLen(text));
    x = r.topLeft.x + col * cw;
    y = r.topLeft.y + (row + 1) * rh;
    WinDrawChars(text, StrLen(text), x + (cw - w) / 2, y);
    if (d == st->day)
    {
      RctSetRectangle(&cell, x, y, cw, rh);
      WinInvertRectangle(&cell, 0);
    }
  }
  FntSetFont(font);
}

/**
 * The day under a tap in the grid, 0 if there is none.
 */
static Int16
DayAt(FormType *frm, const PickerState *st, Coord px, Coord py)
{
  RectangleType r;
  Int16         cw, rh, col, row, first, d;

  FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, sdGridGadget), &r);
  if (!RctPtInRectangle(px, py, &r)) return 0;
  cw  = r.extent.x / GRID_COLS;
  rh  = r.extent.y / (GRID_ROWS + 1);
  col = (px - r.topLeft.x) / cw;
  row = (py - r.topLeft.y) / rh - 1;
  if ((row < 0) || (col >= GRID_COLS)) return 0;

  first = Column(DayOfWeek(st->month, 1, st->year), st->weekStart);
  d = row * 7 + col - first + 1;
  if ((d < 1) || (d > DaysInMonthOf(st->month, st->year))) return 0;
  return d;
}

static void
ShowMonth(FormType *frm, PickerState *st)
{
  Int16 days = DaysInMonthOf(st->month, st->year);

  if (st->day > days) st->day = days;
  FrmSetControlGroupSelection(frm, sdMonthGroup, sdMonthFirst + st->month - 1);
  DrawYear(frm, st);
  DrawGrid(frm, st);
}

/**
 * The date picker; same interface and semantics as the system's.
 * Dates in and out are internal (as stored), the user sees real years.
 */
Boolean
DfSelectDay(const SelectDayType selectDayBy, Int16 *month, Int16 *day,
            Int16 *year, const Char *title)
{
  PickerState   *st;
  FormType      *frm, *previous;
  DmOpenRef      db;
  EventType      event;
  DateTimeType   now;
  Char           t[4];
  UInt32         offset;
  UInt16         i;
  Int16          tapped, weekday;
  Boolean        done = false, chosen = false;

  st = (PickerState *)MemPtrNew(sizeof(PickerState));
  if (st == NULL) return false;
  if (FtrGet(appCreator, ftrOffsetYears, &offset) != errNone) offset = 0;
  st->offsetYears = (Int16)offset;
  st->weekStart   = (UInt16)PrefGetPreference(prefWeekStartDay);
  st->year  = ((*year >= FIRST_YEAR) && (*year <= LAST_YEAR)) ? *year : FIRST_YEAR;
  st->month = ((*month >= 1) && (*month <= 12)) ? *month : 1;
  st->day   = (*day >= 1) ? *day : 1;

  // our form resources; opened last, so they are found first
  db = DmOpenDatabaseByTypeCreator(sysFileTApplication, appCreator,
                                   dmModeReadOnly);
  if (db == NULL)
  {
    MemPtrFree(st);
    return false;
  }

  previous = FrmGetActiveForm();
  frm = FrmInitForm(sdForm);
  FrmSetActiveForm(frm);
  if (title != NULL) FrmSetTitle(frm, (Char *)title);

  SetTemplate(t, '3');
  for (i = 0; i < 12; i++)
  {
    DateTemplateToAscii(t, (UInt8)(i + 1), 1, (UInt16)st->year,
                        st->monthNames[i], sizeof(st->monthNames[i]) - 1);
    CtlSetLabel((ControlType *)FrmGetObjectPtr(frm,
                  FrmGetObjectIndex(frm, sdMonthFirst + i)),
                st->monthNames[i]);
  }

  FrmDrawForm(frm);
  ShowMonth(frm, st);

  while (!done)
  {
    EvtGetEvent(&event, evtWaitForever);
    if (SysHandleEvent(&event)) continue;

    switch (event.eType)
    {
      case appStopEvent:
           EvtAddEventToQueue(&event);          // the caller quits
           done = true;
           continue;

      case penDownEvent:
           tapped = DayAt(frm, st, event.screenX, event.screenY);
           if (tapped == 0) break;
           if (selectDayBy == selectDayByWeek)
           {
             // the tapped week, on the weekday of the date passed in
             weekday = DayOfWeek(*month, (*day >= 1) ? *day : 1, *year);
             tapped += weekday - DayOfWeek(st->month, tapped, st->year);
             if (tapped < 1) tapped += 7;
             if (tapped > DaysInMonthOf(st->month, st->year)) tapped -= 7;
           }
           else if (selectDayBy == selectDayByMonth)
           {
             // the shown month, on the day of the month passed in
             tapped = *day;
             if (tapped > DaysInMonthOf(st->month, st->year))
               tapped = DaysInMonthOf(st->month, st->year);
           }
           st->day = tapped;
           DrawGrid(frm, st);
           chosen = done = true;
           continue;

      case ctlSelectEvent:
           i = event.data.ctlSelect.controlID;
           if (i == sdCancel)
             done = true;
           else if (i == sdToday)
           {
             TimSecondsToDateTime(TimGetSeconds(), &now);
             st->year  = now.year;
             st->month = now.month;
             st->day   = now.day;
             chosen = done = true;
           }
           else if ((i >= sdMonthFirst) && (i < sdMonthFirst + 12))
           {
             st->month = (Int16)(i - sdMonthFirst + 1);
             ShowMonth(frm, st);
           }
           if (done) continue;
           break;

      case ctlRepeatEvent:
           i = event.data.ctlRepeat.controlID;
           if ((i == sdPrevYear) && (st->year > FIRST_YEAR)) st->year--;
           if ((i == sdNextYear) && (st->year < LAST_YEAR))  st->year++;
           ShowMonth(frm, st);
           break;

      default:
           break;
    }
    FrmHandleEvent(frm, &event);
  }

  FrmEraseForm(frm);
  FrmDeleteForm(frm);
  if (previous != NULL) FrmSetActiveForm(previous);
  DmCloseDatabase(db);

  if (chosen)
  {
    *year  = st->year;
    *month = st->month;
    *day   = st->day;
  }
  MemPtrFree(st);
  return chosen;
}
