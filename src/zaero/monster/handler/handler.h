#ifndef Z_HANDLER_H
#define Z_HANDLER_H

/*
 00-30  Idle1 (sitting down)
 31-59  Idle2 (pat on head)
 60-89  Idle3 (standing)
 90-100 Stand (standing up from sitting)
101-110 Sit   (sitting down from standing)
111-128 Restrain (handler lets go)
*/

/*
	Hound constants
*/
#define FRAME_stand1start	    0
#define FRAME_stand1end		    30
#define FRAME_stand2start	    31
#define FRAME_stand2end		    59
#define FRAME_stand3start	    60
#define FRAME_stand3end		    89
#define FRAME_stand4start	    90
#define FRAME_stand4end		    100
#define FRAME_stand5start	    101
#define FRAME_stand5end		    110

#define FRAME_attack1Start    111
#define FRAME_attack1Sep	    122
#define FRAME_attack1End	    128

#define MODEL_SCALE	1.000

#endif
