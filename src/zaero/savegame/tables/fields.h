/*
 * =======================================================================
 *
 * Game fields to be saved.
 *
 * =======================================================================
 */ 

// from entity_state_s
{"origin", FOFS(s.origin), F_VECTOR},
{"angles", FOFS(s.angles), F_VECTOR},
// missing fields...
{"owner", FOFS(owner), F_EDICT, FFL_NOSPAWN},
// missing fields...
{"model", FOFS(model), F_LSTRING},
{"model2", FOFS(model2), F_LSTRING}, // DG: new
{"model3", FOFS(model3), F_LSTRING}, // DG: new
{"model4", FOFS(model4), F_LSTRING}, // DG: new
// freetime?
{"classname", FOFS(classname), F_LSTRING},
{"spawnflags", FOFS(spawnflags), F_INT},
// timestamp?
{"angle", FOFS(s.angles), F_ANGLEHACK},
{"target", FOFS(target), F_LSTRING},
{"targetname", FOFS(targetname), F_LSTRING},
{"killtarget", FOFS(killtarget), F_LSTRING},
{"team", FOFS(team), F_LSTRING},
{"pathtarget", FOFS(pathtarget), F_LSTRING},
{"deathtarget", FOFS(deathtarget), F_LSTRING},
{"combattarget", FOFS(combattarget), F_LSTRING},
{"target_ent", FOFS(target_ent), F_EDICT, FFL_NOSPAWN},
{"speed", FOFS(speed), F_FLOAT},
{"accel", FOFS(accel), F_FLOAT},
{"decel", FOFS(decel), F_FLOAT},
{"aspeed", FOFS(aspeed), F_FLOAT}, // DG: new
// movedir?
// pos1, pos2?
// velocity, avelocity?
{"mass", FOFS(mass), F_INT},
// air_finished?
{"message", FOFS(message), F_LSTRING},
// gravity?
{"goalentity", FOFS(goalentity), F_EDICT, FFL_NOSPAWN},
{"movetarget", FOFS(movetarget), F_EDICT, FFL_NOSPAWN},
// yaw_speed, ideal_yaw ?
// nextthink?
{"prethink", FOFS(prethink), F_FUNCTION, FFL_NOSPAWN},
{"think", FOFS(think), F_FUNCTION, FFL_NOSPAWN},
{"blocked", FOFS(blocked), F_FUNCTION, FFL_NOSPAWN},
{"touch", FOFS(touch), F_FUNCTION, FFL_NOSPAWN},
{"use", FOFS(use), F_FUNCTION, FFL_NOSPAWN},
{"pain", FOFS(pain), F_FUNCTION, FFL_NOSPAWN},
{"die", FOFS(die), F_FUNCTION, FFL_NOSPAWN},
// touch_debounce_time, pain_debounce_time, fly_sound_debounce_time?
// last_move_time?
{"health", FOFS(health), F_INT},
// max_health, gib_health, deadflag, show_hostile?
// powerarmor_time?
{"map", FOFS(map), F_LSTRING},
// viewheight, takedamage?
{"dmg", FOFS(dmg), F_INT},
// radius_dmg, dmg_radius?
{"sounds", FOFS(sounds), F_INT},
{"count", FOFS(count), F_INT},
{"chain", FOFS(chain), F_EDICT, FFL_NOSPAWN},
{"enemy", FOFS(enemy), F_EDICT, FFL_NOSPAWN},
{"oldenemy", FOFS(oldenemy), F_EDICT, FFL_NOSPAWN},
{"activator", FOFS(activator), F_EDICT, FFL_NOSPAWN},
{"groundentity", FOFS(groundentity), F_EDICT, FFL_NOSPAWN},
// groundentity_linkcount?
{"teamchain", FOFS(teamchain), F_EDICT, FFL_NOSPAWN},
{"teammaster", FOFS(teammaster), F_EDICT, FFL_NOSPAWN},
{"mynoise", FOFS(mynoise), F_EDICT, FFL_NOSPAWN},
{"mynoise2", FOFS(mynoise2), F_EDICT, FFL_NOSPAWN},
// noise_index, noise_index2?
{"volume", FOFS(volume), F_FLOAT},
{"attenuation", FOFS(attenuation), F_FLOAT},
{"wait", FOFS(wait), F_FLOAT},
{"delay", FOFS(delay), F_FLOAT},
{"random", FOFS(random), F_FLOAT},
// teleport_time?
// watertype, waterlevel?
{"move_origin", FOFS(move_origin), F_VECTOR},
{"move_angles", FOFS(move_angles), F_VECTOR},
// light_level?
{"style", FOFS(style), F_INT},
// Note: "item" needs to be way down *after* spawn_temp_t::item!

