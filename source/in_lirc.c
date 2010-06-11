/*
Copyright (C) 2010 skuller.net

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

#include "com_local.h"
#include "key_public.h"
#include "in_public.h"
#include "cl_public.h"
#include "sys_public.h"
#include "io_sleep.h"
#include "in_lirc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <lirc/lirc_client.h>

static cvar_t   *lirc_enable;
static cvar_t   *lirc_config;

static struct {
    qboolean initialized;
    struct lirc_config *config;
    int fd;
    ioentry_t *io;
} lirc;

void Lirc_GetEvents( void ) {
    char *code, *str;
    int ret, key;
    unsigned time;

    if( !lirc.initialized || !lirc.io->canread ) {
        return;
    }

    while( ( ret = lirc_nextcode( &code ) ) == 0 && code ) {
        time = Sys_Milliseconds();
        Com_DPrintf( "%s: %u %s\n", __func__, time, code );
        while( ( ret = lirc_code2char( lirc.config, code, &str ) ) == 0 && str ) {
            if( *str == '@' ) {
                key = Key_StringToKeynum( str + 1 );
                if( key > 0 ) {
                   Key_Event( key, qtrue, time );
                   Key_Event( key, qfalse, time );
                }
            } else {
                Cbuf_AddText( &cmd_buffer, str );
                Cbuf_AddText( &cmd_buffer, "\n" );
            }
        }
        free( code );
        if( ret ) {
            goto error;
        }
    }

    if( ret ) {
error:
        Com_EPrintf( "Error reading from LIRC.\n" );
        Cvar_Reset( lirc_enable );
        Lirc_Shutdown();
    }
}

void Lirc_Shutdown( void ) {
    if( !lirc.initialized ) {
        return;
    }
    lirc_freeconfig( lirc.config );
    IO_Remove( lirc.fd );
    lirc_deinit();
    memset( &lirc, 0, sizeof( lirc ) );
}

static void lirc_param_changed( cvar_t *self ) {
    Lirc_Shutdown();
    Lirc_Init();
}

qboolean Lirc_Init( void ) {
    lirc_enable = Cvar_Get( "lirc_enable", "0", 0 );
    lirc_enable->changed = lirc_param_changed;
    lirc_config = Cvar_Get( "lirc_config", "", 0 );
    lirc_config->changed = lirc_param_changed;

    if( !lirc_enable->integer ) {
        return qfalse;
    }

    lirc.fd = lirc_init( APPLICATION, 0 );
    if( lirc.fd == -1 ) {
        Com_EPrintf( "Failed to initialize LIRC.\n" );
        Cvar_Reset( lirc_enable );
        return qfalse;
    }

    if( lirc_readconfig( lirc_config->string[0] ? lirc_config->string : NULL,
        &lirc.config, NULL ) != 0 )
    {
        Com_EPrintf( "Failed to read LIRC config.\n" );
        lirc_deinit();
        Cvar_Reset( lirc_enable );
        return qfalse;
    }
 
    fcntl( lirc.fd, F_SETFL, fcntl( lirc.fd, F_GETFL, 0 ) | FNDELAY );
    lirc.io = IO_Add( lirc.fd );
    lirc.io->wantread = qtrue;

    Com_Printf( "LIRC interface initialized.\n" );
    lirc.initialized = qtrue;

    return qtrue;
}


