/*
Copyright (C) 2011 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "common/x86/fpu.h"

#if (defined __i386__) || (defined _M_IX86)

uint16_t pushed_cw, single_cw, full_cw, chop_cw, ceil_cw;

void X86_SetFPCW(void)
{
    uint16_t cw;

    // save the control word into pushed_cw
    X86_PUSH_FPCW;

    // mask off RC and PC bits
    cw = pushed_cw & 0xf0ff;

    single_cw = cw;         // round mode, 24-bit precision
    full_cw = cw | 0x300;   // round mode, 64-bit precision
    chop_cw = cw | 0xc00;   // chop mode, 24-bit precision
    ceil_cw = cw | 0x800;   // ceil mode, 24-bit precision
}

#endif

