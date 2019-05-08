/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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
#include "gstimxvpucommon.h"
#include "gstimxvpuenc.h"
#include "gstimxvpuencjpeg.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_jpeg_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_jpeg_debug


struct _GstImxVpuEncJPEG
{
	GstImxVpuEnc parent;
};


struct _GstImxVpuEncJPEGClass
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncJPEG, gst_imx_vpu_enc_jpeg, GST_TYPE_IMX_VPU_ENC);


GstCaps* gst_imx_vpu_enc_jpeg_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_jpeg_class_init(GstImxVpuEncJPEGClass *klass)
{
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_jpeg_debug, "imxvpuenc_jpeg", 0, "NXP i.MX VPU JPEG video encoder");

	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_jpeg_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_JPEG, FALSE, TRUE, FALSE);
}


static void gst_imx_vpu_enc_jpeg_init(GstImxVpuEncJPEG *imx_vpu_enc_jpeg)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_jpeg));
}


GstCaps* gst_imx_vpu_enc_jpeg_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	GstVideoInfo *info = &(imx_vpu_enc->in_video_info);

	return gst_caps_new_simple(
		"image/jpeg",
		"format",    G_TYPE_STRING,     gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(info)),
		"width",     G_TYPE_INT,        GST_VIDEO_INFO_WIDTH(info),
		"height",    G_TYPE_INT,        GST_VIDEO_INFO_HEIGHT(info),
		"framerate", GST_TYPE_FRACTION, (gint)(stream_info->frame_rate_numerator), (gint)(stream_info->frame_rate_denominator),
		"parsed",    G_TYPE_BOOLEAN,    (gboolean)TRUE,
		NULL
	);
}
