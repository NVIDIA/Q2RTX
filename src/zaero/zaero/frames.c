#if defined(_DEBUG) && defined(_Z_TESTMODE)

#include "../header/local.h"
#include "../header/frames.h"

/**************************************************************************
   Monster frame move lists.
  **************************************************************************/

/*=========================================================================
   Hound.
  =========================================================================*/

extern mmove_t hound_stand1;
extern mmove_t hound_stand2;
extern mmove_t hound_move_run;
extern mmove_t hound_move_walk;
extern mmove_t hound_move_pain1;
extern mmove_t hound_move_pain2;
extern mmove_t hound_move_attack1;
extern mmove_t hound_move_attack2;
extern mmove_t hound_move_jump;
extern mmove_t hound_move_death;

mmove_t *frame_moves_hound[] = {
         &hound_stand1, &hound_stand2, &hound_move_run,
         &hound_move_walk, &hound_move_pain1, &hound_move_pain2,
         &hound_move_attack1, &hound_move_attack2, &hound_move_jump,
         &hound_move_death,
         NULL};

/*=========================================================================
   Sentien.
  =========================================================================*/
extern mmove_t sentien_move_stand1;
extern mmove_t sentien_move_stand2;
extern mmove_t sentien_move_stand3;
extern mmove_t sentien_move_walk_start;
extern mmove_t sentien_move_walk;
extern mmove_t sentien_move_walk_end;
extern mmove_t sentien_move_run_start;
extern mmove_t sentien_move_run;
extern mmove_t sentien_move_run_end;
extern mmove_t sentien_move_pre_blast_attack;
extern mmove_t sentien_move_blast_attack;
extern mmove_t sentien_move_post_blast_attack;
extern mmove_t sentien_move_pre_laser_attack;
extern mmove_t sentien_move_laser_attack;
extern mmove_t sentien_move_post_laser_attack;
extern mmove_t sentien_move_pain1;
extern mmove_t sentien_move_pain2;
extern mmove_t sentien_move_pain3;
extern mmove_t sentien_move_death1;
extern mmove_t sentien_move_death2;

mmove_t *frame_moves_sentien[] = {
         &sentien_move_stand1, &sentien_move_stand2, &sentien_move_stand3,
         &sentien_move_walk_start, &sentien_move_walk, &sentien_move_walk_end,
         &sentien_move_run_start, &sentien_move_run, &sentien_move_run_end,
         &sentien_move_pre_blast_attack, &sentien_move_blast_attack,
         &sentien_move_post_blast_attack, &sentien_move_pre_laser_attack,
         &sentien_move_laser_attack, &sentien_move_post_laser_attack,
         &sentien_move_pain1, &sentien_move_pain2, &sentien_move_pain3,
         &sentien_move_death1, &sentien_move_death2,
         NULL};

/*=========================================================================
   Gunner.
  =========================================================================*/
extern mmove_t gunner_move_fidget;
extern mmove_t gunner_move_stand;
extern mmove_t gunner_move_walk;
extern mmove_t gunner_move_run;
extern mmove_t gunner_move_runandshoot;
extern mmove_t gunner_move_pain3;
extern mmove_t gunner_move_pain2;
extern mmove_t gunner_move_pain1;
extern mmove_t gunner_move_death;
extern mmove_t gunner_move_duck;
extern mmove_t gunner_move_attack_chain;
extern mmove_t gunner_move_fire_chain;
extern mmove_t gunner_move_endfire_chain;
extern mmove_t gunner_move_attack_grenade;

mmove_t *frame_moves_gunner[] = {
         &gunner_move_fidget, &gunner_move_stand, &gunner_move_walk,
         &gunner_move_run, &gunner_move_runandshoot,
         &gunner_move_pain3, &gunner_move_pain2, &gunner_move_pain1,
         &gunner_move_death, &gunner_move_duck,
         &gunner_move_attack_chain, &gunner_move_fire_chain,
         &gunner_move_endfire_chain, &gunner_move_attack_grenade,
         NULL};

/*=========================================================================
   Infantry.
  =========================================================================*/
extern mmove_t infantry_move_stand;
extern mmove_t infantry_move_fidget;
extern mmove_t infantry_move_walk;
extern mmove_t infantry_move_run;
extern mmove_t infantry_move_pain1;
extern mmove_t infantry_move_pain2;
extern mmove_t infantry_move_death1;
extern mmove_t infantry_move_death2;
extern mmove_t infantry_move_death3;
extern mmove_t infantry_move_duck;
extern mmove_t infantry_move_attack1;
extern mmove_t infantry_move_attack2;
extern mmove_t infantry_move_attack2;
extern mmove_t infantry_move_attack2;

