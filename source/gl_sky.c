/*
Copyright (C) 2003-2006 Andrey Nazarov
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "gl_local.h"

static char	skyname[MAX_QPATH];
static float	skyrotate;
static vec3_t	skyaxis;
static image_t	*sky_images[6];

static vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};

// 1 = s, 2 = t, 3 = 2048
static int	st_to_vec[6][3] = {
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down
};

// s = [0]/[2], t = [1]/[2]
static int	vec_to_st[6][3] = {
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}
};

static float	skymins[2][6], skymaxs[2][6];
static float	sky_min, sky_max;

static void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

static void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		Com_Error( ERR_DROP,  "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = qfalse;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = qtrue;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = qtrue;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}

/*
=================
R_AddSkySurface
=================
*/
void R_AddSkySurface( bspSurface_t *fa ) {
	int			i;
	vec3_t		verts[MAX_CLIP_VERTS];
	bspPoly_t	*p;
    vec_t *vert;

	// calculate vertex values for sky box
	for (p=fa->polys ; p ; p=p->next)
	{
        vert = p->vertices;
		for (i=0 ; i<p->numVerts ; i++)
		{
			VectorSubtract (vert, modelViewOrigin, verts[i]);
            vert += VERTEX_SIZE;
		}
		ClipSkyPolygon (p->numVerts, verts[0], 0);
	}
}


/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox( void ) {
	int		i;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}
}


static void MakeSkyVec (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s*4800;//2300;
	b[1] = t*4800;//2300;
	b[2] = 4800;//2300;

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}

	// avoid bilerp seam
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	if (s < sky_min)
		s = sky_min;
	else if (s > sky_max)
		s = sky_max;
	if (t < sky_min)
		t = sky_min;
	else if (t > sky_max)
		t = sky_max;

	t = 1.0 - t;
	qglTexCoord2f (s, t);
	qglVertex3fv (v);
}


/*
==============
R_DrawSkyBox
==============
*/
void R_DrawSkyBox( void ) {
	int		i;
    static int	skytexorder[6] = {0,2,1,3,4,5};

	if (skyrotate)
	{	// check for no sky at all
		for (i=0 ; i<6 ; i++)
			if (skymins[0][i] < skymaxs[0][i]
			&& skymins[1][i] < skymaxs[1][i])
				break;
		if (i == 6)
			return;		// nothing visible
	}

	qglPushMatrix ();
	qglTranslatef (modelViewOrigin[0], modelViewOrigin[1], modelViewOrigin[2]);
	qglRotatef (glr.fd.time * skyrotate,
            skyaxis[0], skyaxis[1], skyaxis[2]);
	qglColor3f( 1, 1, 1 );
	GL_Bits( GLS_DEFAULT );

	for (i=0 ; i<6 ; i++)
	{
		if (skyrotate)
		{	// hack, forces full sky to draw when rotating
			skymins[0][i] = -1;
			skymins[1][i] = -1;
			skymaxs[0][i] = 1;
			skymaxs[1][i] = 1;
		}

		if (skymins[0][i] >= skymaxs[0][i]
		|| skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_BindTexture (sky_images[skytexorder[i]]->texnum);

		qglBegin (GL_QUADS);
		MakeSkyVec (skymins[0][i], skymins[1][i], i);
		MakeSkyVec (skymins[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymaxs[1][i], i);
		MakeSkyVec (skymaxs[0][i], skymins[1][i], i);
		qglEnd ();
	}
	qglPopMatrix ();
}


/*
============
R_SetSky
============
*/
void R_SetSky( const char *name, float rotate, vec3_t axis ) {
	int		i;
	char	pathname[MAX_QPATH];
    // 3dstudio environment map names
    static char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

	Q_strncpyz( skyname, name, sizeof( skyname ) );
	skyrotate = rotate;
	VectorCopy (axis, skyaxis);

	for (i=0 ; i<6 ; i++)
	{
		Com_sprintf (pathname, sizeof(pathname), "env/%s%s.tga",
                skyname, suf[i]);

		sky_images[i] = R_FindImage (pathname, it_sky);
		if (!sky_images[i])
			sky_images[i] = r_notexture;

		/*if (gl_skymip->integer || skyrotate)
		{	// take less memory
			sky_min = 1.0/256;
			sky_max = 255.0/256;
		}
		else*/	
		{
			sky_min = 1.0/512;
			sky_max = 511.0/512;
		}
	}

}

