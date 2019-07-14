//-----------------------------------------------------------------------------
// g_svcmds.c
//
// $Id: g_svcmds.c,v 1.15 2003/12/09 20:53:35 igor_rock Exp $
//
//-----------------------------------------------------------------------------
// $Log: g_svcmds.c,v $
// Revision 1.15  2003/12/09 20:53:35  igor_rock
// added player console info if stuffcmd used (to avoid admin cheating)
//
// Revision 1.14  2003/06/15 21:43:53  igor
// added IRC client
//
// Revision 1.13  2002/03/28 15:32:47  ra
// No softmapping during LCA
//
// Revision 1.12  2002/01/22 16:55:49  deathwatch
// fixed a bug with rrot which would make it override sv softmap (moved the dosoft check up in g_main.c
// fixed a bug with rrot which would let it go to the same map (added an if near the end of the rrot statement in the EndDMLevel function)
//
// Revision 1.11  2001/11/04 15:15:19  ra
// New server commands: "sv softmap" and "sv map_restart".  sv softmap
// takes one map as argument and starts is softly without restarting
// the server.   map_restart softly restarts the current map.
//
// Revision 1.10  2001/09/28 13:48:34  ra
// I ran indent over the sources. All .c and .h files reindented.
//
// Revision 1.9  2001/06/25 11:44:47  slicerdw
// New Video Check System - video_check and video_check_lockpvs no longer latched
//
// Revision 1.8  2001/06/21 00:05:30  slicerdw
// New Video Check System done -  might need some revision but works..
//
// Revision 1.5  2001/05/31 16:58:14  igor_rock
// conflicts resolved
//
// Revision 1.4.2.2  2001/05/25 18:59:52  igor_rock
// Added CTF Mode completly :)
// Support for .flg files is still missing, but with "real" CTF maps like
// tq2gtd1 the ctf works fine.
// (I hope that all other modes still work, just tested DM and teamplay)
//
// Revision 1.4.2.1  2001/05/20 15:17:31  igor_rock
// removed the old ctf code completly
//
// Revision 1.4  2001/05/11 16:07:25  mort
// Various CTF bits and pieces...
//
// Revision 1.3  2001/05/07 22:03:15  slicerdw
// Added sv stuffcmd
//
// Revision 1.2  2001/05/07 21:18:35  slicerdw
// Added Video Checking System
//
// Revision 1.1.1.1  2001/05/06 17:31:45  igor_rock
// This is the PG Bund Edition V1.25 with all stuff laying around here...
//
//-----------------------------------------------------------------------------

#include "g_local.h"

extern int dosoft;
extern int softquit;

void SVCmd_ReloadMOTD_f ()
{
	ReadMOTDFile ();
	gi.cprintf (NULL, PRINT_HIGH, "MOTD reloaded.\n");
}

/*
==============================================================================

PACKET FILTERING
 

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and any unspecified digits will match any value, so you can specify an entire class C network with "addip 192.246.40".

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

writeip
Dumps "addip <ip>" commands to listip.cfg so it can be execed at a later date.  The filter lists are not saved and restored by default, because I beleive it would cause too much confusion.

filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.


==============================================================================
*/

typedef struct
{
  unsigned mask;
  unsigned compare;

//AZEROV
  int temp_ban_games;
//AZEROV
}
ipfilter_t;

#define       MAX_IPFILTERS   1024

ipfilter_t ipfilters[MAX_IPFILTERS];
int numipfilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter (char *s, ipfilter_t * f, int temp_ban_games)
{
	char num[128];
	int i, j;
	byte b[4] = {0,0,0,0};
	byte m[4] = {0,0,0,0};

	for (i = 0; i < 4; i++)
	{
		if (*s < '0' || *s > '9')
		{
			gi.cprintf (NULL, PRINT_HIGH, "Bad filter address: %s\n", s);
			return false;
		}

		j = 0;
		while (*s >= '0' && *s <= '9')
		{
			num[j++] = *s++;
		}
		num[j] = 0;
		b[i] = atoi (num);
		if (b[i] != 0)
			m[i] = 255;

		if (!*s)
			break;
		s++;
	}

	f->mask = *(unsigned *) m;
	f->compare = *(unsigned *) b;

	f->temp_ban_games = temp_ban_games;

	return true;
}

