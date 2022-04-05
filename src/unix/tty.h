/*
Copyright (C) 2012 Andrey Nazarov

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

#ifndef TTY_H
#define TTY_H

#if USE_SYSCON
bool        tty_init_input(void);
void        tty_shutdown_input(void);
#else
#define     tty_init_input()        false
#define     tty_shutdown_input()    (void)0
#endif

#endif // TTY_H