mmove_t *frame_moves_infantry[] = {
         &infantry_move_stand, &infantry_move_fidget, &infantry_move_walk,
         &infantry_move_run, &infantry_move_pain1, &infantry_move_pain2,
         &infantry_move_death1, &infantry_move_death2, &infantry_move_death3,
         &infantry_move_duck, &infantry_move_attack1, &infantry_move_attack2,
         NULL};

/*=========================================================================
   Chick.
  =========================================================================*/
extern mmove_t chick_move_fidget;
extern mmove_t chick_move_stand;
extern mmove_t chick_move_start_run;
extern mmove_t chick_move_run;
extern mmove_t chick_move_walk;
extern mmove_t chick_move_pain1;
extern mmove_t chick_move_pain2;
extern mmove_t chick_move_pain3;
extern mmove_t chick_move_death2;
extern mmove_t chick_move_death1;
extern mmove_t chick_move_duck;
extern mmove_t chick_move_start_attack1;
extern mmove_t chick_move_attack1;
extern mmove_t chick_move_end_attack1;
extern mmove_t chick_move_slash;
extern mmove_t chick_move_end_slash;
extern mmove_t chick_move_start_slash;

mmove_t *frame_moves_chick[] = {
         &chick_move_fidget, &chick_move_stand, &chick_move_start_run,
         &chick_move_run, &chick_move_walk,
         &chick_move_pain1, &chick_move_pain2, &chick_move_pain3,
         &chick_move_death2, &chick_move_death1, &chick_move_duck,
         &chick_move_start_attack1, &chick_move_attack1, &chick_move_end_attack1,
         &chick_move_start_slash, &chick_move_slash, &chick_move_end_slash,
         NULL};

/*=========================================================================
   Gladiator.
  =========================================================================*/
extern mmove_t gladiator_move_stand;
extern mmove_t gladiator_move_walk;
extern mmove_t gladiator_move_run;
extern mmove_t gladiator_move_attack_melee;
extern mmove_t gladiator_move_attack_gun;
extern mmove_t gladiator_move_pain;
extern mmove_t gladiator_move_pain_air;
extern mmove_t gladiator_move_death;
extern mmove_t gladiator_move_death;
extern mmove_t gladiator_move_death;
extern mmove_t gladiator_move_death;

mmove_t *frame_moves_gladiator[] = {
         &gladiator_move_stand, &gladiator_move_walk, &gladiator_move_run,
         &gladiator_move_attack_melee, &gladiator_move_attack_gun, &gladiator_move_pain,
         &gladiator_move_pain_air, &gladiator_move_death,
         NULL};

/*=========================================================================
   Soldier.
  =========================================================================*/
extern mmove_t soldier_move_stand1;
extern mmove_t soldier_move_stand3;
extern mmove_t soldier_move_walk1;
extern mmove_t soldier_move_walk2;
extern mmove_t soldier_move_start_run;
extern mmove_t soldier_move_run;
extern mmove_t soldier_move_pain1;
extern mmove_t soldier_move_pain2;
extern mmove_t soldier_move_pain3;
extern mmove_t soldier_move_pain4;
extern mmove_t soldier_move_attack1;
extern mmove_t soldier_move_attack2;
extern mmove_t soldier_move_attack3;
extern mmove_t soldier_move_attack4;
extern mmove_t soldier_move_attack6;
extern mmove_t soldier_move_duck;
extern mmove_t soldier_move_death1;
extern mmove_t soldier_move_death2;
extern mmove_t soldier_move_death3;
extern mmove_t soldier_move_death4;
extern mmove_t soldier_move_death5;
extern mmove_t soldier_move_death6;
extern mmove_t soldier_move_death6;
extern mmove_t soldier_move_death6;
extern mmove_t soldier_move_death6;

mmove_t *frame_moves_soldier[] = {
         &soldier_move_stand1, &soldier_move_stand3,
         &soldier_move_walk1, &soldier_move_walk2,
         &soldier_move_start_run, &soldier_move_run,
         &soldier_move_pain1, &soldier_move_pain2,
         &soldier_move_pain3, &soldier_move_pain4,
         &soldier_move_attack1, &soldier_move_attack2, &soldier_move_attack3,
         &soldier_move_attack4, &soldier_move_attack6,
         &soldier_move_duck,
         &soldier_move_death1, &soldier_move_death2, &soldier_move_death3,
         &soldier_move_death4, &soldier_move_death5, &soldier_move_death6,
         NULL};

