/* Allocation functions for virtual and physical memory
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
#include <string.h>
#include <vpu_wrapper.h>
#include "alloc.h"


GST_DEBUG_CATEGORY_STATIC(fslalloc_debug);
#define GST_CAT_DEFAULT fslalloc_debug


/* TODO: allocate physical memory without the VPU wrapper if possible
 * (gets rid of the somewhat odd-looking dependency of gstfslcommon on the VPU wrapper) */


static void setup_debug_category(void)
{
	static gsize initialized = 0;

	if (g_once_init_enter(&initialized))
	{
		GST_DEBUG_CATEGORY_INIT(fslalloc_debug, "fslalloc", 0, "Freescale VPU allocation functions");
		g_once_init_leave(&initialized, 1);
	}
}


gboolean gst_fsl_alloc_virt_mem_block(unsigned char **mem_block, int size)
{
	setup_debug_category();

	*mem_block = (unsigned char *)g_try_malloc(size);
	if ((*mem_block) == NULL)
	{
		GST_ERROR("could not request %d bytes of heap memory", size);
		return FALSE;
	}
	else
		GST_DEBUG("allocated %d bytes of heap memory at virt addr %p", size, *mem_block);

	return TRUE;
}


void gst_fsl_append_virt_mem_block(unsigned char *mem_block, GSList **virt_mem_blocks)
{
	setup_debug_category();

	*virt_mem_blocks = g_slist_append(*virt_mem_blocks, (gpointer)mem_block);
}


gboolean gst_fsl_free_virt_mem_blocks(GSList **virt_mem_blocks)
{
	GSList *mem_block_node;

	setup_debug_category();

	g_assert(virt_mem_blocks != NULL);

	mem_block_node = *virt_mem_blocks;
	if (mem_block_node == NULL)
		return TRUE;

	for (; mem_block_node != NULL; mem_block_node = mem_block_node->next)
	{
		g_free(mem_block_node->data);
		GST_DEBUG("freed heap memory block at virt addr %p", mem_block_node->data);
	}

	g_slist_free(*virt_mem_blocks);
	*virt_mem_blocks = NULL;

	return TRUE;
}


gboolean gst_fsl_alloc_phys_mem_block(gst_fsl_phys_mem_block **mem_block, int size)
{
	VpuDecRetCode ret;
	VpuMemDesc mem_desc;

	setup_debug_category();

	*mem_block = g_slice_alloc(sizeof(gst_fsl_phys_mem_block));
	if (*mem_block == NULL)
	{
		GST_ERROR("could not allocate VPU memory descriptor: slice allocator failed");
		return FALSE;
	}

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize = size;
	ret = VPU_DecGetMem(&mem_desc);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR("failed to allocate %d bytes of physical memory", size);
		return FALSE;
	}

	(*mem_block)->size = mem_desc.nSize;
	(*mem_block)->virt_addr = (gpointer)(mem_desc.nVirtAddr);
	(*mem_block)->phys_addr = (gpointer)(mem_desc.nPhyAddr);
	(*mem_block)->cpu_addr  = (gpointer)(mem_desc.nCpuAddr);

	GST_DEBUG("allocated %d bytes of physical memory at virt addr %p phys addr %p cpu addr %p", size, (*mem_block)->virt_addr, (*mem_block)->phys_addr, (*mem_block)->cpu_addr);

	return TRUE;
}


gboolean gst_fsl_free_phys_mem_block(gst_fsl_phys_mem_block *mem_block)
{
	VpuDecRetCode ret;
	VpuMemDesc mem_desc;

	setup_debug_category();

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize = mem_block->size;
	mem_desc.nVirtAddr = (unsigned long)(mem_block->virt_addr);
	mem_desc.nPhyAddr = (unsigned long)(mem_block->phys_addr);
	mem_desc.nCpuAddr = (unsigned long)(mem_block->cpu_addr);

	ret = VPU_DecFreeMem(&mem_desc);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR("failed to free %u bytes of physical memory at virt addr %p phys addr %p cpu addr %p", mem_block->size, mem_block->virt_addr, mem_block->phys_addr, mem_block->cpu_addr);
		return FALSE;
	}
	else
	{
		GST_DEBUG("freed %u bytes of physical memory at virt addr %p phys addr %p cpu addr %p", mem_block->size, mem_block->virt_addr, mem_block->phys_addr, mem_block->cpu_addr);
		return TRUE;
	}
}


void gst_fsl_append_phys_mem_block(gst_fsl_phys_mem_block *mem_block, GSList **phys_mem_blocks)
{
	setup_debug_category();

	*phys_mem_blocks = g_slist_append(*phys_mem_blocks, (gpointer)mem_block);
}


gboolean gst_fsl_free_phys_mem_blocks(GSList **phys_mem_blocks)
{
	GSList *mem_block_node;

	setup_debug_category();

	g_assert(phys_mem_blocks != NULL);

	mem_block_node = *phys_mem_blocks;
	if (mem_block_node == NULL)
		return TRUE;

	for (; mem_block_node != NULL; mem_block_node = mem_block_node->next)
	{
		gst_fsl_phys_mem_block *mem_block = (gst_fsl_phys_mem_block *)(mem_block_node->data);
		gst_fsl_free_phys_mem_block(mem_block);
		g_slice_free1(sizeof(gst_fsl_phys_mem_block), mem_block);
	}

	g_slist_free(*phys_mem_blocks);
	*phys_mem_blocks = NULL;

	return TRUE;
}

