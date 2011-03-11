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

#include "cl_local.h"
#include "d_md2.h"
#include "d_sp2.h"

/*
===============
CL_CheckOrDownloadFile

Returns qtrue if the file exists, otherwise it attempts
to start a download from the server.
===============
*/
static qboolean CL_CheckOrDownloadFile( const char *path ) {
    qhandle_t f;
    size_t len;
    ssize_t ret;

    len = strlen( path );
    if( len < 1 || len >= MAX_QPATH
        || !Q_ispath( path[0] )
        || !Q_ispath( path[ len - 1 ] )
        || strchr( path, '\\' )
        || strchr( path, ':' )
        || !strchr( path, '/' )
        || strstr( path, ".." ) )
    {
        Com_Printf( "Refusing to download file with invalid path.\n" );
        return qtrue;
    }

    if( FS_FileExists( path ) ) {
        // it exists, no need to download
        return qtrue;
    }

#if USE_CURL
    if( HTTP_QueueDownload( path ) ) {
        //we return true so that the precache check keeps feeding us more files.
        //since we have multiple HTTP connections we want to minimize latency
        //and be constantly sending requests, not one at a time.
        return qtrue;
    }
#endif

    memcpy( cls.download.name, path, len + 1 );

    // download to a temp name, and only rename
    // to the real name when done, so if interrupted
    // a runt file wont be left
    memcpy( cls.download.temp, path, len );
    memcpy( cls.download.temp + len, ".tmp", 5 );

//ZOID
    // check to see if we already have a tmp for this file, if so, try to resume
    // open the file if not opened yet
    ret = FS_FOpenFile( cls.download.temp, &f, FS_MODE_RDWR );
    if( ret >= 0 ) { // it exists
        cls.download.file = f;
        // give the server an offset to start the download
        Com_Printf( "Resuming %s\n", cls.download.name );
        CL_ClientCommand( va( "download \"%s\" %d", cls.download.name, (int)ret ) );
    } else if( ret == Q_ERR_NOENT ) { // it doesn't exist
        Com_Printf( "Downloading %s\n", cls.download.name );
        CL_ClientCommand( va( "download \"%s\"", cls.download.name ) );
    } else { // error happened
        Com_EPrintf( "Couldn't open %s for appending: %s\n", cls.download.temp,
            Q_ErrorString( ret ) );
        return qtrue;
    }

    return qfalse;
}

/*
===============
CL_Download_f

Request a download from the server
===============
*/
void CL_Download_f( void ) {
    char *path;

    if( cls.state < ca_connected ) {
        Com_Printf( "Must be connected to a server.\n" );
        return;
    }
    if( !allow_download->integer ) {
        Com_Printf( "Downloading is disabled.\n" );
        return;
    }

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: download <filename>\n" );
        return;
    }

    if( cls.download.temp[0] ) {
        Com_Printf( "Already downloading.\n" );
        if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
            Com_Printf( "Try using 'stopdl' command to abort the download.\n" );
        }
        return;
    }

    path = Cmd_Argv( 1 );

    if( FS_FileExists( path ) ) {
        Com_Printf( "%s already exists.\n", path );
        return;
    }

    CL_CheckOrDownloadFile( path );
}

/*
=====================
CL_HandleDownload

A download data has been received from the server
=====================
*/
void CL_HandleDownload( const byte *data, int size, int percent ) {
    qerror_t ret;

    if( size == -1 ) {
        if( !percent ) {
            Com_Printf( "Server was unable to send this file.\n" );
        } else {
            Com_Printf( "Server stopped the download.\n" );
        }
        if( cls.download.file ) {
            // if here, we tried to resume a file but the server said no
            FS_FCloseFile( cls.download.file );
        }
        goto another;
    }

    // open the file if not opened yet
    if( !cls.download.file ) {
        ret = FS_FOpenFile( cls.download.temp, &cls.download.file, FS_MODE_WRITE );
        if( !cls.download.file ) {
            Com_EPrintf( "Couldn't open %s for writing: %s\n",
                cls.download.temp, Q_ErrorString( ret ) );
            goto another;
        }
    }

    FS_Write( data, size, cls.download.file );

    if( percent != 100 ) {
        // request next block
        // change display routines by zoid
        cls.download.percent = percent;

        CL_ClientCommand( "nextdl" );
    } else {
        FS_FCloseFile( cls.download.file );

        // rename the temp file to it's final name
        ret = FS_RenameFile( cls.download.temp, cls.download.name );
        if( ret ) {
            Com_EPrintf( "Couldn't rename %s to %s: %s\n",
                cls.download.temp, cls.download.name, Q_ErrorString( ret ) );
        } else {
            Com_Printf( "Downloaded successfully.\n" );
        }

another:
        // get another file if needed
        memset( &cls.download, 0, sizeof( cls.download ) );
        CL_RequestNextDownload();
    }
}

