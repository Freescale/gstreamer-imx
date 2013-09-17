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


#ifndef GST_FSL_VPU_ENCODER_BASE_ENC_H
#define GST_FSL_VPU_ENCODER_BASE_ENC_H

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <vpu_wrapper.h>

#include "../common/alloc.h"
#include "../framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstFslVpuBaseEnc GstFslVpuBaseEnc;
typedef struct _GstFslVpuBaseEncClass GstFslVpuBaseEncClass;


#define GST_TYPE_FSL_VPU_BASE_ENC             (gst_fsl_vpu_base_enc_get_type())
#define GST_FSL_VPU_BASE_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_BASE_ENC, GstFslVpuBaseEnc))
#define GST_FSL_VPU_BASE_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_BASE_ENC, GstFslVpuBaseEncClass))
#define GST_FSL_VPU_BASE_ENC_CAST(obj)        ((GstFslVpuBaseEnc)(obj))
#define GST_IS_FSL_VPU_BASE_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_VPU_BASE_ENC))
#define GST_IS_FSL_VPU_BASE_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_VPU_BASE_ENC))


struct _GstFslVpuBaseEnc
{
	GstVideoEncoder parent;

	VpuEncHandle handle;

	VpuEncInitInfo init_info;
	VpuMemInfo mem_info;

	GstVideoInfo video_info;

	VpuEncOpenParam open_param;

	gboolean vpu_inst_opened;

	GstFslVpuFramebuffers *framebuffers;
	gst_fsl_phys_mem_block *output_phys_buffer;

	GSList *virt_enc_mem_blocks, *phys_enc_mem_blocks;
};


struct _GstFslVpuBaseEncClass
{
	GstVideoEncoderClass parent_class;
	gint inst_counter;

	gboolean (*set_open_params)(GstFslVpuBaseEnc *vpu_base_enc, VpuEncOpenParam *open_param);
	GstCaps* (*get_output_caps)(GstFslVpuBaseEnc *vpu_base_enc);
	gboolean (*set_frame_enc_params)(GstFslVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParam *open_param);
	
};


GType gst_fsl_vpu_base_enc_get_type(void);


G_END_DECLS


#endif

