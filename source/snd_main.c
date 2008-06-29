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

// =======================================================================
// Internal sound data & structures
// =======================================================================

// only begin attenuating sound volumes when outside the FULLVOLUME range
#define		SOUND_FULLVOLUME	80

#define		SOUND_LOOPATTENUATE	0.003

int			s_registration_sequence;

channel_t   channels[MAX_CHANNELS];

qboolean	sound_started;

dma_t		dma;

vec3_t		listener_origin;
vec3_t		listener_forward;
vec3_t		listener_right;
vec3_t		listener_up;
int			listener_entnum;

qboolean	s_registering;

int   		paintedtime; 	// sample PAIRS

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.
#define		MAX_SFX		(MAX_SOUNDS*2)
static sfx_t		known_sfx[MAX_SFX];
static int			num_sfx;

#define		MAX_PLAYSOUNDS	128
playsound_t	s_playsounds[MAX_PLAYSOUNDS];
playsound_t	s_freeplays;
playsound_t	s_pendingplays;

static int			s_beginofs;

cvar_t		*s_khz;
cvar_t		*s_volume;
cvar_t		*s_testsound;

static cvar_t	    *s_enable;
#if USE_DSOUND
static cvar_t	    *s_direct;
#endif
static cvar_t		*s_show;
static cvar_t		*s_mixahead;
static cvar_t		*s_ambient;

void WAVE_FillAPI( snddmaAPI_t *api );

#if USE_DSOUND
void DS_FillAPI( snddmaAPI_t *api );
#endif

snddmaAPI_t	snddma;

/*
==========================================================================
console functions
==========================================================================
*/

static void S_SoundInfo_f( void ) {
	if( !sound_started ) {
		Com_Printf( "Sound system not started.\n" );
		return;
	}
	
    Com_Printf( "%5d stereo\n", dma.channels - 1 );
    Com_Printf( "%5d samples\n", dma.samples );
    Com_Printf( "%5d samplepos\n", dma.samplepos );
    Com_Printf( "%5d samplebits\n", dma.samplebits );
    Com_Printf( "%5d submission_chunk\n", dma.submission_chunk );
    Com_Printf( "%5d speed\n", dma.speed );
    Com_Printf( "0x%p dma buffer\n", dma.buffer );
}


static void S_Play_c( genctx_t *ctx, int state ) {
    FS_File_g( "sound", "*.wav", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER | 0x80000000, ctx );
}

static void S_Play_f( void ) {
	int 	i;
	char name[MAX_QPATH];

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <sound> [...]\n", Cmd_Argv( 0 ) );
		return;
	}

	for( i = 1; i < Cmd_Argc(); i++ ) {
		Cmd_ArgvBuffer( i, name, sizeof( name ) );
		COM_DefaultExtension( name, ".wav", sizeof( name ) );
		S_StartLocalSound( name );
	}
}

static void S_SoundList_f( void ) {
	int		i;
	sfx_t	*sfx;
	sfxcache_t	*sc;
	int		size, total;

	total = 0;
	for( sfx=known_sfx, i=0 ; i<num_sfx ; i++, sfx++ ) {
		if( !sfx->name[0] )
			continue;
		sc = sfx->cache;
		if( sc ) {
			size = sc->length * sc->width;
			total += size;
			if( sc->loopstart >= 0 )
				Com_Printf( "L" );
			else
				Com_Printf( " " );
			Com_Printf( "(%2db) %6i : %s\n", sc->width * 8,  size, sfx->name) ;
		} else {
			if( sfx->name[0] == '*' )
				Com_Printf( "  placeholder : %s\n", sfx->name );
			else
				Com_Printf( "  not loaded  : %s\n", sfx->name );
		}
	}
	Com_Printf( "Total resident: %i\n", total );
}

static const cmdreg_t c_sound[] = {
	{ "play", S_Play_f, S_Play_c },
	{ "stopsound", S_StopAllSounds },
	{ "soundlist", S_SoundList_f },
	{ "soundinfo", S_SoundInfo_f },

    { NULL }
};

