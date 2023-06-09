#ifndef BOTHEAD
#define BOTHEAD
#include "../header/local.h"

//general func
void player_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

//bot spawn & remove
qboolean	SpawnBot(int i);
void		Bot_LevelChange();
void		Load_BotInfo();
void		Bot_SpawnCall();
void		RemoveBot();
void		SpawnBotReserving();

//weapon
void Weapon_Blaster (edict_t *ent);
void Weapon_Shotgun (edict_t *ent);
void Weapon_SuperShotgun (edict_t *ent);
void Weapon_Machinegun (edict_t *ent);
void Weapon_Chaingun (edict_t *ent);
void Weapon_HyperBlaster (edict_t *ent);
void Weapon_RocketLauncher (edict_t *ent);
void Weapon_Grenade (edict_t *ent);
void Weapon_GrenadeLauncher (edict_t *ent);
void Weapon_Railgun (edict_t *ent);
void Weapon_BFG (edict_t *ent);
void CTFWeapon_Grapple (edict_t *ent);

// RAFAEL
void Weapon_Ionripper (edict_t *ent);
void Weapon_Phalanx (edict_t *ent);
void Weapon_Trap (edict_t *ent);

// wideuse
qboolean Bot_trace (edict_t *ent,edict_t *other);
qboolean Bot_trace2 (edict_t *ent,vec3_t ttz);
float Get_yaw (vec3_t vec);		//
float Get_pitch (vec3_t vec);	//
float Get_vec_yaw (vec3_t vec,float yaw);
void ShowGun(edict_t *ent);
void SpawnItem3 (edict_t *ent, gitem_t *item);
int Bot_moveT ( edict_t *ent,float ryaw,vec3_t pos,float dist,float *bottom);
void Set_BotAnim(edict_t *ent,int anim,int frame,int end);
void plat_go_up (edict_t *ent);
int Get_KindWeapon(gitem_t	*it);
qboolean TargetJump(edict_t *ent,vec3_t tpos);
qboolean Bot_traceS (edict_t *ent,edict_t *other);
qboolean Bot_Fall(edict_t *ent,vec3_t pos,float dist);

void SelectSpawnPoint (edict_t *ent, vec3_t origin, vec3_t angles);
void ClientUserinfoChanged (edict_t *ent, char *userinfo);
void CopyToBodyQue (edict_t *ent);

//route util
qboolean TraceX (edict_t *ent,vec3_t p2);
void Move_LastRouteIndex();
void Get_RouteOrigin(int index,vec3_t pos);

//Bot Func
void ZigockJoinMenu(edict_t *ent);
qboolean ZigockStartClient(edict_t *ent);
void Cmd_AirStrike(edict_t *ent);
void BotEndServerFrame (edict_t *ent);
void SpawnItem2 (edict_t *ent, gitem_t *item);
void Get_WaterState(edict_t *ent);
void Bot_Think (edict_t *self);
void PutBotInServer (edict_t *ent);
void SpawnBotReserving2(int *red,int *blue);

//Combat AI
void Combat_Level0(edict_t *ent,int foundedenemy,int enewep,float aim,float distance,int skill);
void Combat_LevelX(edict_t *ent,int foundedenemy,int enewep,float aim,float distance,int skill);
void UsePrimaryWeapon(edict_t *ent);

//Explotion Index
void UpdateExplIndex(edict_t* ent);

//flag
qboolean ZIGDrop_Flag(edict_t *ent, gitem_t *item);

//p_view.c
void BotEndServerFrame (edict_t *ent);

//Bot AI routine
void Bots_Move_NORM (edict_t *ent);		//normal AI

//spawn
void SetBotFlag1(edict_t *ent);	//チーム1の旗
void SetBotFlag2(edict_t *ent);  //チーム2の旗
void CTFSetupNavSpawn();	//ナビの設置

//ctf
void CTFJobAssign (void);		//job assign

//VWep
// ### Hentai ### BEGIN
//void ShowGun(edict_t *ent);
// ###	Hentai ### END

//----------------------------------------------------------------

//moving speed
#define	MOVE_SPD_WALK	20
#define MOVE_SPD_RUN	32
#define MOVE_SPD_DUCK	10
#define MOVE_SPD_WATER	16
#define MOVE_SPD_JUMP	32
#define VEL_BOT_JUMP	340//341		//jump vel
//#define VEL_BOT_ROCJ	500		//roc jump
#define VEL_BOT_WJUMP	341//150		//waterjump vel
#define VEL_BOT_LADRUP	200			//ladderup vel
#define VEL_BOT_WLADRUP	200	//0			//water ladderup gain


