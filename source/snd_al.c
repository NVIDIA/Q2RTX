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

#include "cl_local.h"
#include "snd_local.h"
#include "qal_api.h"

// translates from AL coordinate system to quake
#define OAL_POS3f(v)  -v[1], v[2], -v[0]

static ALuint s_srcnums[MAX_CHANNELS];
static int s_framecount;

void AL_SoundInfo( void ) {
    Com_Printf( "AL_VENDOR: %s\n", qalGetString( AL_VENDOR ) );
    Com_Printf( "AL_RENDERER: %s\n", qalGetString( AL_RENDERER ) );
    Com_Printf( "AL_VERSION: %s\n", qalGetString( AL_VERSION ) );
    Com_Printf( "AL_EXTENSIONS: %s\n", qalGetString( AL_EXTENSIONS ) );
}

static void AL_ShowError( const char *func ) {
    ALenum err;

    if( ( err = qalGetError() ) != AL_NO_ERROR ) {
        Com_EPrintf( "%s: %s\n", func, qalGetString( err ) );
    }
}

qboolean AL_Init( void ) {
    if( !QAL_Init() ) {
        Com_EPrintf( "OpenAL failed to initialize.\n" );
        return qfalse;
    }

    // generate source names
    qalGenSources( MAX_CHANNELS, s_srcnums );
    AL_ShowError( __func__ );

    Com_Printf( "OpenAL initialized.\n" );
    return qtrue;
}

void AL_Shutdown( void ) {
    Com_Printf( "Shutting down OpenAL.\n" );

    if( s_srcnums[0] ) {
        // delete source names
        qalDeleteSources( MAX_CHANNELS, s_srcnums );
        memset( s_srcnums, 0, sizeof( s_srcnums ) );
    }

    QAL_Shutdown();
}

sfxcache_t *AL_UploadSfx( sfx_t *s ) {
    sfxcache_t *sc;
    ALsizei size = s_info.samples * s_info.width;
    ALenum format = s_info.width == 2 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;
    ALuint name;

    qalGenBuffers( 1, &name );
    qalBufferData( name, format, s_info.data, size, s_info.rate ); 
    AL_ShowError( __func__ );

    // allocate placeholder sfxcache
    sc = s->cache = S_Malloc( sizeof( *sc ) );
    sc->length = s_info.samples * 1000 / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->size = size;
    sc->bufnum = name;

    return sc;
}

void AL_DeleteSfx( sfx_t *s ) {
    sfxcache_t *sc;
    ALuint name;

    sc = s->cache;
    if( !sc ) {
        return;
    }

    name = sc->bufnum;
    qalDeleteBuffers( 1, &name );
}

static void AL_Spatialize( channel_t *ch ) {
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    // no attenuation = no spatialization
    if( ch->entnum == -1 || ch->entnum == listener_entnum || !ch->dist_mult ) {
        VectorCopy( listener_origin, origin );
    } else if( ch->fixed_origin ) {
        VectorCopy( ch->origin, origin );
    } else {
        CL_GetEntitySoundOrigin( ch->entnum, origin );
    }

    qalSource3f( ch->srcnum, AL_POSITION, OAL_POS3f( origin ) );
}

void AL_StopChannel( channel_t *ch ) {
#ifdef _DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name );
#endif

    // stop it
    qalSourceStop( ch->srcnum );
    qalSourcei( ch->srcnum, AL_BUFFER, AL_NONE );
    AL_ShowError( __func__ );
    memset (ch, 0, sizeof(*ch));
}

void AL_PlayChannel( channel_t *ch ) {
    sfxcache_t *sc = ch->sfx->cache;

#ifdef _DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name );
#endif

    ch->srcnum = s_srcnums[ch - channels];
    qalSourcei( ch->srcnum, AL_BUFFER, sc->bufnum );
    //qalSourcei( ch->srcnum, AL_LOOPING, sc->loopstart == -1 ? AL_FALSE : AL_TRUE );
    qalSourcei( ch->srcnum, AL_LOOPING, ch->autosound ? AL_TRUE : AL_FALSE );
    qalSourcef( ch->srcnum, AL_GAIN, ch->master_vol );
    qalSourcef( ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME );
    qalSourcef( ch->srcnum, AL_MAX_DISTANCE, 8192 );
    qalSourcef( ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * ( 8192 - SOUND_FULLVOLUME ) );

    AL_Spatialize( ch );

    // play it
    qalSourcePlay( ch->srcnum );
    AL_ShowError( __func__ );
}

