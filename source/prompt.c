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
// prompt.c
//

#include "com_local.h"
#include "q_field.h"
#include "prompt.h"

static char		*matches[MAX_MATCHES];
static char		*sortedMatches[MAX_MATCHES];
static int		numMatches;
static int		numCommands;
static int		numCvars;
static int		numAliases;

static cvar_t	*com_completion_mode;
static cvar_t	*com_completion_treshold;

/*
====================
Prompt_FreeMatches
====================
*/
static void Prompt_FreeMatches( void ) {
	int i;

	// free them
	for( i = 0; i < numMatches; i++ ) {
		Z_Free( matches[i] );
	}

	numMatches = 0;
	numCommands = 0;
	numCvars = 0;
	numAliases = 0;

}

static void Prompt_FooGenerator( xgenerator_t generator, const char *partial ) {
	const char *match;

    match = (*generator)( partial, 0 );
    while( match ) {
        matches[numMatches++] = Z_CopyString( match );
		if( numMatches == MAX_MATCHES ) {
			(*generator)( partial, 2 );
			return;
		}
        match = (*generator)( partial, 1 );
    }
}

static void Prompt_GenerateMatches( const char *partial ) {
	Prompt_FooGenerator( Cmd_Command_g, partial );
    numCommands = numMatches;
    
	if( numMatches != MAX_MATCHES ) {
		Prompt_FooGenerator( Cvar_Generator, partial );
		numCvars = numMatches - numCommands;

		if( numMatches != MAX_MATCHES ) {
			Prompt_FooGenerator( Cmd_Alias_g, partial );
			numAliases = numMatches - numCvars - numCommands;
		}
	}
}

static void Prompt_ShowMatches( commandPrompt_t *prompt, char **matches,
                                int start, int end )
{
    int count = end - start;
    int numCols = 7, numLines;
    int i, j, k, max, len, total;
    int colwidths[6];
    char *match;

	do {
		numCols--;
		numLines = ceil( ( float )count / numCols );
        total = 0;
        for( i = 0; i < numCols; i++ ) {
            k = start + numLines * i;
			if( k >= end ) {
				break;
			}
            max = -9999;
            j = k;
            while( j - k < numLines && j < end ) {
                len = strlen( matches[j++] );
                if( max < len ) {
                    max = len;
                }
            }
			colwidths[i] = max > prompt->widthInChars - 2 ?
				prompt->widthInChars - 2 : max;
            total += max + 2;
        }
        if( total < prompt->widthInChars ) {
            break;
        }
    } while( numCols > 1 );

    for( i = 0; i < numLines; i++ ) {
        for( j = 0; j < numCols; j++ ) {
            k = start + j * numLines + i;
            if( k >= end ) {
                break;
            }
            match = matches[k];
            prompt->printf( "%s", match );
            len = colwidths[j] - strlen( match );
            for( k = 0; k < len + 2; k++ ) {
				prompt->printf( " " );
            }
        }
        prompt->printf( "\n" );
    }
    
}

static void Prompt_ShowIndividualMatches( commandPrompt_t *prompt ) {
	int offset = 0;

	if( numCommands ) {
		qsort( matches + offset, numCommands,
				sizeof( matches[0] ), SortStrcmp );

		prompt->printf( "\n" S_COLOR_YELLOW "%i possible command%s:\n",
			numCommands, ( numCommands % 10 ) != 1 ? "s" : "" );

        Prompt_ShowMatches( prompt, matches, offset, offset + numCommands );
		offset += numCommands;
	}

	if( numCvars ) {
		qsort( matches + offset, numCvars,
				sizeof( matches[0] ), SortStrcmp );

		prompt->printf( "\n" S_COLOR_YELLOW "%i possible variable%s:\n",
				numCvars, ( numCvars % 10 ) != 1 ? "s" : "" );

        Prompt_ShowMatches( prompt, matches, offset, offset + numCvars );
		offset += numCvars;
	}

	if( numAliases ) {
		qsort( matches + offset, numAliases,
				sizeof( matches[0] ), SortStrcmp );

		prompt->printf( "\n" S_COLOR_YELLOW "%i possible alias%s:\n",
				numAliases, ( numAliases % 10 ) != 1 ? "es" : "" );

        Prompt_ShowMatches( prompt, matches, offset, offset + numAliases );
		offset += numAliases;
	}
}

