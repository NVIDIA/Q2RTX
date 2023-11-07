/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "gl.h"

tesselator_t tess;

#define FACE_HASH_BITS  5
#define FACE_HASH_SIZE  (1 << FACE_HASH_BITS)
#define FACE_HASH_MASK  (FACE_HASH_SIZE - 1)

static mface_t  *faces_head[FACE_HASH_SIZE];
static mface_t  **faces_next[FACE_HASH_SIZE];
static mface_t  *faces_alpha;

void GL_Flush2D(void)
{
    glStateBits_t bits;

    if (!tess.numverts) {
        return;
    }

    bits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_FALSE | GLS_CULL_DISABLE;
    if (tess.flags & 2) {
        bits |= GLS_BLEND_BLEND;
    } else if (tess.flags & 1) {
        bits |= GLS_ALPHATEST_ENABLE;
    }

    Scrap_Upload();

    GL_BindTexture(0, tess.texnum[0]);
    GL_StateBits(bits);
    GL_ArrayBits(GLA_VERTEX | GLA_TC | GLA_COLOR);

    GL_VertexPointer(2, 4, tess.vertices);
    GL_TexCoordPointer(2, 4, tess.vertices + 2);
    GL_ColorBytePointer(4, 0, tess.colors);

    GL_LockArrays(tess.numverts);

    qglDrawElements(GL_TRIANGLES, tess.numindices, QGL_INDEX_ENUM, tess.indices);

    if (gl_showtris->integer > 1) {
        GL_DrawOutlines(tess.numindices, tess.indices);
    }

    GL_UnlockArrays();

    c.batchesDrawn2D++;

    tess.numindices = 0;
    tess.numverts = 0;
    tess.texnum[0] = 0;
    tess.flags = 0;
}

#define PARTICLE_SIZE   (1 + (float)M_SQRT1_2)
#define PARTICLE_SCALE  (1 / (2 * PARTICLE_SIZE))

void GL_DrawParticles(void)
{
    particle_t *p;
    int total, count;
    vec3_t transformed;
    vec_t scale, scale2, dist;
    color_t color;
    int numverts;
    vec_t *dst_vert;
    uint32_t *dst_color;
    int bits;

    if (!glr.fd.num_particles)
        return;

    GL_LoadMatrix(glr.viewmatrix);

    GL_VertexPointer(3, 5, tess.vertices);
    GL_TexCoordPointer(2, 5, tess.vertices + 3);
    GL_ColorBytePointer(4, 0, tess.colors);

    bits = (gl_partstyle->integer ? GLS_BLEND_ADD : GLS_BLEND_BLEND) | GLS_DEPTHMASK_FALSE;

    p = glr.fd.particles;
    total = glr.fd.num_particles;
    do {
        GL_BindTexture(0, TEXNUM_PARTICLE);
        GL_StateBits(bits);
        GL_ArrayBits(GLA_VERTEX | GLA_TC | GLA_COLOR);

        count = min(total, TESS_MAX_VERTICES / 3);
        total -= count;

        dst_vert = tess.vertices;
        dst_color = (uint32_t *)tess.colors;
        numverts = count * 3;
        do {
            VectorSubtract(p->origin, glr.fd.vieworg, transformed);
            dist = DotProduct(transformed, glr.viewaxis[0]);

            scale = gl_partscale->value;
            if (dist > 20)
                scale += dist * 0.01f;
            scale2 = scale * PARTICLE_SCALE;

            VectorMA(p->origin, scale2, glr.viewaxis[1], dst_vert);
            VectorMA(dst_vert, -scale2, glr.viewaxis[2], dst_vert);
            VectorMA(dst_vert,  scale,  glr.viewaxis[2], dst_vert +  5);
            VectorMA(dst_vert, -scale,  glr.viewaxis[1], dst_vert + 10);

            dst_vert[ 3] = 0;               dst_vert[ 4] = 0;
            dst_vert[ 8] = 0;               dst_vert[ 9] = PARTICLE_SIZE;
            dst_vert[13] = PARTICLE_SIZE;   dst_vert[14] = 0;

            dst_vert += 15;

            if (p->color == -1) {
                color.u32 = p->rgba.u32;
            } else {
                color.u32 = d_8to24table[p->color & 0xff];
            }
            color.u8[3] *= p->alpha;

            dst_color[0] = color.u32;
            dst_color[1] = color.u32;
            dst_color[2] = color.u32;
            dst_color += 3;

            p++;
        } while (--count);

        qglDrawArrays(GL_TRIANGLES, 0, numverts);

        if (gl_showtris->integer) {
            GL_DrawOutlines(numverts, NULL);
        }
    } while (total);
}

