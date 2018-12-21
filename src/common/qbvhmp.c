/*
    This file is part of corona-13.

    copyright (c) 2016 johannes hanika.

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

#include "common/qbvhmp.h"
#include "shared/shared.h"
#include "threads.h"

#include <math.h>
#include <float.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <xmmintrin.h>

#ifdef ACCEL_NO_VLA
#define PER_THREAD_VLA(x) 1024 // ((assert(x<=1024)),1024)
#else
#define PER_THREAD_VLA(x) x
#endif

// enforced during construction:
#define MAX_TREE_DEPTH 100
// skip a couple of prims for SAH evaluation on the top levels:
#define SAH_LOG_STEP 3
// number of primitives per leaf has to be < 32
#define NUM_TRIS_PER_LEAF 2
// minimum primitives per thread to parallelise split plane search:
#define NUM_PRIMS_PER_THREAD 100ul
// turn off to compute more honest bvh boxes. being honest slows everything down it seems
#define ACCEL_KD_SPLIT
// dump some stats at the end:
// #define ACCEL_STATS

typedef struct
{
  int64_t num_tris;
  int64_t max_tris_leaf;
  int64_t num_nodes;
  int64_t max_depth;
  int64_t num_leaves;
  int64_t sum_depth;
  int64_t num_empties;
  float parent_box;
  float SA;
}
qbvh_stats_t;

void accel_cleanup(accel_t *b)
{
  if(!b) return;
#ifdef ACCEL_DEBUG
  for(int t=0;t<b->threads->num_threads;t++)
    fprintf(stderr, "[accel stats] thread %d accel_intersect: %lu aabb_intersect %lu / %lu prims_intersect %lu\n", t,
        b->debug[t].accel_intersect, b->debug[t].aabb_true, b->debug[t].aabb_intersect, b->debug[t].prims_intersect);
  free(b->debug);
#endif
  aligned_free(b->tree);
  if(b->shadow_cache) free(b->shadow_cache);
  for(int t=0;t<b->threads->num_threads;t++)
  {
    threads_mutex_destroy(&b->queue[t].mutex);
    free(b->queue[t].jobs);
  }
  free(b->queue);
  b->queue = 0;
  threads_mutex_destroy(&b->mutex);
  free(b);
}

#ifdef ACCEL_STATS
static void checktree(accel_t *b, qbvh_node_t *node, qbvh_stats_t *stats, const int depth)
{
  if(depth == 0) stats->parent_box = (b->aabb[5] - b->aabb[2])*(b->aabb[4] - b->aabb[1]) + (b->aabb[3] - b->aabb[0])*(b->aabb[4] - b->aabb[1]) + (b->aabb[5] - b->aabb[2])*(b->aabb[3] - b->aabb[0]);
  const float pbox = stats->parent_box;
  int child_tris[4] = {0,0,0,0};
  float child_box[4];
  for(int c=0;c<4;c++)
    child_box[c] = (node->aabb0[5].f[c] - node->aabb0[2].f[c])*(node->aabb0[4].f[c] - node->aabb0[1].f[c]) +
                   (node->aabb0[3].f[c] - node->aabb0[0].f[c])*(node->aabb0[4].f[c] - node->aabb0[1].f[c]) +
                   (node->aabb0[5].f[c] - node->aabb0[2].f[c])*(node->aabb0[3].f[c] - node->aabb0[0].f[c]);
  stats->num_nodes++;
  // recurse in all children, collect num tris etc.
  int axis0  = (node->child[0] >> 29) & 3;
  int axis00 = (node->child[1] >> 29) & 3;
  int axis01 = (node->child[2] >> 29) & 3;
  assert(axis0  >= 0 && axis0  < 3);
  assert(axis00 >= 0 && axis00 < 3);
  assert(axis01 >= 0 && axis01 < 3);
  // assert(node - b->tree == 0 || (node->parent != -1 && node->parent < b->num_nodes));
  for(int c=0;c<4;c++)
  {
    uint64_t tris_before = stats->num_tris;
    // leaf?
    if(node->child[c] & (1u<<31))
    {
      stats->num_leaves++;
      stats->sum_depth += depth;
      if(depth > stats->max_depth) stats->max_depth = depth;
      const int64_t num_prims = node->child[c] & ~-(1<<5);
      const int64_t prims = (node->child[c] & 0x1fffffffu) >> 5;
      stats->max_tris_leaf = MAX(num_prims, stats->max_tris_leaf);
      if(num_prims > 0)
      {
        stats->num_tris += num_prims;
        if(prims + num_prims > b->num_prims) fprintf(stderr, "[accel] ERR: prims index out of range!\n");
      }
      else stats->num_empties++;
    }
    else
    {
      if(child_box[c] > 0) stats->parent_box = child_box[c];
      else                 stats->parent_box = 0.0f;
      qbvh_node_t *child = b->tree + (node->child[c] & 0x1fffffffu);
      checktree(b, child, stats, depth + 1);
    }
    child_tris[c] = stats->num_tris - tris_before;
    if(isfinite(child_box[c]) && isfinite(pbox)) stats->SA += (child_tris[c]*child_box[c])/pbox;
  }
}
#endif

static inline void accel_refit(accel_t *b, qbvh_node_t *node)
{
#ifndef ACCEL_STATIC
  // ideally this would catch all `root node is a leaf' cases:
  if(b->prims->num_prims == 0) return;
  for(int c=0;c<4;c++)
  {
    // leaves are already done, process inner only:
    if(!(node->child[c] & (1ul<<63)))
    {
      qbvh_node_t *child = b->tree + node->child[c];
      accel_refit(b, child);
      // set aabb1[c] to fit the four boxes of child[c]
      for(int d=0;d<6;d++)
        node->aabb1[d].f[c] = child->aabb1[d].f[0];
      for(int k=1;k<4;k++)
        for(int d=0;d<3;d++)
        {
          node->aabb1[d].f[c] = MIN(node->aabb1[d].f[c], child->aabb1[d].f[k]);
          node->aabb1[3+d].f[c] = MAX(node->aabb1[3+d].f[c], child->aabb1[3+d].f[k]);
        }
    }
  }
#endif
}

accel_t* accel_init(
    float *aabb,           // bounding boxes of primitives
    uint32_t *primid,      // primitive id array
    const int num_prims,   // number of primitives
    threads_t *t)          // thread pool to use during construction
{
  accel_t *b = malloc(sizeof(accel_t));
#ifdef ACCEL_DEBUG
  b->debug = malloc(sizeof(accel_debug_t)*b->threads->num_threads);
  memset(b->debug, 0, sizeof(accel_debug_t)*b->threads->num_threads);
#endif
  b->shadow_cache = NULL;
  b->aabb[0] = b->aabb[1] = b->aabb[2] = FLT_MAX;
  b->aabb[3] = b->aabb[4] = b->aabb[5] = - FLT_MAX;

  b->threads = t;
  b->num_prims = num_prims;
  b->prim_aabb = aabb;
  b->primid = primid;

  b->num_nodes = 0;
  b->node_bufsize = b->num_prims > 100 ? 4*b->num_prims: 100;
  b->tree = aligned_alloc(128, b->node_bufsize * sizeof(qbvh_node_t));
  if(!b->tree)
    fprintf(stderr, "qbvh could not allocate enough memory for node buffer!\n");

  // init shadow cache:
  b->shadow_cache_last = (1<<12) - 1;
  b->shadow_cache = malloc(b->threads->num_threads * sizeof(unsigned int) * (b->shadow_cache_last+1));
  memset(b->shadow_cache, 0, b->threads->num_threads * sizeof(unsigned int) * (b->shadow_cache_last+1));

  // init temp memory for scheduler
  threads_mutex_init(&b->mutex, 0);
  b->queue = malloc(sizeof(queue_t)*b->threads->num_threads);
  for(int t=0;t<b->threads->num_threads;t++)
  {
    threads_mutex_init(&b->queue[t].mutex, 0);
    b->queue[t].jobs = malloc(sizeof(job_t)*MAX_TREE_DEPTH * 3  + b->threads->num_threads);
    b->queue[t].num_jobs = 0;
  }

  return b;
}

void split_job_work(
    accel_t *b,
    shared_split_job_t *const job,
    const int64_t left,
    const int64_t right)
{
  const float width = job->aabb1 - job->aabb0;
  for(int64_t k=left;k<right;k+=job->step)
  {
    const float min = b->prim_aabb[6 * k + job->d];
    const float max = b->prim_aabb[6 * k + 3 + job->d];
    const int imin = (int)CLAMP((SAH_TESTS+1)*(min - job->aabb0)/width, 0, SAH_TESTS);
    const int imax = (int)CLAMP((SAH_TESTS+1)*(max - job->aabb0)/width, 0, SAH_TESTS);
    int cost = 8;
    // if(b->prims->primid[k].vcnt == 4) cost = 16;
    // else if(b->prims->primid[k].vcnt == 2) cost = 1;
    __sync_fetch_and_add(job->binmin+imin, cost);
    __sync_fetch_and_add(job->binmax+imax, cost);
#ifndef ACCEL_KD_SPLIT
    for(int i=imin;i<=imax;i++)
    {
      common_atomic_min(job->bound_m+i, min);
      common_atomic_max(job->bound_M+i, max);
    }
#endif
  }
  __sync_fetch_and_add(&job->done, 1);
}

static uint64_t node_job_work(
    accel_t *b,
    qbvh_node_t *node,
    const int64_t left,
    const int64_t right,
    float *paabb,
    const int depth,
    qbvh_node_t *parent,
    const int child);
static void scan_job_work(accel_t *a, job_t *j);
static void swap_job_work(accel_t *a, job_t *j);

static int do_one_job(accel_t *a, job_type_t mask)
{
  int t = threads_id;
  int tries = 0;
  job_t job;
  while(1)
  {
    threads_mutex_lock(&a->queue[t].mutex);
    int gotjob = a->queue[t].num_jobs > 0;
    if(gotjob)
      gotjob = (mask == s_job_all) || (a->queue[t].jobs[a->queue[t].num_jobs-1].type != s_job_node);
    if(gotjob)
    {
      job = a->queue[t].jobs[--a->queue[t].num_jobs]; // take copy for sync
      threads_mutex_unlock(&a->queue[t].mutex);
      break;
    }
    else
    {
      threads_mutex_unlock(&a->queue[t].mutex);
      if(a->built >= a->num_prims) return 1; // reading is atomic
      // search the other queues:
      t = (t+1)%a->threads->num_threads;
      if(tries ++ >= a->threads->num_threads)
      {
        usleep(100);
        return 0;
      }
      continue;
    }
  }
  switch(job.type)
  {
    case s_job_split:
      split_job_work(a, job.split.job, job.split.left, job.split.right);
      break;
    case s_job_node:
    {
      const uint64_t done = node_job_work(a, job.node.node, job.node.left, job.node.right, job.node.paabb, job.node.depth, job.node.parent, job.node.child);
      __sync_fetch_and_add(&a->built, done);
      break;
    }
    case s_job_scan:
      scan_job_work(a, &job);
      break;
    case s_job_swap:
      swap_job_work(a, &job);
      break;
    default: // to formally handle s_job_all
      break;
  }
  return 0;
}

static float accel_get_split_with_dim(
    accel_t *b,
    const int64_t left_in,
    const int64_t right_in,
    const float aabb[6],
    const int d,
    float *split)
{
  assert(right_in >= left_in);
  //*split = 0.5f*(aabb[3+d] + aabb[d]);
  //return 0.0f;

  float best = (aabb[d] + aabb[3+d])*0.5f;
  float best_score = FLT_MAX;
  if(right_in == left_in)
  {
    *split = best;
    return best_score;
  }

  shared_split_job_t job;
  memset(&job, 0, sizeof(job));

  job.d = d;
  job.p = d == 2 ? 0 : d+1;
  job.q = d == 0 ? 2 : d-1;
  job.left  = left_in;
  job.right = right_in;
  job.step = (int)(log10f(SAH_LOG_STEP*(right_in - left_in) + 1.0f) + 1.0f);
  job.aabb0 = aabb[d];
  job.aabb1 = aabb[3+d];
#ifndef ACCEL_KD_SPLIT
  for(int k=0;k<SAH_TESTS+1;k++)
  {
    job.bound_m[k] =   FLT_MAX;
    job.bound_M[k] = - FLT_MAX;
  }
#endif

  if(right_in - left_in > b->threads->num_threads * NUM_PRIMS_PER_THREAD)
  { // run in parallel
    const int tid = threads_id;
    queue_t *q = b->queue+tid;
    threads_mutex_lock(&q->mutex);
    for(int t=0;t<b->threads->num_threads;t++)
    {
      int j = q->num_jobs++;
      q->jobs[j].type = s_job_split;
      q->jobs[j].split.left  = left_in + (int64_t)( t   /(double)b->threads->num_threads * (right_in-left_in));
      q->jobs[j].split.right = left_in + (int64_t)((t+1)/(double)b->threads->num_threads * (right_in-left_in));
      q->jobs[j].split.job = &job;
      assert(q->jobs[j].split.left < q->jobs[j].split.right);
      assert(q->num_jobs <= 3*MAX_TREE_DEPTH + b->threads->num_threads);
    }
    threads_mutex_unlock(&q->mutex);
    // // keep the t=0 one for us:
    // split_job_work(b, &job,
    //   left_in + (int64_t)( 0   /(double)b->threads->num_threads * (right_in-left_in)),
    //   left_in + (int64_t)((0+1)/(double)b->threads->num_threads * (right_in-left_in)));
    // work some more until done:
    while(job.done < b->threads->num_threads) do_one_job(b, s_job_split);
  }
  else
  { // do it all ourselves
    split_job_work(b, &job, job.left, job.right);
  }

  // this is kd-code ;)
  int left = job.binmin[0];
  int right = 0;
  const float com = (aabb[3+job.p] - aabb[job.p])*(aabb[3+job.q] - aabb[job.q]);
  const float width = aabb[3+d] - aabb[d];
  for(int i=1;i<SAH_TESTS+1;i++) right += job.binmax[i];
  for(int k=0;k<SAH_TESTS;k++)
  {
    float splitc = aabb[job.d] + (k+1)*width/(SAH_TESTS + 1.0f);
#ifdef ACCEL_KD_SPLIT
    float splitwl = splitc - aabb[job.d];
    float splitwr = aabb[3+job.d] - splitc;
#else
    float splitwl = job.bound_M[k] - aabb[job.d];
    float splitwr = aabb[3+job.d] - job.bound_m[k];
#endif
    float stepl = com +
        (aabb[3+job.p] - aabb[job.p])*splitwl +
        (aabb[3+job.q] - aabb[job.q])*splitwl;
    float stepr = com +
        (aabb[3+job.p] - aabb[job.p])*splitwr +
        (aabb[3+job.q] - aabb[job.q])*splitwr;
    float score = stepl*left + stepr*right;
    if(score < best_score)
    {
      best_score = score;
      best = splitc;
    }
    left  += job.binmin[k+1];
    right -= job.binmax[k+1];
  }
  *split = best;
  return best_score;
}

// return start of right block after sort
static uint64_t sort_primids_helper(
    accel_t *b,
    const int axis,      // split plane dimension [0 1 2]
    const float split,   // split plane coordinate
    const int64_t begin, // begin of buffer
    const int64_t back,  // element after buffer
    float *aabbl,        // will be bounding box of left child
    float *aabbr)        // will be bounding box of right child
{
  int64_t right = back; // start of right block
  for(int k=0;k<3;k++) aabbl[k] = aabbr[k] =  FLT_MAX;
  for(int k=3;k<6;k++) aabbl[k] = aabbr[k] = -FLT_MAX;
  float pmin, pmax;
  for(int64_t i=begin;i<right;)
  {
    pmin = b->prim_aabb[6 * i + axis];
    pmax = b->prim_aabb[6 * i + 3 + axis];
    if(.5f*(pmin + pmax) >= split)
    { // prim is right
      // update right bounding box:
      for(int k=0;k<3;k++) aabbr[k] = MIN(aabbr[k], b->prim_aabb[6*i+k]);
      for(int k=3;k<6;k++) aabbr[k] = MAX(aabbr[k], b->prim_aabb[6*i+k]);
      // swap bufs:
      uint32_t tmp = b->primid[i];
      b->primid[i] = b->primid[--right];
      b->primid[right] = tmp;
      for(int k=0;k<6;k++)
      {
        float tmp = b->prim_aabb[6*i+k];
        b->prim_aabb[6*i+k] = b->prim_aabb[6*right+k];
        b->prim_aabb[6*right+k] = tmp;
      }
    }
    else
    { // primitive is left
      // update left bounding box
      for(int k=0;k<3;k++) aabbl[k] = MIN(aabbl[k], b->prim_aabb[6*i+k]);
      for(int k=3;k<6;k++) aabbl[k] = MAX(aabbl[k], b->prim_aabb[6*i+k]);
      i++; // buffers are all good already
    }
  }
  return right;
}

static void scan_job_work(accel_t *a, job_t *j)
{
  *j->scan.right = sort_primids_helper(a, j->scan.axis, j->scan.split,
      j->scan.begin, j->scan.back, j->scan.aabbl, j->scan.aabbr);
  __sync_fetch_and_add(j->scan.done, 1);
}

static void swap_job_work(accel_t *a, job_t *j)
{
  uint64_t r = j->swap.read - 1; // will increment first thing below
  uint64_t w = j->swap.write - 1;
  uint64_t rb = j->swap.read_block;
  uint64_t wb = j->swap.write_block;
  uint64_t rB = j->swap.b_e[j->swap.read_block];
  uint64_t wB = j->swap.b_r[j->swap.write_block];
  // fprintf(stderr, "swapping %lu elements from blocks %lu (%lu) <-> %lu (%lu)\n", j->swap.num_read, rb, r+1, wb, w+1);
  for(uint64_t i=0;i<j->swap.num_read;i++)
  {
    if(++r >= rB)
    { // move to next read block (read dislocated right prims)
      while(++rb<a->threads->num_threads)
      { // jump over empty blocks
        assert(rb < a->threads->num_threads);
        r  = j->swap.b_r[rb];
        rB = j->swap.b_e[rb];
        // fprintf(stderr, "r=%lu moving read block to %lu\n", r, rb);
        if(r < rB) break;
      }
    }
    assert(rb < a->threads->num_threads);
    assert(r < rB);
    if(++w >= wB)
    { // move to next write block (write dislocated left prims)
      while(++wb<a->threads->num_threads)
      { // jump over empty blocks
        assert(wb < a->threads->num_threads);
        w  = j->swap.b_l[wb];
        wB = j->swap.b_r[wb];
        // fprintf(stderr, "w=%lu moving write block to %lu\n", w, wb);
        if(w < wB) break;
      }
    }
    assert(wb < a->threads->num_threads);
    assert(w < wB);

    // swap primid and aabb
    uint32_t tmp = a->primid[r];
    a->primid[r] = a->primid[w];
    a->primid[w] = tmp;
    for(int k=0;k<6;k++)
    {
      float tmp = a->prim_aabb[6*r+k];
      a->prim_aabb[6*r+k] = a->prim_aabb[6*w+k];
      a->prim_aabb[6*w+k] = tmp;
    }
  }
  __sync_fetch_and_add(j->swap.done, 1);
}

// sort primid indices in parallel
static uint64_t sort_primids_par(
    accel_t *b,
    const int axis,      // split plane dimension [0 1 2]
    const float split,   // split plane coordinate
    const int64_t begin, // begin of buffer
    const int64_t back,  // element after buffer
    float *aabblo,       // will be bounding box of left child
    float *aabbro)       // will be bounding box of right child
{
  // sort_tris with couple of threads for couple of blocks between [begin..back]
  // TODO: if we knew the max num threads it would be faster to alloc these arrays as such:
  int64_t right[PER_THREAD_VLA(b->threads->num_threads)];
  float aabbl[PER_THREAD_VLA(b->threads->num_threads)][6];
  float aabbr[PER_THREAD_VLA(b->threads->num_threads)][6];
  uint64_t b_l[PER_THREAD_VLA(b->threads->num_threads)], b_r[PER_THREAD_VLA(b->threads->num_threads)], b_e[PER_THREAD_VLA(b->threads->num_threads)];

  int done = 0;
  queue_t *q = b->queue+threads_id;
  threads_mutex_lock(&q->mutex);
  for(int t=0;t<b->threads->num_threads;t++)
  {
    int j = q->num_jobs++;
    assert(j < 3*MAX_TREE_DEPTH + b->threads->num_threads);
    job_t *job = q->jobs + j;
    job->type = s_job_scan;
    job->scan.right = right+t;
    job->scan.axis = axis;
    job->scan.split = split;
    job->scan.begin = begin + (uint64_t)( t   /(double)b->threads->num_threads*(back-begin));
    job->scan.back  = begin + (uint64_t)((t+1)/(double)b->threads->num_threads*(back-begin));
    b_l[t] = job->scan.begin;
    b_e[t] = job->scan.back;
    job->scan.aabbl = aabbl[t];
    job->scan.aabbr = aabbr[t];
    job->scan.done = &done;
  }
  threads_mutex_unlock(&q->mutex);

  // work while waiting
  while(done < b->threads->num_threads) do_one_job(b, s_job_scan);

  // merge aabb into slot 0
  for(int k=0;k<6;k++) aabblo[k] = aabbl[0][k];
  for(int k=0;k<6;k++) aabbro[k] = aabbr[0][k];
  for(int t=1;t<b->threads->num_threads;t++)
  {
    for(int k=0;k<3;k++) aabblo[k] = MIN(aabblo[k], aabbl[t][k]);
    for(int k=3;k<6;k++) aabblo[k] = MAX(aabblo[k], aabbl[t][k]);
    for(int k=0;k<3;k++) aabbro[k] = MIN(aabbro[k], aabbr[t][k]);
    for(int k=3;k<6;k++) aabbro[k] = MAX(aabbro[k], aabbr[t][k]);
  }

  uint64_t num_left = 0, num_right = 0;
  for(int t=0;t<b->threads->num_threads;t++)
  {
    num_left  += right[t] - b_l[t];
    num_right += b_e[t] - right[t];
    b_r[t] = right[t];
  }

  // then merge the blocks by copying the data again :(
  // list will look something like this (blocked by thread):
  // [ l r | l r | l r | .. | l r ]
  // then determine globally from reduction:
  // [ sum(l)       | sum (r)     ]
  // and swap all displaced r and l blocks:
  // generate global swap lists and parallelise displaced (r,l) pair indices
  uint64_t num_dislocated = 0;
  for(int t=0;t<b->threads->num_threads;t++)
  { // count all primitives classified `right' which are left of the pivot index:
    if(b_r[t] - b_l[0] >= num_left) break;
    num_dislocated += MIN(num_left, b_e[t] - b_l[0]) - (b_r[t] - b_l[0]);
  }

  // XXX TODO: find out how often this really happens.
  // XXX i think the morton order of geo verts may be upside down.
  if(num_dislocated == 0) return begin + num_left; // yay.
  // single threaded construction never gets here.

  // submit swap_job_t
  done = 0;
  threads_mutex_lock(&q->mutex);
  uint64_t read = b_r[0];
  uint64_t write = -1;
  uint64_t read_block = 0, write_block = 0;
  for(int t=0;t<b->threads->num_threads;t++)
  {
    if(b_r[t] - b_l[0] > num_left)
    {
      write_block = t;
      write = MAX(b_l[0] + num_left, b_l[t]);
      break;
    }
  }
  for(int t=0;t<b->threads->num_threads;t++)
  {
    int j = q->num_jobs++;
    assert(j < 3*MAX_TREE_DEPTH + b->threads->num_threads);
    job_t *job = q->jobs + j;
    job->type = s_job_swap;
    uint64_t beg = ( t     *num_dislocated)/b->threads->num_threads;
    uint64_t end = ((t+1ul)*num_dislocated)/b->threads->num_threads;
    job->swap.read  = read;
    job->swap.write = write;
    job->swap.read_block  = read_block;
    job->swap.write_block = write_block;
    job->swap.num_read = end - beg;
    job->swap.b_l = b_l;
    job->swap.b_r = b_r;
    job->swap.b_e = b_e;
    job->swap.done = &done;
    int64_t cnt = end - beg;
    // skip for last iteration:
    if(t == b->threads->num_threads-1) break;
    do
    { // jump over empty blocks
      int64_t remaining = b_e[read_block] - read;
      if(remaining > cnt)
      {
        read += cnt;
        cnt = 0;
      }
      else
      {
        cnt -= remaining;
        read_block++;
        assert(read_block < b->threads->num_threads);
        read = b_r[read_block];
      }
    }
    while(cnt > 0);
    cnt = end - beg;
    do
    { // jump over empty blocks
      int64_t remaining = b_r[write_block] - write;
      if(remaining > cnt)
      {
        write += cnt;
        cnt = 0;
      }
      else
      {
        cnt -= remaining;
        write_block++;
        assert(write_block < b->threads->num_threads);
        write = b_l[write_block];
      }
    }
    while(cnt > 0);
  }
  threads_mutex_unlock(&q->mutex);

  // work while waiting
  while(done < b->threads->num_threads) do_one_job(b, s_job_swap);

  return begin + num_left;
}

// returns pivot element in sorted list where the split occurs begin<pivot<back
static uint64_t sort_primids(
    accel_t *b,
    const int axis,      // split plane dimension [0 1 2]
    const float split,   // split plane coordinate
    const int64_t begin, // begin of buffer
    const int64_t back,  // element after buffer
    float *aabblo,       // will be bounding box of left child
    float *aabbro)       // will be bounding box of right child
{
  if(b->threads->num_threads > 1 && back - begin > b->threads->num_threads * 1000)
    return sort_primids_par(b, axis, split, begin, back, aabblo, aabbro);

  return sort_primids_helper(b, axis, split, begin, back, aabblo, aabbro);
}

static void bound_leaf_t1(accel_t *b, qbvh_node_t *node, int c)
{
#ifndef ACCEL_STATIC
  for(int k=0;k<3;k++) node->aabb1[k].f[c] =  FLT_MAX;
  for(int k=3;k<6;k++) node->aabb1[k].f[c] = -FLT_MAX;
  const uint64_t num_prims = node->child[c] & ~-(1<<5);
  const uint64_t prims = (node->child[c] ^ (1ul<<63)) >> 5;
  for(uint64_t k=prims;k<prims+num_prims;k++)
  {
    // get bounds from geometric primitive and refit shutter close box aabb1[c]
    for(int d=0;d<3;d++)
    {
      float m, M;
      prims_get_bounds_shutter_close(b->prims, b->prims->primid[k], d, &m, &M);
      node->aabb1[d].f[c] = MIN(node->aabb1[d].f[c], m);
      node->aabb1[3+d].f[c] = MAX(node->aabb1[3+d].f[c], M);
    }
  }
#endif
}

static uint64_t node_job_work(
    accel_t *b,
    qbvh_node_t *node,
    const int64_t left,
    const int64_t right,
    float *paabb,
    const int depth,
    qbvh_node_t *parent,
    const int child)
{
  // node->parent = parent - b->tree;
  // split axis0
  int axis0, axis00, axis01;
  uint64_t part[5];
  float split0, split1l, split1r;
  float aabb[4][6];
  part[0] = left;
  part[4] = right;
  axis0 = 0;
  float score, split;
  float best_score = accel_get_split_with_dim(b, left, right, paabb, 0, &split0);
  if((score = accel_get_split_with_dim(b, left, right, paabb, 1, &split)) < best_score) { best_score = score; split0 = split; axis0 = 1; }
  if((score = accel_get_split_with_dim(b, left, right, paabb, 2, &split)) < best_score) { best_score = score; split0 = split; axis0 = 2; }

#if 1
  if(best_score < 0.0f && (right - left) < 32)
  {
    // terminate
    // FIXME: wastes mem for already alloc'ed node :(
    if(parent)
    {
      fprintf(stderr, "making parent a leaf.\n");
      // keep axis bits (0x6) intact:
      parent->child[child] = (parent->child[child] & 0x60000000u) | (1u<<31) | (left<<5) | ((right - left) & ~-(1<<5));
      // parent->child[child] = (1ul<<63) | (left<<5) | ((right - left) & ~-(1<<5));
      bound_leaf_t1(b, parent, child);
      return right - left;
    }
  }
#endif

  // TODO: avoid stupid splits one by one!

  // split triangle list at axis0/split0.
  part[2] = sort_primids(b, axis0, split0, left, right, aabb[0], aabb[1]);

  // split axis1 (2x)
  axis00 = 0;
  best_score = accel_get_split_with_dim(b, left, part[2], aabb[0], 0, &split1l);
  if((score = accel_get_split_with_dim(b, left, part[2], aabb[0], 1, &split)) < best_score) { best_score = score; split1l = split; axis00 = 1; }
  if((score = accel_get_split_with_dim(b, left, part[2], aabb[0], 2, &split)) < best_score) { split1l = split; axis00 = 2; }
  axis01 = 0;
  best_score = accel_get_split_with_dim(b, part[2], right, aabb[1], 0, &split1r);
  if((score = accel_get_split_with_dim(b, part[2], right, aabb[1], 1, &split)) < best_score) { best_score = score; split1r = split; axis01 = 1; }
  if((score = accel_get_split_with_dim(b, part[2], right, aabb[1], 2, &split)) < best_score) { split1r = split; axis01 = 2; }

  part[1] = sort_primids(b, axis00, split1l, left, part[2], aabb[0], aabb[1]);
  part[3] = sort_primids(b, axis01, split1r, part[2], right, aabb[2], aabb[3]);

  if((right   - part[3] == (uint64_t)(right - left)) ||
     (part[3] - part[2] == (uint64_t)(right - left)) ||
     (part[2] - part[1] == (uint64_t)(right - left)) ||
     (part[1] - left    == (uint64_t)(right - left)))
  {
    // if((parent && (right - left > ~-(1<<5))) || !parent)
    if((parent && (right - left > NUM_TRIS_PER_LEAF)) || !parent)
    {
      // fprintf(stderr, "stupid split!\n");
      // split list in the middle, for 1tri/leaf case:
      part[2] = (right   + left)/2;
      part[3] = (right   + part[2])/2;
      part[1] = (part[2] + left)/2;
      // find aabbs
      // start with inverted parent aabb, so we can quantize later
      for(int k=0;k<3;k++) for(int i=0;i<4;i++) { aabb[i][k] = paabb[k+3]; aabb[i][k+3] = paabb[k]; }
      for(int p=0;p<4;p++)
      {
        for(uint64_t k=part[p];k<part[p+1];k++)
        {
          for(int i=0;i<3;i++)
          {
            const float min = b->prim_aabb[6*k+i];
            const float max = b->prim_aabb[6*k+3+i];
            if(min < aabb[p][i  ]) aabb[p][i  ] = min;
            if(max > aabb[p][i+3]) aabb[p][i+3] = max;
          }
        }
      }
    }
    else
    { // regular leaf node case:
      // fprintf(stderr, "stupid leaf!\n");
      parent->child[child] = (parent->child[child] & 0x60000000u) | (1u<<31) | (left<<5) | ((right - left) & ~-(1<<5));
      // parent->child[child] = (1ul<<63) | (left<<5) | ((right - left) & ~-(1<<5));
      bound_leaf_t1(b, parent, child);
      return right - left;
    }
  }

  node->child[0] = axis0 << 29;
  node->child[1] = axis00 << 29;
  node->child[2] = axis01 << 29;
  node->child[3] = 0;
  // node->axis0  = axis0;
  // node->axis00 = axis00;
  // node->axis01 = axis01;

  // quantize parent box:
  uint32_t pbx = CLAMP((int)floorf(0xffffu * (paabb[0] - b->aabb[0])/(b->aabb[3+0]-b->aabb[0])), 0, 0xffffu);
  uint32_t pby = CLAMP((int)floorf(0xffffu * (paabb[1] - b->aabb[1])/(b->aabb[3+1]-b->aabb[1])), 0, 0xffffu);
  uint32_t pbz = CLAMP((int)floorf(0xffffu * (paabb[2] - b->aabb[2])/(b->aabb[3+2]-b->aabb[2])), 0, 0xffffu);
  uint32_t pbX = CLAMP((int)ceilf (0xffffu * (paabb[3] - b->aabb[0])/(b->aabb[3+0]-b->aabb[0])), 0, 0xffffu);
  uint32_t pbY = CLAMP((int)ceilf (0xffffu * (paabb[4] - b->aabb[1])/(b->aabb[3+1]-b->aabb[1])), 0, 0xffffu);
  uint32_t pbZ = CLAMP((int)ceilf (0xffffu * (paabb[5] - b->aabb[2])/(b->aabb[3+2]-b->aabb[2])), 0, 0xffffu);
  node->paabbx = pbx | (pbX<<16);
  node->paabby = pby | (pbY<<16);
  node->paabbz = pbz | (pbZ<<16);

  float box[6] = {
    b->aabb[0] + pbx * (b->aabb[3]-b->aabb[0])/0xffffu,
    b->aabb[1] + pby * (b->aabb[4]-b->aabb[1])/0xffffu,
    b->aabb[2] + pbz * (b->aabb[5]-b->aabb[2])/0xffffu,
    b->aabb[0] + pbX * (b->aabb[3]-b->aabb[0])/0xffffu,
    b->aabb[1] + pbY * (b->aabb[4]-b->aabb[1])/0xffffu,
    b->aabb[2] + pbZ * (b->aabb[5]-b->aabb[2])/0xffffu};

  // quantize child boxes relative to quantized parent box:
  node->aabb_mx  = CLAMP((int)floorf(255*(aabb[0][0] - box[0])/(box[3+0]-box[0])), 0, 255);
  node->aabb_mx |= CLAMP((int)floorf(255*(aabb[1][0] - box[0])/(box[3+0]-box[0])), 0, 255)<<8;
  node->aabb_mx |= CLAMP((int)floorf(255*(aabb[2][0] - box[0])/(box[3+0]-box[0])), 0, 255)<<16;
  node->aabb_mx |= CLAMP((int)floorf(255*(aabb[3][0] - box[0])/(box[3+0]-box[0])), 0, 255)<<24;
  node->aabb_my  = CLAMP((int)floorf(255*(aabb[0][1] - box[1])/(box[3+1]-box[1])), 0, 255);
  node->aabb_my |= CLAMP((int)floorf(255*(aabb[1][1] - box[1])/(box[3+1]-box[1])), 0, 255)<<8;
  node->aabb_my |= CLAMP((int)floorf(255*(aabb[2][1] - box[1])/(box[3+1]-box[1])), 0, 255)<<16;
  node->aabb_my |= CLAMP((int)floorf(255*(aabb[3][1] - box[1])/(box[3+1]-box[1])), 0, 255)<<24;
  node->aabb_mz  = CLAMP((int)floorf(255*(aabb[0][2] - box[2])/(box[3+2]-box[2])), 0, 255);
  node->aabb_mz |= CLAMP((int)floorf(255*(aabb[1][2] - box[2])/(box[3+2]-box[2])), 0, 255)<<8;
  node->aabb_mz |= CLAMP((int)floorf(255*(aabb[2][2] - box[2])/(box[3+2]-box[2])), 0, 255)<<16;
  node->aabb_mz |= CLAMP((int)floorf(255*(aabb[3][2] - box[2])/(box[3+2]-box[2])), 0, 255)<<24;
  node->aabb_Mx  = CLAMP((int)ceilf (255*(aabb[0][3] - box[0])/(box[3+0]-box[0])), 0, 255);
  node->aabb_Mx |= CLAMP((int)ceilf (255*(aabb[1][3] - box[0])/(box[3+0]-box[0])), 0, 255)<<8;
  node->aabb_Mx |= CLAMP((int)ceilf (255*(aabb[2][3] - box[0])/(box[3+0]-box[0])), 0, 255)<<16;
  node->aabb_Mx |= CLAMP((int)ceilf (255*(aabb[3][3] - box[0])/(box[3+0]-box[0])), 0, 255)<<24;
  node->aabb_My  = CLAMP((int)ceilf (255*(aabb[0][4] - box[1])/(box[3+1]-box[1])), 0, 255);
  node->aabb_My |= CLAMP((int)ceilf (255*(aabb[1][4] - box[1])/(box[3+1]-box[1])), 0, 255)<<8;
  node->aabb_My |= CLAMP((int)ceilf (255*(aabb[2][4] - box[1])/(box[3+1]-box[1])), 0, 255)<<16;
  node->aabb_My |= CLAMP((int)ceilf (255*(aabb[3][4] - box[1])/(box[3+1]-box[1])), 0, 255)<<24;
  node->aabb_Mz  = CLAMP((int)ceilf (255*(aabb[0][5] - box[2])/(box[3+2]-box[2])), 0, 255);
  node->aabb_Mz |= CLAMP((int)ceilf (255*(aabb[1][5] - box[2])/(box[3+2]-box[2])), 0, 255)<<8;
  node->aabb_Mz |= CLAMP((int)ceilf (255*(aabb[2][5] - box[2])/(box[3+2]-box[2])), 0, 255)<<16;
  node->aabb_Mz |= CLAMP((int)ceilf (255*(aabb[3][5] - box[2])/(box[3+2]-box[2])), 0, 255)<<24;

  // for(int k=0;k<6;k++) for(int p=0;p<4;p++) node->aabb0[k].f[p] = aabb[p][k];

  uint64_t done = 0;
  // block siblings together
  int childcnt = 0;
  for (int p=0;p<4;p++)
    if (!(depth == MAX_TREE_DEPTH || part[p+1] - part[p] <= NUM_TRIS_PER_LEAF || b->num_nodes >= b->node_bufsize - 1))
      childcnt++;
  uint64_t num_nodes =  __sync_fetch_and_add(&b->num_nodes, childcnt);
  queue_t *q = b->queue + threads_id;
  for (int p=0;p<4;p++)
  {
    // TODO: quads shouldn't count as one but as two primitives here!
    if (depth == MAX_TREE_DEPTH || part[p+1] - part[p] <= NUM_TRIS_PER_LEAF || b->num_nodes >= b->node_bufsize - 1)
    { // make a leaf node
      if(part[p+1]-part[p] > 10)
      {
        fprintf(stderr, "making extremely fat leaf at depth %d nodes %lu/%lu\n", depth, b->num_nodes, b->node_bufsize);
      }
      // node->child[p] = (1ul<<63) | (part[p]<<5) | ((part[p+1] - part[p]) & ~-(1<<5));
      node->child[p] = (node->child[p] & 0x60000000u) | (1u<<31) | (part[p]<<5) | ((part[p+1] - part[p]) & ~-(1<<5));
      bound_leaf_t1(b, node, p);
      done += part[p+1]-part[p];
    }
    else
    {
      node->child[p] |= 0x1fffffffu & (num_nodes ++);
      assert(b->num_nodes < b->node_bufsize);

      if(q->num_jobs >= b->threads->num_threads)
      { // in this thread:
        done += node_job_work(b, b->tree + (0x1fffffffu & node->child[p]), part[p], part[p+1], aabb[p], depth + 1, node, p);
      }
      else
      { // launch parallel threads:
        threads_mutex_lock(&q->mutex);
        int j = q->num_jobs++;
        assert(j < 3*MAX_TREE_DEPTH + b->threads->num_threads);
        job_t *job = q->jobs + j;
        job->type = s_job_node;
        job->node.node = b->tree + (0x1fffffffu & node->child[p]);
        job->node.left  = part[p];
        job->node.right = part[p+1];
        assert(job->node.left < job->node.right);
        for(int k=0;k<6;k++) job->node.paabb[k] = aabb[p][k];
        job->node.depth = depth + 1;
        job->node.parent = node;
        job->node.child = p;
        threads_mutex_unlock(&q->mutex);
      }
    }
  }
  return done;
}

static void *accel_work(void *arg)
{
  accel_t *a = (accel_t *)arg;
  while(1)
  {
    if(do_one_job(a, s_job_all) || (a->built >= a->num_prims))
      return 0;
  }
}

static void *compute_aabb(void *arg)
{
  accel_t *b = arg;
  const uint64_t tid = threads_id;
  const uint64_t beg = (b->num_prims* tid   )/(uint64_t)b->threads->num_threads;
  const uint64_t end = (b->num_prims*(tid+1))/(uint64_t)b->threads->num_threads;
  float aabb[6];
  aabb[0] = aabb[1] = aabb[2] =   FLT_MAX;
  aabb[3] = aabb[4] = aabb[5] = - FLT_MAX;
  for(uint64_t i=beg; i<end; i++)
  {
    for(int k=0;k<3;k++)
    {
      const float min = b->prim_aabb[6*i+k];
      const float max = b->prim_aabb[6*i+3+k];
      if(aabb[k]   > min) aabb[k]   = min;
      if(aabb[k+3] < max) aabb[k+3] = max;
    }
  }
  threads_mutex_lock(&b->mutex);
  for(int k=0;k<3;k++) if(b->aabb[k] > aabb[k]) b->aabb[k] = aabb[k];
  for(int k=3;k<6;k++) if(b->aabb[k] < aabb[k]) b->aabb[k] = aabb[k];
  b->built ++;
  threads_mutex_unlock(&b->mutex);
  // make sure we don't pick another job in this thread (intervals depend on tid)
  while(b->built < b->threads->num_threads) sched_yield();
  return 0;
}

void accel_build_async(accel_t *b)
{
  b->num_nodes = 1;
  b->aabb[0] = b->aabb[1] = b->aabb[2] = FLT_MAX;
  b->aabb[3] = b->aabb[4] = b->aabb[5] = - FLT_MAX;
  b->built = 0;
  for(int k=0;k<b->threads->num_threads;k++)
    pthread_pool_task_init(b->threads->task + k, &b->threads->pool, compute_aabb, b);
  pthread_pool_wait(&b->threads->pool);

  for(int t=0;t<b->threads->num_threads;t++)
    b->queue[t].num_jobs = 0;
  b->built = 0;

  if(b->num_prims == 0)
  {
    qbvh_node_t *node = b->tree;
    memset(node, 0, sizeof(qbvh_node_t));
    // fill with empty leaves
    node->paabbx = 0xffffu;
    node->paabby = 0xffffu;
    node->paabbz = 0xffffu;
#ifndef ACCEL_STATIC
#error "deleted too much code"
#endif
    for(int c=0;c<4;c++)
      node->child[c] = (1u<<31);
    node->child[1] |= 0x20000000u;
    node->child[2] |= 0x20000000u;
    // node->axis0 = 0;
    // node->axis00 = 1;
    // node->axis01 = 1;
    // node->parent = -1;
    // actually we're done now, no waiting needed.
  }
  else
  {
    // first job, root node:
    threads_mutex_lock(&b->queue[0].mutex);
    b->queue[0].jobs[0].type = s_job_node;
    b->queue[0].jobs[0].node.node = b->tree;
    b->queue[0].jobs[0].node.left = 0;
    b->queue[0].jobs[0].node.right = b->num_prims;
    for(int k=0;k<6;k++) b->queue[0].jobs[0].node.paabb[k] = b->aabb[k];
    b->queue[0].jobs[0].node.depth = 0;
    b->queue[0].jobs[0].node.parent = 0;
    b->queue[0].jobs[0].node.child = 0;
    b->queue[0].num_jobs = 1;
    threads_mutex_unlock(&b->queue[0].mutex);

    for(int k=0;k<b->threads->num_threads;k++)
      pthread_pool_task_init(b->threads->task + k, &b->threads->pool, accel_work, b);
  }
}

void accel_build_wait(accel_t *b)
{
  if(b->num_prims == 0) return; // didn't build anything, don't wait.

  // wait for the worker threads
  pthread_pool_wait(&b->threads->pool);

  // clean up parent
  // b->tree[0].parent = -1;
  // init motion boxes:
  accel_refit(b, b->tree);

#ifdef ACCEL_STATS
  qbvh_stats_t stats;
  memset(&stats, 0, sizeof(qbvh_stats_t));
  checktree(b, b->tree, &stats, 0);
  printf("[accel] num nodes created: %ld\n", b->num_nodes);
  printf("[accel] created qbvh with %ld nodes, %ld leaves (avg %.3f/%.3f depth/num prims, %ld empty), %ld prims, max %ld/%ld (depth/prims).\n",
      stats.num_nodes, stats.num_leaves, stats.sum_depth/(float)stats.num_leaves,
      stats.num_tris/(float)(stats.num_leaves-stats.num_empties), stats.num_empties, stats.num_tris,
      stats.max_depth, stats.max_tris_leaf);
  printf("[accel] overall SAV: %f\n", stats.SA);
#endif
}

void accel_build(accel_t *b)
{
  accel_build_async(b);
  accel_build_wait(b);
}

// ===================================================
// below are the traversal routines for reference:
#if 0
static inline void aabb_intersect(
    const qbvh_node_t *n,
    const __m128 *t0,
    const __m128 *t1,
    const __m128 *pos4,
    const __m128 *invdir4,
    qbvh_float4_t *tmin,
    qbvh_float4_t *tmax)
{
  const qbvh_float4_t *aabb0 = n->aabb0;
#ifdef ACCEL_STATIC
  for(int k=0;k<3;k++)
  {
    __m128 t[2];
    t[0] = _mm_mul_ps(_mm_sub_ps(aabb0[k  ].m, pos4[k]), invdir4[k]);
    t[1] = _mm_mul_ps(_mm_sub_ps(aabb0[k+3].m, pos4[k]), invdir4[k]);
    tmin->m = _mm_max_ps(tmin->m, _mm_min_ps(t[0], t[1]));
    tmax->m = _mm_min_ps(tmax->m, _mm_max_ps(t[0], t[1]));
  }
#else
  const qbvh_float4_t *aabb1 = n->aabb1;
#if 1
  for(int k=0;k<3;k++)
  {
    __m128 t[2];
    t[0] = _mm_mul_ps(_mm_sub_ps(_mm_add_ps(
            _mm_mul_ps(aabb0[k].m, *t0),
            _mm_mul_ps(aabb1[k].m, *t1)), pos4[k]), invdir4[k]);
    t[1] = _mm_mul_ps(_mm_sub_ps(_mm_add_ps(
            _mm_mul_ps(aabb0[k+3].m, *t0),
            _mm_mul_ps(aabb1[k+3].m, *t1)), pos4[k]), invdir4[k]);
    // unfortunately cannot use near and far bit masks to replace this last min/max instruction.
    // since the bounding box interpolation for time comes with slight numeric jitter, the t[.]
    // will have slightly different results even for zero-extent boxes and cause holes.
    tmin->m = _mm_max_ps(tmin->m, _mm_min_ps(t[0], t[1]));
    tmax->m = _mm_min_ps(tmax->m, _mm_max_ps(t[0], t[1]));
  }
#else
  for(int k=0;k<3;k++)
  {
    for(int j=0;j<4;j++)
    {
      // non-sse version:
      const float tm0 = ((aabb0[k].f[j] * ((float*)t0)[j] +
                          aabb1[k].f[j] * ((float*)t0)[j]) - ((float*)(pos4+k))[j])
                     * ((float*)(invdir4+k))[j];
      const float tM0 = ((aabb0[k+3].f[j] * ((float*)t0)[j] +
                          aabb1[k+3].f[j] * ((float*)t1)[j]) - ((float*)(pos4+k))[j])
                     * ((float*)(invdir4+k))[j];
      const float tm = MIN(tm0, tM0);
      const float tM = MAX(tm0, tM0);

      tmin->f[j] = tm > tmin->f[j] ? tm : tmin->f[j];
      tmax->f[j] = tM < tmax->f[j] ? tM : tmax->f[j];
    }
  }
#endif
#endif
}

#if 0
static inline int accel_swap(const qbvh_float4_t tmin4, uint32_t *a, uint32_t *b)
{
  if(tmin4.f[*a] > tmin4.f[*b])
  {
    uint32_t tmp=*b;
    *b=*a;
    *a=tmp;
    return 1;
  }
  return 0;
}
#endif

void accel_intersect(const accel_t *b, const ray_t *ray, hit_t *hit)
{
#ifdef ACCEL_DEBUG
  b->debug[threads_id].accel_intersect++;
#endif
  int near[3];
  int far[3];
  for(int k=0;k<3;k++)
  {
    // need to take sign bit or test invdir for < 0 to catch +-inf cases in aabb intersection.
    near[k] = (*(unsigned int*)(&(ray->dir[k])))>>31;
    far[k] = 1 ^ near[k];
  }

  const qbvh_node_t *node = b->tree;
  uint64_t stack[3*MAX_TREE_DEPTH];
  float stack_dist[3*MAX_TREE_DEPTH];
  int stackpos = 0;
  uint64_t current;
  stack[0] = 0;
  stack_dist[0] = -FLT_MAX;

  __m128 invdir4[3], pos4[3];
  const __m128 t0 = _mm_set1_ps(1.0f-ray->time);
  const __m128 t1 = _mm_set1_ps(ray->time);

  for(int k=0;k<3;k++)
  {
    invdir4[k] = _mm_set1_ps(1.0f/ray->dir[k]);
    pos4[k] = _mm_load_ps1(ray->pos + k);
  }

  while(1)
  {
    // 4x aabb test, sort and push selected index to stack, hold one reg
    // push children according to volume intersection test.
    qbvh_float4_t tmin4, tmax4;
    tmin4.m = _mm_setzero_ps();
    tmax4.m = _mm_set_ps1(hit->dist);
    aabb_intersect(node, &t0, &t1, pos4, invdir4, &tmin4, &tmax4);
    const __m128 leq = _mm_cmple_ps(tmin4.m, tmax4.m);
    // embree guys say this is a good idea, and on ryzen it helps some 3-4% together with tmin stack
    if(__builtin_expect(_mm_movemask_ps(leq) == 0, 0)) goto pop;
    const unsigned int *i = (unsigned int*)&leq;
#ifdef ACCEL_DEBUG
    b->debug[threads_id].aabb_intersect++;
    for(int k=0;k<4;k++) if(i[k]) b->debug[threads_id].aabb_true++;
#endif

    //const unsigned int i[4] = {1, 1, 1, 1};
#if 1 // topological sort
    const int axis0  = node->axis0;
    const int axis1n = near[axis0] ? node->axis01 : node->axis00;
    const int axis1f = near[axis0] ? node->axis00 : node->axis01;

    const unsigned int n11 = (far [axis0]<<1) | far [axis1f];
    const unsigned int n10 = (far [axis0]<<1) | near[axis1f];
    const unsigned int n01 = (near[axis0]<<1) | far [axis1n];
    const unsigned int n00 = (near[axis0]<<1) | near[axis1n];
#else // sort by entry point distance, 11% slower on emily
    uint32_t n00 = 0, n01 = 1, n10 = 2, n11 = 3;
    uint32_t swapped = accel_swap(tmin4, &n00, &n01);
    swapped += accel_swap(tmin4, &n01, &n10);
    swapped += accel_swap(tmin4, &n10, &n11);
    // if(swapped)
    {
      swapped  = accel_swap(tmin4, &n00, &n01);
      swapped += accel_swap(tmin4, &n01, &n10);
      // if(swapped)
        accel_swap(tmin4, &n00, &n01);
    }
#undef SWAP
#endif
   
    if(i[n00])
    {
      current = node->child[n00];
      if(i[n11]) { stack_dist[stackpos] = tmin4.f[n11]; stack[stackpos++] = node->child[n11]; }
      if(i[n10]) { stack_dist[stackpos] = tmin4.f[n10]; stack[stackpos++] = node->child[n10]; }
      if(i[n01]) { stack_dist[stackpos] = tmin4.f[n01]; stack[stackpos++] = node->child[n01]; }
    }
    else if(i[n01])
    {
      current = node->child[n01];
      if(i[n11]) { stack_dist[stackpos] = tmin4.f[n11]; stack[stackpos++] = node->child[n11]; }
      if(i[n10]) { stack_dist[stackpos] = tmin4.f[n10]; stack[stackpos++] = node->child[n10]; }
    }
    else if(i[n10])
    {
      current = node->child[n10];
      if(i[n11]) { stack_dist[stackpos] = tmin4.f[n11]; stack[stackpos++] = node->child[n11]; }
    }
    else if(i[n11]) current = node->child[n11];
    else
    {
      do pop:
      {
        if(stackpos == 0) return;
        stackpos--;
        current = stack[stackpos];
      }
      while(__builtin_expect(stack_dist[stackpos] > hit->dist, 0));
    }

    const uint64_t leaf_mask = 1ul<<63;
    while(current & leaf_mask)
    {
      // intersect primitives
      uint64_t tmp_index = (current ^ leaf_mask) >> 5;
      const uint64_t num = current & ~-(1<<5);
      for (uint64_t i = 0; i < num; i++)
      {
#ifdef ACCEL_DEBUG
        b->debug[threads_id].prims_intersect++;
#endif
        prims_intersect(b->prims, b->prims->primid[tmp_index], ray, hit);
        tmp_index++;
      }
      do
      {
        if (stackpos == 0) return;
        --stackpos;
        current = stack[stackpos];
      }
      while(__builtin_expect(stack_dist[stackpos] > hit->dist, 0));
    }
    node = b->tree + current;
  }
}

int accel_visible(const accel_t *b, const ray_t *ray, const float max_dist)
{
#ifdef ACCEL_DEBUG
  b->debug[threads_id].accel_intersect++;
#endif
  int near[3];
  int far[3];
  for(int k=0;k<3;k++)
  {
    near[k] = (*(unsigned int*)(&(ray->dir[k])))>>31;
    far[k] = 1 ^ near[k];
  }

  const int hash = (b->shadow_cache_last+1)*threads_id + (((((int)ray->pos[0])*73856093) ^ (((int)ray->pos[1])*19349663) ^ (((int)ray->pos[2])*83492791)) & b->shadow_cache_last);
  const qbvh_node_t *node = b->tree + b->shadow_cache[hash];
  const qbvh_node_t *ep = node;
  uint64_t stack[3*MAX_TREE_DEPTH];
  int stackpos = 0;
  uint64_t current;
  stack[0] = 0;

  __m128 invdir4[3], pos4[3];
  const __m128 t0 = _mm_set1_ps(1.0f-ray->time);
  const __m128 t1 = _mm_set1_ps(ray->time);

  for(int k=0;k<3;k++)
  {
    invdir4[k] = _mm_set1_ps(1.0f/ray->dir[k]);
    pos4[k] = _mm_load_ps1(ray->pos + k);
  }

  while(1)
  {
    qbvh_float4_t tmin4, tmax4;
    tmin4.m = _mm_setzero_ps();
    tmax4.m = _mm_set_ps1(max_dist);
    aabb_intersect(node, &t0, &t1, pos4, invdir4, &tmin4, &tmax4);
    const __m128 leq = _mm_cmple_ps(tmin4.m, tmax4.m);
    const int *i = (int*)&leq;
#ifdef ACCEL_DEBUG
    b->debug[threads_id].aabb_intersect++;
    for(int k=0;k<4;k++) if(i[k]) b->debug[threads_id].aabb_true++;
#endif
    for(int k=0;k<4;k++)
    {
      stack[stackpos] = node->child[k];
      stackpos -= i[k];
    }
    while(1)
    {
      while (stackpos == 0)
      {
        // push children of node->parent
        if(ep->parent == (uint64_t)-1) return 1;
        node = b->tree + ep->parent;
        // only push intersecting aabbs.
        qbvh_float4_t tmin4, tmax4;
        tmin4.m = _mm_setzero_ps();
        tmax4.m = _mm_set_ps1(max_dist);
        aabb_intersect(node, &t0, &t1, pos4, invdir4, &tmin4, &tmax4);
        const __m128 leq = _mm_cmple_ps(tmin4.m, tmax4.m);
        const int *i = (int*)&leq;
#ifdef ACCEL_DEBUG
        b->debug[threads_id].aabb_intersect++;
        for(int k=0;k<4;k++) if(i[k]) b->debug[threads_id].aabb_true++;
#endif
        for(int k=0;k<4;k++)
        {
          if(node->child[k] != ep - b->tree)
          {
            stack[stackpos] = node->child[k];
            stackpos -= i[k];
          }
        }
        ep = node;
      }
      --stackpos;
      current = stack[stackpos];
      if(!(current & (0x80000000ul<<32))) break;
      // intersect primitives
      uint64_t tmp_index = (current ^ (0x80000000ul<<32)) >> 5;
      const uint64_t num = current & ~-(1<<5);
      for (uint64_t i = 0; i < num; i++)
      {
#ifdef ACCEL_DEBUG
        b->debug[threads_id].prims_intersect++;
#endif
        if(prims_intersect_visible(b->prims, b->prims->primid[tmp_index], ray, max_dist))
        {
          // cache node:
          b->shadow_cache[hash] = node - b->tree;
          return 0;
        }
        tmp_index++;
      }
    }
    node = b->tree + current;
  }
}

// XXX TODO: const ray and min dist on hit?
void accel_closest(const accel_t *b, ray_t *ray, hit_t *hit, const float centre)
{
  int near[3];
  int far[3];
  for(int k=0;k<3;k++)
  { // need to take sign bit or test invdir for < 0 to catch +-inf cases in aabb intersection.
    near[k] = (*(unsigned int*)(&(ray->dir[k])))>>31;
    far[k] = 1 ^ near[k];
  }

  const qbvh_node_t *node = b->tree;
  uint64_t stack[3*MAX_TREE_DEPTH];
  int stackpos = 0;
  uint64_t current;
  stack[0] = 0;
  hit_t tent = *hit;

  __m128 invdir4[3], pos4[3];
  const __m128 t0 = _mm_set1_ps(1.0f-ray->time);
  const __m128 t1 = _mm_set1_ps(ray->time);

  for(int k=0;k<3;k++)
  {
    invdir4[k] = _mm_set1_ps(1.0f/ray->dir[k]);
    pos4[k] = _mm_load_ps1(ray->pos + k);
  }

  while(1)
  {
    // 4x aabb test, sort and push selected index to stack, hold one reg
    // push children according to volume intersection test.
    qbvh_float4_t tmin4, tmax4;
    tmin4.m = _mm_set_ps1(ray->min_dist);
    tmax4.m = _mm_set_ps1(hit->dist);
    aabb_intersect(node, &t0, &t1, pos4, invdir4, &tmin4, &tmax4);
    const __m128 leq = _mm_cmple_ps(tmin4.m, tmax4.m);
    const unsigned int *i = (unsigned int*)&leq;
    const int axis0  = node->axis0;
    const int axis1n = near[axis0] ? node->axis01 : node->axis00;
    const int axis1f = near[axis0] ? node->axis00 : node->axis01;

    const unsigned int n11 = (far [axis0]<<1) | far [axis1f];
    const unsigned int n10 = (far [axis0]<<1) | near[axis1f];
    const unsigned int n01 = (near[axis0]<<1) | far [axis1n];
    const unsigned int n00 = (near[axis0]<<1) | near[axis1n];
   
    if(i[n00])
    {
      current = node->child[n00];
      if(i[n11]) stack[stackpos++] = node->child[n11];
      if(i[n10]) stack[stackpos++] = node->child[n10];
      if(i[n01]) stack[stackpos++] = node->child[n01];
    }
    else if(i[n01])
    {
      current = node->child[n01];
      if(i[n11]) stack[stackpos++] = node->child[n11];
      if(i[n10]) stack[stackpos++] = node->child[n10];
    }
    else if(i[n10])
    {
      current = node->child[n10];
      if(i[n11]) stack[stackpos++] = node->child[n11];
    }
    else if(i[n11]) current = node->child[n11];
    else
    {
      if(stackpos == 0)
      {
        *hit = tent;
        return;
      }
      stackpos--;
      current = stack[stackpos];
    }

    while(current & (0x80000000ul<<32))
    {
      // intersect primitives
      uint64_t tmp_index = (current ^ (0x80000000ul<<32)) >> 5;
      const uint64_t num = current & ~-(1<<5);
      for (uint64_t i = 0; i < num; i++)
      {
        prims_intersect(b->prims, b->prims->primid[tmp_index], ray, hit);
        if(fabsf(hit->dist - centre) < fabsf(tent.dist - centre)) tent = *hit;
        if(hit->dist > centre + 1e-6f)
        { // move interval up from below
          ray->min_dist = 2.0f*centre - hit->dist;
        }
        else if(hit->dist < centre - 1e-6f)
        { // move interval up and clear far end to be two-sided around centre again
          ray->min_dist = hit->dist;
          hit->dist = 2.0f*centre - ray->min_dist;
        }
        else return; // hit->dist ~= centre, found a straight hit.
        tmp_index++;
      }
      if (stackpos == 0)
      {
        *hit = tent;
        return;
      }
      --stackpos;
      current = stack[stackpos];
    }
    node = b->tree + current;
  }
}

#if 0
// TODO: callback to search for closeby points
// TODO: don't call into any primitive interface, just return a list of primids
// TODO: get fixed radius and hand out all overlapping boxes worth of data
// return the number of prims found
int accel_collect(
    const accel_t *const a,
    const float *const x,
    const float radius,
    primid_t *buf,
    const int bufsize)
{
  int cnt = 0;
  // TODO: intersect boxes with sphere around point
  // jim arvo's version
  bool BoxIntersectsSphere(Vec3 Bmin, Vec3 Bmax, Vec3 C, float r) {
    float r2 = r * r;
    dmin = 0;
    for( i = 0; i < 3; i++ ) {
      if( C[i] < Bmin[i] ) dmin += SQR( C[i] - Bmin[i] );
      else if( C[i] > Bmax[i] ) dmin += SQR( C[i] - Bmax[i] );     
    }
    return dmin <= r2;
  }
}
#endif

#endif
