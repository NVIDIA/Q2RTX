/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

typedef enum {
	TT_EOF,
	TT_NAME,
	TT_IMMEDIATE,
	TT_PUNCTUATION
} lexTokenType_t;

/* bitmasks, so they may be combined for Lex_ExpectImmediate */
typedef enum {
	IT_INT			= ( 1 << 0 ),
	IT_FLOAT		= ( 1 << 1 ),
	IT_STRING		= ( 1 << 2 ),
	IT_VECTOR1		= ( 1 << 3 ),
	IT_VECTOR2		= ( 1 << 4 ),
	IT_VECTOR3		= ( 1 << 5 ),
	IT_VECTOR4		= ( 1 << 6 )
} lexImmType_t;

typedef union {
	int		integer;
	float	value;
	vec4_t	vector;
	char	string[MAX_STRING_CHARS];
} lexImm_t;

typedef struct {
	const char	*data;
	int			currentLine;
	qboolean	rejectNewLines;

	char	token[MAX_TOKEN_CHARS];
	int		tokenLength;
	lexTokenType_t	tokenType;
} lexContext_t;

extern lexContext_t	lex;

extern lexImm_t		lex_immediate;
extern lexImmType_t	lex_immediateType;

extern jmp_buf	lex_jmpbuf;

void Lex( void );
void Lex_ExpectImmediate( lexImmType_t expect );
void Lex_ExpectToken( const char *expect );
void Lex_Expect( const char *expect );
qboolean Lex_Check( const char *check );
void Lex_Error( const char *fmt, ... );
void Lex_BeginParse( const char *data );

