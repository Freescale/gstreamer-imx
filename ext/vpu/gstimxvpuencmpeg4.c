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
#include "gstimxvpuencmpeg4.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_mpeg4_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_mpeg4_debug


enum
{
	PROP_0 = GST_IMX_VPU_ENC_BASE_PROP_VALUE,
	PROP_ENABLE_DATA_PARTITIONING,
	PROP_ENABLE_REVERSIBLE_VLC,
	PROP_INTRA_DC_VLC_THR,
	PROP_ENABLE_HEC,
	PROP_VERSION_ID
};


#define DEFAULT_ENABLE_DATA_PARTITIONING FALSE
#define DEFAULT_ENABLE_REVERSIBLE_VLC    FALSE
#define DEFAULT_INTRA_DC_VLC_THR         0
#define DEFAULT_ENABLE_HEC               FALSE
#define DEFAULT_VERSION_ID               2


struct _GstImxVpuEncMPEG4
{
	GstImxVpuEnc parent;

	gboolean enable_data_partitioning;
	gboolean enable_reversible_vlc;
	guint intra_dc_vlc_thr;
	gboolean enable_hec;
	guint version_id;
};


struct _GstImxVpuEncMPEG4Class
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncMPEG4, gst_imx_vpu_enc_mpeg4, GST_TYPE_IMX_VPU_ENC)


static void gst_imx_vpu_enc_mpeg4_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_mpeg4_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

