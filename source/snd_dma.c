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
// snd_dma.c -- main control for any streaming sound output device

#include "cl_local.h"
#include "snd_local.h"

dma_t       dma;

cvar_t      *s_khz;
cvar_t      *s_testsound;
#if USE_DSOUND
static cvar_t       *s_direct;
#endif
static cvar_t       *s_mixahead;

void WAVE_FillAPI( snddmaAPI_t *api );

#if USE_DSOUND
void DS_FillAPI( snddmaAPI_t *api );
#endif

static snddmaAPI_t snddma;

void DMA_SoundInfo( void ) {
    Com_Printf( "%5d channels\n", dma.channels );
    Com_Printf( "%5d samples\n", dma.samples );
    Com_Printf( "%5d samplepos\n", dma.samplepos );
    Com_Printf( "%5d samplebits\n", dma.samplebits );
    Com_Printf( "%5d submission_chunk\n", dma.submission_chunk );
    Com_Printf( "%5d speed\n", dma.speed );
    Com_Printf( "%p dma buffer\n", dma.buffer );
}

qboolean DMA_Init( void ) {
    sndinitstat_t ret = SIS_FAILURE;

    s_khz = Cvar_Get( "s_khz", "22", CVAR_ARCHIVE|CVAR_SOUND );
    s_mixahead = Cvar_Get( "s_mixahead", "0.2", CVAR_ARCHIVE );
    s_testsound = Cvar_Get( "s_testsound", "0", 0 );

#if USE_DSOUND
    s_direct = Cvar_Get( "s_direct", "1", CVAR_ARCHIVE|CVAR_SOUND );
    if( s_direct->integer ) {
        DS_FillAPI( &snddma );
        ret = snddma.Init();
        if( ret != SIS_SUCCESS ) {
            Cvar_Set( "s_direct", "0" );
        }
    }
#endif
    if( ret != SIS_SUCCESS ) {
        WAVE_FillAPI( &snddma );
        ret = snddma.Init();
        if( ret != SIS_SUCCESS ) {
            Cvar_Set( "s_enable", "0" );
            return qfalse;
        }
    }

    S_InitScaletable();

    Com_Printf( "sound sampling rate: %i\n", dma.speed );

    return qtrue;
}

void DMA_Shutdown( void ) {
    snddma.Shutdown();
}

void DMA_Activate( void ) {
    if( snddma.Activate ) {
        S_StopAllSounds();
        snddma.Activate( cls.active == ACT_ACTIVATED ? qtrue : qfalse );
    }
}

//=============================================================================

/*
=================
S_SpatializeOrigin

Used for spatializing channels and autosounds
=================
*/
static void S_SpatializeOrigin( const vec3_t origin, float master_vol, float dist_mult, int *left_vol, int *right_vol ) {
    vec_t       dot;
    vec_t       dist;
    vec_t       lscale, rscale, scale;
    vec3_t      source_vec;

    if( cls.state != ca_active ) {
        *left_vol = *right_vol = 255;
        return;
    }

// calculate stereo seperation and distance attenuation
    VectorSubtract( origin, listener_origin, source_vec );

    dist = VectorNormalize( source_vec );
    dist -= SOUND_FULLVOLUME;
    if( dist < 0 )
        dist = 0;           // close enough to be at full volume
    dist *= dist_mult;      // different attenuation levels
    
    dot = DotProduct( listener_right, source_vec );

    if( dma.channels == 1 || !dist_mult ) { 
        // no attenuation = no spatialization
        rscale = 1.0;
        lscale = 1.0;
    } else {
        rscale = 0.5 * ( 1.0 + dot );
        lscale = 0.5 * ( 1.0 - dot );
    }

    master_vol *= 255.0;

    // add in distance effect
    scale = ( 1.0 - dist ) * rscale;
    *right_vol = (int)( master_vol * scale );
    if( *right_vol < 0 )
        *right_vol = 0;

    scale = ( 1.0 - dist ) * lscale;
    *left_vol = (int)( master_vol * scale );
    if (*left_vol < 0)
        *left_vol = 0;
}

/*
=================
S_Spatialize
=================
*/
void S_Spatialize( channel_t *ch ) {
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    if( ch->entnum == -1 || ch->entnum == listener_entnum ) {
        ch->leftvol = ch->master_vol * 255;
        ch->rightvol = ch->master_vol * 255;
        return;
    }

    if( ch->fixed_origin ) {
        VectorCopy( ch->origin, origin );
    } else {
        CL_GetEntitySoundOrigin( ch->entnum, origin );
    }

    S_SpatializeOrigin( origin, ch->master_vol, ch->dist_mult, &ch->leftvol, &ch->rightvol );
}           