static void AL_IssuePlaysounds( void ) {
    playsound_t *ps;

    // start any playsounds
    while (1) {
        ps = s_pendingplays.next;
        if (ps == &s_pendingplays)
            break;  // no more pending sounds
        if (ps->begin > paintedtime)
            break;
        S_IssuePlaysound (ps);
    }
}

void AL_StopAllChannels( void ) {
    int         i;
    channel_t   *ch;

    ch = channels;
    for( i = 0; i < MAX_CHANNELS; i++, ch++ ) {
        if (!ch->sfx)
            continue;
        AL_StopChannel( ch );
    }
}

static channel_t *AL_FindLoopingSound( int entnum, sfx_t *sfx ) {
    int         i;
    channel_t   *ch;

    ch = channels;
    for( i = 0; i < MAX_CHANNELS; i++, ch++ ) {
        if( !ch->sfx )
            continue;
        if( !ch->autosound )
            continue;
        if( ch->entnum != entnum )
            continue;
        if( ch->sfx != sfx )
            continue;
        return ch;
    }
    return NULL;
}

static void AL_AddLoopSounds( void ) {
    int         i;
    int         sounds[MAX_PACKET_ENTITIES];
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t  *ent;

    if( cls.state != ca_active || sv_paused->integer || !s_ambient->integer ) {
        return;
    }

    S_BuildSoundList( sounds );

    for( i = 0; i < cl.frame.numEntities; i++ ) {
        if (!sounds[i])
            continue;

        sfx = S_SfxForHandle( cl.sound_precache[sounds[i]] );
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        num = ( cl.frame.firstEntity + i ) & PARSE_ENTITIES_MASK;
        ent = &cl.entityStates[num];

        ch = AL_FindLoopingSound( ent->number, sfx );
        if( ch ) {
            ch->autoframe = s_framecount;
            ch->end = paintedtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        ch->autosound = qtrue;  // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        ch->master_vol = 1;
        ch->dist_mult = SOUND_LOOPATTENUATE;
        ch->end = paintedtime + sc->length;

        AL_PlayChannel( ch );
    }
}

void AL_Update( void ) {
    int         i;
    channel_t   *ch;
    vec_t       orientation[6];

    if( cls.active != ACT_ACTIVATED ) {
        return;
    }

    paintedtime = cl.time;

    qalListener3f( AL_POSITION, OAL_POS3f( listener_origin ) );
    orientation[0] = -listener_forward[1];
    orientation[1] = listener_forward[2];
    orientation[2] = -listener_forward[0];
    orientation[3] = -listener_up[1];
    orientation[4] = listener_up[2];
    orientation[5] = -listener_up[0];
    qalListenerfv( AL_ORIENTATION, orientation );
    qalListenerf( AL_GAIN, s_volume->value );
    qalDistanceModel( AL_LINEAR_DISTANCE_CLAMPED );

    // update spatialization for dynamic sounds 
    ch = channels;
    for( i = 0; i < MAX_CHANNELS; i++, ch++ ) {
        if( !ch->sfx )
            continue;

        if( ch->autosound ) {
            // autosounds are regenerated fresh each frame
            if( ch->autoframe != s_framecount ) {
                AL_StopChannel( ch );
                continue;
            }
        } else {
            ALenum state;

            qalGetSourcei( ch->srcnum, AL_SOURCE_STATE, &state );
            AL_ShowError( __func__ );
            if( state == AL_STOPPED ) {
                AL_StopChannel( ch );
                continue;
            }
        }

#ifdef _DEBUG
        if (s_show->integer) {
            Com_Printf ("%.1f %s\n", ch->master_vol, ch->sfx->name);
        //    total++;
        }
#endif

        AL_Spatialize(ch);         // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    AL_AddLoopSounds ();

    AL_IssuePlaysounds();
}

