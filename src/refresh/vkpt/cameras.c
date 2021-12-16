/*
Copyright (C) 2021, NVIDIA CORPORATION. All rights reserved.

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

#include "cameras.h"
#include "vkpt.h"

#include <string.h>

void
vkpt_cameras_load(bsp_mesh_t* wm, const char* map_name)
{
	wm->num_cameras = 0;

	char* filebuf = NULL;

    char filename[MAX_QPATH];
    Q_snprintf(filename, sizeof(filename), "maps/cameras/%s.txt", map_name);
	FS_LoadFile(filename, (void**)&filebuf);

	if (!filebuf)
	{
		Com_DPrintf("Couldn't read %s\n", filename);
		return;
	}

	char const * ptr = (char const *)filebuf;
	char linebuf[1024];

	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		{ char* t = strchr(linebuf, '#'); if (t) *t = 0; }   // remove comments
		{ char* t = strchr(linebuf, '\n'); if (t) *t = 0; }  // remove newline

		vec3_t pos, dir;

		if (sscanf(linebuf, "(%f, %f, %f) (%f, %f, %f)", &pos[0], &pos[1], &pos[2], &dir[0], &dir[1], &dir[2]) == 6)
		{
			if (wm->num_cameras < MAX_CAMERAS)
			{
				VectorCopy(pos, wm->cameras[wm->num_cameras].pos);
				VectorCopy(dir, wm->cameras[wm->num_cameras].dir);
				wm->num_cameras++;
			}
			else
			{
				Com_WPrintf("Map has too many cameras (max: %i)\n", MAX_CAMERAS);
				break;
			}
		}
	}

	Z_Free(filebuf);
}

static void Camera_Cmd_f(void)
{
	vec3_t forward;
	AngleVectors(vkpt_refdef.fd->viewangles, forward, NULL, NULL);

	Com_Printf("(%f, %f, %f) (%f, %f, %f)\n", vkpt_refdef.fd->vieworg[0], vkpt_refdef.fd->vieworg[1], vkpt_refdef.fd->vieworg[2], forward[0], forward[1], forward[2]);
}

static const cmdreg_t cmds[] = {
	{ "camera", &Camera_Cmd_f, NULL },
	{ NULL, NULL, NULL }
};

void vkpt_cameras_init(void)
{
	Cmd_Register(cmds);
}

void vkpt_cameras_shutdown(void)
{
	Cmd_RemoveCommand("camera");
}
