#ifndef Z_ANIM_H
#define Z_ANIM_H

#if defined(_DEBUG) && defined(_Z_TESTMODE)

/*=========================================================================
   Animation player types.
  =========================================================================*/
typedef enum
{
   DIR_FIXED,
   DIR_AT_CLIENT,
   DIR_PARA_CLIENT
} anim_dir_t;


typedef struct
{
   edict_t *monster; //monster being animated
   mframe_t monster_frames[1]; //current frame buffer
   mmove_t monster_move; //replacement currentmove for monster
   mmove_t **monster_sequences; //list of frame sequences for monster

   int current_sequence; //0 to play all frames
   int current_frame; //current frame in the current sequence (0 based)

   float last_dist; //last distance travelled, for going backwards
   qboolean moving_forward;

   int actual_frame, last_actual_frame;
   mmove_t *actual_sequence;
   int actual_sequence_idx;

   qboolean paused; //is animation paused or looping
   qboolean stationary; //allow movement in frame
   qboolean frame_events; //play frame events?
   qboolean active; //apply state changes to this animation

   anim_dir_t facing;
   anim_dir_t aim;
   vec3_t v_aim;
} anim_data_t;

//for aim correction during animation playback
qboolean anim_player_correct_aim(edict_t *self, vec3_t aim);

//for console commands
void anim_player_cmd(edict_t *ent);

#define ANIM_AIM(x, y) anim_player_correct_aim(x, y)

#else

#define ANIM_AIM(x, y) 

#endif

#endif
