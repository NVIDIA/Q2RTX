/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "vkpt.h"
#include "../../client/client.h"

/*
The FreeCam system activates when the user pauses the game.
In that case, all game input is redirected into this subsystem,
which processes certain keys and mouse movement to control the 
camera position, orientation, zoom, focus, and aperture.

When the camera is moved for the first time after activating
the freecam mode, the gun is removed from the view, and the 
player model is switched to third person.
*/


static vec3_t freecam_vieworg = { 0.f };
static vec3_t freecam_viewangles = { 0.f };
static float freecam_zoom = 1.f;
static bool freecam_keystate[6] = { 0 };
static bool freecam_active = false;
static int freecam_player_model = 0;

extern float autosens_x;
extern float autosens_y;
extern cvar_t *m_accel;
extern cvar_t *m_autosens;
extern cvar_t *m_pitch;
extern cvar_t *m_invert;
extern cvar_t *m_yaw;
extern cvar_t *sensitivity;
extern cvar_t *cvar_pt_dof;
extern cvar_t *cvar_pt_aperture;
extern cvar_t *cvar_pt_focus;
extern cvar_t *cvar_pt_freecam;


void vkpt_freecam_reset()
{
	if (!freecam_active)
		return;

	Cvar_SetByVar(cl_player_model, va("%d", freecam_player_model), FROM_CODE);
	freecam_active = false;
}

static void vkpt_freecam_mousemove(void)
{
	int dx, dy;
	float mx, my;
	float speed;

	if (!vid.get_mouse_motion)
		return;

	if (!vid.get_mouse_motion(&dx, &dy))
		return;

	mx = dx;
	my = dy;

	if (!mx && !my) {
		return;
	}

	if (Key_IsDown(K_MOUSE1) && Key_IsDown(K_MOUSE2))
	{
		mx *= sensitivity->value;
		freecam_viewangles[ROLL] += m_yaw->value * mx;
	}
	else if (Key_IsDown(K_MOUSE1))
	{
		Cvar_ClampValue(m_accel, 0, 1);

		speed = sqrtf(mx * mx + my * my);
		speed = sensitivity->value + speed * m_accel->value;

		mx *= speed;
		my *= speed;

		if (m_autosens->integer) {
			mx *= cl.fov_x * autosens_x;
			my *= cl.fov_y * autosens_y;
		}

		mx /= freecam_zoom;
		my /= freecam_zoom;

		freecam_viewangles[YAW] -= m_yaw->value * mx;
		freecam_viewangles[PITCH] += m_pitch->value * my * (m_invert->integer ? -1.f : 1.f);

		freecam_viewangles[PITCH] = max(-90.f, min(90.f, freecam_viewangles[PITCH]));
	}
	else if (Key_IsDown(K_MOUSE2))
	{
		freecam_zoom *= powf(0.5f, my * m_pitch->value * 0.1f);
		freecam_zoom = max(0.5f, min(20.f, freecam_zoom));
	}
}

