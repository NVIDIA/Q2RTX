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

#include "sv_local.h"


/*
===============================================================================

SAVEGAME FILES

===============================================================================
*/

static void write_binary_file( const char *name ) {
    fileHandle_t f;

    FS_FOpenFile( name, &f, FS_MODE_WRITE );
    if( !f ) {
        Com_EPrintf( "%s: couldn't open %s\n", __func__, name );
        return;
    }

    FS_Write( msg_write.data, msg_write.cursize, f );

    FS_FCloseFile( f );
}

static void write_server_file( qboolean autosave ) {
    char name[MAX_OSPATH];
    cvar_t *var;

    // write the comment field
    MSG_WriteByte( autosave );
    MSG_WriteLong( time( NULL ) );
    MSG_WriteString( sv.configstrings[CS_NAME] );

    // write the mapcmd
    MSG_WriteString( sv.name );

    // write all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    for( var = cvar_vars; var; var = var->next ) {
        if (!(var->flags & CVAR_LATCH))
            continue;
        MSG_WriteString( var->name );
        MSG_WriteString( var->string );
    }
    MSG_WriteString( NULL );

    Q_snprintf (name, sizeof(name), "save/current/server.state");
    write_binary_file( name );

    SZ_Clear( &msg_write );

    // write game state
    Q_snprintf (name, sizeof(name), "%s/save/current/game.state", fs_gamedir);
    ge->WriteGame (name, autosave);
}

static void write_level_file( void ) {
    char    name[MAX_OSPATH];
    int     i;
    char    *s;
    size_t  len;
    byte    portalbits[MAX_MAP_PORTAL_BYTES];

    Com_DPrintf( "%s()\n", __func__ );

    // write configstrings
    for( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
        s = sv.configstrings[i];
        if( !s[0] ) {
            continue;
        }
        len = strlen( s );
        if( len > MAX_QPATH ) {
            len = MAX_QPATH;
        }

        MSG_WriteShort( i );
        MSG_WriteData( s, len );
        MSG_WriteByte( 0 );
    }
    MSG_WriteShort( MAX_CONFIGSTRINGS );

    len = CM_WritePortalBits( &sv.cm, portalbits );
    MSG_WriteByte( len );
    MSG_WriteData( portalbits, len );

    Q_snprintf (name, sizeof(name), "save/current/server.level");
    write_binary_file( name );

    SZ_Clear( &msg_write );

    Q_snprintf( name, sizeof( name ), "%s/save/current/game.level", fs_gamedir );
    ge->WriteLevel( name );
}


static void read_binary_file( const char *name ) {
    fileHandle_t f;
    size_t len;

    len = FS_FOpenFile( name, &f, FS_MODE_READ|FS_TYPE_REAL|FS_PATH_GAME );
    if( !f ) {
        Com_Error( ERR_DROP, "%s: couldn't open %s\n", __func__, name );
    }

    if( len > MAX_MSGLEN ) {
        FS_FCloseFile( f );
        Com_Error( ERR_DROP, "%s: %s is too large\n", __func__, name );
    }

    FS_Read( msg_read_buffer, len, f );

    SZ_Init( &msg_read, msg_read_buffer, len );
    msg_read.cursize = len;
    msg_read.allowunderflow = qfalse;

    FS_FCloseFile( f );
}

static void read_server_file( void ) {
    char    name[MAX_OSPATH], string[MAX_STRING_CHARS];
    char    mapcmd[MAX_TOKEN_CHARS];

    Com_DPrintf( "%s()\n", __func__ );

    Q_snprintf (name, sizeof(name), "save/current/server.state");
    read_binary_file( name );

    // read the comment field
    MSG_ReadByte();
    MSG_ReadLong();
    MSG_ReadString( NULL, 0 );

    // read the mapcmd
    MSG_ReadString( mapcmd, sizeof( mapcmd ) );

    // read all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    while( 1 ) {
        MSG_ReadString( name, MAX_QPATH );
        if( !name[0] )
            break;
        MSG_ReadString( string, sizeof( string ) );
        Cvar_Set( name, string );
    }

    // start a new game fresh with new cvars
    SV_InitGame( qfalse );

    // error out immediately if game doesn't support safe savegames
    if( !( g_features->integer & GMF_ENHANCED_SAVEGAMES ) ) {
        Com_Error( ERR_DROP, "Game does not support enhanced savegames" );
    }

    // read game state
    Q_snprintf (name, sizeof(name), "%s/save/current/game.state", fs_gamedir);
    ge->ReadGame (name);

    // go to the map
    sv.state = ss_game;        // don't save current level when changing
    SV_Map( mapcmd, qfalse );
}

