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

//
// lex.c
//

#include "config.h"
#include <setjmp.h>
#include "q_shared.h"
#include "com_public.h"
#include "q_list.h"
#include "q_lex.h"

lexContext_t	lex;

lexImm_t		lex_immediate;
lexImmType_t	lex_immediateType;

jmp_buf			lex_jmpbuf;

static char *lex_punctuation[] = {	"&&", "||", "<=", ">=","==", "!=", "+=", "-=",
									"*=", "/=", "|=", "&=", "^=", "++", "--", "(*",
									";", ",", "!", "*", "/", "(", ")", "-",
									"+", "=", "[", "]", "{", "}", ".", "<",
									">", "#", "&", "$", "|", "~", "^", "%", ":", NULL };


void Lex_Error( const char *fmt, ... ) {
	va_list argptr;
	char buffer[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "*************\n" );
	Com_Printf( "ERROR: line %i: %s\n", lex.currentLine, buffer );
	Com_Printf( "*************\n" );

	longjmp( lex_jmpbuf, -1 );
}

/*
===============
Lex_NewLine
===============
*/
void Lex_NewLine( void ) {
	if( lex.rejectNewLines ) {
		Lex_Error( "No newlines expected" );
	}
	lex.currentLine++;
}

/*
===============
Lex_Whitespace
===============
*/
static void Lex_Whitespace( void ) {
	while( 1 ) {
		while( *lex.data <= ' ' ) {
			if( !lex.data[0] ) {
				return;
			}
			if( *lex.data == '\n' ) {
				Lex_NewLine();
			}
			lex.data++;
		}

		if( lex.data[0] == '/' && lex.data[1] == '/' ) {
			lex.data += 2;
			while( *lex.data ) {
				if( *lex.data == '\n' ) {
					lex.data++;
					Lex_NewLine();
					break;
				}
				lex.data++;
			}
			if( !lex.data[0] ) {
				return;
			}
			continue;
		}

		if( lex.data[0] == '/' && lex.data[1] == '*' ) {
			lex.data += 2;
			while( *lex.data ) {
				if( *lex.data == '\n' ) {
					Lex_NewLine();
				} else if( lex.data[0] == '*' && lex.data[1] == '/' ) {
					lex.data += 2;
					break;
				}
				lex.data++;
			}
			if( !lex.data[0] ) {
				return;
			}
			continue;
		}

		break;
	}

}


/*
===============
Lex_String
===============
*/
static void Lex_String( void ) {
	int	c;
	int stringLength;
	int integer;

	lex.token[0] = 0;
	lex.tokenLength = 0;
	lex.tokenType = TT_IMMEDIATE;
	lex_immediateType = IT_STRING;
	lex_immediate.string[0] = 0;
	stringLength = 0;

	while( 1 ) {
		lex.data++;
		
		while( 1 ) {
			c = *lex.data++;

			if( c == '\"' ) {
				break;
			}

			switch( c ) {
			case '\0':
				Lex_Error( "EOF inside quoted string" );
				break;
			case '\n':
				Lex_Error( "Newline inside quoted string" );
				break;
			case '\\':
				c = *lex.data++;
			
				switch( c ) {
				case '\0':
					Lex_Error( "EOF inside quoted string" );
					break;
				case '\n':
					Lex_Error( "Newline inside quoted string" );
					break;
				case 'n':
					c = '\n';
					break;
				case 'r':
					c = '\r';
					break;
				case 't':
					c = '\t';
					break;
				case 'b':
					c = '\b';
					break;
				case '\"':
					c = '\"';
					break;
				case '\\':
					c = '\\';
					break;
				//case '\n':
				//	break;
				case 'x':
					integer = 0;
					while( 1 ) {
						c = *lex.data;

						if( c >= '0' && c <= '9' ) {
							c = ( c - '0' );
						} else if( c >= 'a' && c <= 'f' ) {
							c = ( c - 'a' ) + 10;
						} else if( c >= 'A' && c <= 'F' ) {
							c = ( c - 'A' ) + 10;
						} else {
							break;
						}

						integer = ( integer << 4 ) + c;

						lex.data++;
					}
					if( integer > 0xff ) {
						Lex_Error( "\\x%x does not fit in a character", integer );
					}
					c = integer;
					break;
				default:
					Lex_Error( "Unrecognized escape char" );
					break;
				}
				break;
			default:
				break;
			}

			if( stringLength == MAX_STRING_CHARS - 1 ) {
				Lex_Error( "String exceeded %i chars", MAX_STRING_CHARS );
			}
			lex_immediate.string[stringLength++] = c;
		
		}

		lex_immediate.string[stringLength] = 0;
		Lex_Whitespace();

		c = *lex.data;
		if( c != '\"' ) {
			break;
		}
	}
}