/* all things serve the Beam */
void GL_DrawBeams(void)
{
    vec3_t d1, d2, d3;
    vec_t *start, *end;
    color_t color;
    vec_t *dst_vert;
    uint32_t *dst_color;
    QGL_INDEX_TYPE *dst_indices;
    vec_t length;
    int numverts;
    int numindices;
    entity_t *ent;
    int i;

    if (!glr.num_beams) {
        return;
    }

    GL_LoadMatrix(glr.viewmatrix);
    GL_BindTexture(0, TEXNUM_BEAM);
    GL_StateBits(GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE);
    GL_ArrayBits(GLA_VERTEX | GLA_TC | GLA_COLOR);

    GL_VertexPointer(3, 5, tess.vertices);
    GL_TexCoordPointer(2, 5, tess.vertices + 3);
    GL_ColorBytePointer(4, 0, tess.colors);

    numverts = numindices = 0;
    for (i = 0, ent = glr.fd.entities; i < glr.fd.num_entities; i++, ent++) {
        if (!(ent->flags & RF_BEAM)) {
            continue;
        }

        start = ent->origin;
        end = ent->oldorigin;
        VectorSubtract(end, start, d1);
        VectorSubtract(glr.fd.vieworg, start, d2);
        CrossProduct(d1, d2, d3);
        VectorNormalize(d3);
        length = ent->frame * 1.2f;
        VectorScale(d3, length, d3);

        length = VectorLength(d1);
        if (length < 0.001f)
            continue;

        if (ent->skinnum == -1) {
            color.u32 = ent->rgba.u32;
        } else {
            color.u32 = d_8to24table[ent->skinnum & 0xff];
        }
        color.u8[3] *= ent->alpha;

        if (numverts + 4 > TESS_MAX_VERTICES ||
            numindices + 6 > TESS_MAX_INDICES) {
            qglDrawElements(GL_TRIANGLES, numindices,
                            QGL_INDEX_ENUM, tess.indices);
            numverts = numindices = 0;
        }

        dst_vert = tess.vertices + numverts * 5;
        VectorAdd(start, d3, dst_vert);
        VectorSubtract(start, d3, dst_vert + 5);
        VectorSubtract(end, d3, dst_vert + 10);
        VectorAdd(end, d3, dst_vert + 15);

        dst_vert[3] = 0; dst_vert[4] = 0;
        dst_vert[8] = 1; dst_vert[9] = 0;
        dst_vert[13] = 1; dst_vert[14] = length;
        dst_vert[18] = 0; dst_vert[19] = length;

        dst_color = (uint32_t *)tess.colors + numverts;
        dst_color[0] = color.u32;
        dst_color[1] = color.u32;
        dst_color[2] = color.u32;
        dst_color[3] = color.u32;

        dst_indices = tess.indices + numindices;
        dst_indices[0] = numverts + 0;
        dst_indices[1] = numverts + 2;
        dst_indices[2] = numverts + 3;
        dst_indices[3] = numverts + 0;
        dst_indices[4] = numverts + 1;
        dst_indices[5] = numverts + 2;

        numverts += 4;
        numindices += 6;
    }

    qglDrawElements(GL_TRIANGLES, numindices,
                    QGL_INDEX_ENUM, tess.indices);
}