{"light", 0, F_IGNORE}, // FIXME: what's this good for?!
// fields of moveinfo
{"endfunc", FOFS(moveinfo.endfunc), F_FUNCTION, FFL_NOSPAWN},
// fields of monsterinfo
{"stand", FOFS(monsterinfo.stand), F_FUNCTION, FFL_NOSPAWN},
{"idle", FOFS(monsterinfo.idle), F_FUNCTION, FFL_NOSPAWN},
{"search", FOFS(monsterinfo.search), F_FUNCTION, FFL_NOSPAWN},
{"walk", FOFS(monsterinfo.walk), F_FUNCTION, FFL_NOSPAWN},
{"run", FOFS(monsterinfo.run), F_FUNCTION, FFL_NOSPAWN},
{"dodge", FOFS(monsterinfo.dodge), F_FUNCTION, FFL_NOSPAWN},
{"attack", FOFS(monsterinfo.attack), F_FUNCTION, FFL_NOSPAWN},
{"melee", FOFS(monsterinfo.melee), F_FUNCTION, FFL_NOSPAWN},
{"sight", FOFS(monsterinfo.sight), F_FUNCTION, FFL_NOSPAWN},
{"checkattack", FOFS(monsterinfo.checkattack), F_FUNCTION, FFL_NOSPAWN},
{"currentmove", FOFS(monsterinfo.currentmove), F_MMOVE, FFL_NOSPAWN},
// zaero-specific stuff, several fields added, especially edicts
// timeout?
{"active", FOFS(active), F_INT},
// seq?
{"spawnflags2", FOFS(spawnflags2), F_INT},
// oldentnum?
{"laser", FOFS(laser), F_EDICT, FFL_NOSPAWN},
// weaponsound_time?
{"zRaduisList", FOFS(zRaduisList), F_EDICT, FFL_NOSPAWN},
{"zSchoolChain", FOFS(zSchoolChain), F_EDICT, FFL_NOSPAWN},
// zDistance?
{"rideWith0", FOFS(rideWith[0]), F_EDICT, FFL_NOSPAWN},
{"rideWith1", FOFS(rideWith[1]), F_EDICT, FFL_NOSPAWN},
{"rideWithOffset0", FOFS(rideWithOffset[0]), F_VECTOR},
{"rideWithOffset1", FOFS(rideWithOffset[1]), F_VECTOR},
{"mangle", FOFS(mangle), F_VECTOR},
// visorFrames?
{"mteam", FOFS(mteam), F_LSTRING},
{"mirrortarget", 0, F_IGNORE}, // FIXME: ??
{"mirrorlevelsave", 0, F_IGNORE}, // FIXME: ??
// targets, numTargets?
// onFloor, bossFireTimeout, bossFireCount?

// spawn_temp_t stuff
{"sky", STOFS(sky), F_LSTRING, FFL_SPAWNTEMP},
{"skyrotate", STOFS(skyrotate), F_FLOAT, FFL_SPAWNTEMP},
{"skyaxis", STOFS(skyaxis), F_VECTOR, FFL_SPAWNTEMP},
{"nextmap", STOFS(nextmap), F_LSTRING, FFL_SPAWNTEMP},
{"lip", STOFS(lip), F_INT, FFL_SPAWNTEMP},
{"distance", STOFS(distance), F_INT, FFL_SPAWNTEMP},
{"height", STOFS(height), F_INT, FFL_SPAWNTEMP},
{"noise", STOFS(noise), F_LSTRING, FFL_SPAWNTEMP},
{"pausetime", STOFS(pausetime), F_FLOAT, FFL_SPAWNTEMP},
{"item", STOFS(item), F_LSTRING, FFL_SPAWNTEMP},
{"item", FOFS(item), F_ITEM}, // DG: NOTE: this is down here because the "item" read from a level in ED_ParseField() must be the spawn_temp_t one!
{"gravity", STOFS(gravity), F_LSTRING, FFL_SPAWNTEMP},
{"minyaw", STOFS(minyaw), F_FLOAT, FFL_SPAWNTEMP},
{"maxyaw", STOFS(maxyaw), F_FLOAT, FFL_SPAWNTEMP},
{"minpitch", STOFS(minpitch), F_FLOAT, FFL_SPAWNTEMP},
{"maxpitch", STOFS(maxpitch), F_FLOAT, FFL_SPAWNTEMP},

{0, 0, 0, 0}
