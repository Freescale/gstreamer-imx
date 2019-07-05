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
#include "gstimxvpuencvp8.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_vp8_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_vp8_debug


struct _GstImxVpuEncVP8
{
	GstImxVpuEnc parent;
};


struct _GstImxVpuEncVP8Class
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncVP8, gst_imx_vpu_enc_vp8, GST_TYPE_IMX_VPU_ENC)


gboolean gst_imx_vpu_enc_vp8_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
GstCaps* gst_imx_vpu_enc_vp8_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_vp8_class_init(GstImxVpuEncVP8Class *klass)
{
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_vp8_debug, "imxvpuenc_vp8", 0, "NXP i.MX VPU VP8 video encoder");

	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);

	imx_vpu_enc_class->set_open_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_vp8_set_open_params);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_vp8_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_VP8, TRUE, TRUE, TRUE);
}


static void gst_imx_vpu_enc_vp8_init(GstImxVpuEncVP8 *imx_vpu_enc_vp8)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_vp8));
}


gboolean gst_imx_vpu_enc_vp8_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params)
{
	gboolean ret = TRUE;
	GstStructure *s;
	gchar const *str;
	GstCaps *allowed_srccaps;
	ImxVpuApiEncVP8Params *vp8_params = &(open_params->format_specific_params.vp8_params);

	allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));

	if (allowed_srccaps == NULL)
		allowed_srccaps = gst_pad_get_pad_template_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));
	if (G_UNLIKELY(allowed_srccaps == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set VP8 params; unable to get allowed src caps or src template caps");
		ret = FALSE;
		goto finish;
	}
	else if (G_UNLIKELY(gst_caps_is_empty(allowed_srccaps)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set VP8 params; downstream caps are empty");
		ret = FALSE;
		goto finish;
	}

	s = gst_caps_get_structure(allowed_srccaps, 0);

	str = gst_imx_vpu_get_string_from_structure_field(s, "profile");
	if (str != NULL)
	{
		if      (g_strcmp0(str, "0") == 0) vp8_params->profile = IMX_VPU_API_VP8_PROFILE_0;
		else if (g_strcmp0(str, "1") == 0) vp8_params->profile = IMX_VPU_API_VP8_PROFILE_1;
		else if (g_strcmp0(str, "2") == 0) vp8_params->profile = IMX_VPU_API_VP8_PROFILE_2;
		else if (g_strcmp0(str, "3") == 0) vp8_params->profile = IMX_VPU_API_VP8_PROFILE_3;
		else
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "unsupported VP8 profile \"%s\"", str);
			ret = FALSE;
			goto finish;
		}
	}


finish:
	if (allowed_srccaps != NULL)
		gst_caps_unref(allowed_srccaps);

	return ret;
}


GstCaps* gst_imx_vpu_enc_vp8_get_output_caps(G_GNUC_UNUSED GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	gchar const *profile_str;

	switch (stream_info->format_specific_params.vp8_params.profile)
	{
		case IMX_VPU_API_VP8_PROFILE_0: profile_str = "0"; break;
		case IMX_VPU_API_VP8_PROFILE_1: profile_str = "1"; break;
		case IMX_VPU_API_VP8_PROFILE_2: profile_str = "2"; break;
		case IMX_VPU_API_VP8_PROFILE_3: profile_str = "3"; break;
		default: g_assert_not_reached();
	}

	return gst_caps_new_simple(
		"video/x-vp8",
		"profile", G_TYPE_STRING, profile_str,
		NULL
	);
}
