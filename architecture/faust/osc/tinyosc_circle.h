/************************************************************************
 FAUST OSC Architecture File
 Copyright (C) 2025 Thomas Hopman, Tomatek Audio
 ---------------------------------------------------------------------
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ************************************************************************/

//
// tinyosc_circle.h
// Wrapper to make tinyosc work with Circle (bare metal)
//
// Provides byte order conversion functions (ntohl/htonl/ntohs/htons)
// that tinyosc needs for OSC packet parsing/generation.
// Circle runs on ARM (little-endian), OSC uses network byte order (big-endian).
//

#ifndef _TINYOSC_CIRCLE_H_
#define _TINYOSC_CIRCLE_H_

#include <circle/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// ntohl - network to host long (32-bit)
static inline u32 ntohl(u32 netlong)
{
    return ((netlong & 0x000000FFU) << 24) |
           ((netlong & 0x0000FF00U) << 8) |
           ((netlong & 0x00FF0000U) >> 8) |
           ((netlong & 0xFF000000U) >> 24);
}

// htonl - host to network long (32-bit)
static inline u32 htonl(u32 hostlong)
{
    return ntohl(hostlong);  // Same operation
}

// ntohs - network to host short (16-bit)
static inline u16 ntohs(u16 netshort)
{
    return ((netshort & 0x00FFU) << 8) |
           ((netshort & 0xFF00U) >> 8);
}

// htons - host to network short (16-bit)
static inline u16 htons(u16 hostshort)
{
    return ntohs(hostshort);  // Same operation
}

// Now include tinyosc with our definitions in place
#include "tinyosc/tinyosc.h"

#ifdef __cplusplus
}
#endif

#endif  // _TINYOSC_CIRCLE_H_
