/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "ui_local.h"

/*
=============================================================================

END GAME MENU

=============================================================================
*/

static int credits_start_time;
static const char **credits;
static const char *idcredits[] = {
	"+Q2PRO",
	"http://q2pro.sf.net/",
	"",
	"+PROGRAMMING",
	"Andrey '[SkulleR]' Nazarov",
	"",
	"+ADDITIONAL PROGRAMMING",
	"Al_Uz",
	"",
	"+THANKS TO",
	"Vic",
	"R1CH",
	"",
	"",
	"",
	"",
	"",
	"",
	"+QUAKE II BY ID SOFTWARE",
	"",
	"+PROGRAMMING",
	"John Carmack",
	"John Cash",
	"Brian Hook",
	"",
	"+ART",
	"Adrian Carmack",
	"Kevin Cloud",
	"Paul Steed",
	"",
	"+LEVEL DESIGN",
	"Tim Willits",
	"American McGee",
	"Christian Antkow",
	"Paul Jaquays",
	"Brandon James",
	"",
	"+BIZ",
	"Todd Hollenshead",
	"Barrett (Bear) Alexander",
	"Donna Jackson",
	"",
	"",
	"+SPECIAL THANKS",
	"Ben Donges for beta testing",
	"",
	"",
	"",
	"",
	"",
	"",
	"+ADDITIONAL SUPPORT",
	"",
	"+LINUX PORT AND CTF",
	"Dave \"Zoid\" Kirsch",
	"",
	"+CINEMATIC SEQUENCES",
	"Ending Cinematic by Blur Studio - ",
	"Venice, CA",
	"",
	"Environment models for Introduction",
	"Cinematic by Karl Dolgener",
	"",
	"Assistance with environment design",
	"by Cliff Iwai",
	"",
	"+SOUND EFFECTS AND MUSIC",
	"Sound Design by Soundelux Media Labs.",
	"Music Composed and Produced by",
	"Soundelux Media Labs.  Special thanks",
	"to Bill Brown, Tom Ozanich, Brian",
	"Celano, Jeff Eisner, and The Soundelux",
	"Players.",
	"",
	"\"Level Music\" by Sonic Mayhem",
	"www.sonicmayhem.com",
	"",
	"\"Quake II Theme Song\"",
	"(C) 1997 Rob Zombie. All Rights",
	"Reserved.",
	"",
	"Track 10 (\"Climb\") by Jer Sypult",
	"",
	"Voice of computers by",
	"Carly Staehlin-Taylor",
	"",
	"+THANKS TO ACTIVISION",
	"+IN PARTICULAR:",
	"",
	"John Tam",
	"Steve Rosenthal",
	"Marty Stratton",
	"Henk Hartong",
	"",
	"Quake II(tm) (C)1997 Id Software, Inc.",
	"All Rights Reserved.  Distributed by",
	"Activision, Inc. under license.",
	"Quake II(tm), the Id Software name,",
	"the \"Q II\"(tm) logo and id(tm)",
	"logo are trademarks of Id Software,",
	"Inc. Activision(R) is a registered",
	"trademark of Activision, Inc. All",
	"other trademarks and trade names are",
	"properties of their respective owners.",
	0
};

static menuFrameWork_t	m_creditsMenu;

#define BORDER_HEIGHT	60
#define FADE_HIGHT		20

void M_Credits_MenuDraw( menuFrameWork_t *self ) {
	int i;
	float y, yMax;
	const char *string;
	int flags;
	float alpha;

	yMax = uis.height - BORDER_HEIGHT - 8;

	/*
	** draw the credits
	*/
	y = yMax - ( uis.realtime - credits_start_time ) / 20.0f;
	for( i = 0 ; credits[i] && y < yMax ; i++, y += 8 ) {
		if( y < BORDER_HEIGHT ) {
			continue;
		}

		string = credits[i];
		flags = UI_CENTER;
		if( *string == '+' ) {
			string++;
			flags |= UI_ALTCOLOR;
		}

		alpha = 1;
		if( y < BORDER_HEIGHT + FADE_HIGHT ) {
			alpha = ( y - BORDER_HEIGHT ) / ( float )FADE_HIGHT;
		} else if( y > yMax - FADE_HIGHT ) {
			alpha = 1 - ( ( y - ( yMax - FADE_HIGHT ) ) / ( float )FADE_HIGHT );
		}

		ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );
		UI_DrawString( uis.width / 2, y, NULL, flags, string );
		ref.SetColor( DRAW_COLOR_CLEAR, NULL );

	}

	if( y < BORDER_HEIGHT - FADE_HIGHT ) {
		credits_start_time = uis.realtime;
	}

	Menu_Draw( &m_creditsMenu );

}


static int M_Credits_Callback( int id, int msg, int param ) {
	if( msg != QM_KEY ) {
		return QMS_NOTHANDLED;
	}

	UI_PopMenu();
	return QMS_OUT;
}


static void Credits_MenuInit( void ) {
	memset( &m_creditsMenu, 0, sizeof( m_creditsMenu ) );

	credits = idcredits;	
	credits_start_time = uis.realtime;

	m_creditsMenu.draw = M_Credits_MenuDraw;
	m_creditsMenu.callback = M_Credits_Callback;
}


void M_Menu_Credits_f( void ) {
	Credits_MenuInit();
	UI_PushMenu( &m_creditsMenu );
}
