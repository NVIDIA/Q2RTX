Title       : 3rd-Zigock Bot II 0.97 For Quake2 3.20
              with deathmatch routes 
File Name   : 3zb2098.zip
Date        : 1999/10/25
Author      : ponpoko
E-mail add. : ponpoko@axcx.com
Web Site    : http://users.jp.tri6.net/~ponpoko/3zb2/
Build time  : a lot of days


File List
---------
gamex86.dll	module
3ZBConfig.cfg	configuration file for bot infomation and server message 
3ZBMaps.lst	maplist file
3ZBDoc.txt	this file

./chdtm/*.chn	deathmatch chaining file
./chctf/*.chf	Capture The Flag chaining file

3zbII Users Guide.htm
	006.9's 3zbII guide
	
-----3zb2 quickstart file set-----Thanks 006.9
3ZBII Quickstart Guide.txt
game.cfg
pak6.pak
3zb2.bat
Run 3ZBII.pif
---------------------------------
/*----------------------------------------------------------------------------------*/
// Setup and run
//-----------------
1.Unzip file and directory under your Quake2 diretory,and run Quake2 like below

	Quake2.exe +set game 3zb2

2.Start deathmatch mode.


*** note this 1 ***
If you make bot to run around the map better in deathmatch,you must to put the
chain file(*.chn) under \Quake2\3zb2\chdtm

For example,if you want to play on map "Q2DM1",you need chain file "Q2DM1.chn".

Lot of chain file will find here

http://users.jp.tri6.net/~ponpoko/3zb2/routes.html

*** note this 2 ***
This bot is supported Quake II mission pack 1 "The Reckoning"'s weapons.
If you want to play with bots on Reckoning's map,copy the \xatrix\pak0.pak
to \3zb2.

*** note this 3 ***
If you want to play CTF(Capture The Flag) with bots,copy the \ctf\pak0.pak
to \3zb2.If pak0.pak already exist,rename pak0.pak to pakX.pak(X=any other number).

/*----------------------------------------------------------------------------------*/
// Console commands
//-----------------

-Console Value
1.modification of these values will activate when restart the game
vwep			Visible weapon on/off (default on = 1,off = 0)

maplist			set the maplist section name(default = "default")

autospawn		Autospawn on/off(default off = 0,on = 1)
			bot will spawn automatically when set the value to "1"

chedit			Chaining mode on/off (default off = 0,on = 1)

zigmode			Flag keeper mode on/off (default off = 0,on = 1)

botlist			set the botlist section name(default = "default")

ctf			CTF mode on/off (default off = 0,on = 1)

2.modification of this values will activate during the game is running

spectator		set the spectator modeon/off (default off = 0,on = 1)


-Server Command
sv	spb	$	spawn $ of bots
sv	rspb	$	spawn $ of bots(ramdom selection from list)
sv	rmb	$	remove $ of bots
sv	dsp	$	debug spawning,to make bot running from $ counted oldposition
sv	savechain	save the chaining route file


-Client Command
cmd	undo	$	 take back $ of putting pods


/*----------------------------------------------------------------------------------*/
// How To Chaining
//-----------------
1.Set the value "chedit" to 1 from console

ex.	chedit 1

2.Start the game like below

ex.	map q2dm1

3.Roam around the map

4.If use the elevator with button etc...and do you want to test for bot can 
use it certainly.

ex.If you make bot run from 20 foward positioned pod to current pod,type below

	sv dsp 20 

If bot can arrive to current pod,bot will be removed automatically.
If bot failed to trace chaining,bot will be removed automatically too.

When do you want to take back 20 of chaining,type below

	cmd undo 20

5.When chaining ended,save to chaining route file.Will be saved file under
"chdtm" directory.

ex.	sv savechain

caution!
-don't tricky moving(like rocket jump,grenade jump)
-if you died(drop to lava or slime,etc...),route will be fixed to safely route.
-if bot don't be removed automatically,execute remove command.

/*----------------------------------------------------------------------------------*/
// Map List
//-----------------
You can set up map looping pattern on "3ZBMaps.lst".

1.Write pattern section name between "[" and "]".
2.Write mapname below section. 
3.Start Quake2.
4.Set the section name to console-value "maplist"

ex.if section name is "list1"

	maplist list1
 
5.Start deathmatch mode on map one of list.

/*----------------------------------------------------------------------------------*/
// Configuration
//-----------------
You can set up some setting of 3ZB on configuration file "3ZBConfig.cfg".
This file separated some section.

-Starting message
This sections are setting of starting message for true client.

[MessDeathMatch]	deathmatch mode
[MessChainDM]		deathmatch chaining mode
[MessCTF]		ctf mode
[MessChainCTF]		ctf chaining mode

-Bot list
[BotList]
this section is default list of bot(max 64 bot).this format is