/*
====================
Prompt_CompleteCommand
====================
*/
void Prompt_CompleteCommand( commandPrompt_t *prompt, qboolean backslash ) {
	inputField_t *inputLine = &prompt->inputLine;
	char *text, *partial, *s;
	int i, argc, pos, currentArg, size, length, relative;
	char *first, *last;
	xgenerator_t generator;

	text = inputLine->text;
	size = sizeof( inputLine->text );
	pos = inputLine->cursorPos;
	if( backslash ) {
		if( inputLine->text[0] != '\\' && inputLine->text[0] != '/' ) {
			memmove( inputLine->text + 1, inputLine->text, size - 1 );
			inputLine->text[0] = '\\';
		}
		text++; size--; pos--;
	}
	
	Cmd_TokenizeString( text, qfalse );

	argc = Cmd_Argc();

	currentArg = Cmd_FindArgForOffset( pos );
	i = strlen( text );
	if( i != 0 && text[ i - 1 ] == ' ' ) {
		if( currentArg == argc - 1 ) {
			currentArg++;
		}
	}
    relative = 0;
    s = Cmd_Argv( 0 );
    for( i = 0; i < currentArg; i++ ) {
        partial = Cmd_Argv( i );
        relative++;
        if( *partial == ';' ) {
	        s = Cmd_Argv( i + 1 );
            relative = 0;
        }
    }

	partial = Cmd_Argv( currentArg );
    if( *partial == ';' ) {
        currentArg++;
        partial = Cmd_Argv( currentArg );
        relative = 0;
    }

    if( relative ) {
        generator = Cmd_FindGenerator( s, relative );
        if( generator ) {
            Prompt_FooGenerator( generator, partial );
        }
    } else {
        generator = NULL;
        Prompt_GenerateMatches( partial );
    }

	if( !numMatches ) {
		inputLine->cursorPos = strlen( inputLine->text );
        prompt->tooMany = qfalse;
		return; /* nothing found */
	}

	pos = Cmd_ArgOffset( currentArg );
	text += pos;
	size -= pos;

	if( numMatches == 1 ) {
		/* we have finished completion! */
        s = Cmd_RawArgsFrom( currentArg + 1 ); 
        if( COM_HasSpaces( matches[0] ) ) {
		    pos += Q_concat( text, size, "\"", matches[0], "\" ", s, NULL );
        } else {
		    pos += Q_concat( text, size, matches[0], " ", s, NULL );
        }
		inputLine->cursorPos = pos;
        prompt->tooMany = qfalse;
		Prompt_FreeMatches();
		return;
	}

    if( numMatches > com_completion_treshold->integer && !prompt->tooMany ) {
        prompt->printf( "Press TAB again to display all %d possibilities.\n",
            numMatches );
		inputLine->cursorPos = strlen( inputLine->text );
        prompt->tooMany = qtrue;
		Prompt_FreeMatches();
        return;
    }

    prompt->tooMany = qfalse;

	/* sort matches alphabethically */
	for( i = 0; i < numMatches; i++ ) {
		sortedMatches[i] = matches[i];
	}
	qsort( sortedMatches, numMatches, sizeof( sortedMatches[0] ), SortStrcmp );

	/* copy matching part */
	first = sortedMatches[0];
	last = sortedMatches[ numMatches - 1 ];
	length = 0;
	do {
        if( *first != *last ) {
            break;
        }
		text[length++] = *first;
		if( length == size - 1 ) {
			break;
		}

		first++;
		last++;
	} while( *first );

	text[length] = 0;
    pos += length;
    size -= length;

	if( currentArg + 1 < argc ) {
        s = Cmd_RawArgsFrom( currentArg + 1 ); 
		pos += Q_concat( text + length, size, " ", s, NULL );
	}

	inputLine->cursorPos = pos;

	prompt->printf( "]\\%s\n", Cmd_ArgsFrom( 0 ) );
	if( generator ) {
		goto multicolumn;
	}
    
	switch( com_completion_mode->integer ) {
	case 0:
		/* print in solid list */
		for( i = 0 ; i < numMatches; i++ ) {
			prompt->printf( "%s\n", sortedMatches[i] ); 
		}
		break;
	case 1:
    multicolumn:
		/* print in multiple columns */
        Prompt_ShowMatches( prompt, sortedMatches, 0, numMatches );
		break;
	case 2:
	default:
		/* resort matches by type and print in multiple columns */
		Prompt_ShowIndividualMatches( prompt );
		break;
	}
	
	Prompt_FreeMatches();
}

