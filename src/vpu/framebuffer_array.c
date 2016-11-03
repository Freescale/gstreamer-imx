/* Structure containing a framebuffer array that is registered with the VPU
 * Copyright (C) 2015  Carlos Rafael Giani
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
#include "framebuffer_array.h"
#include "allocator.h"
#include "../common/phys_mem_meta.h"
#include "vpu_framebuffer_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_framebuffer_array_debug);
#define GST_CAT_DEFAULT imx_vpu_framebuffer_array_debug


G_DEFINE_TYPE(GstImxVpuFramebufferArray, gst_imx_vpu_framebuffer_array, GST_TYPE_OBJECT)


static void gst_imx_vpu_framebuffer_array_finalize(GObject *object);






void gst_imx_vpu_framebuffer_array_class_init(GstImxVpuFramebufferArrayClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_vpu_framebuffer_array_finalize);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_framebuffer_array_debug, "imxvpuframebufferarray", 0, "Freescale i.MX VPU framebuffer array");
}


void gst_imx_vpu_framebuffer_array_init(GstImxVpuFramebufferArray *framebuffer_array)
{
	framebuffer_array->framebuffers = NULL;
	framebuffer_array->num_framebuffers = 0;

	memset(&(framebuffer_array->framebuffer_sizes), 0, sizeof(framebuffer_array->framebuffer_sizes));
	framebuffer_array->original_frame_width = 0;
	framebuffer_array->original_frame_height = 0;

	framebuffer_array->allocator = NULL;

	GST_DEBUG_OBJECT(framebuffer_array, "initialized framebuffer array %p", (gpointer)framebuffer_array);
}


static void gst_imx_vpu_framebuffer_array_finalize(GObject *object)
{
	guint i;
	GstImxVpuFramebufferArray *framebuffer_array = GST_IMX_VPU_FRAMEBUFFER_ARRAY(object);

	GST_DEBUG_OBJECT(object, "shutting down framebuffer array %p", (gpointer)object);

	if (framebuffer_array->framebuffers != NULL)
	{
		for (i = 0; i < framebuffer_array->num_framebuffers; ++i)
		{
			ImxVpuFramebuffer *framebuffer = &(framebuffer_array->framebuffers[i]);
			GstImxPhysMemory *memory = gst_imx_vpu_framebuffer_array_get_gst_phys_memory(framebuffer);

			if(memory != NULL)
			{
				GST_DEBUG_OBJECT(object, "freeing gstmemory block %p with physical address %" GST_IMX_PHYS_ADDR_FORMAT " and ref count %d", (gpointer)memory, memory->phys_addr, GST_MINI_OBJECT_REFCOUNT_VALUE(memory));

				/* at this point, the memory's refcount is 1, so unref'ing will deallocate it */
				gst_memory_unref((GstMemory *)memory);
			}
		}

		g_slice_free1(sizeof(ImxVpuFramebuffer) * framebuffer_array->num_framebuffers, framebuffer_array->framebuffers);
	}

	if (framebuffer_array->allocator != NULL)
		gst_object_unref(GST_OBJECT(framebuffer_array->allocator));

	G_OBJECT_CLASS(gst_imx_vpu_framebuffer_array_parent_class)->finalize(object);
}