/*=========================================================================
   Supertank.
  =========================================================================*/
extern mmove_t supertank_move_stand;
extern mmove_t supertank_move_run;
extern mmove_t supertank_move_forward;
extern mmove_t supertank_move_turn_right;
extern mmove_t supertank_move_turn_left;
extern mmove_t supertank_move_pain3;
extern mmove_t supertank_move_pain2;
extern mmove_t supertank_move_pain1;
extern mmove_t supertank_move_death;
extern mmove_t supertank_move_backward;
extern mmove_t supertank_move_attack4;
extern mmove_t supertank_move_attack3;
extern mmove_t supertank_move_attack2;
extern mmove_t supertank_move_attack1;
extern mmove_t supertank_move_end_attack1;
extern mmove_t supertank_move_end_attack1;
extern mmove_t supertank_move_end_attack1;
extern mmove_t supertank_move_end_attack1;
extern mmove_t supertank_move_end_attack1;

mmove_t *frame_moves_supertank[] = {
         &supertank_move_stand, &supertank_move_run,
         &supertank_move_forward, &supertank_move_backward,
         &supertank_move_turn_right, &supertank_move_turn_left,
         &supertank_move_pain1, &supertank_move_pain2, &supertank_move_pain3,
         &supertank_move_death,
         &supertank_move_attack1, &supertank_move_end_attack1, 
         &supertank_move_attack2, &supertank_move_attack3, &supertank_move_attack4,
         NULL};

/*=========================================================================
   Tank.
  =========================================================================*/
extern mmove_t tank_move_stand;
extern mmove_t tank_move_start_walk;
extern mmove_t tank_move_walk;
extern mmove_t tank_move_stop_walk;
extern mmove_t tank_move_start_run;
extern mmove_t tank_move_run;
extern mmove_t tank_move_stop_run;
extern mmove_t tank_move_pain1;
extern mmove_t tank_move_pain2;
extern mmove_t tank_move_pain3;
extern mmove_t tank_move_attack_blast;
extern mmove_t tank_move_reattack_blast;
extern mmove_t tank_move_attack_post_blast;
extern mmove_t tank_move_attack_strike;
extern mmove_t tank_move_attack_pre_rocket;
extern mmove_t tank_move_attack_fire_rocket;
extern mmove_t tank_move_attack_post_rocket;
extern mmove_t tank_move_attack_chain;
extern mmove_t tank_move_death;
extern mmove_t tank_move_death;

mmove_t *frame_moves_tank[] = {
         &tank_move_stand,
         &tank_move_start_walk, &tank_move_walk, &tank_move_stop_walk,
         &tank_move_start_run, &tank_move_run, &tank_move_stop_run,
         &tank_move_pain1, &tank_move_pain2, &tank_move_pain3,
         &tank_move_attack_blast, &tank_move_reattack_blast, &tank_move_attack_post_blast,
         &tank_move_attack_strike, &tank_move_attack_chain,
         &tank_move_attack_pre_rocket, &tank_move_attack_fire_rocket, &tank_move_attack_post_rocket,
         &tank_move_death,
         NULL};

/**************************************************************************
   Sequence resolver for classnames.
  **************************************************************************/
mmove_t **z_frame_get_sequence(char *classname)
{
   if(Q_stricmp(classname, "monster_hound") == 0)
      return frame_moves_hound;

   if(Q_stricmp(classname, "monster_sentien") == 0)
      return frame_moves_sentien;

   if(Q_stricmp(classname, "monster_gunner") == 0)
      return frame_moves_gunner;

   if(Q_stricmp(classname, "monster_infantry") == 0)
      return frame_moves_infantry;

   if(Q_stricmp(classname, "monster_chick") == 0)
      return frame_moves_chick;

   if(Q_stricmp(classname, "monster_gladiator") == 0)
      return frame_moves_gladiator;

   if(Q_stricmp(classname, "monster_soldier") == 0)
      return frame_moves_soldier;

   if(Q_stricmp(classname, "monster_supertank") == 0)
      return frame_moves_supertank;

   if(Q_stricmp(classname, "monster_tank") == 0)
      return frame_moves_tank;


   gi.dprintf("classname <%s> not registered\n", classname);
   return NULL;
}

#endif
