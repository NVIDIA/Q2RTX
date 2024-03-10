#ifndef BOTSTRUCT
#define BOTSTRUCT

//Zigock client info
#define ALEAT_MAX	10

typedef struct zgcl_s
{
	int			zclass;			//class no.

	int			botindex;		//botlist's index NO.

// true client用 zoom フラグ	
	int			aiming;			//0-not 1-aiming  2-firing zoomingflag
	float		distance;		//zoom中のFOV値
	float		olddistance;	//旧zooming FOV値
	qboolean	autozoom;		//autozoom
	qboolean	lockon;			//lockon flag false-not true-locking

// bot用	
	int			zcstate;		//status
	int			zccmbstt;		//combat status

	//duck
	float		n_duckedtime;	//non ducked time

	//targets
	edict_t		*first_target;	//enemy		uses LockOntarget(for client)
	float		targetlock;		//target locking time
	short		firstinterval;	//enemy search count
	edict_t		*second_target;	//kindof items
	short		secondinterval;	//item pickup call count

	//waiting
	vec3_t		movtarget_pt;	//moving target waiting point
	edict_t		*waitin_obj;	//for waiting sequence complete

	//basical moving
	float		moveyaw;		//true moving yaw

	//combat
	int			total_bomb;		//total put bomb
	float		gren_time;		//grenade time

	//contents
//	int			front_contents;
	int			ground_contents;
	float		ground_slope;

	//count (inc only)
	int			tmpcount;

	//moving hist
	float		nextcheck;		//checking time
	vec3_t		pold_origin;	//old origin
	vec3_t		pold_angles;	//old angles

	//target object shot
	qboolean	objshot;		


	edict_t		*sighten;		//sighting enemy to me info from entity sight
	edict_t		*locked;		//locking enemy to me info from lockon missile

	//waterstate
	int			waterstate;

	//route
	qboolean	route_trace;
	int			routeindex;		//routing index
	float		rt_locktime;
	float		rt_releasetime;
	qboolean	havetarget;		//target on/off
	int			targetindex;

	//battle
	edict_t		*last_target;	//old enemy
	vec3_t		last_pos;		//old origin
	int			battlemode;		//mode
	int			battlecount;	//temporary count
	int			battlesubcnt;	//subcount
	int			battleduckcnt;	//duck
	float		fbattlecount;	//float temoporary count
	vec3_t		vtemp;			//temporary vec
	int			foundedenemy;	//foundedenemy
	char		secwep_selected;//secondweapon selected

	vec3_t		aimedpos;		//shottenpoint
	qboolean	trapped;		//trapflag

	//team
	short		tmplstate;		//teamplay state
	short		ctfstate;		//ctf state
	edict_t		*followmate;	//follow
	float		matelock;		//team mate locking time
} zgcl_t;

#endif
