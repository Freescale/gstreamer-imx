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


#ifndef GST_IMX_VPU_FRAMEBUFFER_ARRAY_H
#define GST_IMX_VPU_FRAMEBUFFER_ARRAY_H

#include <gst/gst.h>
#include "imxvpuapi/imxvpuapi.h"
#include "../common/phys_mem_allocator.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuFramebufferArray GstImxVpuFramebufferArray;
typedef struct _GstImxVpuFramebufferArrayClass GstImxVpuFramebufferArrayClass;


#define GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY             (gst_imx_vpu_framebuffer_array_get_type())
#define GST_IMX_VPU_FRAMEBUFFER_ARRAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY, GstImxVpuFramebufferArray))
#define GST_IMX_VPU_FRAMEBUFFER_ARRAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY, GstImxVpuFramebufferArrayClass))
#define GST_IMX_VPU_FRAMEBUFFER_ARRAY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY, GstImxVpuFramebufferArrayClass))
#define GST_IS_IMX_VPU_FRAMEBUFFER_ARRAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY))
#define GST_IS_IMX_VPU_FRAMEBUFFER_ARRAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_FRAMEBUFFER_ARRAY))


/**
 * GstImxVpuFramebufferArray:
 *
 * Object containing an array of imxvpuapi framebuffers.
 * Both en- and decoder require one instance of this object to be
 * able to process video. The decoder uses the array as a memory pool.
 * The encoder uses the array for temporary storage during the encoding.
 *
 * The framebuffer_sizes struct contains sizes computed by imxvpuapi.
 * These are necessary for en- and decoder operation and for allocating
 * framebuffers with the proper size. Widths and heights are padded.
 * The original_frame_width and original_frame_height are the original,
 * non-padded widths/heights.
 */
struct _GstImxVpuFramebufferArray
{
	GstObject parent;

	ImxVpuFramebuffer *framebuffers;
	unsigned int num_framebuffers;

	ImxVpuFramebufferSizes framebuffer_sizes;
	unsigned int original_frame_width, original_frame_height;
	
	GstAllocator *allocator;
};


struct _GstImxVpuFramebufferArrayClass
{
	GstObjectClass parent_class;
};


/* Create ones framebuffer array instance.
 *
 * frame_width and frame_height do not have to be aligned sizes;
 * internally, the alignment is done automatically. The unmodified
 * frame_width/frame_height values are copied over to the
 * original_frame_width/original_frame_height members.
 * The contents of framebuffer_sizes is computed by the imxvpuapi
 * imx_vpu_calc_framebuffer_sizes() function.
 *
 * The return value is a floating GLib reference. See
 * gst_object_ref_sink() for details.
 */
GstImxVpuFramebufferArray * gst_imx_vpu_framebuffer_array_new(ImxVpuColorFormat color_format, unsigned int frame_width, unsigned int frame_height, unsigned int framebuffer_alignment, gboolean uses_interlacing, gboolean chroma_interleave, unsigned int num_framebuffers, GstImxPhysMemAllocator *phys_mem_allocator);

/* Returns a GstImxPhysMemory block associated with the given imxvpu framebuffer. */
GstImxPhysMemory* gst_imx_vpu_framebuffer_array_get_gst_phys_memory(ImxVpuFramebuffer *framebuffer);

/* Fills a GstBuffer with all the necessary GstMeta and memory blocks for the framebuffer.
 *
 * This inserts a GstVideoMeta, GstImxPhysMemMeta, and GstImxVpuFramebufferMeta.
 * The phys mem meta's x/y padding values are also computed.
 * Afterwards, the GstBuffer can be pushed downstream. Due to the phys mem meta,
 * downstream elements capable of zerocopy can access the framebuffer's DMA
 * memory directly.
 */
gboolean gst_imx_vpu_framebuffer_array_set_framebuffer_in_gstbuffer(GstImxVpuFramebufferArray *framebuffer_array, GstBuffer *buffer, ImxVpuFramebuffer *framebuffer);


G_END_DECLS


#endif