/*
===============
Lex_Punctuation
===============
*/
static void Lex_Punctuation( void ) {
	char **p;
	int len = 0;

	for( p = lex_punctuation; *p; p++ ) {
		len = strlen( *p );
		if( !strncmp( lex.data, *p, len ) ) {
			break;
		}
	}

	if( !p[0] ) {
		Lex_Error( "Unknown punctuation" );
	}

	lex.data += len;
	strcpy( lex.token, *p );
	lex.tokenLength = len;
	lex.tokenType = TT_PUNCTUATION;
}

/*
===============
Lex_Name
===============
*/
static void Lex_Name( void ) {
	int	c;

	lex.token[0] = 0;
	lex.tokenLength = 0;
	lex.tokenType = TT_NAME;
	
	c = *lex.data++;
	while( 1 ) {
		if( lex.tokenLength == MAX_TOKEN_CHARS - 1 ) {
			Lex_Error( "Name exceeded %i chars", MAX_TOKEN_CHARS );
		}
		lex.token[lex.tokenLength++] = c;

		c = *lex.data;
		if( ( c < 'A' || c > 'Z' ) && ( c < 'a' || c > 'z' ) && ( c < '0' || c > '9' ) && c != '_' ) {
			break;
		}
		
		lex.data++;
	}

	lex.token[lex.tokenLength] = 0;

}


/*
===============
Lex_VectorToken
===============
*/
#define MAX_VECTOR_TOKEN	32

char *Lex_VectorToken( void ) {
	static char token[MAX_VECTOR_TOKEN];
	int tokenLength;
	int	c;

	token[0] = 0;
	tokenLength = 0;

	Lex_Whitespace();

	c = *lex.data++;
	if( c == '\'' ) {
		return NULL;
	}
	
	while( 1 ) {

		if( ( c < '0' || c > '9' ) && c != '.' && c != '-' ) {
			Lex_Error( "Illegal char inside vector" );
		}
		if( tokenLength == MAX_VECTOR_TOKEN - 1 ) {
			Lex_Error( "Vector component exceeded %i chars", MAX_VECTOR_TOKEN );
		}
		token[tokenLength++] = c;

		c = *lex.data;
		if( c <= ' ' || c == '\'' ) {
			break;
		}

		lex.data++;
	}

	token[tokenLength] = 0;

	return token;
}


/*
===============
Lex_Vector
===============
*/
static void Lex_Vector( void ) {
	char *token;
	vec4_t	vector;
	int numComponents;

	lex.token[0] = 0;
	lex.tokenLength = 0;
	lex.tokenType = TT_IMMEDIATE;
	
	lex.data++;
	lex.rejectNewLines = qtrue;

	numComponents = 0;
	while( 1 ) {
		if( !( token = Lex_VectorToken() ) ) {
			break;
		}
		
		if( numComponents == 4 ) {
			Lex_Error( "Too many vector components" );
		}

		vector[numComponents++] = atof( token );

	}

	lex.rejectNewLines = qfalse;

	switch( numComponents ) {
	case 0:
		Lex_Error( "Empty vectors not allowed" );
		break;
	case 1:
		lex_immediate.vector[0] = vector[0];
		lex_immediate.vector[1] = 0;
		lex_immediate.vector[2] = 0;
		lex_immediate.vector[3] = 0;
		lex_immediateType = IT_VECTOR1;
		break;
	case 2:
		lex_immediate.vector[0] = vector[0];
		lex_immediate.vector[1] = vector[1];
		lex_immediate.vector[2] = 0;
		lex_immediate.vector[3] = 0;
		lex_immediateType = IT_VECTOR2;
		break;
	case 3:
		lex_immediate.vector[0] = vector[0];
		lex_immediate.vector[1] = vector[1];
		lex_immediate.vector[2] = vector[2];
		lex_immediate.vector[3] = 0;
		lex_immediateType = IT_VECTOR3;
		break;
	case 4:
		lex_immediate.vector[0] = vector[0];
		lex_immediate.vector[1] = vector[1];
		lex_immediate.vector[2] = vector[2];
		lex_immediate.vector[3] = vector[3];
		lex_immediateType = IT_VECTOR4;
		break;
	}

}