static void read_level_file( void ) {
    char name[MAX_OSPATH];
    size_t len;
    int index;

    Com_DPrintf( "%s\n", __func__ );

    Q_snprintf (name, sizeof(name), "save/current/server.level");
    read_binary_file( name );

    while( 1 ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }
        if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
            Com_Error( ERR_DROP, "%s: bad configstring index", __func__ );
        }
        MSG_ReadString( sv.configstrings[index], MAX_QPATH );
    }

    len = MSG_ReadByte();
    if( len > MAX_MAP_PORTAL_BYTES ) {
        Com_Error( ERR_DROP, "%s: bad portalbits length", __func__ );
    }

    SV_ClearWorld();

    CM_SetPortalStates( &sv.cm, MSG_ReadData( len ), len );

    Q_snprintf( name, sizeof( name ), "%s/save/current/game.level", fs_gamedir );
    ge->ReadLevel( name );

    ge->RunFrame();
    ge->RunFrame();
}


/*
==============
SV_Loadgame_f

==============
*/
void SV_Loadgame_f (void) {
    char    name[MAX_OSPATH];
    char    *dir;

    if (Cmd_Argc() != 2) {
        Com_Printf ("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    if( dedicated->integer ) {
        Com_Printf ("Savegames are for listen servers only\n");
        return;
    }

    dir = Cmd_Argv(1);
    if (strstr (dir, "..") || strchr (dir, '/') || strchr (dir, '\\') ) {
        Com_Printf ("Bad savedir.\n");
        return;
    }

    // make sure the server.ssv file exists
    Q_snprintf (name, sizeof(name), "save/%s/server.state", Cmd_Argv(1));
    if (FS_LoadFile( name, NULL ) == INVALID_LENGTH ) {
        Com_Printf ("No such savegame: %s\n", name);
        return;
    }

    Com_Printf ("Loading game...\n");

    //SV_CopySaveGame (Cmd_Argv(1), "current");

    read_server_file();

    read_level_file();
}


/*
==============
SV_Savegame_f

==============
*/
void SV_Savegame_f( void ) {
    char *dir;

    if (sv.state != ss_game) {
        Com_Printf ("You must be in a game to save.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf ("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    if( dedicated->integer ) {
        Com_Printf ("Savegames are for listen servers only\n");
        return;
    }
   
    // don't bother saving if we can't read them back!
    if( !( g_features->integer & GMF_ENHANCED_SAVEGAMES ) ) {
        Com_Printf ("Game does not support enhanced savegames\n");
        return;
    }

    if (Cvar_VariableInteger("deathmatch")) {
        Com_Printf ("Can't savegame in a deathmatch\n");
        return;
    }

    if (sv_maxclients->integer == 1 && svs.udp_client_pool[0].edict->client->ps.stats[STAT_HEALTH] <= 0) {
        Com_Printf ("Can't savegame while dead!\n");
        return;
    }

    dir = Cmd_Argv(1);
    if (strstr (dir, "..") || strchr (dir, '/') || strchr (dir, '\\') ) {
        Com_Printf ("Bad savedir.\n");
        return;
    }

    if (!strcmp (dir, "current")) {
        Com_Printf ("Can't save to 'current'\n");
        return;
    }

    Com_Printf ("Saving game...\n");

    // archive current level, including all client edicts.
    // when the level is reloaded, they will be shells awaiting
    // a connecting client
    write_level_file ();

    // save server state
    write_server_file( qfalse );

    // copy it off
    //SV_CopySaveGame ("current", dir);

    Com_Printf ("Done.\n");
}

