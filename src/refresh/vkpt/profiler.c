/*
Copyright (C) 2018 Christoph Schied
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

static VkQueryPool query_pool;

static uint64_t query_pool_results[NUM_PROFILER_QUERIES_PER_FRAME * 2];
//                                                                ^^^
// not sure why (* 2) is necessary, looks like there is a bug in AMD drivers 
// causing vkGetQueryPoolResults to stop writing the results halfway through 
// the buffer if it's properly sized.

extern cvar_t *cvar_profiler_scale;
extern cvar_t *cvar_pt_reflect_refract;
extern cvar_t *cvar_flt_fsr_enable;

static bool profiler_queries_used[NUM_PROFILER_QUERIES_PER_FRAME * MAX_FRAMES_IN_FLIGHT] = { 0 };

VkResult
vkpt_profiler_initialize()
{
	VkQueryPoolCreateInfo query_pool_info = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = MAX_FRAMES_IN_FLIGHT * NUM_PROFILER_QUERIES_PER_FRAME,
	};
	vkCreateQueryPool(qvk.device, &query_pool_info, NULL, &query_pool);
	return VK_SUCCESS;
}

VkResult
vkpt_profiler_destroy()
{
	vkDestroyQueryPool(qvk.device, query_pool, NULL);
	return VK_SUCCESS;
}

VkResult
vkpt_profiler_query(VkCommandBuffer cmd_buf, int idx, VKPTProfilerAction action)
{
	idx = idx * 2 + action + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME;

	set_current_gpu(cmd_buf, 0);

	VkPipelineStageFlagBits stage = (action == PROFILER_START) 
		? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 
		: VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	vkCmdWriteTimestamp(cmd_buf, stage, query_pool, idx);

	set_current_gpu(cmd_buf, ALL_GPUS);

	profiler_queries_used[idx] = true;

	return VK_SUCCESS;
}

VkResult
vkpt_profiler_next_frame(VkCommandBuffer cmd_buf)
{
	bool any_queries_used = false;

	for (int idx = 0; idx < NUM_PROFILER_QUERIES_PER_FRAME; idx++)
	{
		if (profiler_queries_used[idx + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME])
		{
			any_queries_used = true;
			break;
		}
	}

	if (any_queries_used)
	{
		VkResult result = vkGetQueryPoolResults(qvk.device, query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * qvk.current_frame_index,
			NUM_PROFILER_QUERIES_PER_FRAME,
			sizeof(query_pool_results),
			query_pool_results,
			sizeof(query_pool_results[0]),
			VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

		if (result != VK_SUCCESS && result != VK_NOT_READY)
		{
			Com_EPrintf("Failed call to vkGetQueryPoolResults, error code = %d\n", result);
			any_queries_used = false;
		}
	}

	if (any_queries_used)
	{
		for (int idx = 0; idx < NUM_PROFILER_QUERIES_PER_FRAME; idx++)
		{
			if (!profiler_queries_used[idx + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME])
				query_pool_results[idx] = 0;
		}
	}
	else
	{
		memset(query_pool_results, 0, sizeof(query_pool_results));
	}

	vkCmdResetQueryPool(cmd_buf, query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * qvk.current_frame_index, 
			NUM_PROFILER_QUERIES_PER_FRAME);

	memset(profiler_queries_used + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME, 0, sizeof(bool) * NUM_PROFILER_QUERIES_PER_FRAME);

	return VK_SUCCESS;
}

static void
draw_query(int x, int y, qhandle_t font, const char *enum_name, int idx)
{
	char buf[256];
	int i;
	for(i = 0; i < LENGTH(buf) - 1 && enum_name[i]; i++)
		buf[i] = enum_name[i] == '_' ? ' ' : (char)tolower(enum_name[i]); 
	buf[i] = 0;

	R_DrawString(x, y, 0, 128, buf, font);
	double ms = vkpt_get_profiler_result(idx);

	if(ms > 0.0)
		snprintf(buf, sizeof buf, "%8.2f ms", ms);
	else
		snprintf(buf, sizeof buf, "       N/A");

	R_DrawString(x + 256, y, 0, 128, buf, font);
}

void
draw_profiler(int enable_asvgf)
{
	float profiler_scale = R_ClampScale(cvar_profiler_scale);
	int x = 500 * profiler_scale;
	int y = 100 * profiler_scale;

	qhandle_t font;
	font = R_RegisterFont("conchars");
	if(!font)
		return;

	R_SetScale(profiler_scale);

#define PROFILER_DO(name, indent) \
	draw_query(x, y, font, &#name[9], name); y += 10;

	PROFILER_DO(PROFILER_FRAME_TIME, 0);
	PROFILER_DO(PROFILER_INSTANCE_GEOMETRY, 1);
	PROFILER_DO(PROFILER_BVH_UPDATE, 1);
	PROFILER_DO(PROFILER_UPDATE_ENVIRONMENT, 1);
	PROFILER_DO(PROFILER_SHADOW_MAP, 1);
	PROFILER_DO(PROFILER_PRIMARY_RAYS, 1);
	if (cvar_pt_reflect_refract->integer > 0) { PROFILER_DO(PROFILER_REFLECT_REFRACT_1, 1); }
	if (cvar_pt_reflect_refract->integer > 1) { PROFILER_DO(PROFILER_REFLECT_REFRACT_2, 1); }
	if (enable_asvgf)
	{
		PROFILER_DO(PROFILER_ASVGF_GRADIENT_REPROJECT, 1);
	}
	PROFILER_DO(PROFILER_DIRECT_LIGHTING, 1);
	PROFILER_DO(PROFILER_INDIRECT_LIGHTING, 1);
	PROFILER_DO(PROFILER_GOD_RAYS, 1);
	PROFILER_DO(PROFILER_GOD_RAYS_REFLECT_REFRACT, 1);
	PROFILER_DO(PROFILER_GOD_RAYS_FILTER, 1);
	if (enable_asvgf)
	{
		PROFILER_DO(PROFILER_ASVGF_FULL, 1);
		PROFILER_DO(PROFILER_ASVGF_RECONSTRUCT_GRADIENT, 2);
		PROFILER_DO(PROFILER_ASVGF_TEMPORAL, 2);
		PROFILER_DO(PROFILER_ASVGF_ATROUS, 2);
		PROFILER_DO(PROFILER_ASVGF_TAA, 2);
	}
	else
	{
		PROFILER_DO(PROFILER_COMPOSITING, 1);
	}
	if (qvk.device_count > 1) {
		PROFILER_DO(PROFILER_MGPU_TRANSFERS, 1);
	}
	PROFILER_DO(PROFILER_INTERLEAVE, 1);
	PROFILER_DO(PROFILER_BLOOM, 1);
	PROFILER_DO(PROFILER_TONE_MAPPING, 2);
	if(cvar_flt_fsr_enable->integer != 0)
	{
		PROFILER_DO(PROFILER_FSR, 1);
		PROFILER_DO(PROFILER_FSR_EASU, 2);
		PROFILER_DO(PROFILER_FSR_RCAS, 2);
	}
#undef PROFILER_DO

	R_SetScale(1.0f);
}

double vkpt_get_profiler_result(int idx)
{
	uint64_t begin = query_pool_results[idx * 2 + 0];
	uint64_t end = query_pool_results[idx * 2 + 1];

	if (begin == 0 || end == 0)
		return 0.0; // one of these queries was unavailable at the time vkGetQueryPoolResults was called

	double ms = (double)(end - begin) * 1e-6 * qvk.timestampPeriod;
	return ms;
}