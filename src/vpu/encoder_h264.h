/* GStreamer h.264 video encoder using the Freescale VPU hardware video engine
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


#ifndef GST_IMX_VPU_ENCODER_H264_H
#define GST_IMX_VPU_ENCODER_H264_H

#include "encoder_base.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuEncoderH264 GstImxVpuEncoderH264;
typedef struct _GstImxVpuEncoderH264Class GstImxVpuEncoderH264Class;


#define GST_TYPE_IMX_VPU_ENCODER_H264             (gst_imx_vpu_encoder_h264_get_type())
#define GST_IMX_VPU_ENCODER_H264(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_ENCODER_H264, GstImxVpuEncoderH264))
#define GST_IMX_VPU_ENCODER_H264_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_ENCODER_H264, GstImxVpuEncoderH264Class))
#define GST_IMX_VPU_ENCODER_H264_CAST(obj)        ((GstImxVpuEncoderH264)(obj))
#define GST_IS_IMX_VPU_ENCODER_H264(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_ENCODER_H264))
#define GST_IS_IMX_VPU_ENCODER_H264_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_ENCODER_H264))


struct _GstImxVpuEncoderH264
{
	GstImxVpuEncoderBase parent;

	guint quant_param;
	guint idr_interval;
	gboolean produce_access_units;
	guint frame_count;
	ImxVpuDMABuffer *cbcr_buffer;
	unsigned long cbcr_physaddr;
};


struct _GstImxVpuEncoderH264Class
{
	GstImxVpuEncoderBaseClass parent_class;
};


GType gst_imx_vpu_encoder_h264_get_type(void);


G_END_DECLS


#endif