/*
================
S_Init
================
*/
void S_Init( void ) {
    sndinitstat_t ret = SIS_FAILURE;

	s_enable = Cvar_Get( "s_enable", "1", CVAR_SOUND );
	if( !s_enable->integer ) {
		Com_Printf( "Sound initialization disabled.\n" );
		return;
	}

	Com_Printf( "------- S_Init -------\n" );

	s_khz = Cvar_Get( "s_khz", "22", CVAR_ARCHIVE|CVAR_SOUND );

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
            return;
        }
    }

	s_volume = Cvar_Get( "s_volume", "0.7", CVAR_ARCHIVE );
	s_mixahead = Cvar_Get( "s_mixahead", "0.2", CVAR_ARCHIVE );
	s_show = Cvar_Get( "s_show", "0", 0 );
	s_testsound = Cvar_Get( "s_testsound", "0", 0 );
	s_ambient = Cvar_Get( "s_ambient", "1", 0 );
	
    Cmd_Register( c_sound );

	S_InitScaletable();

	sound_started = qtrue;
	num_sfx = 0;

	paintedtime = 0;

	s_registration_sequence = 1;

	Com_Printf( "sound sampling rate: %i\n", dma.speed );

	S_StopAllSounds();

	Com_Printf( "----------------------\n" );
}


// =======================================================================
// Shutdown sound engine
// =======================================================================

void S_FreeAllSounds( void ) {
	int		i;
	sfx_t	*sfx;

	// free all sounds
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] )
			continue;
		if( sfx->cache )
			Z_Free( sfx->cache );
        if( sfx->truename )
            Z_Free( sfx->truename );
	    memset( sfx, 0, sizeof( *sfx ) );
	}

	num_sfx = 0;
}

void S_Shutdown( void ) {
	if( !sound_started )
		return;

	snddma.Shutdown();

	sound_started = qfalse;

#if USE_DSOUND
    s_direct->changed = NULL;
#endif
    s_khz->changed = NULL;

    Cmd_Deregister( c_sound );

	S_FreeAllSounds();

    Z_LeakTest( TAG_SOUND );
}

void S_Activate( void ) {
	if( sound_started ) {
#ifdef _WIN32
        S_StopAllSounds(); // FIXME
#endif
		snddma.Activate( cls.active == ACT_ACTIVATED ? qtrue : qfalse );
	}
}


// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_SfxForHandle
==================
*/
static sfx_t *S_SfxForHandle( qhandle_t hSfx ) {
	if( !hSfx ) {
		return NULL;
	}

	if( hSfx < 1 || hSfx > num_sfx ) {
		Com_Error( ERR_DROP, "S_SfxForHandle: %d out of range", hSfx );
	}

	return &known_sfx[hSfx - 1];
}

static sfx_t *S_AllocSfx( const char *name ) {
	sfx_t	*sfx;
	int		i;

	// find a free sfx
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] )
			break;
    }

	if( i == num_sfx ) {
		if( num_sfx == MAX_SFX )
			Com_Error( ERR_DROP, "S_AllocSfx: out of sfx_t" );
		num_sfx++;
	}
	
	memset( sfx, 0, sizeof( *sfx ) );
	Q_strncpyz( sfx->name, name, sizeof( sfx->name ) );
	sfx->registration_sequence = s_registration_sequence;

    return sfx;
}

/*
==================
S_FindName

==================
*/
static sfx_t *S_FindName( const char *name ) {
	int		i;
	sfx_t	*sfx;

	if( !name )
		Com_Error( ERR_FATAL, "S_FindName: NULL" );

	if( !name[0] )
		Com_Error( ERR_DROP, "S_FindName: empty name" );

	// see if already loaded
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !Q_stricmp( sfx->name, name ) ) {
			sfx->registration_sequence = s_registration_sequence;
			return sfx;
		}
	}

    // allocate new one
    sfx = S_AllocSfx( name );
	
	return sfx;
}

