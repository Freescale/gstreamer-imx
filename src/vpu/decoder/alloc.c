/* VPU decoder specific allocation functions
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


static gboolean gst_fsl_vpu_dec_alloc_phys_mem(gsize size, gst_fsl_phys_mem_block *block)
{
	VpuDecRetCode ret;
	VpuMemDesc mem_desc;

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize = size;
	ret = VPU_DecGetMem(&mem_desc);

	if (ret == VPU_DEC_RET_SUCCESS)
	{
		block->size      = mem_desc.nSize;
		block->virt_addr = (gpointer)(mem_desc.nVirtAddr);
		block->phys_addr = (gpointer)(mem_desc.nPhyAddr);
		block->cpu_addr  = (gpointer)(mem_desc.nCpuAddr);
		return TRUE;
	}
	else
		return FALSE;
}


static gboolean gst_fsl_vpu_dec_free_phys_mem(gst_fsl_phys_mem_block *block)
{
        VpuDecRetCode ret;
        VpuMemDesc mem_desc;

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize = block->size;
	mem_desc.nVirtAddr = (unsigned long)(block->virt_addr);
	mem_desc.nPhyAddr = (unsigned long)(block->phys_addr);
	mem_desc.nCpuAddr = (unsigned long)(block->cpu_addr);

	ret = VPU_DecFreeMem(&mem_desc);

	return (ret == VPU_DEC_RET_SUCCESS);
}


gst_fsl_phys_mem_allocator gst_fsl_vpu_dec_alloc =
{
	gst_fsl_vpu_dec_alloc_phys_mem,
	gst_fsl_vpu_dec_free_phys_mem
};