//classes
#define CLS_NONE	0	//normal
#define CLS_ALPHA	1	//sniper	
#define CLS_BETA	2
#define CLS_GAMMA	3
#define CLS_DELTA	4
#define CLS_EPSILON	5
#define CLS_ZETA	6
#define CLS_ETA		7

// function's state P
#define	PSTATE_TOP			0
#define	PSTATE_BOTTOM		1
#define PSTATE_UP			2
#define PSTATE_DOWN			3

#define PDOOR_TOGGLE		32

// height
#define TOP_LIMIT			52
#define TOP_LIMIT_WATER		100
#define BOTTOM_LIMIT		-52
#define BOTTOM_LIMIT_WATER	-8190
#define BOTTOM_LIMITM		-300

//waterstate
#define	WAS_NONE			0
#define	WAS_FLOAT			1	
#define	WAS_IN				2

//route
//chaining pod state
#define GRS_NORMAL		0
#define GRS_ONROTATE	1
#define GRS_TELEPORT	2
#define GRS_ITEMS		3
#define GRS_ONPLAT		4
#define	GRS_ONTRAIN		5
#define GRS_ONDOOR		6
#define GRS_PUSHBUTTON	7

#define GRS_GRAPSHOT	20
#define GRS_GRAPHOOK	21
#define GRS_GRAPRELEASE	22

#define GRS_REDFLAG		-10
#define GRS_BLUEFLAG	-11

#define POD_LOCKFRAME	15	//20
#define POD_RELEFRAME	20	//25

#define MAX_SEARCH		12	//max search count/FRAMETIME
#define MAX_DOORSEARCH	10

//trace param
#define	TRP_NOT			0	//don't trace
#define TRP_NORMAL		1	//trace normal
#define	TRP_ANGLEKEEP	2	//trace and keep angle
#define TRP_MOVEKEEP	3	//angle and move vec keep but move
#define TRP_ALLKEEP		4	//don't move

// bot spawning status
#define BOT_SPAWNNOT	0
#define BOT_SPRESERVED	1
#define BOT_SPAWNED		2
#define BOT_NEXTLEVEL	3

//combat
#define AIMING_POSGAP		5
#define AIMING_ANGLEGAP_S	0.75	//shot gun
#define AIMING_ANGLEGAP_M	0.35 //machine gun	

//team play state
#define TMS_NONE		0
#define TMS_LEADER		1
#define TMS_FOLLOWER	2

//ctf state
#define CTFS_NONE		0
#define CTFS_CARRIER	1
#define CTFS_ROAMER		2
#define CTFS_OFFENCER	3
#define CTFS_DEFENDER	4
#define CTFS_SUPPORTER	5

#define FOR_FLAG1		1
#define FOR_FLAG2		2

//fire----------------------------------------------------------
#define FIRE_SLIDEMODE		0x00000001	//slide with route
#define	FIRE_PRESTAYFIRE	0x00000002	//X	pre don't move fire
#define FIRE_STAYFIRE		0x00000004	//X	don't move
#define FIRE_CHIKEN			0x00000008	//X chiken fire
#define FIRE_RUSH			0x00000010	//X	rush
#define FIRE_JUMPNRUSH		0x00000020	//
#define	FIRE_ESTIMATE		0x00000040	//X estimate 予測
#define FIRE_SCATTER		0x00000080	//scatter バラ撒き
#define	FIRE_RUNNIN			0x00000100	//run & shot(normal)
#define FIRE_JUMPROC		0x00000200	//X ジャンプふぁいあ

#define FIRE_REFUGE			0x00001000	//X	避難
#define FIRE_EXPAVOID		0x00002000	//X	爆発よけ

#define FIRE_QUADUSE		0x00004000	//X	Quad時の連射武器選択
#define FIRE_AVOIDINV		0x00008000	//X 相手がペンタの時逃げる

#define FIRE_BFG			0x00010000	//X 普通にBFGを撃つ

#define FIRE_SHIFT_R		0x00020000	//X 右スライド
#define FIRE_SHIFT_L		0x00040000	//X 左スライド

#define FIRE_SHIFT			(FIRE_SHIFT_R | FIRE_SHIFT_L)//X 右スライド

