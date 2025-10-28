#ifndef Z_BOSS_H
#define Z_BOSS_H

/*
	Boss constants

/zaero/models/monsters/bossz/mech/tris.md2

001-031 Idle1 (waves arms around menacingly)
032-056 Idle2 (grapple thing)

057-066 RHook (raises weapon from Idle to fire Grappling Hook or Rockets)
 - weapon arm rotates as it raises

067-070 RCannon (raises weapon from Idle to fire Cannon)

071-091 Attack1a (fires 7 Rockets)
 - you can skip some frames if not firing all 7 rockets and jump to the reload sequence next
 - (33, 50, 5) frame071
 - (27, 39, 5) frame074
 - (39, 39, 5) frame077
 - (27, 44, 5) frame080
 - (39, 44, 5) frame083
 - (29, 48, 5) frame086
 - (38, 48, 5) frame089

092-098 Attack1b (reloads rockets)

099-106 Attack2a (fires Grappling Hook)
 - (34, 24, 3) frame104

107-109 Attack2b (reels in Grappling Hook)
 - loop as long as neccessary
 - return hook to (34, 24, 5) approx. (arm is moving round)

110-118 Attack2c (punch/swing, use at end of grapple attack2b)

119-132 Attack3 (Cannon)
 - spread is approx 50 degress wide
 - (30, 44, 19) 25 degrees left of target, frame119
 - (32, 33, 14) 16 degrees left of target, frame121
 - (32, 45, 4) 8 degrees left of target, frame123
 - (32, 34, 2) towards target, frame125
 - (32, 49, -7) 8 degrees right of target, frame127
 - (34, 36, -6) 16 degrees right of target, frame129
 - (34, 36, -6) 16 degrees right of target, frame131

133-135 LCannon (lowers weapon from Cannon firing to Idle)
136-141 LHook (lowers weapon from Grappling Hook or Rocket firing to Idle)

142-147 H2C (switches from Rocket firing to Cannon)
148-153 C2H (switches from Cannon firing to Grappling Hook or Rockets)
- use these to switch from one attack to another without resetting to Idle

154-160 Walk1 (steps out from Idle on left foot)
161-169 Walk2 (steps forward on right foot)
170-176 Walk3 (steps forward on left foot)
177-184 Walk4 (steps to Idle on right foot)

185-187 Pain1 (short)
188-192 Pain2 (medium)
193-217 Pain3 (very long)

218-236 Death1 (falls over backwards)

237-281 Death2 (head blown off)
 - steps forward to gain balance
 - rocket (25, 26, 26) frame249
 - rocket (20, 21, 16) frame250
 - rocket (30, 20, 17) frame251
 - rocket (17, 16, 8) frame252
 - rocket (30, 16, 10) frame253
 - rocket (25, 18, 0) frame254
 - rocket (30, 27, -17) frame255
 - cannon (33, 46, -9) 20 degrees right of target, frame257
 - cannon (37, 31, -3) 15 degrees right of target, frame258
 - cannon (24, 19, 21) 25 degrees left of target, frame264
 - grappling (28, -8, 35) straight up end of death2 seq.
   (make grapple hook only occur 15% of the time)


/zaero/models/monsters/bossz/grapple/tris.md2

282-282 Grapple1 (shooting out)
283-283 Grapple2 (retracting)

*/

#define FRAME_stand1start	    1
#define FRAME_stand1end		    31
#define FRAME_stand2start	    32
#define FRAME_stand2end		    56
#define FRAME_preHookStart		57
#define FRAME_preHookEnd			66
#define FRAME_preCannonStart	67
#define FRAME_preCannonEnd		70
#define FRAME_attack1aStart   71
#define FRAME_attack1aEnd	    91
#define FRAME_attack1bStart   92
#define FRAME_attack1bEnd	    98
#define FRAME_attack2aStart   99
#define FRAME_attack2aEnd	    106
#define FRAME_attack2bStart   107
#define FRAME_attack2bEnd	    109
#define FRAME_attack2cStart   110
#define FRAME_attack2cEnd	    118
#define FRAME_attack3Start		119
#define FRAME_attack3End	    132
#define FRAME_postCannonStart	133
#define FRAME_postCannonEnd		135
#define FRAME_postHookStart		136
#define FRAME_postHookEnd			141
#define FRAME_attackH2CStart	142
#define FRAME_attackH2CEnd		147
#define FRAME_attackC2HStart	148
#define FRAME_attackC2HEnd		153
#define FRAME_preWalkStart 	  154
#define FRAME_preWalkEnd   	  160
#define FRAME_walkStart 	    161
#define FRAME_walkEnd   	    176
#define FRAME_postWalkStart 	177
#define FRAME_postWalkEnd   	184
#define FRAME_pain1Start	    185
#define FRAME_pain1End		    187
#define FRAME_pain2Start	    188
#define FRAME_pain2End		    192
#define FRAME_pain3Start	    193
#define FRAME_pain3End		    217
#define FRAME_die1Start		    218
#define FRAME_die1End		      236
#define FRAME_die2Start		    237
#define FRAME_die2End		      281

#define MODEL_SCALE	1.000

#endif
