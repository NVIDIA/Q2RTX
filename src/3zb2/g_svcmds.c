
#include "header/local.h"
#include "header/bot.h"



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
	unsigned	mask;
	unsigned	compare;
} ipfilter_t;

#define	MAX_IPFILTERS	1024

ipfilter_t	ipfilters[MAX_IPFILTERS];
int			numipfilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter (char *s, ipfilter_t *f)
{
	char	num[128];
	int		i, j;
	byte	b[4];
	byte	m[4];
	
	for (i=0 ; i<4 ; i++)
	{
		b[i] = 0;
		m[i] = 0;
	}
	
	for (i=0 ; i<4 ; i++)
	{
		if (*s < '0' || *s > '9')
		{
			gi.cprintf(NULL, PRINT_HIGH, "Bad filter address: %s\n", s);
			return false;
		}
		
		j = 0;
		while (*s >= '0' && *s <= '9')
		{
			num[j++] = *s++;
		}
		num[j] = 0;
		b[i] = atoi(num);
		if (b[i] != 0)
			m[i] = 255;

		if (!*s)
			break;
		s++;
	}
	
	f->mask = *(unsigned *)m;
	f->compare = *(unsigned *)b;
	
	return true;
}

/*
=================
SV_FilterPacket
=================
*/
qboolean SV_FilterPacket (char *from)
{
	int		i;
	unsigned	in;
	byte m[4];
	char *p;

	i = 0;
	p = from;
	while (*p && i < 4) {
		m[i] = 0;
		while (*p >= '0' && *p <= '9') {
			m[i] = m[i]*10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++, p++;
	}
	
	in = *(unsigned *)m;

	for (i=0 ; i<numipfilters ; i++)
		if ( (in & ipfilters[i].mask) == ipfilters[i].compare)
			return (int)filterban->value;

	return (int)!filterban->value;
}


/*
=================
SV_AddIP_f
=================
*/
void SVCmd_AddIP_f (void)
{
	int		i;
	
	if (gi.argc() < 3) {
		gi.cprintf(NULL, PRINT_HIGH, "Usage:  addip <ip-mask>\n");
		return;
	}

	for (i=0 ; i<numipfilters ; i++)
		if (ipfilters[i].compare == 0xffffffff)
			break;		// free spot
	if (i == numipfilters)
	{
		if (numipfilters == MAX_IPFILTERS)
		{
			gi.cprintf (NULL, PRINT_HIGH, "IP filter list is full\n");
			return;
		}
		numipfilters++;
	}
	
	if (!StringToFilter (gi.argv(2), &ipfilters[i]))
		ipfilters[i].compare = 0xffffffff;
}

/*
=================
SV_RemoveIP_f
=================
*/
void SVCmd_RemoveIP_f (void)
{
	ipfilter_t	f;
	int			i, j;

	if (gi.argc() < 3) {
		gi.cprintf(NULL, PRINT_HIGH, "Usage:  sv removeip <ip-mask>\n");
		return;
	}

	if (!StringToFilter (gi.argv(2), &f))
		return;

	for (i=0 ; i<numipfilters ; i++)
		if (ipfilters[i].mask == f.mask
		&& ipfilters[i].compare == f.compare)
		{
			for (j=i+1 ; j<numipfilters ; j++)
				ipfilters[j-1] = ipfilters[j];
			numipfilters--;
			gi.cprintf (NULL, PRINT_HIGH, "Removed.\n");
			return;
		}
	gi.cprintf (NULL, PRINT_HIGH, "Didn't find %s.\n", gi.argv(2));
}

/*
=================
SV_ListIP_f
=================
*/
void SVCmd_ListIP_f (void)
{
	int		i;
	byte	b[4];

	gi.cprintf (NULL, PRINT_HIGH, "Filter list:\n");
	for (i=0 ; i<numipfilters ; i++)
	{
		*(unsigned *)b = ipfilters[i].compare;
		gi.cprintf (NULL, PRINT_HIGH, "%3i.%3i.%3i.%3i\n", b[0], b[1], b[2], b[3]);
	}
}

/*
=================
SV_WriteIP_f
=================
*/
void SVCmd_WriteIP_f (void)
{
	FILE	*f;
	char	name[MAX_OSPATH];
	byte	b[4];
	int		i;
	cvar_t	*game;

	game = gi.cvar("game", "", 0);

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
	
	fprintf(f, "set filterban %d\n", (int)filterban->value);

	for (i=0 ; i<numipfilters ; i++)
	{
		*(unsigned *)b = ipfilters[i].compare;
		fprintf (f, "sv addip %i.%i.%i.%i\n", b[0], b[1], b[2], b[3]);
	}
	
	fclose (f);
}





//ルート修正
//ノーマルポッドは全て切り捨て
void Move_LastRouteIndex()
{
	int	i;

	for(i = CurrentIndex - 1 ; i >= 0;i--)
	{
		if(Route[i].state) break;
		else if(!Route[i].index) break;
	}
	if(!CurrentIndex || !Route[i].index) CurrentIndex = i;
	else CurrentIndex = i + 1;

	if(CurrentIndex < MAXNODES)
	{
		memset(&Route[CurrentIndex],0,sizeof(route_t));
		if(CurrentIndex > 0) Route[CurrentIndex].index = Route[CurrentIndex - 1].index + 1; 
	}
}

//分岐付きに変換処理
void	RouteTreepointSet()
{
	int	i;

	for(i = 0;i < CurrentIndex;i++)
	{
		if(Route[i].state == GRS_NORMAL)
		{
			

		}
	}
}




void	Svcmd_Test_f (void)
{
	gi.cprintf (NULL, PRINT_HIGH, "Svcmd_Test_f()\n");
}

//chainファイルのセーブ
void SaveChain()
{
	char name[256];
	FILE *fpout;
	unsigned int size;

	if(!chedit->value)
	{
		gi.cprintf (NULL, PRINT_HIGH, "Not a chaining mode.\n");
		return;
	}

	//とりあえずCTFだめ
	if(ctf->value) 	sprintf(name,".\\%s\\chctf\\%s.chf",gamepath->string,level.mapname);
	else 	sprintf(name,".\\%s\\chdtm\\%s.chn",gamepath->string,level.mapname);

	fpout = fopen(name,"wb");
	if(fpout == NULL) gi.cprintf(NULL,PRINT_HIGH,"Can't open %s\n",name);
	else
	{
		if(!ctf->value)	fwrite("3ZBRGDTM",sizeof(char),8,fpout);
		else fwrite("3ZBRGCTF",sizeof(char),8,fpout);

		fwrite(&CurrentIndex,sizeof(int),1,fpout);

		size = (unsigned int)CurrentIndex * sizeof(route_t);

		fwrite(Route,size,1,fpout);

		gi.cprintf (NULL, PRINT_HIGH,"%s Saving done.\n",name);
		fclose(fpout);
	}
}
//Spawn Command
void SpawnCommand(int i)
{
	int	j;

	if(chedit->value){ gi.cprintf(NULL,PRINT_HIGH,"Can't spawn.");return;}

	if(i <= 0) {gi.cprintf(NULL,PRINT_HIGH,"Specify num of bots.");return;}

	for(j = 0;j < i;j++)
	{
		SpawnBotReserving();
	}
}

//Random Spawn Command

void RandomSpawnCommand(int i)
{
	int	j,k,red = 0,blue = 0;

	edict_t	*e;

	if(chedit->value){ gi.cprintf(NULL,PRINT_HIGH,"Can't spawn.");return;}

	if(i <= 0) {gi.cprintf(NULL,PRINT_HIGH,"Specify num of bots.");return;}

	//count current teams
	for ( k = 1 ; k <= maxclients->value ; k++)
	{
		e = &g_edicts[k];
		if(e->inuse && e->client)
		{
			if(e->client->resp.ctf_team == CTF_TEAM1) red++;
			else if(e->client->resp.ctf_team == CTF_TEAM2) blue++;
		}
	}

	for(j = 0;j < i;j++)
	{
		SpawnBotReserving2(&red,&blue);
//gi.cprintf(NULL,PRINT_HIGH,"R B %i %i\n",red,blue);
	}
}

//Remove Command
void RemoveCommand(int i)
{
	int	j;

	if(i <= 0) i = 1;//gi.cprintf(NULL,PRINT_HIGH,"Specify num of bots.");


	for(j = 0;j < i;j++)
	{
		RemoveBot();
	}
}

//Debug Spawn Command
void DebugSpawnCommand(int i)
{
	if(!chedit->value) {gi.cprintf(NULL,PRINT_HIGH,"Can't debug.");return;}

	if(targetindex) {gi.cprintf(NULL,PRINT_HIGH,"Now debugging.");return;}

	if(i < 1) i = 1;

	targetindex = i;

	SpawnBotReserving();
}


/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue gi.argc() / gi.argv() commands to get the rest
of the parameters
=================
*/
void	ServerCommand (void)
{
	char	*cmd;

	cmd = gi.argv(1);
	if (Q_stricmp (cmd, "test") == 0)
		Svcmd_Test_f ();
	else if (Q_stricmp (cmd, "savechain") == 0)
		SaveChain ();
	else if (Q_stricmp (cmd, "spb") == 0)
	{
		if(gi.argc() <= 1) SpawnCommand(1);
		else SpawnCommand (atoi(gi.argv(2)));
	}
	else if (Q_stricmp (cmd, "rspb") == 0)
	{
		if(gi.argc() <= 1) RandomSpawnCommand(1);
		else RandomSpawnCommand (atoi(gi.argv(2)));
	}
	else if (Q_stricmp (cmd, "rmb") == 0)
	{
		if(gi.argc() <= 1) RemoveCommand(1);
		else RemoveCommand (atoi(gi.argv(2)));
	}
	else if (Q_stricmp (cmd, "dsp") == 0)
	{
		if(gi.argc() <= 1) DebugSpawnCommand(1);
		else DebugSpawnCommand (atoi(gi.argv(2)));
	}
	else if (Q_stricmp (cmd, "addip") == 0)
		SVCmd_AddIP_f ();
	else if (Q_stricmp (cmd, "removeip") == 0)
		SVCmd_RemoveIP_f ();
	else if (Q_stricmp (cmd, "listip") == 0)
		SVCmd_ListIP_f ();
	else if (Q_stricmp (cmd, "writeip") == 0)
		SVCmd_WriteIP_f ();
	else
		gi.cprintf (NULL, PRINT_HIGH, "Unknown server command \"%s\"\n", cmd);
}

