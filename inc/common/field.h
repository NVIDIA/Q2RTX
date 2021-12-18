/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#ifndef FIELD_H
#define FIELD_H

//
// field.h -- line editing
//

#define MAX_FIELD_TEXT  256

typedef struct inputField_s {
    char    text[MAX_FIELD_TEXT];
    size_t  maxChars;
    size_t  visibleChars;
    size_t  cursorPos;
} inputField_t;

bool        IF_KeyEvent(inputField_t *field, int key);
bool        IF_CharEvent(inputField_t *field, int key);
void        IF_Init(inputField_t *field, size_t visibleChars, size_t maxChars);
void        IF_Clear(inputField_t *field);
void        IF_Replace(inputField_t *field, const char *text);
int         IF_Draw(inputField_t *field, int x, int y, int flags, qhandle_t font);

#endif // FIELD_H
