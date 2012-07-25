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

#ifndef FPU_H
#define FPU_H

#if (defined __i386__) || (defined _M_IX86)

#ifdef _MSC_VER
#define X86_STORE_FPCW(x)   __asm fnstcw x
#define X86_LOAD_FPCW(x)    __asm fldcw x
#else
#define X86_STORE_FPCW(x)   __asm__ __volatile__("fnstcw %0" : "=m" (x))
#define X86_LOAD_FPCW(x)    __asm__ __volatile__("fldcw %0" : : "m" (x))
#endif

#define X86_PUSH_FPCW   X86_STORE_FPCW(pushed_cw)
#define X86_POP_FPCW    X86_LOAD_FPCW(pushed_cw)

#define X86_SINGLE_FPCW X86_LOAD_FPCW(single_cw)
#define X86_FULL_FPCW   X86_LOAD_FPCW(full_cw)
#define X86_CHOP_FPCW   X86_LOAD_FPCW(chop_cw)
#define X86_CEIL_FPCW   X86_LOAD_FPCW(ceil_cw)

extern uint16_t pushed_cw, single_cw, full_cw, chop_cw, ceil_cw;

void X86_SetFPCW(void);

#else

#define X86_PUSH_FPCW
#define X86_POP_FPCW

#define X86_SINGLE_FPCW
#define X86_FULL_FPCW
#define X86_CHOP_FPCW
#define X86_CEIL_FPCW

#define X86_SetFPCW()

#endif

#endif // FPU_H
