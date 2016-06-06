/* Base class for video encoders using the Freescale VPU hardware video engine
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


#ifndef GST_IMX_VPU_ENCODER_BASE_H
#define GST_IMX_VPU_ENCODER_BASE_H


#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include "imxvpuapi/imxvpuapi.h"
#include "framebuffer_array.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuEncoderBase GstImxVpuEncoderBase;
typedef struct _GstImxVpuEncoderBaseClass GstImxVpuEncoderBaseClass;


#define GST_TYPE_IMX_VPU_ENCODER_BASE             (gst_imx_vpu_encoder_base_get_type())
#define GST_IMX_VPU_ENCODER_BASE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_ENCODER_BASE, GstImxVpuEncoderBase))
#define GST_IMX_VPU_ENCODER_BASE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_ENCODER_BASE, GstImxVpuEncoderBaseClass))
#define GST_IS_IMX_VPU_ENCODER_BASE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_ENCODER_BASE))
#define GST_IS_IMX_VPU_ENCODER_BASE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_ENCODER_BASE))


struct _GstImxVpuEncoderBase
{
	GstVideoEncoder parent;

	/* imxvpuapi encoder. */
	ImxVpuEncoder *encoder;
	/* Bitstream DMA buffer required by the imxvpuapi VPU encoder. */
	GstBuffer *bitstream_buffer;
	/* Allocator for the bitstream buffer, the framebuffer array,
	 * and any other DMA buffer. */
	GstAllocator *phys_mem_allocator;

	/* Initial parameters to be used when opening the imxvpuapi encoder.
	 * Its fields are written to in gst_imx_vpu_encoder_base_set_format(). */
	ImxVpuEncOpenParams open_params;
	/* Initial information communicated by the VPU. Needed for allocating
	 * the bitstream buffer and the framebuffers. */
	ImxVpuEncInitialInfo initial_info;

	/* Structures for internal framebuffers. These are used
	 * in case the incoming data is not using DMA buffers. Such data
	 * needs to be copied to a DMA buffer, otherwise the VPU encoder
	 * cannot read it. If upstream delivers buffers that are
	 * physically contiguous, then they qualify as DMA buffers, and
	 * thus can be used directly, without these internal helper
	 * buffers. */
	GstBufferPool *internal_input_bufferpool;
	GstBuffer *internal_input_buffer;

	/* Structures for incoming data. These are needed for providing
	 * the imxvpuapi encoder functions with input. They are used
	 * even if upstream delivers DMA buffers. */
	ImxVpuRawFrame input_frame;
	ImxVpuFramebuffer input_framebuffer;
	ImxVpuWrappedDMABuffer input_dmabuffer;

	/* The VPU encoder needs this framebuffer array as a backing store
	 * for temporary data during encoding. Unlike with the VPU decoder,
	 * this array is not used as a framebuffer pool. */
	GstImxVpuFramebufferArray *framebuffer_array;

	/* GstVideoInfo structure describing the input video format. */
	GstVideoInfo video_info;

	/* These are set by the GObject properties of the encoder */
	guint gop_size;
	guint bitrate;
	gint slice_size;
	guint intra_refresh;
	ImxVpuEncMESearchRanges me_search_range;

	/* These are used during the actual encoding. The output buffer
	 * is allocated and mapped to receive the encoded data. */
	GstBuffer *output_buffer;
	GstMapInfo output_buffer_map_info;
};


/**
 * GstImxVpuEncoderBaseClass:
 *
 * Base class for encoders using the i.MX VPU.
 * Subclasses must at least set @codec_format and define @get_output_caps.
 *
 * @parent_class:          The parent class structure
 * @codec_format:          Required.
 *                         imxvpu codec format identifier, specifying which format
 *                         the subclass encodes to.
 * @set_open_params:       Optional.
 *                         Gives the subclass the chance to set additional values
 *                         in the open_params structure. Returns TRUE if the call
 *                         succeeded, and FALSE otherwise.
 * @get_output_caps:       Required.
 *                         Returns fixated caps to use for the srcpad. The base class
 *                         takes ownership over the returned caps, meaning they eventually
 *                         get unref'd by the base class.
 * @set_frame_enc_params:  Optional.
 *                         Gives the subclass the chance to set additional values in the
 *                         enc_params structure.
 *                         Returns TRUE if the call succeeded, and FALSE otherwise.
 * @process_output_buffer: Optional.
 *                         Allows for modifying a buffer containing encoded output data.
 *                         If the subclass creates a new buffer for the output data,
 *                         *output_buffer must point to this new buffer, and the previous
 *                         *output_buffer must be unref'd.
 *                         Returns TRUE if the call succeeded, and FALSE otherwise.
 */
struct _GstImxVpuEncoderBaseClass
{
	GstVideoEncoderClass parent_class;

	ImxVpuCodecFormat codec_format;

	gboolean (*set_open_params)(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecState *input_state, ImxVpuEncOpenParams *open_params);
	GstCaps* (*get_output_caps)(GstImxVpuEncoderBase *vpu_encoder_base);
	gboolean (*set_frame_enc_params)(GstImxVpuEncoderBase *vpu_encoder_base, ImxVpuEncParams *enc_params);
	gboolean (*process_output_buffer)(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecFrame *frame, GstBuffer **output_buffer);
};


GType gst_imx_vpu_encoder_base_get_type(void);


/* Returns a const pointer to the open params of the encoder. The returned value's fields
 * are considered read only, and must not be modified at all. */
ImxVpuEncOpenParams const * gst_imx_vpu_encoder_base_get_open_params(GstImxVpuEncoderBase *vpu_encoder_base);


G_END_DECLS


#endif
