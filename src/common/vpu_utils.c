/* Miscellanous utility functions for VPU operations
 * Copyright (C) 2013  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <gst/gst.h>
#include "vpu_utils.h"

GST_DEBUG_CATEGORY_STATIC(vpualloc_debug);


gchar const *gst_fsl_vpu_strerror(VpuDecRetCode code)
{
	switch (code)
	{
		case VPU_DEC_RET_SUCCESS: return "success";
		case VPU_DEC_RET_FAILURE: return "failure";
		case VPU_DEC_RET_INVALID_PARAM: return "invalid param";
		case VPU_DEC_RET_INVALID_HANDLE: return "invalid handle";
		case VPU_DEC_RET_INVALID_FRAME_BUFFER: return "invalid frame buffer";
		case VPU_DEC_RET_INSUFFICIENT_FRAME_BUFFERS: return "insufficient frame buffers";
		case VPU_DEC_RET_INVALID_STRIDE: return "invalid stride";
		case VPU_DEC_RET_WRONG_CALL_SEQUENCE: return "wrong call sequence";
		case VPU_DEC_RET_FAILURE_TIMEOUT: return "failure timeout";
		default:
			return NULL;
	}
}


void gst_fsl_vpu_init_alloc_debug(void)
{
	GST_DEBUG_CATEGORY_INIT(vpualloc_debug, "vpualloc", 0, "VPU allocation functions");
}


gboolean gst_fsl_vpu_alloc_virt_mem_block(unsigned char **mem_block, int size)
{
	*mem_block = (unsigned char *)g_try_malloc(size);
	if ((*mem_block) == NULL)
	{
		GST_CAT_ERROR(vpualloc_debug, "could not request %d bytes of heap memory", size);
		return FALSE;
	}
	else
		GST_CAT_DEBUG(vpualloc_debug, "allocated %d bytes of heap memory at virt addr %p", size, *mem_block);

	return TRUE;
}


void gst_fsl_vpu_append_virt_mem_block(unsigned char *mem_block, GSList **virt_mem_blocks)
{
	*virt_mem_blocks = g_slist_append(*virt_mem_blocks, (gpointer)mem_block);
}


gboolean gst_fsl_vpu_free_virt_mem_blocks(GSList **virt_mem_blocks)
{
	GSList *mem_block_node;
	g_assert(virt_mem_blocks != NULL);
	mem_block_node = *virt_mem_blocks;
	if (mem_block_node == NULL)
		return TRUE;

	for (; mem_block_node != NULL; mem_block_node = mem_block_node->next)
	{
		g_free(mem_block_node->data);
		GST_CAT_DEBUG(vpualloc_debug, "freed heap memory block at virt addr %p", mem_block_node->data);
	}

	g_slist_free(*virt_mem_blocks);
	*virt_mem_blocks = NULL;

	return TRUE;
}


gboolean gst_fsl_vpu_alloc_phys_mem_block(VpuMemDesc **mem_block, int size)
{
	VpuDecRetCode ret;

	*mem_block = g_slice_alloc(sizeof(VpuMemDesc));
	if (*mem_block == NULL)
	{
		GST_ERROR("could not allocate VPU memory descriptor: slice allocator failed");
		return FALSE;
	}

	(*mem_block)->nSize = size;
	ret = VPU_DecGetMem(*mem_block);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR("could not request %d bytes of VPU memory: %s", size, gst_fsl_vpu_strerror(ret));
		return FALSE;
	}
	else
		GST_CAT_DEBUG(vpualloc_debug, "allocated %d bytes of VPU memory at virt addr %x phys addr %x", size, (*mem_block)->nVirtAddr, (*mem_block)->nPhyAddr);

	return TRUE;
}


void gst_fsl_vpu_append_phys_mem_block(VpuMemDesc *mem_block, GSList **phys_mem_blocks)
{
	*phys_mem_blocks = g_slist_append(*phys_mem_blocks, (gpointer)mem_block);
}


gboolean gst_fsl_vpu_free_phys_mem_blocks(GSList **phys_mem_blocks)
{
	GSList *mem_block_node;
	g_assert(phys_mem_blocks != NULL);
	mem_block_node = *phys_mem_blocks;
	if (mem_block_node == NULL)
		return TRUE;

	for (; mem_block_node != NULL; mem_block_node = mem_block_node->next)
	{
		VpuMemDesc *mem_block = (VpuMemDesc *)(mem_block_node->data);
		GST_CAT_DEBUG(vpualloc_debug, "freed %d bytes of VPU memory at virt addr %x phys addr %x", mem_block->nSize, mem_block->nVirtAddr, mem_block->nPhyAddr);
		VPU_DecFreeMem(mem_block);
		g_slice_free1(sizeof(VpuMemDesc), mem_block);
	}

	g_slist_free(*phys_mem_blocks);
	*phys_mem_blocks = NULL;

	return TRUE;
}

