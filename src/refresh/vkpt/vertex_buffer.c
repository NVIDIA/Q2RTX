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

#include "shader/vertex_buffer.h"

#include <assert.h>
#include <stdio.h>

static VkDescriptorPool desc_pool_vertex_buffer;
static VkPipeline       pipeline_instance_geometry;
static VkPipelineLayout pipeline_layout_instance_geometry;

VkResult
vkpt_vertex_buffer_upload_staging()
{
	vkDeviceWaitIdle(qvk.device);
	assert(!qvk.buf_vertex_staging.is_mapped);
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

	VkBufferCopy copyRegion = {
		.size = sizeof(VertexBuffer),
	};
	vkCmdCopyBuffer(cmd_buf, qvk.buf_vertex_staging.buffer, qvk.buf_vertex.buffer, 1, &copyRegion);
	
	vkEndCommandBuffer(cmd_buf);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};

	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);

	vkQueueWaitIdle(qvk.queue_graphics);

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_upload_bsp_mesh_to_staging(bsp_mesh_t *bsp_mesh)
{
	assert(bsp_mesh);
	VertexBuffer *vbo = (VertexBuffer *) buffer_map(&qvk.buf_vertex_staging);
	assert(vbo);

	assert(bsp_mesh->num_vertices < MAX_VERT_BSP);

	memcpy(vbo->positions_bsp,  bsp_mesh->positions, bsp_mesh->num_vertices * sizeof(float) * 3   );
	memcpy(vbo->tex_coords_bsp, bsp_mesh->tex_coords,bsp_mesh->num_vertices * sizeof(float) * 2   );
	memcpy(vbo->materials_bsp,  bsp_mesh->materials, bsp_mesh->num_vertices * sizeof(uint32_t) / 3);
	memcpy(vbo->clusters_bsp,   bsp_mesh->clusters,  bsp_mesh->num_vertices * sizeof(uint32_t) / 3);

	assert(bsp_mesh->num_clusters + 1   < MAX_LIGHT_LISTS);
	assert(bsp_mesh->num_cluster_lights < MAX_LIGHT_LIST_NODES);

	memcpy(vbo->light_list_offsets, bsp_mesh->cluster_light_offsets, (bsp_mesh->num_clusters + 1) * sizeof(uint32_t));
	memcpy(vbo->light_list_lights,  bsp_mesh->cluster_lights,        bsp_mesh->num_cluster_lights * sizeof(uint32_t));

	buffer_unmap(&qvk.buf_vertex_staging);
	vbo = NULL;

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_upload_models_to_staging()
{
	VertexBuffer *vbo = (VertexBuffer *) buffer_map(&qvk.buf_vertex_staging);
	assert(vbo);

	int idx_offset = 0;
	int vertex_offset = 0;
	for(int i = 0; i < MAX_MODELS; i++) {
		if(!r_models[i].meshes) {
			continue;
		}
		maliasmesh_t *m = r_models[i].meshes;

		m->idx_offset    = idx_offset;
		m->vertex_offset = vertex_offset;

		assert(r_models[i].numframes > 0);

		int num_verts = r_models[i].numframes * m->numverts;
		assert(num_verts > 0);
#if 0
		for(int j = 0; j < num_verts; j++)
			Com_Printf("%f %f %f\n",
				m->positions[j][0],
				m->positions[j][1],
				m->positions[j][2]);

		for(int j = 0; j < m->numtris; j++)
			Com_Printf("%d %d %d\n",
				m->indices[j * 3 + 0],
				m->indices[j * 3 + 1],
				m->indices[j * 3 + 2]);
#endif

#if 0
		char buf[1024];
		snprintf(buf, sizeof buf, "model_%04d.obj", i);
		FILE *f = fopen(buf, "wb+");
		assert(f);
		for(int j = 0; j < m->numverts; j++) {
			fprintf(f, "v %f %f %f\n",
				m->positions[j][0],
				m->positions[j][1],
				m->positions[j][2]);
		}
		for(int j = 0; j < m->numindices / 3; j++) {
			fprintf(f, "f %d %d %d\n",
				m->indices[j * 3 + 0] + 1,
				m->indices[j * 3 + 1] + 1,
				m->indices[j * 3 + 2] + 1);
		}
		fclose(f);
#endif

		memcpy(vbo->positions_model  + vertex_offset * 3, m->positions,  sizeof(float)    * 3 * num_verts);
		memcpy(vbo->normals_model    + vertex_offset * 3, m->normals,    sizeof(float)    * 3 * num_verts);
		memcpy(vbo->tex_coords_model + vertex_offset * 2, m->tex_coords, sizeof(float)    * 2 * num_verts);
		memcpy(vbo->idx_model        + idx_offset,        m->indices,    sizeof(uint32_t) * m->numindices);

		vertex_offset += num_verts;
		idx_offset    += m->numtris * 3;

		assert(vertex_offset < MAX_VERT_MODEL);
		assert(idx_offset < MAX_IDX_MODEL);
	}

	buffer_unmap(&qvk.buf_vertex_staging);
	vbo = NULL;

	Com_Printf("uploaded %d vert, %d idx\n", vertex_offset, idx_offset);

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_create()
{
	VkDescriptorSetLayoutBinding vbo_layout_binding = {
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.binding         = VERTEX_BUFFER_BINDING_IDX,
		.stageFlags      = VK_SHADER_STAGE_ALL,
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings    = &vbo_layout_binding,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &qvk.desc_set_layout_vertex_buffer));

	Com_Printf("allocating %.02f MB of memory for vertex buffer\n", (double) sizeof(VertexBuffer) / (1024.0 * 1024.0));
	buffer_create(&qvk.buf_vertex, sizeof(VertexBuffer),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Com_Printf("allocating %.02f MB of memory for staging vertex buffer\n", (double) sizeof(VertexBuffer) / (1024.0 * 1024.0));
	buffer_create(&qvk.buf_vertex_staging, sizeof(VertexBuffer),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = 1,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_vertex_buffer));

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_vertex_buffer,
		.descriptorSetCount = 1,
		.pSetLayouts        = &qvk.desc_set_layout_vertex_buffer,
	};

	_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &qvk.desc_set_vertex_buffer));

	VkDescriptorBufferInfo buf_info = {
		.buffer = qvk.buf_vertex.buffer,
		.offset = 0,
		.range  = sizeof(VertexBuffer),
	};

	VkWriteDescriptorSet output_buf_write = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_vertex_buffer,
		.dstBinding      = 0,
		.dstArrayElement = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo     = &buf_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_destroy()
{
	vkDestroyDescriptorPool(qvk.device, desc_pool_vertex_buffer, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, qvk.desc_set_layout_vertex_buffer, NULL);
	desc_pool_vertex_buffer = VK_NULL_HANDLE;
	qvk.desc_set_layout_vertex_buffer = VK_NULL_HANDLE;

	buffer_destroy(&qvk.buf_vertex);
	buffer_destroy(&qvk.buf_vertex_staging);

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_create_pipelines()
{
	assert(!pipeline_instance_geometry);
	assert(!pipeline_layout_instance_geometry);

	assert(qvk.desc_set_layout_ubo);
	assert(qvk.desc_set_layout_vertex_buffer); 

	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, qvk.desc_set_layout_vertex_buffer
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_instance_geometry, 
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
	);

	VkComputePipelineCreateInfo compute_pipeline_info = {
		.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage  = SHADER_STAGE(QVK_MOD_INSTANCE_GEOMETRY_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
		.layout = pipeline_layout_instance_geometry
	};

	assert(compute_pipeline_info.stage.module);

	_VK(vkCreateComputePipelines(qvk.device, 0, 1, &compute_pipeline_info, 0, &pipeline_instance_geometry));

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_destroy_pipelines()
{
	assert(pipeline_instance_geometry);
	assert(pipeline_layout_instance_geometry);

	vkDestroyPipeline(qvk.device, pipeline_instance_geometry, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_instance_geometry, NULL);

	pipeline_instance_geometry = VK_NULL_HANDLE;
	pipeline_layout_instance_geometry = VK_NULL_HANDLE;

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_create_instance(uint32_t num_instances)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo[qvk.current_image_index],
		qvk.desc_set_vertex_buffer
	};
	vkCmdBindPipeline(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_instance_geometry);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_instance_geometry, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(qvk.cmd_buf_current, num_instances, 1, 1);

	VkBufferMemoryBarrier barrier = {
		.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
		.buffer              = qvk.buf_vertex.buffer,
		.size                = qvk.buf_vertex.size,
		.srcQueueFamilyIndex = qvk.queue_idx_graphics,
		.dstQueueFamilyIndex = qvk.queue_idx_graphics
	};

	vkCmdPipelineBarrier(
			qvk.cmd_buf_current,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, NULL,
			1, &barrier,
			0, NULL);

	return VK_SUCCESS;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