\\netname\model\skin\param0-15\team\autospawnflag

netname		bot's name
model		modelname
skin		skinname
param0		walking	flag		(0 - 1) 1-walk 0-always run
param1		aiming skill level	(0 - 9) high-hard aiming
param2		frq. of pickup items	(0 - 9)	high-mania
param3		character type		(0 - 9)	high-offensive low-defensive
param4		combat skill		(0 - 9) high-professional
param5		rocket jump flag	(0 - 1) 1-on 0-off
param6		reaction skill		(0 - 9) high-high reaction skill
param7		vertical view range	(10 - 180) degree(abs)
param8		horizontal view range	(10 - 180) degree(abs)
param9		primary weapon		(0 - 13) 0-none
param10		secondary weapon	(0 - 13) 0-none
param11		dodge(depend on combat skill)		(0 - 1) 1-On 0-Off
param12		estimate(need to activate param 13)	(0 - 1) 1-On 0-Off
param13		enemy's noise check			(0 - 1) 1-On 0-Off
param14		can't see through water			(0 - 1) 1-On 0-Off
param15		teamworking 		(0 - 9) high-high teamworking
team		team(R or B,refer only when CTF mode)
autospawnflag	autospawning flag(1 or 0)

/*--------------------------------*/
About "autospawnflag"
1.Set to "1" 
2.Start Quake2.
3.Set "1" to console-value "autospawn"

ex.	autospawn 1

4.Start deathmatch mode.
5.Bot will spawn automatically.
/*--------------------------------*/
About primary and secondary weapon
-----------
Blaster			1
Shot Gun		2 
Super Shotgun		3 
Machine Gun		4 
Chain Gun		5 
Hand Grenade		6 
Grenade Launcher	7 
Rocket Launcher		8 
Hyper Blaster		9 
Rail Gun		10
BFG10K			11
Plasma Gun		12
Ion Ripper		13
-----------
/*--------------------------------*/
About custom bot list

if you want to create custom bot list...

1.Create a new section in 3ZBConfig.cfg like below

[MyBotlist]

2.Set the section name to value "botlist" from console
or short cut's command line.

botlist mybotlist




/*----------------------------------------------------------------------------------*/
// teamplay and special mode
//-----------------
1.teamplay
These deathmatch flag will work in this version.

DF_SKINTEAMS		64
DF_MODELTEAMS 		128
DF_NO_FRIENDLY_FIRE 	256

2.special mode
If you set "1" to value "zigmode",special mode will be activate.

Zigock Flag will appear in map.Your frag still inclease(your team
mate too) during you have flag.


/*----------------------------------------------------------------------------------*/
// What is changed from 0.87
//-----------------
-advanced route-linking
-route selecting for primary weapon 
-tweaked item searching
-tweaked moving code for going up ladder  
-fixed abnormal water jumping
-bot will kill self when he fall into out of map area 
-fixed train using codes
-fixed BFG firing
-fixed some fatal errors
-changed rate 11k to 22k for rocket launcher and grenade launcher sound
-supported for skin team and model team of deathmatch flag
-added special gamemode
-supported spectator and chase cum

-updated 006.9's Users Guide.htm and quickstart files(Thanks 006.9)

//------------------------------------------------0.85>>0.87
-fixed,FOV return to 90 after rocket launcher firing.
-fixed,abnormal turbo-jumping.
-fixed,sometime, bot to be missing.
-fixed some combat action's bugs.
-fixed power armor sound.

-changed some combat action.

-activated route linking(test).

//------------------------------------------------0.84>>0.85
Sorry,I forgot.

//------------------------------------------------0.82>>0.84
-fixed bug,don't spawn bots after level change.
-fixed bug,bots can through normaly near of traps(xatrix's weapon).
-fixed bug,abnormal frame of skull and head model.
-fixed route tracing bug.

-added weapon priority.
-added dodge flag.
-added estimate flag.
-added enemy's noise checking flag.
-added make bot can't see through water surface flag.

-added primary and secondary weapon selecting AI.
-rocket jump tweaking.
-added more combat actions.
-turbo jumping support.

//------------------------------------------------0.8>>0.82
-fixed more route-tracing bugs.

-added more variation of combat action.

-added 006.9[RsF]'s 3ZB II Quickstart and Users guide(thanks!).

//------------------------------------------------0.7>>0.8
-fixed more route-tracing bugs.

-fixed aiming code(0.7's aiming was buggy aiming).

-fixed rocket jump(couldn't jump in 0.7).

-this configuration param. added
	reaction skill
	vertical view range
	horizontal view range

-combat skill activated.

-added more route files

//------------------------------------------------0.6>>0.7
-fixed too many route-tracing bugs.
-changed part of .chn format.
-added combat AI test codes(only one combat skill).
-mission pack1's two weapon(Phalanx & Ionripper) activated.