/*
====================
Prompt_Action

User just pressed enter
====================
*/
char *Prompt_Action( commandPrompt_t *prompt ) {
    char *s = prompt->inputLine.text;
	int i, j;
	
    prompt->tooMany = qfalse;
	if( s[0] == 0 || ( ( s[0] == '/' || s[0] == '\\' ) && s[1] == 0 ) ) {
		IF_Clear( &prompt->inputLine );
		return NULL; /* empty line */
	}

	/* save current line in history */
	i = prompt->inputLineNum & HISTORY_MASK;
	j = ( prompt->inputLineNum - 1 ) & HISTORY_MASK;
    if( !prompt->history[j] || strcmp( prompt->history[j], s ) ) {
    	if( prompt->history[i] ) {
	    	Z_Free( prompt->history[i] );
	    }
	    prompt->history[i] = Z_CopyString( s );
	    prompt->inputLineNum++;
    } else {
        i = j;
    }

    /* stop history search */
	prompt->historyLineNum = prompt->inputLineNum;
	
	IF_Clear( &prompt->inputLine );

	return prompt->history[i];
}

/*
====================
Prompt_HistoryUp
====================
*/
void Prompt_HistoryUp( commandPrompt_t *prompt ) {
    int i;

	if( prompt->historyLineNum == prompt->inputLineNum ) {
		/* save current line in history */
	    i = prompt->inputLineNum & HISTORY_MASK;
        if( prompt->history[i] ) {
            Z_Free( prompt->history[i] );
        }
        prompt->history[i] = Z_CopyString( prompt->inputLine.text );
	}

    if( prompt->inputLineNum - prompt->historyLineNum < HISTORY_SIZE &&
        prompt->historyLineNum > 0 ) 
    {
        prompt->historyLineNum--;
    }

    i = prompt->historyLineNum & HISTORY_MASK;
	IF_Replace( &prompt->inputLine, prompt->history[i] );
}

/*
====================
Prompt_HistoryDown
====================
*/
void Prompt_HistoryDown( commandPrompt_t *prompt ) {
    int i;

	if( prompt->historyLineNum == prompt->inputLineNum ) {
		return;
	}
    
    prompt->historyLineNum++;
    
    i = prompt->historyLineNum & HISTORY_MASK;
	IF_Replace( &prompt->inputLine, prompt->history[i] );
}

/*
====================
Prompt_Clear
====================
*/
void Prompt_Clear( commandPrompt_t *prompt ) {
	int i;
	
	for( i = 0; i < HISTORY_SIZE; i++ ) {
		if( prompt->history[i] ) {
			Z_Free( prompt->history[i] );
			prompt->history[i] = NULL;
		}
	}
	
	prompt->historyLineNum = 0;
	prompt->inputLineNum = 0;
	
	IF_Clear( &prompt->inputLine );
}

void Prompt_SaveHistory( commandPrompt_t *prompt, const char *filename ) {
    fileHandle_t f;
    char *s;
    int i;

    FS_FOpenFile( filename, &f, FS_MODE_WRITE );
    if( !f ) {
        return;
    }

    i = prompt->inputLineNum - HISTORY_SIZE;
    if( i < 0 ) {
        i = 0;
    }
    for( ; i < prompt->inputLineNum; i++ ) {
        s = prompt->history[i & HISTORY_MASK];
        if( s ) {
            FS_FPrintf( f, "%s\n", s );
        }
    }

    FS_FCloseFile( f );
}

void Prompt_LoadHistory( commandPrompt_t *prompt, const char *filename ) {
    char buffer[MAX_FIELD_TEXT];
    fileHandle_t f;
    int i, len;

    FS_FOpenFile( filename, &f, FS_MODE_READ|FS_TYPE_REAL );
    if( !f ) {
        return;
    }

	for( i = 0; i < HISTORY_SIZE; i++ ) {
        if( ( len = FS_ReadLine( f, buffer, sizeof( buffer ) ) ) < 1 ) {
            break;
        }
		if( prompt->history[i] ) {
			Z_Free( prompt->history[i] );
        }
		prompt->history[i] = memcpy( Z_Malloc( len + 1 ), buffer, len );
	}

    FS_FCloseFile( f );

	prompt->historyLineNum = i;
	prompt->inputLineNum = i;
}

/*
====================
Prompt_Init
====================
*/
void Prompt_Init( void ) {
	com_completion_mode = Cvar_Get( "com_completion_mode", "1", CVAR_ARCHIVE );
	com_completion_treshold = Cvar_Get( "com_completion_treshold", "50",
        CVAR_ARCHIVE );
}

