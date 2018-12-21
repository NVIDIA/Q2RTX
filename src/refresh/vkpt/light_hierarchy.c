/*
Copyright (C) 2018 Addis Dittebrandt
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Florian Simon

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
#include "shader/light_hierarchy.h"
#include "shader/global_textures.h"

#include <assert.h>
#include <float.h>
#include <math.h>

//#include <sys/time.h>

#define FMINMAXF(A, MIN, MAX) fminf(fmaxf(A, MIN), MAX)
#define BDOT(A, B) FMINMAXF(DotProduct(A, B), -1.0, 1.0)

static VkDescriptorPool        desc_pool_light_hierarchy;
static BufferResource_t        buf_light_hierarchy[MAX_SWAPCHAIN_IMAGES];
static BufferResource_t        buf_light_hierarchy_staging[MAX_SWAPCHAIN_IMAGES];

typedef struct lh_bin_s {
    int num_prims;
    float c_aabb[6];
    float energy;
    float aabb[6];
    lh_cone_t cone;
} lh_bin_t;

typedef struct lh_prim_s {
    int index;
    float c[3];
    float energy;
    float aabb[6];
    lh_cone_t cone;
} lh_prim_t;

static inline float
tofloat(uint32_t i)
{
	union { uint32_t i; float f; } u = {.i=i};
	return u.f;
}

static inline uint32_t
touint(float f)
{
	union { float f; uint32_t i; } u = {.f=f};
	return u.i;
}
// 32-bit normal encoding from Journal of Computer Graphics Techniques Vol. 3, No. 2, 2014
// A Survey of Efficient Representations for Independent Unit Vectors
// almost like oct30, but our error is = 0.00077204 avg = 0.00010846 compared to oct32P 0.00246 0.00122 
// i'm thinking maybe because we use a fixed point quantization (only multiples of 2 are mul/divd here)
// this also enables us to precisely encode (1 0 0) vectors.
static inline uint32_t
encode_normal(const float vec[3])
{
	uint32_t projected0, projected1;
	const float invL1Norm = 1.0f / (fabsf(vec[0]) + fabsf(vec[1]) + fabsf(vec[2]));

	// first find floating point values of octahedral map in [-1,1]:
	float enc0, enc1;
	if (vec[2] < 0.0f)
	{
		enc0 = (1.0f - fabsf(vec[1] * invL1Norm)) * ((vec[0] < 0.0f) ? -1.0f : 1.0f);
		enc1 = (1.0f - fabsf(vec[0] * invL1Norm)) * ((vec[1] < 0.0f) ? -1.0f : 1.0f);
	}
	else
	{
		enc0 = vec[0] * invL1Norm;
		enc1 = vec[1] * invL1Norm;
	}
	// then encode:
	uint32_t enci0 = touint((fabsf(enc0) + 2.0f)/2.0f);
	uint32_t enci1 = touint((fabsf(enc1) + 2.0f)/2.0f);
	// copy over sign bit and truncated mantissa. could use rounding for increased precision here.
	projected0 = ((touint(enc0) & 0x80000000)>>16) | ((enci0 & 0x7fffff)>>8);
	projected1 = ((touint(enc1) & 0x80000000)>>16) | ((enci1 & 0x7fffff)>>8);
	// avoid -0 cases:
	if((projected0 & 0x7fff) == 0) projected0 = 0;
	if((projected1 & 0x7fff) == 0) projected1 = 0;
	return (projected1 << 16) | projected0;
}

static inline int
is_leaf(lh_node_t* n) 
{ 
    return n->c[1].i < 0;
}

static inline int 
prim_offset(lh_node_t *n) 
{ 
    assert(-n->c[0].i >= 0);
    return -n->c[0].i;
}

static inline int 
num_prims(lh_node_t *n) 
{ 
    assert(-n->c[1].i > 0);
    return -n->c[1].i;
}

static inline void 
set_prim_offset(lh_node_t *n, int offset) 
{ 
    assert(offset >= 0); 
    n->c[0].i = -offset;
}

static inline void 
set_num_prims(lh_node_t *n, int num_prims) 
{ 
    assert(num_prims > 0); 
    n->c[1].i = -num_prims;
}

static lh_cone_t
lh_triangle_to_cone(const float* t0, const float* t1, const float* t2)
{
    lh_cone_t r;
    vec3_t u, v;
    VectorSubtract(t1, t0, u);
    VectorSubtract(t2, t0, v);
    CrossProduct(u, v, r.axis);
    VectorNormalize(r.axis);
    r.th_o = 0;
    r.th_e = M_PI/2;
    return r;
}

static lh_cone_t
lh_cone_union(lh_cone_t a, lh_cone_t b)
{
    lh_cone_t r;

    if (b.th_o > a.th_o)
    {
        lh_cone_t t = b;
        b = a;
        a = t;
    }

    float th_d = acosf(BDOT(a.axis, b.axis));
    r.th_o = (a.th_o + th_d + b.th_o) / 2.0f;
    r.th_e = fmaxf(a.th_e, b.th_e);

    if (fminf(th_d + b.th_o, M_PI) <= a.th_o)
    {
        // a already covers b
        r.th_o = a.th_o;
        VectorCopy(a.axis, r.axis);
    }
    else if (M_PI <= r.th_o)
    {
        r.th_o = M_PI;
        VectorCopy(a.axis, r.axis);
    }
    else
    {
        float th_r = r.th_o - a.th_o;
        vec3_t axis_r;
        CrossProduct(a.axis, b.axis, axis_r);
        VectorNormalize(axis_r);
        RotatePointAroundVector(r.axis, axis_r, a.axis, RAD2DEG(th_r));
        VectorNormalize(r.axis);
    }

    return r;
}

static inline void
lh_len(float aabb[6], float lengths[3])
{
    for (int k = 0; k < 3; k++)
        lengths[k] = aabb[k + 3] - aabb[k];
}

static inline float
lh_sur_m(float lengths[3])
{
    float area = 0.0f;
    for (int k = 0; k < 3; k++)
        area += 2.0f * lengths[k] * lengths[(k + 1) % 3];
    return area;
}

static inline float
lh_ori_m(lh_cone_t c)
{
    float th_w = fminf(c.th_o + c.th_e, M_PI);
    return 2.0f * M_PI * (1.0f - cosf(c.th_o)) + 0.5f * M_PI * (
        2.0f * th_w * sinf(c.th_o)
        - cosf(c.th_o - 2.0f * th_w)
        - 2.0f * c.th_o * sinf(c.th_o)
        + cosf(c.th_o)
    );
}

static inline float
lh_reg_m(float lengths[3], int dim)
{
    float max_length = fmaxf(fmaxf(lengths[0], lengths[1]), lengths[2]);
    return max_length / lengths[dim];
}

static inline float
lh_cost_measure(
    int dim,
    float p_aabb[6],
    lh_cone_t p_cone,
    float energy[2],
    float aabb[2][6],
    lh_cone_t cone[2])
{
    float p_lengths[3];
    lh_len(p_aabb, p_lengths);
    float lengths[2][3];
    for (int k = 0; k < 2; k++)
        lh_len(aabb[k], lengths[k]);
    float reg, p_sur, p_ori, sur[2], ori[2];
    reg = lh_reg_m(p_lengths, dim);
    p_sur = lh_sur_m(p_lengths);
    p_ori = lh_ori_m(p_cone);
    for (int k = 0; k < 2; k++)
    {
        sur[k] = lh_sur_m(lengths[k]);
        ori[k] = lh_ori_m(cone[k]);
    }
    return reg *
        (energy[0] * sur[0] * ori[0] + energy[1] * sur[1] * ori[1]) /
        (p_sur * p_ori);
}

static inline void
lh_init_aabb(float aabb[6])
{
    for (int k = 0; k < 3; k++)
    {
        aabb[k] = FLT_MAX;
        aabb[k + 3] = -FLT_MAX;
    }
}

static inline void
lh_enlarge_aabb_point(float aabb[6], const float p[3])
{
    for (int k = 0; k < 3; k++)
    {
        aabb[k] = fminf(aabb[k], p[k]);
        aabb[k+3] = fmaxf(aabb[k+3], p[k]);
    }
}

static inline void
lh_enlarge_aabb_points(float aabb[6], const float *p[], int num_p)
{
    for (int j = 0; j < num_p; j++)
        lh_enlarge_aabb_point(aabb, p[j]);
}

static inline void
lh_enlarge_aabb_aabb(float aabb[6], const float a_aabb[6])
{
    for (int k = 0; k < 3; k++)
    {
        aabb[k] = fminf(aabb[k], a_aabb[k]);
        aabb[k+3] = fmaxf(aabb[k+3], a_aabb[k+3]);
    }
}

void
lh_build_binned_rec(
    light_hierarchy_t* lh,
    lh_child_t* child,
    int offset,
    int num_prims,
    lh_prim_t *prims,
    int num_bins,
    lh_bin_t *bins[3],
    lh_bin_t *a_bins[3][2],
    float c_aabb[6],
    int level)
{
    if (lh->num_nodes == lh->max_num_nodes) {
        Com_Error(ERR_FATAL, "not enough space for light hierarchy nodes\n");
        return;
    }

    assert(num_prims > 0);

    float skip[3], k_0[3], k_1[3];
    for (int d = 0; d < 3; d++)
    {
        k_0[d] = c_aabb[d];
        float diff = c_aabb[d+3] - c_aabb[d];
        skip[d] = diff == 0;

        if (!skip[d])
            k_1[d] = (float)num_bins  / (c_aabb[d+3] - c_aabb[d]);
    }

    if (num_prims == 1 || (skip[0] && skip[1] && skip[2]))
    {
        child->i = lh->num_nodes++;
        lh_init_aabb(child->aabb);

        for (int i = offset; i < offset + num_prims; i++)
        {
            lh_prim_t *prim = &prims[i];
            lh_enlarge_aabb_aabb(child->aabb, prim->aabb);
            if (i == offset)
                child->cone = prim->cone;
            else
                child->cone = lh_cone_union(child->cone, prim->cone);
            child->energy += prim->energy;
        }

        lh_node_t *node = &lh->nodes[child->i];

        // we can currently only have leaves with 1 prim (no index array in shader),
        // so split further with dumb strategy if necessary
        // TODO remove at some point
        if (num_prims == 1)
        {
            set_num_prims(node, num_prims);
            lh_prim_t *prim = &prims[offset];
            set_prim_offset(node, prim->index);
        }
        else
        {
            int mid = offset + num_prims / 2;
            int end = offset + num_prims;
            lh_build_binned_rec(lh, &node->c[0], offset, mid - offset, prims, num_bins, bins, a_bins, c_aabb, level + 1);
            lh_build_binned_rec(lh, &node->c[1], mid, end - mid, prims, num_bins, bins, a_bins, c_aabb, level + 1);
        }
    }
    else
    {
        // initialize bins
        for (int d = 0; d < 3; d++)
            for (int b = 0; b < num_bins; b++)
            {
                lh_bin_t *bin = &bins[d][b];
                bin->num_prims = 0;
                lh_init_aabb(bin->c_aabb);
                bin->energy = 0.0f;
                lh_init_aabb(bin->aabb);
            }

        // populate bins
        for (int i = offset; i < offset + num_prims; i++)
        {
            lh_prim_t *prim = &prims[i];
            for (int d = 0; d < 3; d++)
            {
                if (skip[d])
                    continue;

                int bin_i = min(k_1[d] * (prim->c[d] - k_0[d]), num_bins - 1);
                lh_bin_t *bin = &bins[d][bin_i];
                bin->num_prims++;
                lh_enlarge_aabb_point(bin->c_aabb, prim->c);
                bin->energy += prim->energy;
                lh_enlarge_aabb_aabb(bin->aabb, prim->aabb);
                if (bin->num_prims == 1)
                    bin->cone = prim->cone;
                else
                    bin->cone = lh_cone_union(bin->cone, prim->cone);
            }
        }

        for (int d = 0; d < 3; d++)
            assert(skip[d] ||
                (bins[d][0].num_prims > 0 && bins[d][num_bins - 1].num_prims > 0));

        // initialize accumulated bins
        for (int d = 0; d < 3; d++)
        {
            if (skip[d])
                continue;
            for (int i = 0; i < 2; i++)
                memcpy(a_bins[d][i], bins[d], num_bins * sizeof(lh_bin_t));
        }

        // accumulate bins from left and right
        for (int d = 0; d < 3; d++)
        {
            if (skip[d])
                continue;

            for (int s = 0; s < 2; s++)
            {
                for (int b = 1; b < num_bins; b++)
                {
                    int b_i = s == 0 ? b : num_bins - b - 1;
                    int prev_b_i = s == 0 ? b_i - 1 : b_i + 1;

                    lh_bin_t *prev_bin = &a_bins[d][s][prev_b_i];
                    lh_bin_t *bin = &a_bins[d][s][b_i];

                    if (bin->num_prims == 0)
                        memcpy(bin, prev_bin, sizeof(lh_bin_t));
                    else
                    {
                        bin->num_prims += prev_bin->num_prims;
                        lh_enlarge_aabb_aabb(bin->c_aabb, prev_bin->c_aabb);
                        bin->energy += prev_bin->energy;
                        lh_enlarge_aabb_aabb(bin->aabb, prev_bin->aabb);
                        bin->cone = lh_cone_union(bin->cone, prev_bin->cone);
                    }
                }
            }
        }

        // find best split candidate
        float m_cost = FLT_MAX;
        int m_d, m_s;
        for (int d = 0; d < 3; d++)
        {
            if (skip[d])
                continue;

            float p_aabb[6];
            memcpy(p_aabb, a_bins[d][1][0].aabb, sizeof(p_aabb));
            lh_cone_t p_cone = a_bins[d][1][0].cone;

            for (int s = 0; s < num_bins - 1; s++)
            {
                lh_bin_t *a_bin[] = {&a_bins[d][0][s], &a_bins[d][1][s + 1]};
                assert(a_bin[0]->num_prims != 0 && a_bin[1]->num_prims != 0);
                float energy[2] = {a_bin[0]->energy, a_bin[1]->energy};
                float aabb[2][6];
                for (int i = 0; i < 2; i++)
                    memcpy(aabb[i], a_bin[i]->aabb, sizeof(aabb[i]));
                lh_cone_t cone[2] = {a_bin[0]->cone, a_bin[1]->cone};

                float cost = lh_cost_measure(
                    d, p_aabb, p_cone, energy, aabb, cone);

                if (cost < m_cost)
                {
                    m_cost = cost;
                    m_d = d;
                    m_s = s;
                }
            }
        }

        assert(m_cost != FLT_MAX);

        int d = m_d;
        int s = m_s;

        lh_bin_t *a_bin[] = {&a_bins[d][0][s], &a_bins[d][1][s + 1]};

        // build child
        child->i = lh->num_nodes++;
        memcpy(child->aabb, a_bins[d][1][0].aabb, sizeof(child->aabb));
        child->cone = a_bins[d][1][0].cone;
        child->energy = a_bins[d][1][0].energy;

        // rearrange primitives among subtrees
        int left_start = offset;
        int left_end = offset + a_bin[0]->num_prims;
        int right_start = left_end;
        int right_end = offset + num_prims;
        int left = left_start;
        int right = right_start;
        int term = 0;
        while (1)
        {
            for (; left < left_end; left++)
            {
                lh_prim_t *prim = &prims[left];
                int bin_i = k_1[d] * (prim->c[d] - k_0[d]);
                if (s < bin_i)
                    break;
            }

            if (left == left_end)
                term = 1;

            for (; right < right_end; right++)
            {
                lh_prim_t *prim = &prims[right];
                int bin_i = k_1[d] * (prim->c[d] - k_0[d]);
                if (bin_i <= s)
                    break;
            }

            assert(term ? right == right_end : right < right_end);

            if (term)
                break;

            lh_prim_t t = prims[left];
            prims[left] = prims[right];
            prims[right] = t;
        }

        lh_node_t *node = &lh->nodes[child->i];

        float c_aabb[2][6];
        for (int s = 0; s < 2; s++)
            memcpy(c_aabb[s], a_bin[s]->c_aabb, sizeof(c_aabb[s]));

        // recurse
        lh_build_binned_rec(lh, &node->c[0], left_start, left_end - left_start, prims, num_bins, bins, a_bins, c_aabb[0], level + 1);
        lh_build_binned_rec(lh, &node->c[1], right_start, right_end - right_start, prims, num_bins, bins, a_bins, c_aabb[1], level + 1);
    }
}

void
lh_compactify(light_hierarchy_t *lh, compact_lh_node_t *compact_nodes, const float *positions, const uint32_t *colors)
{
	for(int k = 0; k < lh->num_nodes; k++) {
		compact_lh_node_t *cn = compact_nodes + k;
		lh_node_t         *n  = lh->nodes + k;

		if(is_leaf(n)) {
			int light_idx = prim_offset(n);
			memcpy(cn->c[0].aabb, positions + light_idx * 9, 9 * sizeof(float));
			memcpy(cn->c[0].aabb + 9, colors + light_idx, sizeof(uint32_t));
		}
		else {
			compact_lh_node_t cn_tmp;
			for(int i = 0; i < 2; i++) {
				memcpy(cn_tmp.c[i].aabb, n->c[i].aabb, sizeof(float) * 6);
				cn_tmp.c[i].axis   = encode_normal(n->c[i].cone.axis);
				cn_tmp.c[i].th_o   = n->c[i].cone.th_o;
				//cn_tmp.c[i].energy = n->c[i].energy;
				cn_tmp.idx[i]      = is_leaf(&lh->nodes[n->c[i].i]) ? ~n->c[i].i : n->c[i].i;
			}
			memcpy(cn, &cn_tmp, sizeof(compact_lh_node_t));
		}
	}
}

int
lh_build_binned(void *dst, const float *positions, const uint32_t *colors, int num_prims, int num_bins)
{
	light_hierarchy_t light_hierarchy;
	light_hierarchy_t *lh = &light_hierarchy; // fixme

    lh->max_num_nodes = 3 * num_prims;
    lh->nodes = calloc(lh->max_num_nodes, sizeof(lh_node_t));
    lh->num_nodes = 0;

    float c_aabb[6];
    lh_init_aabb(c_aabb);
    lh_prim_t *prims = calloc(num_prims, sizeof(lh_prim_t));
    for (int i = 0; i < num_prims; i++)
    {
        lh_prim_t *prim = &prims[i];
        prim->index = i;
        lh_init_aabb(prim->aabb);
        prim->energy = 0.5; // TODO use actual emitted energy

		const float *p[] = {
			&positions[9 * i + 0],
			&positions[9 * i + 3],
			&positions[9 * i + 6],
		};
        lh_enlarge_aabb_point(prim->aabb, p[0]);
        lh_enlarge_aabb_point(prim->aabb, p[1]);
        lh_enlarge_aabb_point(prim->aabb, p[2]);

		prim->c[0] = 0.5f * (prim->aabb[0] + prim->aabb[0 + 3]);
		prim->c[1] = 0.5f * (prim->aabb[1] + prim->aabb[1 + 3]);
		prim->c[2] = 0.5f * (prim->aabb[2] + prim->aabb[2 + 3]);

        lh_enlarge_aabb_point(c_aabb, prim->c);

        prim->cone = lh_triangle_to_cone(p[0], p[1], p[2]);
    }


    lh_bin_t *bins[3];
    lh_bin_t *a_bins[3][2];
    for (int d = 0; d < 3; d++)
    {
        bins[d] = calloc(num_bins, sizeof(lh_bin_t));
        for (int i = 0; i < 2; i++)
            a_bins[d][i] = calloc(num_bins, sizeof(lh_bin_t));
    }

    lh_child_t child;
    lh_build_binned_rec(lh, &child, 0, num_prims, prims, num_bins, bins, a_bins, c_aabb, 0);

    for (int d = 0; d < 3; d++)
    {
        free(bins[d]);
        for (int i = 0; i < 2; i++)
            free(a_bins[d][i]);
    }
    free(prims);

	lh_compactify(lh, dst, positions, colors);

    free(lh->nodes);

	return lh->num_nodes;
}

void
lh_dump(light_hierarchy_t *lh, const char *path)
{
    FILE *f = fopen(path, "wb");
    int vcnt = 1;
    for(int i = 0; i < lh->num_nodes; i++)
    {
        lh_node_t *n = lh->nodes + i;

        for(int j=0;j<8;j++)
        {
            fprintf(f, "v %f %f %f\n", n->c[0].aabb[0 + (j & 1) * 3], n->c[0].aabb[1 + 3 * !!(j & 2)], n->c[0].aabb[2 + 3 * !!(j & 4)]);
        }

        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 1, vcnt + 5, vcnt + 4);//bottom
        fprintf(f, "f %d %d %d %d\n", vcnt + 2, vcnt + 3, vcnt + 7, vcnt + 6);//top
        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 1, vcnt + 3, vcnt + 2);//front
        fprintf(f, "f %d %d %d %d\n", vcnt + 4, vcnt + 5, vcnt + 7, vcnt + 6);//back
        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 4, vcnt + 6, vcnt + 2);//left
        fprintf(f, "f %d %d %d %d\n", vcnt + 1, vcnt + 5, vcnt + 7, vcnt + 3);//right
        vcnt += 8;

        for(int j=0;j<8;j++)
        {
            fprintf(f, "v %f %f %f\n", n->c[1].aabb[0 + (j & 1) * 3], n->c[1].aabb[1 + 3 * !!(j & 2)], n->c[1].aabb[2 + 3 * !!(j & 4)]);
        }

        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 1, vcnt + 5, vcnt + 4);//bottom
        fprintf(f, "f %d %d %d %d\n", vcnt + 2, vcnt + 3, vcnt + 7, vcnt + 6);//top
        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 1, vcnt + 3, vcnt + 2);//front
        fprintf(f, "f %d %d %d %d\n", vcnt + 4, vcnt + 5, vcnt + 7, vcnt + 6);//back
        fprintf(f, "f %d %d %d %d\n", vcnt    , vcnt + 4, vcnt + 6, vcnt + 2);//left
        fprintf(f, "f %d %d %d %d\n", vcnt + 1, vcnt + 5, vcnt + 7, vcnt + 3);//right
        vcnt += 8;
    }
    fclose(f);
}

VkResult
vkpt_lh_upload_staging(VkCommandBuffer cmd_buf, int num_nodes)
{
	assert(!qvk.buf_vertex_staging.is_mapped);
#if 0
	VkCommandBufferAllocateInfo cmd_alloc = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool        = qvk.command_pool,
		.commandBufferCount = 1,
	};

	VkCommandBuffer cmd_buf;
	vkAllocateCommandBuffers(qvk.device, &cmd_alloc, &cmd_buf);
	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd_buf, &cmd_begin_info);
#endif

	VkBufferCopy copyRegion = {
		.size = num_nodes * sizeof(compact_lh_node_t)
	};
	vkCmdCopyBuffer(cmd_buf,
		buf_light_hierarchy_staging[qvk.current_image_index].buffer,
		buf_light_hierarchy[qvk.current_image_index].buffer,
		1, &copyRegion);

#if 0
	vkEndCommandBuffer(cmd_buf);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};

	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);

	vkQueueWaitIdle(qvk.queue_graphics);
#endif

	return VK_SUCCESS;
}

VkResult
vkpt_lh_update(
		const float *positions,
		const uint32_t *light_colors,
		int num_primitives,
		VkCommandBuffer cmd_buf)
{
	//struct timeval  tv1, tv2;
	//gettimeofday(&tv1, NULL);
	void *lh = buffer_map(buf_light_hierarchy_staging + qvk.current_image_index);
	int num_nodes = lh_build_binned(lh, positions, light_colors, num_primitives, 8);
	lh = NULL;
	buffer_unmap(buf_light_hierarchy_staging + qvk.current_image_index);

	VkResult res = vkpt_lh_upload_staging(cmd_buf, num_nodes);

//	gettimeofday(&tv2, NULL);
//
//	printf ("Total time = %f seconds\n",
//			 (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
//			 (double) (tv2.tv_sec - tv1.tv_sec));
//

	return res;
}

VkResult
vkpt_lh_initialize()
{
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		_VK(buffer_create(buf_light_hierarchy_staging + i,
			sizeof(lh_node_t) * MAX_LIGHT_HIERARCHY_NODES, 
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
		_VK(buffer_create(buf_light_hierarchy + i,
			sizeof(lh_node_t) * MAX_LIGHT_HIERARCHY_NODES, 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
	}


	VkDescriptorSetLayoutBinding layout_bindings[] = {
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding         = 0,
			.stageFlags      = VK_SHADER_STAGE_ALL,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(layout_bindings),
		.pBindings    = layout_bindings,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL,
		&qvk.desc_set_layout_light_hierarchy));

	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = MAX_SWAPCHAIN_IMAGES,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = MAX_SWAPCHAIN_IMAGES,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_light_hierarchy));

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
			.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool     = desc_pool_light_hierarchy,
			.descriptorSetCount = 1,
			.pSetLayouts        = &qvk.desc_set_layout_light_hierarchy,
		};

		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, qvk.desc_set_light_hierarchy + i));

		VkDescriptorBufferInfo buf_info = {
			.buffer = buf_light_hierarchy[i].buffer,
			.offset = 0,
			.range  = buf_light_hierarchy[i].size,
		};

		VkWriteDescriptorSet output_buf_write = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = qvk.desc_set_light_hierarchy[i],
			.dstBinding      = 0,
			.dstArrayElement = 0,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo     = &buf_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);
	}
	return VK_SUCCESS;
}

VkResult
vkpt_lh_destroy()
{
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		buffer_destroy(buf_light_hierarchy_staging + i);
		buffer_destroy(buf_light_hierarchy + i);
	}
	vkDestroyDescriptorPool(qvk.device, desc_pool_light_hierarchy, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, qvk.desc_set_layout_light_hierarchy, NULL);
	return VK_SUCCESS;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
