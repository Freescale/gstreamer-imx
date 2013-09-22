/* VPU decoder specific allocator
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

#include <string.h>
#include <vpu_wrapper.h>
#include "allocator.h"


GST_DEBUG_CATEGORY_STATIC(fslvpuencallocator_debug);
#define GST_CAT_DEFAULT fslvpuencallocator_debug



static void gst_fsl_vpu_enc_allocator_finalize(GObject *object);

static gboolean gst_fsl_vpu_enc_alloc_phys_mem(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, gssize size);
static gboolean gst_fsl_vpu_enc_free_phys_mem(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory);
static gpointer gst_fsl_vpu_enc_map_phys_mem(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, gssize size, GstMapFlags flags);
static void gst_fsl_vpu_enc_unmap_phys_mem(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory);


G_DEFINE_TYPE(GstFslVpuEncAllocator, gst_fsl_vpu_enc_allocator, GST_TYPE_FSL_PHYS_MEM_ALLOCATOR)




static void gst_fsl_vpu_enc_mem_init(void)
{
	GstAllocator *allocator = g_object_new(gst_fsl_vpu_enc_allocator_get_type(), NULL);
	gst_allocator_register(GST_FSL_VPU_ENC_ALLOCATOR_MEM_TYPE, allocator);
}


GstAllocator* gst_fsl_vpu_enc_allocator_obtain(void)
{
	static GOnce dmabuf_allocator_once = G_ONCE_INIT;
	GstAllocator *allocator;

	g_once(&dmabuf_allocator_once, (GThreadFunc)gst_fsl_vpu_enc_mem_init, NULL);

	allocator = gst_allocator_find(GST_FSL_VPU_ENC_ALLOCATOR_MEM_TYPE);
	if (allocator == NULL)
		GST_WARNING("No allocator named %s found", GST_FSL_VPU_ENC_ALLOCATOR_MEM_TYPE);

	return allocator;
}


static gboolean gst_fsl_vpu_enc_alloc_phys_mem(G_GNUC_UNUSED GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, gssize size)
{
	VpuEncRetCode ret;
	VpuMemDesc mem_desc;

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize = size;
	ret = VPU_EncGetMem(&mem_desc);

	if (ret == VPU_ENC_RET_SUCCESS)
	{
		memory->mem.size         = mem_desc.nSize;
		memory->mapped_virt_addr = (gpointer)(mem_desc.nVirtAddr);
		memory->phys_addr        = (guintptr)(mem_desc.nPhyAddr);
		memory->cpu_addr         = (guintptr)(mem_desc.nCpuAddr);
		return TRUE;
	}
	else
		return FALSE;
}


static gboolean gst_fsl_vpu_enc_free_phys_mem(G_GNUC_UNUSED GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory)
{
        VpuEncRetCode ret;
        VpuMemDesc mem_desc;

	memset(&mem_desc, 0, sizeof(VpuMemDesc));
	mem_desc.nSize     = memory->mem.size;
	mem_desc.nVirtAddr = (unsigned long)(memory->mapped_virt_addr);
	mem_desc.nPhyAddr  = (unsigned long)(memory->phys_addr);
	mem_desc.nCpuAddr  = (unsigned long)(memory->cpu_addr);

	ret = VPU_EncFreeMem(&mem_desc);

	return (ret == VPU_ENC_RET_SUCCESS);
}


static gpointer gst_fsl_vpu_enc_map_phys_mem(G_GNUC_UNUSED GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, G_GNUC_UNUSED gssize size, G_GNUC_UNUSED GstMapFlags flags)
{
	return memory->mapped_virt_addr;
}


static void gst_fsl_vpu_enc_unmap_phys_mem(G_GNUC_UNUSED GstFslPhysMemAllocator *allocator, G_GNUC_UNUSED GstFslPhysMemory *memory)
{
}




static void gst_fsl_vpu_enc_allocator_class_init(GstFslVpuEncAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstFslPhysMemAllocatorClass *parent_class = GST_FSL_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_fsl_vpu_enc_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_fsl_vpu_enc_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_fsl_vpu_enc_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_fsl_vpu_enc_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_fsl_vpu_enc_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(fslvpuencallocator_debug, "fslvpuencallocator", 0, "Freescale VPU encoder physical memory/allocator");
}


static void gst_fsl_vpu_enc_allocator_init(GstFslVpuEncAllocator *allocator)
{
	GstAllocator *base = GST_ALLOCATOR(allocator);
	base->mem_type = GST_FSL_VPU_ENC_ALLOCATOR_MEM_TYPE;
}


static void gst_fsl_vpu_enc_allocator_finalize(GObject *object)
{
	GST_DEBUG_OBJECT(object, "shutting down FSL VPU encoder allocator");
	G_OBJECT_CLASS(gst_fsl_vpu_enc_allocator_parent_class)->finalize(object);
}