#define FIRE_REFLECT		0x00080000	// 壁に反射させる

#define FIRE_IGNORE			0x10000000	//無視して逃げる

// means of death

#define MOD_SNIPERAIL			50		//SNIPE RAIL
#define MOD_LOCMISSILE			51		//LOCKON MISSILE

#define MOD_BFG100K				52		//

#define MOD_AIRSTRIKE			70		//AIRSTRIKE
//----------------------------------------------------------------
//general status list
#define STS_IDLE		0x00000000	//normal running
#define STS_THINK		0x00000001	//stand and analise
#define STS_LADDERUP	0x00000002	//crimb the ladder
#define STS_ROCJ		0x00000004	//rocket jumping
#define STS_TURBOJ		0x00000008	//turbo jump
#define STS_WATERJ		0x00000010	//turbo jump

#define STS_SJMASK		(STS_ROCJ | STS_TURBOJ | STS_WATERJ)	//special jump mask
#define STS_SJMASKEXW	(STS_ROCJ | STS_TURBOJ )				//special jump mask ex. water


#define STS_TALKING		0x00000200	//talking
#define STS_ESC_WXPL	0x00000400	//escape from explode

#define STS_MOVE_WPOINT	0x00000800	//moving waiting point
//#define STS_W_EXPL		0x00001000	//wait for end of explode

//wait
#define STS_W_DONT		0x00001000	//don't wait door or plat
#define STS_W_DOOROPEN	0x00002000	//wait for door open or down to bottom
#define STS_W_ONPLAT	0x00004000	//wait for plat or door reach to da top
#define STS_W_ONDOORUP	0x00008000	//wait for door reach to da top
#define STS_W_ONDOORDWN	0x00010000	//wait for door reach to da bottom
#define STS_W_ONTRAIN	0x00020000	//wait for plat or door reach to da top
#define STS_W_COMETRAIN	0x00040000  //wait for train come
#define STS_W_COMEPLAT	0x00080000  //wait for plat come

#define STS_WAITS		(STS_W_DONT | STS_W_DOOROPEN | STS_W_COMEPLAT | STS_W_ONPLAT | STS_W_ONDOORUP | STS_W_ONDOORDWN | STS_W_ONTRAIN)
#define STS_WAITSMASK	(STS_W_DOOROPEN | STS_W_ONPLAT | STS_W_ONDOORUP | STS_W_COMEPLAT | STS_W_ONDOORDWN | STS_W_ONTRAIN)
#define STS_WAITSMASK2	(STS_W_ONDOORDWN |STS_W_ONDOORUP | STS_W_ONPLAT | STS_W_ONTRAIN)
#define STS_WAITSMASKCOM (STS_W_DOOROPEN | STS_W_ONPLAT | STS_W_ONDOORUP | STS_W_ONDOORDWN | STS_W_ONTRAIN)
//----------------------------------------------------------------
//general status list
#define CTS_ENEM_NSEE	0x00000001	//have enemy but can't see
#define CTS_AGRBATTLE	0x00000002	//aglessive battle
#define	CTS_ESCBATTLE	0x00000004	//escaping battle(item want)
#define CTS_HIPBATTLE	0x00000008	//high position battle(camp)


//shoot
#define CTS_PREAIMING	0x00000010	//prepare for snipe or lockon
#define CTS_AIMING		0x00000020	//aimning for snipe or lockon
#define CTS_GRENADE		0x00000040  //hand grenade mode
#define CTS_JUMPSHOT	0x00000080	//jump shot

#define CTS_COMBS (CTS_AGRBATTLE | CTS_ESCBATTLE | CTS_HIPBATTLE | CTS_ENEM_NSEE)

//----------------------------------------------------------------
//route struct
#define MAXNODES			10000	//5000 added 5000 pods
#define MAXLINKPOD			6		//don't modify this
#define CTF_FLAG1_FLAG		0x0000
#define CTF_FLAG2_FLAG		0x8000

typedef struct
{
	vec3_t	Pt;		//target point
	union
	{
		vec3_t			Tcourner;				//target courner(train and grap-shot only)
		unsigned short	linkpod[MAXLINKPOD];	//(GRS_NORMAL,GRS_ITEMS only 0 = do not select pod)
	};
	edict_t	*ent;	//target ent
	short	index;	//index num
	short	state;	//targetstate
} route_t;

//----------------------------------------------------------------
//bot info struct
#define MAXBOTS		64
#define MAXBOP		16