/*
==================
S_AddLoopSounds

Entities with a ->sound field will generated looped sounds
that are automatically started, stopped, and merged together
as the entities are sent to the client
==================
*/
static void S_AddLoopSounds (void)
{
    int         i, j;
    int         sounds[MAX_PACKET_ENTITIES];
    int         left, right, left_total, right_total;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t  *ent;
    centity_t *cent;

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
        cent = &cl_entities[ent->number];

        // find the total contribution of all sounds of this type
        S_SpatializeOrigin (cent->lerp_origin, 1.0, SOUND_LOOPATTENUATE,
            &left_total, &right_total);
        for (j=i+1 ; j<cl.frame.numEntities ; j++)
        {
            if (sounds[j] != sounds[i])
                continue;
            sounds[j] = 0;  // don't check this again later

            num = ( cl.frame.firstEntity + j ) & PARSE_ENTITIES_MASK;
            ent = &cl.entityStates[num];
            cent = &cl_entities[ent->number];

            S_SpatializeOrigin (cent->lerp_origin, 1.0, SOUND_LOOPATTENUATE, 
                &left, &right);
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            return;

        if (left_total > 255)
            left_total = 255;
        if (right_total > 255)
            right_total = 255;
        ch->leftvol = left_total;
        ch->rightvol = right_total;
        ch->autosound = qtrue;  // remove next frame
        ch->sfx = sfx;
        ch->pos = paintedtime % sc->length;
        ch->end = paintedtime + sc->length - ch->pos;
    }
}

//=============================================================================


int DMA_DriftBeginofs( float timeofs ) {
    static int  s_beginofs;
    int         start;

    // drift s_beginofs
    start = cl.servertime * 0.001 * dma.speed + s_beginofs;
    if( start < paintedtime ) {
        start = paintedtime;
        s_beginofs = start - ( cl.servertime * 0.001 * dma.speed );
    } else if ( start > paintedtime + 0.3 * dma.speed ) {
        start = paintedtime + 0.1 * dma.speed;
        s_beginofs = start - ( cl.servertime * 0.001 * dma.speed );
    } else {
        s_beginofs -= 10;
    }

    return timeofs ? start + timeofs * dma.speed : paintedtime;
}

void DMA_ClearBuffer( void ) {
    int     clear;
 
    if (dma.samplebits == 8)
        clear = 0x80;
    else
        clear = 0;

    snddma.BeginPainting ();
    if (dma.buffer)
        memset(dma.buffer, clear, dma.samples * dma.samplebits/8);
    snddma.Submit ();
}

static int DMA_GetTime(void) {
    static  int     buffers;
    static  int     oldsamplepos;
    int fullsamples = dma.samples / dma.channels;

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
    if (dma.samplepos < oldsamplepos) {
        buffers++;                  // buffer wrapped
        if (paintedtime > 0x40000000) {
            // time to chop things off to avoid 32 bit limits
            buffers = 0;
            paintedtime = fullsamples;
            S_StopAllSounds();
        }
    }
    oldsamplepos = dma.samplepos;

    return buffers*fullsamples + dma.samplepos/dma.channels;
}

static void DMA_PaintBuffer(void) {
    int soundtime, endtime;
    int samps;

    snddma.BeginPainting ();

    if (!dma.buffer)
        return;

// Updates DMA time
    soundtime = DMA_GetTime();

// check to make sure that we haven't overshot
    if (paintedtime < soundtime) {
        Com_DPrintf ("S_Update_ : overflow\n");
        paintedtime = soundtime;
    }

// mix ahead of current position
    endtime = soundtime + s_mixahead->value * dma.speed;
//endtime = (soundtime + 4096) & ~4095;

    // mix to an even submission block size
    endtime = (endtime + dma.submission_chunk - 1)
        & ~(dma.submission_chunk - 1);
    samps = dma.samples >> (dma.channels-1);
    if (endtime - soundtime > samps)
        endtime = soundtime + samps;

    S_PaintChannels (endtime);

    snddma.Submit ();
}

void DMA_Update( void ) {
    int         i;
    channel_t   *ch;

    // rebuild scale tables if volume is modified
    if (s_volume->modified)
        S_InitScaletable ();

    // update spatialization for dynamic sounds 
    ch = channels;
    for (i=0 ; i<MAX_CHANNELS; i++, ch++)
    {
        if (!ch->sfx)
            continue;
        if (ch->autosound)
        {   // autosounds are regenerated fresh each frame
            memset (ch, 0, sizeof(*ch));
            continue;
        }
        S_Spatialize(ch);         // respatialize channel
        if (!ch->leftvol && !ch->rightvol)
        {
            memset (ch, 0, sizeof(*ch));
            continue;
        }
    }

    // add loopsounds
    S_AddLoopSounds ();

#ifdef _DEBUG
    //
    // debugging output
    //
    if (s_show->integer)
    {
        int total = 0;
        ch = channels;
        for (i=0 ; i<MAX_CHANNELS; i++, ch++)
            if (ch->sfx && (ch->leftvol || ch->rightvol) )
            {
                Com_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
                total++;
            }
    
        if( s_show->integer > 1 || total ) {
            Com_Printf ("----(%i)---- painted: %i\n", total, paintedtime);
        }
    }
#endif

// mix some sound
    DMA_PaintBuffer();
}

