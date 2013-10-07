/* GStreamer video encoder base class using the Freescale VPU hardware video engine
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


#ifndef GST_IMX_VPU_ENCODER_BASE_ENC_H
#define GST_IMX_VPU_ENCODER_BASE_ENC_H

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <vpu_wrapper.h>

#include "../../common/phys_mem_allocator.h"
#include "../framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuBaseEnc GstImxVpuBaseEnc;
typedef struct _GstImxVpuBaseEncClass GstImxVpuBaseEncClass;


#define GST_TYPE_IMX_VPU_BASE_ENC             (gst_imx_vpu_base_enc_get_type())
#define GST_IMX_VPU_BASE_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_BASE_ENC, GstImxVpuBaseEnc))
#define GST_IMX_VPU_BASE_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_BASE_ENC, GstImxVpuBaseEncClass))
#define GST_IMX_VPU_BASE_ENC_CAST(obj)        ((GstImxVpuBaseEnc)(obj))
#define GST_IS_IMX_VPU_BASE_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_BASE_ENC))
#define GST_IS_IMX_VPU_BASE_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_BASE_ENC))


struct _GstImxVpuBaseEnc
{
	GstVideoEncoder parent;

	VpuEncHandle handle;

	VpuEncInitInfo init_info;
	VpuMemInfo mem_info;

	GstVideoInfo video_info;

	VpuEncOpenParamSimp open_param;

	gboolean vpu_inst_opened, gen_second_iframe;

	GstImxVpuFramebuffers *framebuffers;
	GstImxPhysMemory *output_phys_buffer;

	GstBufferPool *internal_bufferpool;
	GstBuffer *internal_input_buffer;

	GSList *virt_enc_mem_blocks, *phys_enc_mem_blocks;

	guint gop_size;
	guint bitrate;
};


struct _GstImxVpuBaseEncClass
{
	GstVideoEncoderClass parent_class;
	gint inst_counter;

	gboolean (*set_open_params)(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParamSimp *open_param);
	GstCaps* (*get_output_caps)(GstImxVpuBaseEnc *vpu_base_enc);
	gboolean (*set_frame_enc_params)(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParamSimp *open_param);
	gsize (*fill_output_buffer)(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, void *encoded_data_addr, gsize encoded_data_size, gboolean contains_header);
	
};


GType gst_imx_vpu_base_enc_get_type(void);


G_END_DECLS


#endif