/*
=================
SV_FilterPacket
=================
*/
qboolean SV_FilterPacket (char *from, int *temp)
{
	int i = 0;
	unsigned in;
	byte m[4] = {0,0,0,0};
	char *p;

	p = from;
	while (*p && i < 4)
	{
		while (*p >= '0' && *p <= '9')
		{
			m[i] = m[i] * 10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++, p++;
	}

	in = *(unsigned *) m;

	for (i = 0; i < numipfilters; i++) {
		if ((in & ipfilters[i].mask) == ipfilters[i].compare) {
			*temp = ipfilters[i].temp_ban_games;
			return (int)filterban->value;
		}
	}

	return (int)!filterban->value;
}


/*
=================
SV_AddIP_f
=================
*/
void SVCmd_AddIP_f (void)
{
	int i;

	if (gi.argc () < 3)
	{
		gi.cprintf (NULL, PRINT_HIGH, "Usage:  addip <ip-mask>\n");
		return;
	}

	for (i = 0; i < numipfilters; i++) {
		if (ipfilters[i].compare == 0xffffffff)
			break;			// free spot
	}

	if (i == numipfilters)
	{
		if (numipfilters == MAX_IPFILTERS)
		{
			gi.cprintf (NULL, PRINT_HIGH, "IP filter list is full\n");
			return;
		}
		numipfilters++;
	}

	if (!StringToFilter (gi.argv (2), &ipfilters[i], 0))
		ipfilters[i].compare = 0xffffffff;
}

/*
=================
SV_RemoveIP_f
=================
*/
void SVCmd_RemoveIP_f (void)
{
	ipfilter_t f;
	int i, j;

	if (gi.argc () < 3)
	{
		gi.cprintf (NULL, PRINT_HIGH, "Usage:  sv removeip <ip-mask>\n");
		return;
	}

	if (!StringToFilter (gi.argv (2), &f, 0))
		return;

	for (i = 0; i < numipfilters; i++)
	{
		if (ipfilters[i].mask == f.mask && ipfilters[i].compare == f.compare)
		{
			for (j = i + 1; j < numipfilters; j++)
				ipfilters[j - 1] = ipfilters[j];

			numipfilters--;
			gi.cprintf (NULL, PRINT_HIGH, "Removed.\n");
			return;
		}
	}
	gi.cprintf (NULL, PRINT_HIGH, "Didn't find %s.\n", gi.argv (2));
}

/*
=================
SV_ListIP_f
=================
*/
void SVCmd_ListIP_f (void)
{
	int i;
	byte b[4];

	gi.cprintf (NULL, PRINT_HIGH, "Filter list:\n");
	for (i = 0; i < numipfilters; i++)
	{
		*(unsigned *) b = ipfilters[i].compare;
		if (!ipfilters[i].temp_ban_games)
		{
			gi.cprintf (NULL, PRINT_HIGH, "%3i.%3i.%3i.%3i\n", b[0], b[1], b[2], b[3]);
		}
		else
		{
			gi.cprintf (NULL, PRINT_HIGH, "%3i.%3i.%3i.%3i (%d more game(s))\n",
				b[0], b[1], b[2], b[3], ipfilters[i].temp_ban_games);
		}
	}
}

/*
=================
SV_WriteIP_f
=================
*/
void SVCmd_WriteIP_f (void)
{
	FILE *f;
	char name[MAX_OSPATH];
	byte b[4];
	int i;
	cvar_t *game;

	game = gi.cvar ("game", "", 0);

	if (!*game->string)
		sprintf (name, "%s/listip.cfg", GAMEVERSION);
	else
		sprintf (name, "%s/listip.cfg", game->string);

	gi.cprintf (NULL, PRINT_HIGH, "Writing %s.\n", name);

	f = fopen (name, "wb");
	if (!f)
	{
		gi.cprintf (NULL, PRINT_HIGH, "Couldn't open %s\n", name);
		return;
	}

	fprintf (f, "set filterban %d\n", (int) filterban->value);

	for (i = 0; i < numipfilters; i++)
	{
		if (!ipfilters[i].temp_ban_games)
		{
			*(unsigned *) b = ipfilters[i].compare;
			fprintf (f, "sv addip %i.%i.%i.%i\n", b[0], b[1], b[2], b[3]);
		}
	}

	fclose (f);
}

/*
=================
SV_Nextmap_f
=================
*/
void SVCmd_Nextmap_f (char *arg)
{
	// end level and go to next map in map rotation
	gi.bprintf (PRINT_HIGH, "Changing to next map in rotation.\n");
	EndDMLevel ();
	if (arg != NULL && Q_stricmp (arg, "force") == 0)
		ExitLevel ();
	return;
}

//Black Cross - End

/*
=================
STUFF COMMAND

This will stuff a certain command to the client.
=================
*/
void SVCmd_stuffcmd_f ()
{
	int i, u, team = -1, stuffAll = 0;
	char text[256];
	char user[64], tmp[64];
	edict_t *ent;

	if (gi.argc() < 4) {
		gi.cprintf (NULL, PRINT_HIGH, "Usage: stuffcmd <user id> <text>\n");
		return;
	}

	i = gi.argc ();
	Q_strncpyz (user, gi.argv (2), sizeof (user));
	text[0] = 0;

	for (u = 3; u <= i; u++)
	{
		Q_strncpyz (tmp, gi.argv (u), sizeof (tmp));
		if (tmp[0] == '!')	// Checks for "!" and replaces for "$" to see the user info
			tmp[0] = '$';

		if(text[0])
			Q_strncatz (text, " ", sizeof(text)-1);

		Q_strncatz (text, tmp, sizeof(text)-1);
	}
	Q_strncatz (text, "\n", sizeof(text));

	if (!Q_stricmp(user, "all")) {
		stuffAll = 1;
	} else if (!Q_strnicmp(user, "team", 4)) {
		team = TP_GetTeamFromArg(user+4);
	}

	if(team > -1 && !teamplay->value) {
		gi.cprintf(NULL, PRINT_HIGH, "Not in Teamplay mode\n");
		return;
	}

	if (stuffAll || team > -1)
	{
		for (i = 1; i <= game.maxclients; i++)
		{
			ent = getEnt (i);
			if(!ent->inuse)
				continue;

			if (stuffAll || ent->client->resp.team == team) {
				gi.cprintf(ent, PRINT_HIGH, "Console stuffed: %s", text);
				stuffcmd(ent, text);
			}
		}
		return;
	}

	u = strlen(user);
	for (i = 0; i < u; i++)
	{
		if (!isdigit(user[i]))
		{
			gi.cprintf (NULL, PRINT_HIGH, "Usage: stuffcmd <user id> <text>\n");
			return;
		}
	}

	i = atoi(user) + 1;
	if (i > game.maxclients)
	{				/* if is inserted number > server capacity */
		gi.cprintf (NULL, PRINT_HIGH, "User id is not valid\n");
		return;
	}

	ent = getEnt(i);
	if (ent->inuse) { /* if is inserted a user that exists in the server */
		gi.cprintf(ent, PRINT_HIGH, "Console stuffed: %s", text);
		stuffcmd (ent, text);
	}
	else
		gi.cprintf (NULL, PRINT_HIGH, "User id is not valid\n");
}

/*
=================
SV_Softmap_f
=================
*/
void SVCmd_Softmap_f (void)
{
	if (gi.argc() < 3) {
		gi.cprintf(NULL, PRINT_HIGH, "Usage:  sv softmap <map>\n");
		return;
	}
	if (lights_camera_action) {
		gi.cprintf(NULL, PRINT_HIGH, "Please dont use sv softmap during LCA\n");
		return;
	}

	Q_strncpyz(level.nextmap, gi.argv(2), sizeof(level.nextmap));
	gi.bprintf(PRINT_HIGH, "Console is setting map: %s\n", level.nextmap);
	dosoft = 1;
	EndDMLevel();
	return;
}

/*
=================
SV_Map_restart_f
=================
*/
void SVCmd_Map_restart_f (void)
{
	gi.bprintf(PRINT_HIGH, "Console is restarting map\n");
	dosoft = 1;
	strcpy(level.nextmap, level.mapname);
	EndDMLevel();
	return;
}

void SVCmd_ResetScores_f (void)
{
	ResetScores(true);
	gi.bprintf(PRINT_HIGH, "Scores and time were reset by console.\n");
}

void SVCmd_SetTeamScore_f( int team )
{
	if( ! teamplay->value )
	{
		gi.cprintf( NULL, PRINT_HIGH, "Scores can only be set for teamplay.\n" );
		return;
	}

	if( gi.argc() < 3 )
	{
		gi.cprintf( NULL, PRINT_HIGH, "Usage: sv %s <score>\n", gi.argv(1) );
		return;
	}

	teams[team].score = atoi(gi.argv(2));
	gi.cvar_forceset( teams[team].teamscore->name, va( "%i", teams[team].score ) );

	gi.bprintf( PRINT_HIGH, "Team %i score set to %i by console.\n", team, teams[team].score );
}

void SVCmd_SoftQuit_f (void)
{
	gi.bprintf(PRINT_HIGH, "The server will exit after this map\n");
	softquit = 1;
}

void SVCmd_Slap_f (void)
{
	if( gi.argc() < 3 )
	{
		gi.cprintf( NULL, PRINT_HIGH, "Usage: sv slap <name/id> [<damage>] [<power>]\n" );
		return;
	}
	if( lights_camera_action )
	{
		gi.cprintf( NULL, PRINT_HIGH, "Can't slap yet!\n" );
		return;
	}

	const char *name = gi.argv(2);
	size_t name_len = strlen(name);
	int damage = atoi(gi.argv(3));
	float power = (gi.argc() >= 5) ? atof(gi.argv(4)) : 100.f;
	vec3_t slap_dir = {0.f,0.f,1.f}, slap_normal = {0.f,0.f,-1.f};
	qboolean found_victim = false;
	size_t i = 0;
	int user_id = name_len ? (atoi(name) + 1) : 0;

	// See if we're slapping by user ID.
	for( i = 0; i < name_len; i ++ )
	{
		if( ! isdigit(name[i]) )
			user_id = 0;
	}

	for( i = 0; i < maxclients->value ; i ++ )
	{
		edict_t *ent = g_edicts + i + 1;
		if( ent->inuse && ( (user_id == i + 1) || ((! user_id) && (strncasecmp( ent->client->pers.netname, name, name_len ) == 0)) ) )
		{
			found_victim = true;
			if( IS_ALIVE(ent) )
			{
				slap_dir[ 0 ] = crandom() * 0.5f;
				slap_dir[ 1 ] = crandom() * 0.5f;
				T_Damage( ent, world, world, slap_dir, ent->s.origin, slap_normal, damage, power, 0, MOD_KICK );
				gi.sound( ent, CHAN_WEAPON, gi.soundindex("weapons/kick.wav"), 1, ATTN_NORM, 0 );
				gi.bprintf( PRINT_HIGH, "Admin slapped %s for %i damage.\n", ent->client->pers.netname, damage );
			}
			else
				gi.cprintf( NULL, PRINT_HIGH, "%s is already dead.\n", ent->client->pers.netname );
		}
	}

	if( ! found_victim )
		gi.cprintf( NULL, PRINT_HIGH, "Couldn't find %s to slap.\n", name );
}

qboolean ScrambleTeams(void);

void SVCmd_Scramble_f(void)
{
	if (!teamplay->value) {
		gi.cprintf(NULL, PRINT_HIGH, "Scramble is for teamplay only\n");
		return;
	}

	if (!ScrambleTeams()) {
		gi.cprintf(NULL, PRINT_HIGH, "Need more players to scramble\n");
	}
}

/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue gi.argc() / gi.argv() commands to get the rest
of the parameters
=================
*/
void ServerCommand (void)
{
	char *cmd;

	cmd = gi.argv (1);

	if (Q_stricmp (cmd, "addip") == 0)
		SVCmd_AddIP_f ();
	else if (Q_stricmp (cmd, "removeip") == 0)
		SVCmd_RemoveIP_f ();
	else if (Q_stricmp (cmd, "listip") == 0)
		SVCmd_ListIP_f ();
	else if (Q_stricmp (cmd, "writeip") == 0)
		SVCmd_WriteIP_f ();
	else if (Q_stricmp (cmd, "nextmap") == 0)
		SVCmd_Nextmap_f (gi.argv (2));	// Added by Black Cross
	else if (Q_stricmp (cmd, "reloadmotd") == 0)
		SVCmd_ReloadMOTD_f ();
	//AQ2:TNG - Slicer : CheckCheats & StuffCmd
	else if (Q_stricmp (cmd, "stuffcmd") == 0)
		SVCmd_stuffcmd_f ();
	else if (Q_stricmp (cmd, "ircraw") == 0)
		SVCmd_ircraw_f ();
	else if (Q_stricmp (cmd, "softmap") == 0)
		SVCmd_Softmap_f ();
	else if (Q_stricmp (cmd, "map_restart") == 0)
		SVCmd_Map_restart_f ();
	else if (Q_stricmp (cmd, "resetscores") == 0)
		SVCmd_ResetScores_f ();
	else if (Q_stricmp (cmd, "t1score") == 0)
		SVCmd_SetTeamScore_f( 1 );
	else if (Q_stricmp (cmd, "t2score") == 0)
		SVCmd_SetTeamScore_f( 2 );
	else if (Q_stricmp (cmd, "t3score") == 0)
		SVCmd_SetTeamScore_f( 3 );
	else if (Q_stricmp (cmd, "softquit") == 0)
		SVCmd_SoftQuit_f ();
	else if (Q_stricmp (cmd, "slap") == 0)
		SVCmd_Slap_f ();
	else if (Q_stricmp(cmd, "scramble") == 0)
		SVCmd_Scramble_f();
	else
		gi.cprintf (NULL, PRINT_HIGH, "Unknown server command \"%s\"\n", cmd);
}


/*
==========================
Kick a client entity
==========================
*/
void Kick_Client (edict_t * ent)
{
	if (!ent || !ent->client || !ent->client->pers.connected)
		return;

	// We used to kick on names, but people got crafty and figured
	// out that putting in a space after their name let them get
	// around the stupid 'kick' function. So now we kick by number.
	gi.AddCommandString(va("kick %d\n", ent->client - game.clients));
}

/*
==========================
Ban a client for N rounds
==========================
*/
qboolean Ban_TeamKiller (edict_t * ent, int rounds)
{
	int i = 0;

	if (!ent || !ent->client || !ent->client->pers.ip[0])
	{
		gi.cprintf(NULL, PRINT_HIGH, "Unable to determine client ip address for edict\n");
		return false;
	}

	for (i = 0; i < numipfilters; i++)
	{
		if (ipfilters[i].compare == 0xffffffff)
		break;			// free spot
	}

	if (i == numipfilters)
	{
		if (numipfilters == MAX_IPFILTERS)
		{
			gi.cprintf(NULL, PRINT_HIGH, "IP filter list is full\n");
			return false;
		}
		numipfilters++;
	}
	if (!StringToFilter(ent->client->pers.ip, &ipfilters[i], rounds))
	{
		ipfilters[i].compare = 0xffffffff;
		return false;
	}

	return true;
}

void UnBan_TeamKillers (void)
{
  // We don't directly unban them all - we subtract 1 from temp_ban_games,
  // and unban them if it's 0.

	int i, j;

	for (i = 0; i < numipfilters; i++)
	{
		if (ipfilters[i].temp_ban_games > 0)
		{
			if (!--ipfilters[i].temp_ban_games)
			{
				// re-pack the filters
				for (j = i + 1; j < numipfilters; j++)
					ipfilters[j - 1] = ipfilters[j];
				numipfilters--;
				gi.cprintf (NULL, PRINT_HIGH, "Unbanned teamkiller/vote kickked.\n");

				// since we removed the current we have to re-process the new current
				i--;
			}
		}
	}
}

//AZEROV
