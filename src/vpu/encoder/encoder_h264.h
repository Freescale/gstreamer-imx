/* GStreamer h.264 video encoder using the Freescale VPU hardware video engine
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


#ifndef GST_FSL_VPU_ENCODER_H264_H
#define GST_FSL_VPU_ENCODER_H264_H

#include "base_enc.h"


G_BEGIN_DECLS


typedef struct _GstFslVpuH264Enc GstFslVpuH264Enc;
typedef struct _GstFslVpuH264EncClass GstFslVpuH264EncClass;


#define GST_TYPE_FSL_VPU_H264_ENC             (gst_fsl_vpu_h264_enc_get_type())
#define GST_FSL_VPU_H264_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_H264_ENC, GstFslVpuH264Enc))
#define GST_FSL_VPU_H264_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_H264_ENC, GstFslVpuH264EncClass))
#define GST_FSL_VPU_H264_ENC_CAST(obj)        ((GstFslVpuH264Enc)(obj))
#define GST_IS_FSL_VPU_H264_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_VPU_H264_ENC))
#define GST_IS_FSL_VPU_H264_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_VPU_H264_ENC))


struct _GstFslVpuH264Enc
{
	GstFslVpuBaseEnc parent;
};


struct _GstFslVpuH264EncClass
{
	GstFslVpuBaseEncClass parent_class;
};


GType gst_fsl_vpu_h264_enc_get_type(void);


G_END_DECLS


#endif