/*
=====================
CL_RequestNextDownload

Runs precache check and dispatches downloads.
=====================
*/

static int precache_check; // for autodownload of precache items
static int precache_tex;
static int precache_model_skin;
static void *precache_model; // used for skin checking in alias models

#define PLAYER_MULT 5

// ENV_CNT is map load, ENV_CNT+1 is first env map
#define ENV_CNT (CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT)
#define TEXTURE_CNT (ENV_CNT+13)

static const char env_suf[6][3] = { "rt", "bk", "lf", "ft", "up", "dn" };

void CL_RequestNextDownload ( void ) {
    char fn[ MAX_QPATH ];

    if( cls.state != ca_connected && cls.state != ca_loading )
        return;

    if( ( !allow_download->integer || NET_IsLocalAddress( &cls.serverAddress ) ) && precache_check < ENV_CNT )
        precache_check = ENV_CNT;

    //ZOID
    if( precache_check == CS_MODELS ) { // confirm map
        precache_check = CS_MODELS + 2; // 0 isn't used
        if( allow_download_maps->integer )
            if( !CL_CheckOrDownloadFile( cl.configstrings[ CS_MODELS + 1 ] ) )
                return; // started a download
    }

    if( precache_check >= CS_MODELS && precache_check < CS_MODELS + MAX_MODELS ) {
        if( allow_download_models->integer ) {
            if( precache_model_skin == -1 ) {
                // checking for models
                while( precache_check < CS_MODELS + MAX_MODELS &&
                    cl.configstrings[ precache_check ][ 0 ] )
                {
                    if( cl.configstrings[ precache_check ][ 0 ] == '*' ||
                        cl.configstrings[ precache_check ][ 0 ] == '#' )
                    {
                        precache_check++;
                        continue;
                    }
                    if( !CL_CheckOrDownloadFile( cl.configstrings[ precache_check ] ) ) {
                        precache_check++;
                        return; // started a download
                    }
                    precache_check++;
                }
                precache_model_skin = 0;
                precache_check = CS_MODELS + 2; // 0 isn't used
#if USE_CURL
                if( HTTP_DownloadsPending() ) {
                    //pending downloads (models), let's wait here before we can check skins.
                    return;
                }
#endif
            }

            // checking for skins
            while( precache_check < CS_MODELS + MAX_MODELS &&
                cl.configstrings[ precache_check ][ 0 ] )
            {
                size_t num_skins, ofs_skins, end_skins;
                dmd2header_t *md2header;
                dsp2header_t *sp2header;
                dsp2frame_t *sp2frame;
                uint32_t ident;
                size_t length;

                if( cl.configstrings[ precache_check ][ 0 ] == '*' ||
                    cl.configstrings[ precache_check ][ 0 ] == '#' )
                {
                    precache_check++;
                    continue;
                }

                if( !precache_model ) {
                    length = FS_LoadFile( cl.configstrings[ precache_check ], ( void ** )&precache_model );
                    if( !precache_model ) {
                        precache_model_skin = 0;
                        precache_check++;
                        continue; // couldn't load it
                    }
                    if( length < sizeof( ident ) ) {
                        // file too small
                        goto done;
                    }

                    // check ident
                    ident = LittleLong( *( uint32_t * )precache_model );
                    switch( ident ) {
                    case MD2_IDENT:
                        // alias model
                        md2header = ( dmd2header_t * )precache_model;
                        if( length < sizeof( *md2header ) ||
                            LittleLong( md2header->ident ) != MD2_IDENT ||
                            LittleLong( md2header->version ) != MD2_VERSION )
                        {
                            // not an alias model
                            goto done;
                        }

                        num_skins = LittleLong( md2header->num_skins );
                        ofs_skins = LittleLong( md2header->ofs_skins );
                        end_skins = ofs_skins + num_skins * MD2_MAX_SKINNAME;
                        if( num_skins > MD2_MAX_SKINS || end_skins < ofs_skins || end_skins > length ) {
                            // bad alias model
                            goto done;
                        }
                        break;
                    case SP2_IDENT:
                        // sprite model
                        sp2header = ( dsp2header_t * )precache_model;
                        if( length < sizeof( *sp2header ) ||
                            LittleLong( sp2header->ident ) != SP2_IDENT ||
                            LittleLong( sp2header->version ) != SP2_VERSION )
                        {
                            // not a sprite model
                            goto done;
                        }
                        num_skins = LittleLong( sp2header->numframes );
                        ofs_skins = sizeof( *sp2header );
                        end_skins = ofs_skins + num_skins * sizeof( dsp2frame_t );
                        if( num_skins > SP2_MAX_FRAMES || end_skins < ofs_skins || end_skins > length ) {
                            // bad sprite model
                            goto done;
                        }
                        break;
                    default:
                        // unknown file format
                        goto done;
                    }
                }

                // check ident
                ident = LittleLong( *( uint32_t * )precache_model );
                switch( ident ) {
                case MD2_IDENT:
                    // alias model
                    md2header = ( dmd2header_t * )precache_model;
                    num_skins = LittleLong( md2header->num_skins );
                    ofs_skins = LittleLong( md2header->ofs_skins );

                    while( precache_model_skin < num_skins ) {
                        if( !Q_memccpy( fn, ( char * )precache_model + ofs_skins +
                            precache_model_skin * MD2_MAX_SKINNAME, 0, sizeof( fn ) ) )
                        {
                            // bad alias model
                            goto done;
                        }
                        if( !CL_CheckOrDownloadFile( fn ) ) {
                            precache_model_skin++;
                            return; // started a download
                        }
                        precache_model_skin++;
                    }
                    break;
                case SP2_IDENT:
                    // sprite model
                    sp2header = ( dsp2header_t * )precache_model;
                    num_skins = LittleLong( sp2header->numframes );
                    ofs_skins = sizeof( *sp2header );

                    while( precache_model_skin < num_skins ) {
                        sp2frame = ( dsp2frame_t * )( ( byte * )precache_model + ofs_skins ) + precache_model_skin;
                        if( !Q_memccpy( fn, sp2frame->name, 0, sizeof( fn ) ) ) {
                            // bad sprite model
                            goto done;
                        }
                        if( !CL_CheckOrDownloadFile( fn ) ) {
                            precache_model_skin++;
                            return; // started a download
                        }
                        precache_model_skin++;
                    }
                    break;
                default:
                    // unknown file format
                    break;
                }

done:
                FS_FreeFile( precache_model );
                precache_model = NULL;
                precache_model_skin = 0;
                precache_check++;
            }
        }
        precache_check = CS_SOUNDS;
    }

    if( precache_check >= CS_SOUNDS && precache_check < CS_SOUNDS + MAX_SOUNDS ) {
        if( allow_download_sounds->integer ) {
            if( precache_check == CS_SOUNDS )
                precache_check++; // zero is blank
            while( precache_check < CS_SOUNDS + MAX_SOUNDS &&
                cl.configstrings[ precache_check ][ 0 ] )
            {
                if( cl.configstrings[ precache_check ][ 0 ] == '*' ) {
                    precache_check++;
                    continue;
                }
                Q_concat( fn, sizeof( fn ), "sound/", cl.configstrings[ precache_check++ ], NULL );
                if( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
        }
        precache_check = CS_IMAGES;
    }

    if( precache_check >= CS_IMAGES && precache_check < CS_IMAGES + MAX_IMAGES ) {
        if( allow_download_pics->integer ) {
            if( precache_check == CS_IMAGES )
                precache_check++; // zero is blank
            while( precache_check < CS_IMAGES + MAX_IMAGES &&
                cl.configstrings[ precache_check ][ 0 ] )
            {
                Q_concat( fn, sizeof( fn ), "pics/", cl.configstrings[ precache_check++ ], ".pcx", NULL );
                if( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
        }
        precache_check = CS_PLAYERSKINS;
    }

    // skins are special, since a player has three things to download:
    // model, weapon model and skin
    // so precache_check is now *3
    if( precache_check >= CS_PLAYERSKINS && precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT ) {
        if( allow_download_players->integer ) {
            while( precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT ) {
                int i, n;
                char model[ MAX_QPATH ], skin[ MAX_QPATH ], *p;

                i = ( precache_check - CS_PLAYERSKINS ) / PLAYER_MULT;
                n = ( precache_check - CS_PLAYERSKINS ) % PLAYER_MULT;

                if( !cl.configstrings[ CS_PLAYERSKINS + i ][ 0 ] ) {
                    precache_check = CS_PLAYERSKINS + ( i + 1 ) * PLAYER_MULT;
                    continue;
                }

                if( ( p = strchr( cl.configstrings[ CS_PLAYERSKINS + i ], '\\' ) ) != NULL )
                    p++;
                else
                    p = cl.configstrings[ CS_PLAYERSKINS + i ];
                Q_strlcpy( model, p, sizeof( model ) );
                p = strchr( model, '/' );
                if( !p )
                    p = strchr( model, '\\' );
                if( p ) {
                    *p++ = 0;
                    strcpy( skin, p );
                } else
                    *skin = 0;

                switch( n ) {
                case 0:   // model
                    Q_concat( fn, sizeof( fn ), "players/", model, "/tris.md2", NULL );
                    if( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 1;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 1:   // weapon model
                    Q_concat( fn, sizeof( fn ), "players/", model, "/weapon.md2", NULL );
                    if( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 2;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 2:   // weapon skin
                    Q_concat( fn, sizeof( fn ), "players/", model, "/weapon.pcx", NULL );
                    if( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 3;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 3:   // skin
                    Q_concat( fn, sizeof( fn ), "players/", model, "/", skin, ".pcx", NULL );
                    if( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 4;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 4:   // skin_i
                    Q_concat( fn, sizeof( fn ), "players/", model, "/", skin, "_i.pcx", NULL );
                    if( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 5;
                        return; // started a download
                    }
                    // move on to next model
                    precache_check = CS_PLAYERSKINS + ( i + 1 ) * PLAYER_MULT;
                }
            }
        }
        // precache phase completed
        precache_check = ENV_CNT;
    }

#if USE_CURL
    if( HTTP_DownloadsPending() ) {
        //map might still be downloading?
        return;
    }
#endif

    if( precache_check == ENV_CNT ) {
        CL_RegisterModels();
        precache_check = ENV_CNT + 1;
    }

    if( precache_check > ENV_CNT && precache_check < TEXTURE_CNT ) {
        if( allow_download->integer && allow_download_textures->integer ) {
            while( precache_check < TEXTURE_CNT ) {
                int n = precache_check++ - ENV_CNT - 1;

                if( n & 1 )
                    Q_concat( fn, sizeof( fn ),
                        "env/", cl.configstrings[ CS_SKY ], env_suf[ n / 2 ], ".pcx", NULL );
                else
                    Q_concat( fn, sizeof( fn ),
                        "env/", cl.configstrings[ CS_SKY ], env_suf[ n / 2 ], ".tga", NULL );
                if( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
        }
        precache_check = TEXTURE_CNT;
    }

    if( precache_check == TEXTURE_CNT ) {
        precache_check = TEXTURE_CNT + 1;
        precache_tex = 0;
    }

    // confirm existance of textures, download any that don't exist
    if( precache_check == TEXTURE_CNT + 1 ) {
        if( allow_download->integer && allow_download_textures->integer ) {
            while( precache_tex < cl.bsp->numtexinfo ) {
                char *texname = cl.bsp->texinfo[ precache_tex++ ].name;

                // check if 32-bit replacements are present
                Q_concat( fn, sizeof( fn ), "textures/", texname, ".png", NULL );
                if( !FS_FileExists( fn ) ) {
                    Q_concat( fn, sizeof( fn ), "textures/", texname, ".jpg", NULL );
                    if( !FS_FileExists( fn ) ) {
                        Q_concat( fn, sizeof( fn ), "textures/", texname, ".tga", NULL );
                        if( !FS_FileExists( fn ) ) {
                            Q_concat( fn, sizeof( fn ), "textures/", texname, ".wal", NULL );
                            if( !CL_CheckOrDownloadFile( fn ) ) {
                                return; // started a download
                            }
                        }
                    }
                }
            }
        }
        precache_check = TEXTURE_CNT + 999;
    }

#if USE_CURL
    if( HTTP_DownloadsPending() ) {
        //pending downloads (possibly textures), let's wait here.
        return;
    }
#endif

    // all done, tell server we are ready
    CL_Begin();
}

void CL_ResetPrecacheCheck( void ) {
    precache_check = CS_MODELS;
    if( precache_model ) {
        FS_FreeFile( precache_model );
        precache_model = NULL;
    }
    precache_model_skin = -1;
}

