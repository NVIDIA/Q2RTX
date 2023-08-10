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

#include <assert.h>


extern cvar_t *cvar_profiler_scale;
extern cvar_t *cvar_pt_reflect_refract;
extern cvar_t *cvar_flt_fsr_enable;
extern cvar_t *cvar_profiler_samples;

// Sample data for a single profiled value
typedef struct profiler_entry_samples_s
{
	// Circular buffer with collected samples
	uint64_t *data;
	// Number of collected samples
	size_t num_samples;
	// Index where next sample should be written
	size_t next_idx;
	// Sum of all sample values
	uint64_t accumulated;
} profiler_entry_samples_t;

static struct {
	VkQueryPool query_pool;

	uint64_t query_pool_results[NUM_PROFILER_QUERIES_PER_FRAME * 2];
	//                                                         ^^^
	// not sure why (* 2) is necessary, looks like there is a bug in AMD drivers
	// causing vkGetQueryPoolResults to stop writing the results halfway through
	// the buffer if it's properly sized.

	bool queries_used[NUM_PROFILER_QUERIES_PER_FRAME * MAX_FRAMES_IN_FLIGHT];

	// Number of sample circular buffer size (same for each entry)
	size_t allocated_samples;
	// Sample data for profiled values
	profiler_entry_samples_t samples[NUM_PROFILER_ENTRIES];
} profiler_data;

VkResult
vkpt_profiler_initialize()
{
	memset(&profiler_data, 0, sizeof(profiler_data));

	VkQueryPoolCreateInfo query_pool_info = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = MAX_FRAMES_IN_FLIGHT * NUM_PROFILER_QUERIES_PER_FRAME,
	};
	vkCreateQueryPool(qvk.device, &query_pool_info, NULL, &profiler_data.query_pool);
	return VK_SUCCESS;
}

VkResult
vkpt_profiler_destroy()
{
	vkDestroyQueryPool(qvk.device, profiler_data.query_pool, NULL);
	for (int i = 0; i < NUM_PROFILER_ENTRIES; i++)
		Z_Free(profiler_data.samples[i].data);
	return VK_SUCCESS;
}

static inline void
remove_excess_samples(profiler_entry_samples_t *entry_samples, size_t max_samples)
{
	if (entry_samples->num_samples <= max_samples)
		return;

	// Compute the index of the 'oldest' value in the sample data buffer...
	size_t oldest_idx = (entry_samples->next_idx + profiler_data.allocated_samples - entry_samples->num_samples) % profiler_data.allocated_samples;
	do {
		// ...and keep on removing the oldest data as long the count is above max_samples
		entry_samples->accumulated -= entry_samples->data[oldest_idx];
		entry_samples->num_samples--;
		oldest_idx = (oldest_idx + 1) % profiler_data.allocated_samples;
	} while (entry_samples->num_samples > max_samples);
}

// Change size of profiler samples circular buffer
static void
set_sample_count(size_t new_samples)
{
	size_t old_allocated_samples = profiler_data.allocated_samples;
	for (int i = 0; i < NUM_PROFILER_ENTRIES; i++)
	{
		// Make sure entry has at most as much samples as the new buffer size
		profiler_entry_samples_t *entry_samples = &profiler_data.samples[i];
		remove_excess_samples(entry_samples, new_samples);

		uint64_t* new_sample_data = Z_Malloc(new_samples * sizeof(uint64_t));
		if(entry_samples->num_samples > 0)
		{
			size_t oldest_idx = (entry_samples->next_idx + profiler_data.allocated_samples - entry_samples->num_samples) % profiler_data.allocated_samples;
			// Reassemble sample data at start of new buffer
			if (oldest_idx >= entry_samples->next_idx)
			{
				// Sample data wraps around
				uint64_t *dest = new_sample_data;
				size_t count = old_allocated_samples - oldest_idx;
				assert(count + entry_samples->next_idx <= new_samples);
				memcpy(dest, entry_samples->data + oldest_idx, count * sizeof(uint64_t));
				dest += count;
				memcpy(dest, entry_samples->data, entry_samples->next_idx * sizeof(uint64_t));
				count += entry_samples->next_idx;
				assert(count <= entry_samples->num_samples);
				entry_samples->next_idx = count % new_samples;
			}
			else
			{
				// No wraparound
				assert(entry_samples->num_samples <= new_samples);
				memcpy(new_sample_data, entry_samples->data + oldest_idx, entry_samples->num_samples * sizeof(uint64_t));
				entry_samples->next_idx = entry_samples->num_samples % new_samples;
			}
		}
		else
		{
			entry_samples->next_idx = 0;
		}
		Z_Free(entry_samples->data);
		entry_samples->data = new_sample_data;
	}
	profiler_data.allocated_samples = new_samples;
}