/*
===============
Lex_Number
===============
*/
static void Lex_Number( void ) {
	int	c;
	int integer;
	double value, frac;

	lex.token[0] = 0;
	lex.tokenLength = 0;
	lex.tokenType = TT_IMMEDIATE;

	integer = 0;
	value = 0;
	
	c = *lex.data++;
	if( c == '0' ) {
		c = *lex.data++;

		switch( c ) {
		case '\0':
			Lex_Error( "EOF inside number" );
			break;
		case '.':
			frac = 0.1;
			while( 1 ) {
				c = *lex.data;

				if( c < '0' || c > '9' ) {
					break;
				}

				value += frac * ( c - '0' );
				frac *= 0.1;

				lex.data++;
			}
			lex_immediateType = IT_FLOAT;
			lex_immediate.value = value;
			return;
		case 'x':
			while( 1 ) {
				c = *lex.data;

				if( c >= '0' && c <= '9' ) {
					c = ( c - '0' );
				} else if( c >= 'a' && c <= 'f' ) {
					c = ( c - 'a' ) + 10;
				} else if( c >= 'A' && c <= 'F' ) {
					c = ( c - 'A' ) + 10;
				} else {
					break;
				}

				integer = ( integer << 4 ) + c;

				lex.data++;
			}
			lex_immediateType = IT_INT;
			lex_immediate.integer = integer;
			return;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			while( 1 ) {
				integer = ( integer * 8 ) + ( c - '0' );

				c = *lex.data;

				if( c < '0' || c > '9' ) {
					break;
				}

				if( c == 8 || c == 9 ) {
					Lex_Error( "Bad octal digit" );
				}

				lex.data++;
			}
			lex_immediateType = IT_INT;
			lex_immediate.integer = integer;
			return;

		
		case '0':
		case '8':
		case '9':
			Lex_Error( "Bad octal digit" );
			break;
		default:
			lex.data--;
			break;
		}

		lex_immediateType = IT_INT;
		lex_immediate.integer = 0;
		return;
	}


	while( 1 ) {
		integer = ( integer * 10 ) + ( c - '0' );

		c = *lex.data;
		if( c == '.' ) {
			lex.data++;

			frac = 0.1;
			value = integer;
			while( 1 ) {
				c = *lex.data;

				if( c < '0' || c > '9' ) {
					break;
				}

				value += frac * ( c - '0' );

				frac *= 0.1;
				lex.data++;
			}

			lex_immediateType = IT_FLOAT;
			lex_immediate.value = value;

			return;
		}

		if( c < '0' || c > '9' ) {
			break;
		}

		lex.data++;
		
	}

	lex_immediateType = IT_INT;
	lex_immediate.integer = integer;

}



/*
===============
Lex_Whitespace
===============
*/
static void Lex_Eof( void ) {
	lex.token[0] = 0;
	lex.tokenLength = 0;
	lex.tokenType = TT_EOF;
}

/*
===============
Lex_Check
===============
*/
qboolean Lex_Check( const char *check ) {
	if( !strcmp( lex.token, check ) ) {
		Lex();
		return qtrue;
	}
	return qfalse;
}

/*
===============
Lex_Expect
===============
*/
void Lex_Expect( const char *expect ) {
	if( strcmp( lex.token, expect ) ) {
		Lex_Error( "Expected '%s', found '%s'", expect, lex.token );
	}
	Lex();
}

/*
===============
Lex_ExpectToken
===============
*/
void Lex_ExpectToken( const char *expect ) {
	if( strcmp( lex.token, expect ) ) {
		Lex_Error( "Expected '%s', found '%s'", expect, lex.token );
	}
}

/*
===============
Lex_ExpectImmediate
===============
*/
void Lex_ExpectImmediate( lexImmType_t expect ) {
	if( lex.tokenType != TT_IMMEDIATE ) {
		Lex_Error( "Expected immediate, found '%s'", lex.token );
	}
	if( ( lex_immediateType & expect ) == 0 ) {
		Lex_Error( "Expected different immediate type" );
	}
	Lex();
}

/*
===============
Lex
===============
*/
void Lex( void ) {
	int c;

	Lex_Whitespace();

	c = *lex.data;
	if( c >= 'A' && c <= 'z' ) {
		Lex_Name();
		return;
	}
	if( c >= '0' && c <= '9' ) {
		Lex_Number();
		return;
	}
	
	switch( c ) {
	case '\0':
		Lex_Eof();
		break;
	case '_':
		Lex_Name();
		break;
	case '\'':
		Lex_Vector();
		break;
	case '\"':
		Lex_String();
		break;
	default:
		Lex_Punctuation();
		break;
	}

}

void Lex_Begin( const char *data ) {
	memset( &lex, 0, sizeof( lex ) );
	lex.data = data;
	lex.currentLine = 1;

	memset( &lex_immediate, 0, sizeof( lex_immediate ) );
	lex_immediateType = IT_INT;

	Lex();
}




