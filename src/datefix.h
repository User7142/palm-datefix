/*
 * datefix.h - resource IDs and shared constants of DateFix
 */

#ifndef DATEFIX_H
#define DATEFIX_H

#define appCreator            'DtFx'
#define appPrefID             0
#define appPrefVersion        2
#define testPrefID            1         // clock before "New Year"

#define armcResType           'armc'
#define armcResID             1000

// features (live until the next reset, like the patch itself)
#define ftrInstalled          0         // code pointer while installed
#define ftrOrigBase           100       // + entry: original table entry
#define ftrOffsetYears        1         // S - 1904, read by the date picker
#define ftrOrigSelectDay      2         // the SelectDay trap before DateFix
#define ftrCode68k            9         // locked code resource (68k route)
#define ftrPickerMissing      8         // trap whose export was not found
#define ftrTargetCount        7         // entries patched in the export table
#define ftrPickerCode         3         // locked code resource of the picker

#define mainForm              1000
#define mainFormStatus        1001
#define mainFormToggle        1002
#define mainFormTest          1003
#define mainFormRollover      1004
#define mainFormYearTrigger   1005
#define mainFormYearList      1006

#define aboutAlert            1100
#define errorAlert            1101
#define convertAlert          1102
#define resultAlert           1103
#define interruptedAlert      1104
#define updateAlert           1105
#define traceAlert            1106
#define limitAlert            1107


// date picker (selectday.c)
#define sdForm                1300
#define sdPrevYear            1301
#define sdNextYear            1302
#define sdYearGadget          1303
#define sdGridGadget          1304
#define sdCancel              1305
#define sdToday               1306
#define sdMonthFirst          1310      // .. 1321, January .. December
#define sdMonthGroup          1

#define mainMenu              1200
#define mainMenuAbout         1201
#define mainMenuUpdate        1202
#define mainMenuTrace         1203

#endif