void GL_BindArrays(void)
{
    if (gl_static.world.vertices) {
        GL_VertexPointer(3, VERTEX_SIZE, tess.vertices);
        GL_TexCoordPointer(2, VERTEX_SIZE, tess.vertices + 4);
        if (lm.nummaps) {
            GL_LightCoordPointer(2, VERTEX_SIZE, tess.vertices + 6);
        }
        GL_ColorBytePointer(4, VERTEX_SIZE, (GLubyte *)(tess.vertices + 3));
    } else {
        qglBindBuffer(GL_ARRAY_BUFFER, gl_static.world.bufnum);

        GL_VertexPointer(3, VERTEX_SIZE, (GLfloat *)0);
        GL_TexCoordPointer(2, VERTEX_SIZE, (GLfloat *)(sizeof(GLfloat) * 4));
        if (lm.nummaps) {
            GL_LightCoordPointer(2, VERTEX_SIZE, (GLfloat *)(sizeof(GLfloat) * 6));
        }
        GL_ColorBytePointer(4, VERTEX_SIZE, (GLubyte *)(sizeof(GLfloat) * 3));

        qglBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void GL_Flush3D(void)
{
    glStateBits_t state = tess.flags;
    glArrayBits_t array = GLA_VERTEX | GLA_TC;

    if (!tess.numindices) {
        return;
    }

    if (q_likely(tess.texnum[1])) {
        state |= GLS_LIGHTMAP_ENABLE;
        array |= GLA_LMTC;

        if (q_unlikely(gl_lightmap->integer)) {
            state &= ~GLS_INTENSITY_ENABLE;
        }
    }

    if (!(state & GLS_TEXTURE_REPLACE)) {
        array |= GLA_COLOR;
    }

    GL_StateBits(state);
    GL_ArrayBits(array);

    GL_BindTexture(0, tess.texnum[0]);
    if (q_likely(tess.texnum[1])) {
        GL_BindTexture(1, tess.texnum[1]);
    }

    if (gl_static.world.vertices) {
        GL_LockArrays(tess.numverts);
    }

    qglDrawElements(GL_TRIANGLES, tess.numindices, QGL_INDEX_ENUM, tess.indices);

    if (gl_showtris->integer) {
        GL_DrawOutlines(tess.numindices, tess.indices);
    }

    if (gl_static.world.vertices) {
        GL_UnlockArrays();
    }

    c.batchesDrawn++;

    tess.texnum[0] = tess.texnum[1] = 0;
    tess.numindices = 0;
    tess.numverts = 0;
    tess.flags = 0;
}

static int GL_CopyVerts(mface_t *surf)
{
    void *src, *dst;
    int firstvert;

    if (tess.numverts + surf->numsurfedges > TESS_MAX_VERTICES) {
        GL_Flush3D();
    }

    src = gl_static.world.vertices + surf->firstvert * VERTEX_SIZE;
    dst = tess.vertices + tess.numverts * VERTEX_SIZE;
    memcpy(dst, src, surf->numsurfedges * VERTEX_SIZE * sizeof(vec_t));

    firstvert = tess.numverts;
    tess.numverts += surf->numsurfedges;
    return firstvert;
}

static int GL_TextureAnimation(mtexinfo_t *tex)
{
    if (q_unlikely(tex->next)) {
        unsigned c = (unsigned)glr.ent->frame % tex->numframes;

        while (c) {
            tex = tex->next;
            c--;
        }
    }

    return tex->image->texnum;
}

void GL_DrawFace(mface_t *surf)
{
    int numtris = surf->numsurfedges - 2;
    int numindices = numtris * 3;
    GLuint texnum[2];
    QGL_INDEX_TYPE *dst_indices;
    int i, j;

    if (q_unlikely(gl_lightmap->integer && surf->texnum[1])) {
        texnum[0] = TEXNUM_WHITE;
    } else {
        texnum[0] = GL_TextureAnimation(surf->texinfo);
    }
    texnum[1] = surf->texnum[1];

    if (tess.texnum[0] != texnum[0] ||
        tess.texnum[1] != texnum[1] ||
        tess.flags != surf->statebits ||
        tess.numindices + numindices > TESS_MAX_INDICES) {
        GL_Flush3D();
    }

    tess.texnum[0] = texnum[0];
    tess.texnum[1] = texnum[1];
    tess.flags = surf->statebits;

    if (q_unlikely(gl_static.world.vertices)) {
        j = GL_CopyVerts(surf);
    } else {
        j = surf->firstvert;
    }

    dst_indices = tess.indices + tess.numindices;
    for (i = 0; i < numtris; i++) {
        dst_indices[0] = j;
        dst_indices[1] = j + (i + 1);
        dst_indices[2] = j + (i + 2);
        dst_indices += 3;
    }
    tess.numindices += numindices;

    c.trisDrawn += numtris;
    c.facesTris += numtris;
    c.facesDrawn++;
}

void GL_ClearSolidFaces(void)
{
    int i;

    for (i = 0; i < FACE_HASH_SIZE; i++) {
        faces_next[i] = &faces_head[i];
    }
}

void GL_DrawSolidFaces(void)
{
    mface_t *face;
    int i;

    for (i = 0; i < FACE_HASH_SIZE; i++) {
        for (face = faces_head[i]; face; face = face->next) {
            GL_DrawFace(face);
        }
        faces_head[i] = NULL;
    }
}

void GL_DrawAlphaFaces(void)
{
    mface_t *face;

    if (!faces_alpha) {
        return;
    }

    glr.ent = NULL;

    GL_BindArrays();

    for (face = faces_alpha; face; face = face->next) {
        if (glr.ent != face->entity) {
            glr.ent = face->entity;
            GL_Flush3D();
            GL_SetEntityAxis();
            GL_RotateForEntity();
        }
        GL_DrawFace(face);
    }

    faces_alpha = NULL;

    GL_Flush3D();
}

void GL_AddSolidFace(mface_t *face)
{
    unsigned hash;

    hash = face->texnum[0] ^ face->texnum[1] ^ face->statebits;
    hash ^= hash >> FACE_HASH_BITS;
    hash ^= hash >> (FACE_HASH_BITS * 2);
    hash &= FACE_HASH_MASK;

    // preserve front-to-back ordering
    face->next = NULL;
    *faces_next[hash] = face;
    faces_next[hash] = &face->next;
}

void GL_AddAlphaFace(mface_t *face, entity_t *ent)
{
    // draw back-to-front
    face->entity = ent;
    face->next = faces_alpha;
    faces_alpha = face;
}