// bot params
#define BOP_WALK		0	//flags
#define BOP_AIM			1	//aiming
#define BOP_PICKUP		2	//frq PICKUP
#define BOP_OFFENCE		3	//chiken fire etc.
#define BOP_COMBATSKILL	4	//combat skill
#define BOP_ROCJ		5	//rocket jump
#define BOP_REACTION	6	//reaction skill exp. frq SEARCH ENEMY
#define BOP_VRANGE		7	//V-View of RANGE	縦
#define BOP_HRANGE		8	//H-View of Range	横
#define BOP_PRIWEP		9	//primary weapon
#define BOP_SECWEP		10	//secondary weapon
#define BOP_DODGE		11	//dodge
#define BOP_ESTIMATE	12	//estimate
#define BOP_NOISECHK	13	//noisecheck
#define BOP_NOSTHRWATER	14	//can't see through water
#define BOP_TEAMWORK	15	//teamwork

typedef	struct
{
	char	netname[21];		//netname
	char	model[21];			//model
	char	skin[21];			//skin
	int		spflg;				//spawned flag 0-not 1-waiting 2-spawned
	int		team;				//team NO. 0-noteam 1-RED 2-BLUE
	int		arena;				//if arena is on
	unsigned char	param[MAXBOP];		//Params
}	botinfo_t;

//----------------------------------------------------------------
//message section name
#define MESS_DEATHMATCH		"[MessDeathMatch]"
#define MESS_CHAIN_DM		"[MessChainDM]"
#define	MESS_CTF			"[MessCTF]"
#define MESS_CHAIN_CTF		"[MessChainCTF]"

//----------------------------------------------------------------
//bot list section name
#define BOTLIST_SECTION_DM	"[BotList]"
#define BOTLIST_SECTION_TM	"[BotListTM]"

//----------------------------------------------------------------
#define MAX_BOTSKILL		10

#define FALLCHK_LOOPMAX	30

//laser Index
#define MAX_LASERINDEX		30
extern edict_t*		LaserIndex[MAX_LASERINDEX];

//Explotion Index
#define MAX_EXPLINDEX		12
extern edict_t*		ExplIndex[MAX_EXPLINDEX];
//


extern	int			cumsindex;
extern	int			targetindex;		//debugtarget

extern	int			ListedBotCount;		//bot count of list

extern	int			SpawnWaitingBots;
extern	char		ClientMessage[MAX_STRING_CHARS];
extern	botinfo_t	Bot[MAXBOTS];
extern	route_t		Route[MAXNODES];
extern	int			CurrentIndex;
extern	float		JumpMax;
extern	int			botskill;
extern	int			trace_priority;
extern	int			FFlg[MAX_BOTSKILL];

extern	int			ListedBots;

//for avoid abnormal frame error
extern	int			skullindex;
extern	int			headindex;

extern	gitem_t		*zflag_item;
extern	edict_t		*zflag_ent;
extern	int			zigflag_spawn;

//item index
extern	int			mpindex[MPI_INDEX];

//PON-CTF
extern	edict_t		*bot_team_flag1;
extern	edict_t		*bot_team_flag2;
//PON-CTF

//pre searched items
extern	gitem_t	*Fdi_GRAPPLE;
extern	gitem_t	*Fdi_BLASTER;
extern	gitem_t *Fdi_SHOTGUN;
extern	gitem_t *Fdi_SUPERSHOTGUN;
extern	gitem_t *Fdi_MACHINEGUN;
extern	gitem_t *Fdi_CHAINGUN;
extern	gitem_t *Fdi_GRENADES;
extern	gitem_t *Fdi_GRENADELAUNCHER;
extern	gitem_t *Fdi_ROCKETLAUNCHER;
extern	gitem_t *Fdi_HYPERBLASTER;
extern	gitem_t *Fdi_RAILGUN;
extern	gitem_t *Fdi_BFG;
extern	gitem_t *Fdi_PHALANX;
extern	gitem_t *Fdi_BOOMER;
extern	gitem_t *Fdi_TRAP;

extern	gitem_t *Fdi_SHELLS;
extern	gitem_t *Fdi_BULLETS;
extern	gitem_t *Fdi_CELLS;
extern	gitem_t *Fdi_ROCKETS;
extern	gitem_t *Fdi_SLUGS;
extern	gitem_t *Fdi_MAGSLUGS;

extern	float	ctfjob_update;
#endif
