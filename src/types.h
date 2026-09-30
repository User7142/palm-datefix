/*
 * types.h - fixed size types for the native ARM code and the host tests
 *
 * The ARM code is built without the Palm OS headers (no PACE glue needed),
 * the host tests with the system compiler; both are 32-bit int / long or
 * use <stdint.h>.
 */

#ifndef TYPES_H
#define TYPES_H

#ifdef HOST_TEST
#include <stdint.h>
typedef uint32_t UInt32;
typedef int32_t  Int32;
typedef uint16_t UInt16;
typedef int16_t  Int16;
typedef uint8_t  UInt8;
#elif !defined(DATEFIX_PALMOS_TYPES)
typedef unsigned long  UInt32;
typedef long           Int32;
typedef unsigned short UInt16;
typedef short          Int16;
typedef unsigned char  UInt8;
#endif
// with DATEFIX_PALMOS_TYPES (68k code that includes <PalmOS.h>) the Palm
// OS typedefs are used as they are

#endif