/*
=====================
S_BeginRegistration

=====================
*/
void S_BeginRegistration( void ) {
	s_registration_sequence++;
	s_registering = qtrue;
}

/*
==================
S_RegisterSound

==================
*/
qhandle_t S_RegisterSound( const char *name ) {
	sfx_t	*sfx;

	if( !sound_started )
		return 0;

	sfx = S_FindName( name );

	if( !s_registering ) {
		if( !S_LoadSound( sfx ) ) {
			/*return 0;*/
		}
	}

	return ( sfx - known_sfx ) + 1;
}


/*
=====================
S_EndRegistration

=====================
*/
void S_EndRegistration( void ) {
	int		i;
	sfx_t	*sfx;
    sfxcache_t *sc;
	int		size;

	// clear playsound list, so we don't free sfx still present there
	S_StopAllSounds();

	// free any sounds not from this registration sequence
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] )
			continue;
		if( sfx->registration_sequence != s_registration_sequence ) {	
			// don't need this sound
			if( sfx->cache )	// it is possible to have a leftover
				Z_Free( sfx->cache );	// from a server that didn't finish loading
            if( sfx->truename )
                Z_Free( sfx->truename );
			memset( sfx, 0, sizeof( *sfx ) );
		} else {	
			// make sure it is paged in
            sc = sfx->cache;
			if( sc ) {
				size = sc->length * sc->width;
				Com_PageInMemory( sc, size );
			}
		}
	}

	// load everything in
	for( i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++ ) {
		if( !sfx->name[0] )
			continue;
		S_LoadSound( sfx );
	}

	s_registering = qfalse;
}


//=============================================================================