gboolean gst_imx_vpu_enc_mpeg4_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
GstCaps* gst_imx_vpu_enc_mpeg4_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_mpeg4_class_init(GstImxVpuEncMPEG4Class *klass)
{
	GObjectClass *object_class;
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_mpeg4_debug, "imxvpuenc_mpeg4", 0, "NXP i.MX VPU MPEG4 video encoder");

	object_class = G_OBJECT_CLASS(klass);

	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);
	imx_vpu_enc_class->set_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_mpeg4_set_encoder_property);
	imx_vpu_enc_class->get_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_mpeg4_get_encoder_property);
	imx_vpu_enc_class->set_open_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_mpeg4_set_open_params);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_mpeg4_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_MPEG4, TRUE, TRUE, TRUE);

	g_object_class_install_property(
		object_class,
		PROP_ENABLE_DATA_PARTITIONING,
		g_param_spec_boolean(
			"enable-data-partitioning",
			"Enable data partitioning",
			"Enable MPEG-4 data partitioning mode",
			DEFAULT_ENABLE_DATA_PARTITIONING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_REVERSIBLE_VLC,
		g_param_spec_boolean(
			"enable-reversible-vlc",
			"Enable reversible VLC",
			"Enable reversible variable length codes",
			DEFAULT_ENABLE_REVERSIBLE_VLC,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_INTRA_DC_VLC_THR,
		g_param_spec_uint(
			"intra-dc-vlc-thr",
			"Intra DC VLC threshold",
			"MPEG-4 part 2 intra_dc_vlc_thr mechanism selector for switching between two VLC's for coding of intra DC coefficients",
			0, 7,
			DEFAULT_INTRA_DC_VLC_THR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_HEC,
		g_param_spec_boolean(
			"enable-hec",
			"Enable HEC",
			"Enable header extension code",
			DEFAULT_ENABLE_HEC,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_VERSION_ID,
		g_param_spec_uint(
			"version-id",
			"Version ID",
			"MPEG-4 part 2 standard version ID",
			1, 2,
			DEFAULT_VERSION_ID,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_vpu_enc_mpeg4_init(GstImxVpuEncMPEG4 *imx_vpu_enc_mpeg4)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_mpeg4));

	imx_vpu_enc_mpeg4->enable_data_partitioning = DEFAULT_ENABLE_DATA_PARTITIONING;
	imx_vpu_enc_mpeg4->enable_reversible_vlc    = DEFAULT_ENABLE_REVERSIBLE_VLC;
	imx_vpu_enc_mpeg4->intra_dc_vlc_thr         = DEFAULT_INTRA_DC_VLC_THR;
	imx_vpu_enc_mpeg4->enable_hec               = DEFAULT_ENABLE_HEC;
	imx_vpu_enc_mpeg4->version_id               = DEFAULT_VERSION_ID;
}


static void gst_imx_vpu_enc_mpeg4_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncMPEG4 *imx_vpu_enc_mpeg4 = GST_IMX_VPU_ENC_MPEG4(object);

	switch (prop_id)
	{
		case PROP_ENABLE_DATA_PARTITIONING:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			imx_vpu_enc_mpeg4->enable_data_partitioning = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_ENABLE_REVERSIBLE_VLC:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			imx_vpu_enc_mpeg4->enable_reversible_vlc = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_INTRA_DC_VLC_THR:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			imx_vpu_enc_mpeg4->intra_dc_vlc_thr = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_ENABLE_HEC:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			imx_vpu_enc_mpeg4->enable_hec = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_VERSION_ID:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			imx_vpu_enc_mpeg4->version_id = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


static void gst_imx_vpu_enc_mpeg4_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncMPEG4 *imx_vpu_enc_mpeg4 = GST_IMX_VPU_ENC_MPEG4(object);

	switch (prop_id)
	{
		case PROP_ENABLE_DATA_PARTITIONING:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			g_value_set_boolean(value, imx_vpu_enc_mpeg4->enable_data_partitioning);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_ENABLE_REVERSIBLE_VLC:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			g_value_set_boolean(value, imx_vpu_enc_mpeg4->enable_reversible_vlc);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_INTRA_DC_VLC_THR:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			g_value_set_uint(value, imx_vpu_enc_mpeg4->intra_dc_vlc_thr);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_ENABLE_HEC:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			g_value_set_boolean(value, imx_vpu_enc_mpeg4->enable_hec);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		case PROP_VERSION_ID:
			GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
			g_value_set_uint(value, imx_vpu_enc_mpeg4->version_id);
			GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


gboolean gst_imx_vpu_enc_mpeg4_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params)
{
	GstImxVpuEncMPEG4 *imx_vpu_enc_mpeg4 = GST_IMX_VPU_ENC_MPEG4_CAST(imx_vpu_enc);

	GST_OBJECT_LOCK(imx_vpu_enc_mpeg4);
	open_params->format_specific_params.mpeg4_params.enable_data_partitioning = imx_vpu_enc_mpeg4->enable_data_partitioning;
	open_params->format_specific_params.mpeg4_params.enable_reversible_vlc    = imx_vpu_enc_mpeg4->enable_reversible_vlc;
	open_params->format_specific_params.mpeg4_params.intra_dc_vlc_thr         = imx_vpu_enc_mpeg4->intra_dc_vlc_thr;
	open_params->format_specific_params.mpeg4_params.enable_hec               = imx_vpu_enc_mpeg4->enable_hec;
	open_params->format_specific_params.mpeg4_params.version_id               = imx_vpu_enc_mpeg4->version_id;
	GST_OBJECT_UNLOCK(imx_vpu_enc_mpeg4);

	return TRUE;
}


GstCaps* gst_imx_vpu_enc_mpeg4_get_output_caps(G_GNUC_UNUSED GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	return gst_caps_new_simple(
		"video/mpeg",
		"mpegversion",  G_TYPE_INT,        (gint)4,
		"width",        G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_width),
		"height",       G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_height),
		"framerate",    GST_TYPE_FRACTION, (gint)(stream_info->frame_rate_numerator), (gint)(stream_info->frame_rate_denominator),
		"parsed",       G_TYPE_BOOLEAN,    (gboolean)TRUE,
		"systemstream", G_TYPE_BOOLEAN,    (gboolean)FALSE,
		NULL
	);
}