// Record a single profiler sample value
static inline void
record_sample(int idx, uint64_t value)
{
	profiler_entry_samples_t *entry_samples = &profiler_data.samples[idx];
	remove_excess_samples(entry_samples, profiler_data.allocated_samples - 1);
	entry_samples->data[entry_samples->next_idx] = value;
	entry_samples->accumulated += value;
	entry_samples->num_samples++;
	entry_samples->next_idx = (entry_samples->next_idx + 1) % profiler_data.allocated_samples;
}

// Reset accumulated profiler samples
static inline void
reset_samples(int idx)
{
	profiler_entry_samples_t *entry_samples = &profiler_data.samples[idx];
	entry_samples->accumulated = 0;
	entry_samples->num_samples = 0;
	entry_samples->next_idx = 0;
}

VkResult
vkpt_profiler_query(VkCommandBuffer cmd_buf, int idx, VKPTProfilerAction action)
{
	idx = idx * 2 + action + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME;

	set_current_gpu(cmd_buf, 0);

	VkPipelineStageFlagBits stage = (action == PROFILER_START) 
		? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 
		: VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	vkCmdWriteTimestamp(cmd_buf, stage, profiler_data.query_pool, idx);

	set_current_gpu(cmd_buf, ALL_GPUS);

	profiler_data.queries_used[idx] = true;

	return VK_SUCCESS;
}

VkResult
vkpt_profiler_next_frame(VkCommandBuffer cmd_buf)
{
	// Resize sample buffers, if necessary
	size_t new_samples = max(cvar_profiler_samples->integer, 1);
	if (profiler_data.allocated_samples != new_samples)
		set_sample_count(new_samples);

	bool any_queries_used = false;

	for (int idx = 0; idx < NUM_PROFILER_QUERIES_PER_FRAME; idx++)
	{
		if (profiler_data.queries_used[idx + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME])
		{
			any_queries_used = true;
			break;
		}
	}

	if (any_queries_used)
	{
		VkResult result = vkGetQueryPoolResults(qvk.device, profiler_data.query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * qvk.current_frame_index,
			NUM_PROFILER_QUERIES_PER_FRAME,
			sizeof(profiler_data.query_pool_results),
			profiler_data.query_pool_results,
			sizeof(profiler_data.query_pool_results[0]),
			VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

		if (result != VK_SUCCESS && result != VK_NOT_READY)
		{
			Com_EPrintf("Failed call to vkGetQueryPoolResults, error code = %d\n", result);
			any_queries_used = false;
		}
	}

	if (any_queries_used)
	{
		for (int idx = 0; idx < NUM_PROFILER_ENTRIES; idx++)
		{
			if (!profiler_data.queries_used[idx * 2 + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME])
			{
				profiler_data.query_pool_results[idx * 2 + 0] = 0;
				profiler_data.query_pool_results[idx * 2 + 1] = 0;
				reset_samples(idx);
			}
			else
			{
				uint64_t begin = profiler_data.query_pool_results[idx * 2 + 0];
				uint64_t end = profiler_data.query_pool_results[idx * 2 + 1];

				if (begin != 0 && end != 0)
					record_sample(idx, end - begin);
				else
					reset_samples(idx);
			}
		}
	}
	else
	{
		memset(profiler_data.query_pool_results, 0, sizeof(profiler_data.query_pool_results));
	}

	vkCmdResetQueryPool(cmd_buf, profiler_data.query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * qvk.current_frame_index, 
			NUM_PROFILER_QUERIES_PER_FRAME);

	memset(profiler_data.queries_used + qvk.current_frame_index * NUM_PROFILER_QUERIES_PER_FRAME, 0, sizeof(bool) * NUM_PROFILER_QUERIES_PER_FRAME);

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
	double avg_ms = ((double)profiler_data.samples[idx].accumulated / (profiler_data.samples[idx].num_samples * 1e6)) * qvk.timestampPeriod;

	if(ms > 0.005)
		snprintf(buf, sizeof buf, "%8.2f ms %8.2f ms", ms, avg_ms);
	else if(avg_ms > 0.005)
		snprintf(buf, sizeof buf, "       N/A  %8.2f ms", avg_ms);
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

	// Table headers
	R_DrawString(x + 256, y - 16, 0, 128, "    imm         avg", font);

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
	PROFILER_DO(PROFILER_INDIRECT_LIGHTING_0, 2);
	PROFILER_DO(PROFILER_INDIRECT_LIGHTING_1, 2);
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
	uint64_t begin = profiler_data.query_pool_results[idx * 2 + 0];
	uint64_t end = profiler_data.query_pool_results[idx * 2 + 1];

	if (begin == 0 || end == 0)
		return 0.0; // one of these queries was unavailable at the time vkGetQueryPoolResults was called

	double ms = (double)(end - begin) * 1e-6 * qvk.timestampPeriod;
	return ms;
}