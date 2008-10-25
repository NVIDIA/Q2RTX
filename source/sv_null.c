#include "com_local.h"

cvar_t *allow_download;
cvar_t *allow_download_players;
cvar_t *allow_download_models;
cvar_t *allow_download_sounds;
cvar_t *allow_download_maps;
cvar_t *allow_download_demos;
cvar_t *allow_download_other;

cvar_t	*rcon_password;	

void SV_Init( void ) {
	allow_download = Cvar_Get( "allow_download", "1", CVAR_ARCHIVE );
	allow_download_players = Cvar_Get( "allow_download_players", "0", CVAR_ARCHIVE );
	allow_download_models = Cvar_Get( "allow_download_models", "1", CVAR_ARCHIVE );
	allow_download_sounds = Cvar_Get( "allow_download_sounds", "1", CVAR_ARCHIVE );
	allow_download_maps	= Cvar_Get( "allow_download_maps", "1", CVAR_ARCHIVE );
	allow_download_demos = Cvar_Get( "allow_download_demos", "0", 0 );
	allow_download_other = Cvar_Get( "allow_download_other", "0", 0 );

	rcon_password = Cvar_Get( "rcon_password", "", CVAR_PRIVATE );
}

void SV_Shutdown( const char *finalmsg, killtype_t type ) {
}

void SV_Frame( int msec ) {
}

void SV_ProcessEvents( void ) {
}