/*
=================
S_PickChannel

picks a channel based on priorities, empty slots, number of channels
=================
*/
static channel_t *S_PickChannel( int entnum, int entchannel ) {
    int			ch_idx;
    int			first_to_die;
    int			life_left;
	channel_t	*ch;

	if( entchannel < 0 )
		Com_Error( ERR_DROP, "S_PickChannel: entchannel < 0" );

// Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = 0x7fffffff;
    for( ch_idx = 0; ch_idx < MAX_CHANNELS; ch_idx++ ) {
        ch = &channels[ch_idx];
        // channel 0 never overrides unless out of channels
		if( ch->entnum == entnum && ch->entchannel == entchannel && entchannel != 0 ) {
            if( entchannel == 256 && ch->sfx ) {
                return NULL; // channel 256 never overrides
            }
			// always override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if( ch->entnum == listener_entnum && entnum != listener_entnum && ch->sfx )
			continue;

		if( ch->end - paintedtime < life_left ) {
			life_left = ch->end - paintedtime;
			first_to_die = ch_idx;
		}
    }

	if( first_to_die == -1 )
		return NULL;

	ch = &channels[first_to_die];
	memset( ch, 0, sizeof( *ch ) );

    return ch;
}       

/*
=================
S_SpatializeOrigin

Used for spatializing channels and autosounds
=================
*/
void S_SpatializeOrigin( const vec3_t origin, float master_vol, float dist_mult, int *left_vol, int *right_vol ) {
    vec_t		dot;
    vec_t		dist;
    vec_t		lscale, rscale, scale;
    vec3_t		source_vec;

	if( cls.state != ca_active ) {
		*left_vol = *right_vol = 255;
		return;
	}

// calculate stereo seperation and distance attenuation
	VectorSubtract( origin, listener_origin, source_vec );

	dist = VectorNormalize( source_vec );
	dist -= SOUND_FULLVOLUME;
	if( dist < 0 )
		dist = 0;			// close enough to be at full volume
	dist *= dist_mult;		// different attenuation levels
	
	dot = DotProduct( listener_right, source_vec );

	if( dma.channels == 1 || !dist_mult ) { 
		// no attenuation = no spatialization
		rscale = 1.0;
		lscale = 1.0;
	} else {
		rscale = 0.5 * ( 1.0 + dot );
		lscale = 0.5 * ( 1.0 - dot );
	}

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
	vec3_t		origin;

	// anything coming from the view entity will always be full volume
	if( ch->entnum == -1 || ch->entnum == listener_entnum ) {
		ch->leftvol = ch->master_vol;
		ch->rightvol = ch->master_vol;
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
=================
S_AllocPlaysound
=================
*/
playsound_t *S_AllocPlaysound( void ) {
	playsound_t	*ps;

	ps = s_freeplays.next;
	if( ps == &s_freeplays )
		return NULL;		// no free playsounds

	// unlink from freelist
	ps->prev->next = ps->next;
	ps->next->prev = ps->prev;
	
	return ps;
}


/*
=================
S_FreePlaysound
=================
*/
void S_FreePlaysound( playsound_t *ps ) {
	// unlink from channel
	ps->prev->next = ps->next;
	ps->next->prev = ps->prev;

	// add to free list
	ps->next = s_freeplays.next;
	s_freeplays.next->prev = ps;
	ps->prev = &s_freeplays;
	s_freeplays.next = ps;
}



/*
===============
S_IssuePlaysound

Take the next playsound and begin it on the channel
This is never called directly by S_Play*, but only
by the update loop.
===============
*/
void S_IssuePlaysound( playsound_t *ps ) {
	channel_t	*ch;
	sfxcache_t	*sc;

	if( s_show->integer )
		Com_Printf( "Issue %i\n", ps->begin );
	// pick a channel to play on
	ch = S_PickChannel( ps->entnum, ps->entchannel );
	if( !ch ) {
		S_FreePlaysound( ps );
		return;
	}

	sc = S_LoadSound( ps->sfx );
	if( !sc ) {
		Com_Printf( "S_IssuePlaysound: couldn't load %s\n", ps->sfx->name );
		S_FreePlaysound( ps );
		return;
	}

	// spatialize
	if( ps->attenuation == ATTN_STATIC )
		ch->dist_mult = ps->attenuation * 0.001;
	else
		ch->dist_mult = ps->attenuation * 0.0005;
	ch->master_vol = ps->volume;
	ch->entnum = ps->entnum;
	ch->entchannel = ps->entchannel;
	ch->sfx = ps->sfx;
	VectorCopy( ps->origin, ch->origin );
	ch->fixed_origin = ps->fixed_origin;

	S_Spatialize( ch );

	ch->pos = 0;
    ch->end = paintedtime + sc->length;

	// free the playsound
	S_FreePlaysound( ps );
}

/*
====================
S_RegisterSexedSound
====================
*/
static sfx_t *S_RegisterSexedSound( int entnum, const char *base ) {
	sfx_t		    *sfx;
	char			*model;
	char			buffer[MAX_QPATH];
    clientinfo_t    *ci;

	// determine what model the client is using
	if( entnum > 0 && entnum <= MAX_CLIENTS ) {
        ci = &cl.clientinfo[ entnum - 1 ];
    } else {
        ci = &cl.baseclientinfo;
    }
    model = ci->model_name;

	// if we can't figure it out, they're male
	if( !model[0] ) {
		model = "male";
    }

	// see if we already know of the model specific sound
	Q_concat( buffer, sizeof( buffer ),
        "#players/", model, "/", base + 1, NULL );
	sfx = S_FindName( buffer );

	// see if it exists
    if( !sfx->truename && !S_LoadSound( sfx ) ) {
		// no, revert to the male sound in the pak0.pak
		Q_concat( buffer, sizeof( buffer ),
            "player/male/", base + 1, NULL );
	    sfx->truename = S_CopyString( buffer );
    }

	return sfx;
}


// =======================================================================
// Start a sound effect
// =======================================================================

/*
====================
S_StartSound

Validates the parms and ques the sound up
if pos is NULL, the sound will be dynamically sourced from the entity
Entchannel 0 will never override a playing sound
====================
*/
void S_StartSound( const vec3_t origin, int entnum, int entchannel, qhandle_t hSfx, float fvol, float attenuation, float timeofs ) {
	sfxcache_t	*sc;
	int			vol;
	playsound_t	*ps, *sort;
	int			start;
	sfx_t		*sfx;

	if( !sound_started )
		return;

	if( !( sfx = S_SfxForHandle( hSfx ) ) ) {
		return;
	}

	if( sfx->name[0] == '*' ) {
		sfx = S_RegisterSexedSound( entnum, sfx->name );
	}

	// make sure the sound is loaded
	sc = S_LoadSound( sfx );
	if( !sc )
		return;		// couldn't load the sound's data

	vol = fvol*255;

	// make the playsound_t
	ps = S_AllocPlaysound();
	if( !ps )
		return;

	if( origin ) {
		VectorCopy( origin, ps->origin );
		ps->fixed_origin = qtrue;
	} else {
		ps->fixed_origin = qfalse;
	}

	ps->entnum = entnum;
	ps->entchannel = entchannel;
	ps->attenuation = attenuation;
	ps->volume = vol;
	ps->sfx = sfx;

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

	if( !timeofs )
		ps->begin = paintedtime;
	else
		ps->begin = start + timeofs * dma.speed;

	// sort into the pending sound list
	for( sort = s_pendingplays.next ; sort != &s_pendingplays && sort->begin < ps->begin ; sort = sort->next )
		;

	ps->next = sort;
	ps->prev = sort->prev;

	ps->next->prev = ps;
	ps->prev->next = ps;
}


/*
==================
S_StartLocalSound
==================
*/
void S_StartLocalSound( const char *sound ) {
	if( sound_started ) {
	    qhandle_t sfx = S_RegisterSound( sound );
    	S_StartSound( NULL, listener_entnum, 0, sfx, 1, 1, 0 );
    }
}

void S_StartLocalSound_( const char *sound ) {
	if( sound_started ) {
	    qhandle_t sfx = S_RegisterSound( sound );
    	S_StartSound( NULL, listener_entnum, 256, sfx, 1, 1, 0 );
    }
}


/*
==================
S_ClearBuffer
==================
*/
void S_ClearBuffer (void)
{
	int		clear;
		
	if (!sound_started)
		return;

	if (dma.samplebits == 8)
		clear = 0x80;
	else
		clear = 0;

	snddma.BeginPainting ();
	if (dma.buffer)
		memset(dma.buffer, clear, dma.samples * dma.samplebits/8);
	snddma.Submit ();
}

/*
==================
S_StopAllSounds
==================
*/
void S_StopAllSounds(void)
{
	int		i;

	if (!sound_started)
		return;

	// clear all the playsounds
	memset(s_playsounds, 0, sizeof(s_playsounds));
	s_freeplays.next = s_freeplays.prev = &s_freeplays;
	s_pendingplays.next = s_pendingplays.prev = &s_pendingplays;

	for (i=0 ; i<MAX_PLAYSOUNDS ; i++)
	{
		s_playsounds[i].prev = &s_freeplays;
		s_playsounds[i].next = s_freeplays.next;
		s_playsounds[i].prev->next = &s_playsounds[i];
		s_playsounds[i].next->prev = &s_playsounds[i];
	}

	// clear all the channels
	memset(channels, 0, sizeof(channels));

	S_ClearBuffer ();
}

/*
==================
S_AddLoopSounds

Entities with a ->sound field will generated looped sounds
that are automatically started, stopped, and merged together
as the entities are sent to the client
==================
*/
void S_AddLoopSounds (void)
{
	int			i, j;
	int			sounds[MAX_PACKET_ENTITIES];
	int			left, right, left_total, right_total;
	channel_t	*ch;
	sfx_t		*sfx;
	sfxcache_t	*sc;
	int			num;
	entity_state_t	*ent;
	centity_t *cent;

	if( sv_paused->integer ) {
		return;
	}
	if( cls.state != ca_active ) {
		return;
    }
	if( !s_ambient->integer ) {
		return;
	}

	for( i = 0; i < cl.frame.numEntities; i++ ) {
		num = ( cl.frame.firstEntity + i ) & PARSE_ENTITIES_MASK;
		ent = &cl.entityStates[num];
	    if( s_ambient->integer == 2 && ent->number != listener_entnum ) {
		    sounds[i] = 0;
        } else {
		    sounds[i] = ent->sound;
        }
	}

	for( i = 0; i < cl.frame.numEntities; i++ ) {
		if (!sounds[i])
			continue;

		sfx = S_SfxForHandle( cl.sound_precache[sounds[i]] );
		if (!sfx)
			continue;		// bad sound effect
		sc = sfx->cache;
		if (!sc)
			continue;

		num = ( cl.frame.firstEntity + i ) & PARSE_ENTITIES_MASK;
		ent = &cl.entityStates[num];
		cent = &cl_entities[ent->number];

		// find the total contribution of all sounds of this type
		S_SpatializeOrigin (cent->current.origin, 255.0, SOUND_LOOPATTENUATE,
			&left_total, &right_total);
		for (j=i+1 ; j<cl.frame.numEntities ; j++)
		{
			if (sounds[j] != sounds[i])
				continue;
			sounds[j] = 0;	// don't check this again later

			num = ( cl.frame.firstEntity + j ) & PARSE_ENTITIES_MASK;
			ent = &cl.entityStates[num];
			cent = &cl_entities[ent->number];

			S_SpatializeOrigin (cent->current.origin, 255.0, SOUND_LOOPATTENUATE, 
				&left, &right);
			left_total += left;
			right_total += right;
		}

		if (left_total == 0 && right_total == 0)
			continue;		// not audible

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
		ch->autosound = qtrue;	// remove next frame
		ch->sfx = sfx;
		ch->pos = paintedtime % sc->length;
		ch->end = paintedtime + sc->length - ch->pos;
	}
}

// =======================================================================
// Update sound buffer
// =======================================================================

static int S_GetTime(void) {
	static	int		buffers;
	static	int		oldsamplepos;
	int fullsamples = dma.samples / dma.channels;

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
	if (dma.samplepos < oldsamplepos) {
		buffers++;					// buffer wrapped
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

static void S_Update_(void) {
    int soundtime, endtime;
	int samps;

	snddma.BeginPainting ();

	if (!dma.buffer)
		return;

// Updates DMA time
	soundtime = S_GetTime();

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


/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update( void ) {
	int			i;
	int			total;
	channel_t	*ch;

    if( cvar_modified & CVAR_SOUND ) {
        Cbuf_AddText( "snd_restart\n" );
        return;
    }

	if (!sound_started)
		return;

	// if the laoding plaque is up, clear everything
	// out to make sure we aren't looping a dirty
	// dma buffer while loading
	if (cls.state == ca_loading )
	{
		/* S_ClearBuffer should be already done in S_StopAllSounds */
		return;
	}

	// rebuild scale tables if volume is modified
	if (s_volume->modified)
		S_InitScaletable ();

    // set listener entity number
    // other parameters should be already set up by CL_CalcViewValues
	if( cl.clientNum == -1 || cl.frame.clientNum == CLIENTNUM_NONE ) {
		listener_entnum = -1;
	} else {
    	listener_entnum = cl.frame.clientNum + 1;
    }

	// update spatialization for dynamic sounds	
	ch = channels;
	for (i=0 ; i<MAX_CHANNELS; i++, ch++)
	{
		if (!ch->sfx)
			continue;
		if (ch->autosound)
		{	// autosounds are regenerated fresh each frame
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

	//
	// debugging output
	//
	if (s_show->integer)
	{
		total = 0;
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

// mix some sound
	S_Update_();
}


