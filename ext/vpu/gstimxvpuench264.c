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
#include "gstimxvpuench264.h"


enum
{
	PROP_0 = GST_IMX_VPU_ENC_BASE_PROP_VALUE,
	PROP_ENABLE_AUD
};


#define DEFAULT_ENABLE_AUD TRUE


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_h264_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_h264_debug


struct _GstImxVpuEncH264
{
	GstImxVpuEnc parent;

	gboolean enable_aud;
};


struct _GstImxVpuEncH264Class
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncH264, gst_imx_vpu_enc_h264, GST_TYPE_IMX_VPU_ENC)


static void gst_imx_vpu_enc_h264_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_h264_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_imx_vpu_enc_h264_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
static GstCaps* gst_imx_vpu_enc_h264_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_h264_class_init(GstImxVpuEncH264Class *klass)
{
	GObjectClass *object_class;
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_h264_debug, "imxvpuenc_h264", 0, "NXP i.MX VPU h.264 video encoder");

	object_class = G_OBJECT_CLASS(klass);
	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);

	imx_vpu_enc_class->set_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h264_set_encoder_property);
	imx_vpu_enc_class->get_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h264_get_encoder_property);
	imx_vpu_enc_class->set_open_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h264_set_open_params);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h264_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_H264, TRUE, TRUE, TRUE, TRUE, TRUE);

	imx_vpu_enc_class->use_idr_frame_type_for_keyframes = TRUE;

	g_object_class_install_property(
		object_class,
		PROP_ENABLE_AUD,
		g_param_spec_boolean(
			"enable-aud",
			"Enable access unit delimiters",
			"Enable the generation of access unit delimiters in the encoded output",
			DEFAULT_ENABLE_AUD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_vpu_enc_h264_init(GstImxVpuEncH264 *imx_vpu_enc_h264)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_h264));
	imx_vpu_enc_h264->enable_aud = DEFAULT_ENABLE_AUD;
}


static void gst_imx_vpu_enc_h264_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncH264 *imx_vpu_enc_h264 = GST_IMX_VPU_ENC_H264(object);

	switch (prop_id)
	{
		case PROP_ENABLE_AUD:
			GST_OBJECT_LOCK(imx_vpu_enc_h264);
			imx_vpu_enc_h264->enable_aud = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h264);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


static void gst_imx_vpu_enc_h264_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncH264 *imx_vpu_enc_h264 = GST_IMX_VPU_ENC_H264(object);

	switch (prop_id)
	{
		case PROP_ENABLE_AUD:
			GST_OBJECT_LOCK(imx_vpu_enc_h264);
			g_value_set_boolean(value, imx_vpu_enc_h264->enable_aud);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h264);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