void vkpt_freecam_update(float frame_time)
{
	if (cl_paused->integer != 2 || !sv_paused->integer || !cvar_pt_freecam->integer)
	{
		vkpt_freecam_reset();
		return;
	}

	if (!freecam_active)
	{
		VectorCopy(vkpt_refdef.fd->vieworg, freecam_vieworg);
		VectorCopy(vkpt_refdef.fd->viewangles, freecam_viewangles);
		freecam_zoom = 1.f;
		freecam_player_model = cl_player_model->integer;
		freecam_active = true;
	}

	vec3_t prev_vieworg;
	vec3_t prev_viewangles;
	VectorCopy(freecam_vieworg, prev_vieworg);
	VectorCopy(freecam_viewangles, prev_viewangles);
	float prev_zoom = freecam_zoom;

	vec3_t velocity = { 0.f };
	if (freecam_keystate[0]) velocity[0] += 1.f;
	if (freecam_keystate[1]) velocity[0] -= 1.f;
	if (freecam_keystate[2]) velocity[1] += 1.f;
	if (freecam_keystate[3]) velocity[1] -= 1.f;
	if (freecam_keystate[4]) velocity[2] += 1.f;
	if (freecam_keystate[5]) velocity[2] -= 1.f;

	if (Key_IsDown(K_SHIFT))
		VectorScale(velocity, 5.f, velocity);
	else if (Key_IsDown(K_CTRL))
		VectorScale(velocity, 0.1f, velocity);

	vec3_t forward, right, up;
	AngleVectors(freecam_viewangles, forward, right, up);
	float speed = 100.f;
	VectorMA(freecam_vieworg, velocity[0] * frame_time * speed, forward, freecam_vieworg);
	VectorMA(freecam_vieworg, velocity[1] * frame_time * speed, right, freecam_vieworg);
	VectorMA(freecam_vieworg, velocity[2] * frame_time * speed, up, freecam_vieworg);

	vkpt_freecam_mousemove();

	VectorCopy(freecam_vieworg, vkpt_refdef.fd->vieworg);
	VectorCopy(freecam_viewangles, vkpt_refdef.fd->viewangles);
	vkpt_refdef.fd->fov_x = RAD2DEG(atanf(tanf(DEG2RAD(vkpt_refdef.fd->fov_x) * 0.5f) / freecam_zoom)) * 2.f;
	vkpt_refdef.fd->fov_y = RAD2DEG(atanf(tanf(DEG2RAD(vkpt_refdef.fd->fov_y) * 0.5f) / freecam_zoom)) * 2.f;

	if (!VectorCompare(freecam_vieworg, prev_vieworg) || !VectorCompare(freecam_viewangles, prev_viewangles))
	{
		if (freecam_player_model != CL_PLAYER_MODEL_DISABLED && cl_player_model->integer != CL_PLAYER_MODEL_THIRD_PERSON)
			Cvar_SetByVar(cl_player_model, va("%d", CL_PLAYER_MODEL_THIRD_PERSON), FROM_CODE);

		vkpt_reset_accumulation();
	}

	if (freecam_zoom != prev_zoom)
	{
		// zoom doesn't reset the player model, but does reset the accumulation
		vkpt_reset_accumulation();
	}
}

bool R_InterceptKey_RTX(unsigned key, bool down)
{
	if (cl_paused->integer != 2 || !sv_paused->integer)
		return false;

	const char* kb = Key_GetBindingForKey(key);
	if (kb && strstr(kb, "pause"))
		return false;

	if (cvar_pt_dof->integer != 0 && down && (key == K_MWHEELUP || key == K_MWHEELDOWN))
	{
		cvar_t* var;
		float minvalue;
		float maxvalue;

		if (Key_IsDown(K_SHIFT))
		{
			var = cvar_pt_aperture;
			minvalue = 0.01f;
			maxvalue = 10.f;
		}
		else
		{
			var = cvar_pt_focus;
			minvalue = 1.f;
			maxvalue = 10000.f;
		}

		float factor = Key_IsDown(K_CTRL) ? 1.01f : 1.1f;

		if (key == K_MWHEELDOWN)
			factor = 1.f / factor;

		float value = var->value;
		value *= factor;
		value = max(minvalue, min(maxvalue, value));
		Cvar_SetByVar(var, va("%f", value), FROM_CONSOLE);

		return true;
	}

	switch (key)
	{
	case 'w': freecam_keystate[0] = down; return true;
	case 's': freecam_keystate[1] = down; return true;
	case 'd': freecam_keystate[2] = down; return true;
	case 'a': freecam_keystate[3] = down; return true;
	case 'e': freecam_keystate[4] = down; return true;
	case 'q': freecam_keystate[5] = down; return true;

    // make sure that other keys that control the freecam mode don't
    // interfere with the game, for example MOUSE1 usually maps to fire
    case K_CTRL:
    case K_SHIFT:
    case K_MWHEELDOWN:
    case K_MWHEELUP:
    case K_MOUSE1:
    case K_MOUSE2:
        return true;
	}

	return false;
}
