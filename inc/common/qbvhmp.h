/*
    This file is part of corona-13.

    corona-13 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    corona-13 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with corona-13. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include "threads.h"
#include <stdio.h>
#include <stdint.h>
#include <xmmintrin.h>

// will keep some stats and print them at the end:
// #define ACCEL_DEBUG
// don't do motion blur:
#define ACCEL_STATIC
// number of split planes tested for surface area heuristic:
#define SAH_TESTS 7

typedef union
{
  __m128 m;
  float f[4];
  unsigned int i[4];
}
qbvh_float4_t;

typedef struct
{
#ifdef ACCEL_STATIC
  uint32_t paabbx, paabby, paabbz;
  uint32_t aabb_mx, aabb_my, aabb_mz, aabb_Mx, aabb_My, aabb_Mz;
  uint32_t pad0, pad1, pad2;

  // uint16_t paabb[6];   // more parent box in uint16_t
  // uint8_t aabb[6][4];  // 8-bit quantised child boxes
  // uint32_t pad[3];

  // qbvh_float4_t aabb0[6];
  // TODO: we'll get away with very few bits here, say 16 per child
  // TODO: use the rest for the axis bits
  //            1    2     24     5
  // child is | 1 | ax | prim | cnt | for leaves and
  //          | 0 | ax |      child | for inner
  uint32_t child[4];  // child index or, if leaf -(prim<<5|num_prims) index
#else // with motion blur: double a regular qbvh node, 256 bytes
  qbvh_float4_t aabb0[6];
  qbvh_float4_t aabb1[6];
  uint64_t child[4];  // child index or, if leaf -(prim<<5|num_prims) index
  uint64_t parent;
  int64_t axis0;
  int64_t axis00;
  int64_t axis01;
#endif
}
qbvh_node_t;

#ifdef ACCEL_DEBUG
typedef struct accel_debug_t
{
  uint64_t aabb_intersect;
  uint64_t prims_intersect;
  uint64_t accel_intersect;
  uint64_t aabb_true;
}
accel_debug_t;
#endif

typedef enum job_type_t
{
  s_job_all = 0,
  s_job_node,
  s_job_split,
  s_job_scan,
  s_job_swap
}
job_type_t;

// shared struct for split job
typedef struct shared_split_job_t
{
  int binmin[SAH_TESTS+1];
  int binmax[SAH_TESTS+1];
  float bound_m[SAH_TESTS+1];
  float bound_M[SAH_TESTS+1];
  int64_t left;  // prim index bounds
  int64_t right;
  int64_t step;  // step size
  int64_t done;  // thread counter
  int p, q, d;   // split dim and the other two (permutation of 0 1 2)
  float aabb0, aabb1;
}
shared_split_job_t;

typedef struct job_t
{
  job_type_t type;
  union
  {
    struct
    {
      qbvh_node_t *node;
      qbvh_node_t *parent;
      int64_t left;
      int64_t right;
      float paabb[6];
      int depth;
      int child;
    }
    node;
    struct
    {
      shared_split_job_t *job;
      int64_t left;
      int64_t right;
    }
    split;
    struct
    {
      uint64_t begin, back;
      int axis;
      float split;
      // output:
      int64_t *right;
      float *aabbl, *aabbr;
      int *done;
    }
    scan;
    struct
    {
      uint64_t read, num_read;          // indices to read from
      uint64_t write;                   // current write index
      uint64_t read_block, write_block; // block id of read/write indices
      uint64_t *b_l, *b_r, *b_e;        // pointers to shared thread blocks
      int *done;
    }
    swap;
  };
}
job_t;

typedef struct queue_t
{
  pthread_mutex_t mutex;
  job_t *jobs;
  int32_t num_jobs;
}
queue_t;

typedef struct accel_t
{
  queue_t *queue;
  uint64_t built;
  pthread_mutex_t mutex;
  threads_t *threads;

  float *prim_aabb;     // cached prim aabb, allocated from outside
  uint32_t *primid;     // primid array, passed in, too
  uint32_t num_prims;   // number of primitives

  uint64_t num_nodes;
  uint64_t node_bufsize;
  float aabb[6];
  qbvh_node_t *tree;

  uint32_t *shadow_cache;
  uint32_t shadow_cache_last;
#ifdef ACCEL_DEBUG
  accel_debug_t *debug;
#endif
}
accel_t;

// init new acceleration structure for given primitives (don't build yet)
accel_t* accel_init(
    float *aabb,           // bounding boxes of primitives
    uint32_t *primid,      // primitive id array
    const int num_prims,   // number of primitives
    threads_t *t);         // thread pool to use during construction

// free memory
void accel_cleanup(accel_t *b);

// build acceleration structure
void accel_build(accel_t *b);

// build in the background
void accel_build_async(accel_t *b);

// block until the background threads have finished working
void accel_build_wait(accel_t *b);

#if 0 // disabled for now, we want to ray trace on GPU
// intersect ray (closest point)
void accel_intersect(const accel_t *b, const ray_t *ray, hit_t *hit);

// test visibility up to max distance
int  accel_visible(const accel_t *b, const ray_t *ray, const float max_dist);

// find closest geo intersection point to world space point at ray with parameter centre:
// ray->pos + centre * ray->dir
void accel_closest(const accel_t *b, ray_t *ray, hit_t *hit, const float centre);

// return pointer to the 6-float minxyz-maxxyz aabb
const float *accel_aabb(const accel_t *b);
#endif