gboolean gst_imx_vpu_enc_h264_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params)
{
	gboolean ret = TRUE;
	GstStructure *s;
	gchar const *str;
	GstCaps *allowed_srccaps;
	gboolean full_video_range;
	ImxVpuApiEncH264OpenParams *h264_params = &(open_params->format_specific_open_params.h264_open_params);

	allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));

	if (allowed_srccaps == NULL)
		allowed_srccaps = gst_pad_get_pad_template_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));
	if (G_UNLIKELY(allowed_srccaps == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set h.264 params; unable to get allowed src caps or src template caps");
		ret = FALSE;
		goto finish;
	}
	else if (G_UNLIKELY(gst_caps_is_empty(allowed_srccaps)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set h.264 params; downstream caps are empty");
		ret = FALSE;
		goto finish;
	}

	s = gst_caps_get_structure(allowed_srccaps, 0);

	str = gst_imx_vpu_get_string_from_structure_field(s, "profile");
	if (str != NULL)
	{
		if      (g_strcmp0(str, "constrained-baseline") == 0) h264_params->profile = IMX_VPU_API_H264_PROFILE_CONSTRAINED_BASELINE;
		else if (g_strcmp0(str, "baseline") == 0)             h264_params->profile = IMX_VPU_API_H264_PROFILE_BASELINE;
		else if (g_strcmp0(str, "main") == 0)                 h264_params->profile = IMX_VPU_API_H264_PROFILE_MAIN;
		else if (g_strcmp0(str, "high") == 0)                 h264_params->profile = IMX_VPU_API_H264_PROFILE_HIGH;
		else if (g_strcmp0(str, "high-10") == 0)              h264_params->profile = IMX_VPU_API_H264_PROFILE_HIGH10;
		else
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "unsupported h.264 profile \"%s\"", str);
			ret = FALSE;
			goto finish;
		}
	}

	str = gst_imx_vpu_get_string_from_structure_field(s, "level");
	if (str != NULL)
	{
		if      (g_strcmp0(str, "1") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_1;
		else if (g_strcmp0(str, "1b") == 0)  h264_params->level = IMX_VPU_API_H264_LEVEL_1B;
		else if (g_strcmp0(str, "1.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_1_1;
		else if (g_strcmp0(str, "1.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_1_2;
		else if (g_strcmp0(str, "1.3") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_1_3;
		else if (g_strcmp0(str, "2") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_2;
		else if (g_strcmp0(str, "2.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_2_1;
		else if (g_strcmp0(str, "2.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_2_2;
		else if (g_strcmp0(str, "3") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_3;
		else if (g_strcmp0(str, "3.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_3_1;
		else if (g_strcmp0(str, "3.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_3_2;
		else if (g_strcmp0(str, "4") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_4;
		else if (g_strcmp0(str, "4.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_4_1;
		else if (g_strcmp0(str, "4.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_4_2;
		else if (g_strcmp0(str, "5") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_5;
		else if (g_strcmp0(str, "5.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_5_1;
		else if (g_strcmp0(str, "5.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_5_2;
		else if (g_strcmp0(str, "6") == 0)   h264_params->level = IMX_VPU_API_H264_LEVEL_6;
		else if (g_strcmp0(str, "6.1") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_6_1;
		else if (g_strcmp0(str, "6.2") == 0) h264_params->level = IMX_VPU_API_H264_LEVEL_6_2;
		else
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "unsupported h.264 level \"%s\"", str);
			ret = FALSE;
			goto finish;
		}
	}

	GST_OBJECT_LOCK(imx_vpu_enc);
	h264_params->enable_access_unit_delimiters = GST_IMX_VPU_ENC_H264_CAST(imx_vpu_enc)->enable_aud;
	GST_OBJECT_UNLOCK(imx_vpu_enc);

	GST_INFO_OBJECT(imx_vpu_enc, "access unit delimiters enabled: %d", h264_params->enable_access_unit_delimiters);

	full_video_range = (GST_VIDEO_INFO_COLORIMETRY(&(imx_vpu_enc->in_video_info)).range == GST_VIDEO_COLOR_RANGE_0_255);
	open_params->flags |= (full_video_range ? IMX_VPU_API_ENC_H264_OPEN_PARAMS_FLAG_FULL_VIDEO_RANGE : 0);


finish:
	if (allowed_srccaps != NULL)
		gst_caps_unref(allowed_srccaps);

	return ret;
}


GstCaps* gst_imx_vpu_enc_h264_get_output_caps(G_GNUC_UNUSED GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	GstCaps *caps;
	gchar const *level_str, *profile_str;

	switch (stream_info->format_specific_open_params.h264_open_params.level)
	{
		case IMX_VPU_API_H264_LEVEL_1:   level_str = "1";   break;
		case IMX_VPU_API_H264_LEVEL_1B:  level_str = "1b";  break;
		case IMX_VPU_API_H264_LEVEL_1_1: level_str = "1.1"; break;
		case IMX_VPU_API_H264_LEVEL_1_2: level_str = "1.2"; break;
		case IMX_VPU_API_H264_LEVEL_1_3: level_str = "1.3"; break;
		case IMX_VPU_API_H264_LEVEL_2:   level_str = "2";   break;
		case IMX_VPU_API_H264_LEVEL_2_1: level_str = "2.1"; break;
		case IMX_VPU_API_H264_LEVEL_2_2: level_str = "2.2"; break;
		case IMX_VPU_API_H264_LEVEL_3:   level_str = "3";   break;
		case IMX_VPU_API_H264_LEVEL_3_1: level_str = "3.1"; break;
		case IMX_VPU_API_H264_LEVEL_3_2: level_str = "3.2"; break;
		case IMX_VPU_API_H264_LEVEL_4:   level_str = "4";   break;
		case IMX_VPU_API_H264_LEVEL_4_1: level_str = "4.1"; break;
		case IMX_VPU_API_H264_LEVEL_4_2: level_str = "4.2"; break;
		case IMX_VPU_API_H264_LEVEL_5:   level_str = "5";   break;
		case IMX_VPU_API_H264_LEVEL_5_1: level_str = "5.1"; break;
		case IMX_VPU_API_H264_LEVEL_5_2: level_str = "5.2"; break;
		case IMX_VPU_API_H264_LEVEL_6:   level_str = "6";   break;
		case IMX_VPU_API_H264_LEVEL_6_1: level_str = "6.1"; break;
		case IMX_VPU_API_H264_LEVEL_6_2: level_str = "6.2"; break;
		default: g_assert_not_reached();
	}

	switch (stream_info->format_specific_open_params.h264_open_params.profile)
	{
		case IMX_VPU_API_H264_PROFILE_CONSTRAINED_BASELINE: profile_str = "constrained-baseline"; break;
		case IMX_VPU_API_H264_PROFILE_BASELINE:             profile_str = "baseline";             break;
		case IMX_VPU_API_H264_PROFILE_MAIN:                 profile_str = "main";                 break;
		case IMX_VPU_API_H264_PROFILE_HIGH:                 profile_str = "high";                 break;
		case IMX_VPU_API_H264_PROFILE_HIGH10:               profile_str = "high-10";              break;
		default: g_assert_not_reached();
	}

	caps = gst_caps_new_simple(
		"video/x-h264",
		"stream-format", G_TYPE_STRING,     "byte-stream",
		/* "au" refers to "access unit". This is unrelated to access unit delimiters,
		 * and instead means that the encoder always produces entire access unit,
		 * which the CODA VPU does. */
		"alignment",     G_TYPE_STRING,     "au",
		"level",         G_TYPE_STRING,     level_str,
		"profile",       G_TYPE_STRING,     profile_str,
		"width",         G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_width),
		"height",        G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_height),
		"framerate",     GST_TYPE_FRACTION, (gint)(stream_info->frame_rate_numerator), (gint)(stream_info->frame_rate_denominator),
		NULL
	);

	return caps;
}