GstImxVpuFramebufferArray * gst_imx_vpu_framebuffer_array_new(ImxVpuColorFormat color_format, unsigned int frame_width, unsigned int frame_height, unsigned int framebuffer_alignment, gboolean uses_interlacing, gboolean chroma_interleave, unsigned int num_framebuffers, GstImxPhysMemAllocator *phys_mem_allocator)
{
	guint i;
	GstImxVpuFramebufferArray *framebuffer_array = g_object_new(gst_imx_vpu_framebuffer_array_get_type(), NULL);

	framebuffer_array->original_frame_width = frame_width;
	framebuffer_array->original_frame_height = frame_height;

	imx_vpu_calc_framebuffer_sizes(
		color_format,
		frame_width, frame_height,
		framebuffer_alignment,
		uses_interlacing,
		chroma_interleave,
		&(framebuffer_array->framebuffer_sizes)
	);

	framebuffer_array->framebuffers = (ImxVpuFramebuffer *)g_slice_alloc(sizeof(ImxVpuFramebuffer) * num_framebuffers);
	memset(framebuffer_array->framebuffers, 0, sizeof(ImxVpuFramebuffer) * num_framebuffers);
	framebuffer_array->num_framebuffers = num_framebuffers;

	framebuffer_array->allocator = (GstAllocator *)gst_object_ref(GST_OBJECT(phys_mem_allocator));

	GST_DEBUG_OBJECT(framebuffer_array, "allocating and registering %u framebuffers", num_framebuffers);

	for (i = 0; i < num_framebuffers; ++i)
	{
		GstImxPhysMemory *memory;
		ImxVpuFramebuffer *framebuffer = &(framebuffer_array->framebuffers[i]);

		// TODO: pass on framebuffer_alignment to the internal allocator somehow
		memory = (GstImxPhysMemory *)gst_allocator_alloc(
			framebuffer_array->allocator,
			framebuffer_array->framebuffer_sizes.total_size,
			NULL
		);

		if (memory == NULL)
			goto cleanup;

		/* When filling in the params, use "memory" as the user-defined context parameter
		 * This is useful to be able to later determine which memory block this framebuffer
		 * is associated with. See gst_imx_vpu_framebuffer_array_get_gst_phys_memory(). */
		imx_vpu_fill_framebuffer_params(
			framebuffer,
			&(framebuffer_array->framebuffer_sizes),
			GST_IMX_VPU_MEM_IMXVPUAPI_DMA_BUFFER(memory),
			memory
		);

		GST_DEBUG_OBJECT(
			framebuffer_array,
			"memory block %p   physical address %" GST_IMX_PHYS_ADDR_FORMAT "  ref count %d",
			(gpointer)memory,
			memory->phys_addr,
			GST_MINI_OBJECT_REFCOUNT_VALUE(memory)
		);
	}

	return framebuffer_array;

cleanup:
	gst_object_unref(GST_OBJECT(framebuffer_array));
	return NULL;
}


GstImxPhysMemory* gst_imx_vpu_framebuffer_array_get_gst_phys_memory(ImxVpuFramebuffer *framebuffer)
{
	return (GstImxPhysMemory *)(framebuffer->context);
}


gboolean gst_imx_vpu_framebuffer_array_set_framebuffer_in_gstbuffer(GstImxVpuFramebufferArray *framebuffer_array, GstBuffer *buffer, ImxVpuFramebuffer *framebuffer)
{
	GstVideoMeta *video_meta;
	GstImxVpuFramebufferMeta *vpu_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxPhysMemory *memory;

	video_meta = gst_buffer_get_video_meta(buffer);
	if (video_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no video metadata", (gpointer)buffer);
		return FALSE;
	}

	vpu_meta = GST_IMX_VPU_FRAMEBUFFER_META_GET(buffer);
	if (vpu_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no VPU metadata", (gpointer)buffer);
		return FALSE;
	}

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer);
	if (phys_mem_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no phys mem metadata", (gpointer)buffer);
		return FALSE;
	}

	{
		gsize x_padding = 0, y_padding = 0;

		if (framebuffer_array->framebuffer_sizes.aligned_frame_width > video_meta->width)
			x_padding = framebuffer_array->framebuffer_sizes.aligned_frame_width - video_meta->width;
		if (framebuffer_array->framebuffer_sizes.aligned_frame_height > video_meta->height)
			y_padding = framebuffer_array->framebuffer_sizes.aligned_frame_height - video_meta->height;

		vpu_meta->framebuffer = framebuffer;

		phys_mem_meta->phys_addr = (gst_imx_phys_addr_t)imx_vpu_dma_buffer_get_physical_address(framebuffer->dma_buffer);
		phys_mem_meta->x_padding = x_padding;
		phys_mem_meta->y_padding = y_padding;

		GST_LOG("setting phys mem meta for buffer with pointer %p: phys addr %" GST_IMX_PHYS_ADDR_FORMAT " x/y padding %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT, (gpointer)buffer, phys_mem_meta->phys_addr, phys_mem_meta->x_padding, phys_mem_meta->y_padding);
	}

	memory = gst_imx_vpu_framebuffer_array_get_gst_phys_memory(framebuffer);

	/* remove any existing memory blocks */
	gst_buffer_remove_all_memory(buffer);
	/* and append the new memory block
	 * the memory is ref'd to prevent deallocation when it is later removed
	 * (either because this function is called again, or because the buffer
	 * is deallocated); refcount is 1 already at this point, since the memory
	 * is ref'd inside the framebuffer array, and unref'd when the array is
	 * shut down */
	gst_buffer_append_memory(buffer, gst_memory_ref((GstMemory *)memory));

	return TRUE;
}
