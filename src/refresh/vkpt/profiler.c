/*
Copyright (C) 2018 Christoph Schied

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
static uint64_t query_pool_results[NUM_PROFILER_QUERIES_PER_FRAME];

VkResult
vkpt_profiler_initialize()
{
	VkQueryPoolCreateInfo query_pool_info = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = MAX_SWAPCHAIN_IMAGES * NUM_PROFILER_ENTRIES * 2,
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
vkpt_profiler_query(int idx, VKPTProfilerAction action)
{
	idx = idx * 2 + action + qvk.current_image_index * NUM_PROFILER_QUERIES_PER_FRAME;
	vkCmdWriteTimestamp(qvk.cmd_buf_current, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			query_pool, idx);

	return VK_SUCCESS;
}

VkResult
vkpt_profiler_next_frame(int frame_num)
{ 
	_VK(vkGetQueryPoolResults(qvk.device, query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * frame_num, 
			NUM_PROFILER_QUERIES_PER_FRAME,
			sizeof(query_pool_results),
			query_pool_results,
			sizeof(query_pool_results[0]),
			VK_QUERY_RESULT_64_BIT));

	/*
	// crashes, need to add!!! queries are undefined if not reset before
	vkCmdResetQueryPool(qvk.cmd_buf_current, query_pool,
			NUM_PROFILER_QUERIES_PER_FRAME * frame_num, 
			NUM_PROFILER_QUERIES_PER_FRAME);
			*/

	//Com_Printf("%ld %ld\n", query_pool_results[0], query_pool_results[1]);

	//double ms = (double) (query_pool_results[1] - query_pool_results[0]) * 1e-6;
	//Com_Printf("%f\n", ms);

	return VK_SUCCESS;
}

static void
draw_query(int x, int y, qhandle_t font, const char *enum_name, int idx)
{
	char buf[256];
	int i;
	for(i = 0; i < LENGTH(buf) - 1 && enum_name[i]; i++)
		buf[i] = enum_name[i] == '_' ? ' ' : tolower(enum_name[i]); 
	buf[i] = 0;

	R_DrawString(x, y, 0, 128, buf, font);
	double ms = (double) (query_pool_results[idx * 2 + 1] - query_pool_results[idx * 2 + 0]) * 1e-6;
	snprintf(buf, sizeof buf, "%8.2f ms", ms);
	R_DrawString(x + 256, y, 0, 128, buf, font);
}

void
draw_profiler()
{
	int x = 500;
	int y = 100;

	qhandle_t font;
	font = R_RegisterFont("conchars");
	if(!font)
		return;

#define PROFILER_DO(name, indent) \
	draw_query(x, y, font, #name + 9, name); y += 10;
PROFILER_LIST
#undef PROFILER_DO
}
