/*
 * clockcheck.h - real date of a raw clock value (see clockcheck.c)
 */

#ifndef CLOCKCHECK_H
#define CLOCKCHECK_H

void ClockCheckRealDate(UInt32 seconds, UInt16 startYear, Int16 *year,
                        Int16 *month, Int16 *day, Int16 *weekDay);

#endif
