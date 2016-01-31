/* GStreamer video decoder using the Freescale VPU hardware video engine
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


#ifndef GST_IMX_VPU_DECODER_H
#define GST_IMX_VPU_DECODER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include "imxvpuapi/imxvpuapi.h"

#include "decoder_context.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuDecoder GstImxVpuDecoder;
typedef struct _GstImxVpuDecoderClass GstImxVpuDecoderClass;


#define GST_TYPE_IMX_VPU_DECODER             (gst_imx_vpu_decoder_get_type())
#define GST_IMX_VPU_DECODER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DECODER, GstImxVpuDecoder))
#define GST_IMX_VPU_DECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DECODER, GstImxVpuDecoderClass))
#define GST_IS_IMX_VPU_DECODER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_DECODER))
#define GST_IS_IMX_VPU_DECODER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DECODER))


/**
 * GstImxVpuDecoder:
 *
 * Video decoding element based on the i.MX VPU.
 *
 * Internally, the decoder uses an imxvpuapi ImxVpuDecoder instance
 * to perform the actual decoding.
 *
 * GStreamer buffer pools and the VPU's own buffer pool mechanism are at
 * odds with each other, because both pick a buffer on their own, and
 * usually, their picks do not match. Since it is not possible to disable
 * the VPU pool, a trick is used.
 * The decoder uses a "decoder context", which represents a state where
 * a set of DMA-allocated framebuffers are registered with the VPU. This
 * creates the VPU pool; the VPU will later automatically pick one of
 * the framebuffers in this pool which are marked as "free". This context
 * is created whenever a new video format is set via @set_caps. Later,
 * during decoding, when an output gstbuffer is requested from the
 * GstVideoDecoder base class, this will cause a new GStreamer buffer pool
 * to be created in the @decide_allocation function. In this function,
 * a decoder_framebuffer_pool is created, and it is associated with the
 * current decoder context. This pool returns "empty" buffers, that is,
 * buffers which contain only GstMeta blocks, no GstMemory ones. After
 * getting the buffer, the decoder manually adds a GstMemory block to
 * this buffer with @gst_imx_vpu_framebuffer_array_set_framebuffer_in_gstbuffer.
 * As a result, the buffer contains the newly decoded imxvpu framebuffer
 * that the VPU just emitted. This makes sure the output gstbuffers conform
 * to the output of the VPU. It essentially circumvents the GstBufferPool
 * logic, but this necessary in this case.
 *
 * The decoder also keeps track of unfinished GstVideoCodecFrame instances
 * via the unfinished_frames_table. When the decoder is stopped, any
 * remaining unfinished frames will be released, avoiding memory leaks.
 */
struct _GstImxVpuDecoder
{
	GstVideoDecoder parent;

	ImxVpuDecoder *decoder;
	GstBuffer *codec_data;
	GstBuffer *bitstream_buffer;
	GstImxVpuDecoderContext *decoder_context;
	GstVideoCodecState *current_output_state;
	GstAllocator *phys_mem_allocator;
	guint num_additional_framebuffers;
	gint chroma_interleave;

	GHashTable *unfinished_frames_table;

	gboolean fatal_error;
};


struct _GstImxVpuDecoderClass
{
	GstVideoDecoderClass parent_class;
};


GType gst_imx_vpu_decoder_get_type(void);


G_END_DECLS


#endif
