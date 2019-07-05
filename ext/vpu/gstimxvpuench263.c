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
#include "gstimxvpuench263.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_h263_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_h263_debug


enum
{
	PROP_0 = GST_IMX_VPU_ENC_BASE_PROP_VALUE,
	PROP_ENABLE_ANNEX_I,
	PROP_ENABLE_ANNEX_J,
	PROP_ENABLE_ANNEX_K,
	PROP_ENABLE_ANNEX_T
};


#define DEFAULT_ENABLE_ANNEX_I FALSE
#define DEFAULT_ENABLE_ANNEX_J TRUE
#define DEFAULT_ENABLE_ANNEX_K FALSE
#define DEFAULT_ENABLE_ANNEX_T FALSE


struct _GstImxVpuEncH263
{
	GstImxVpuEnc parent;

	gboolean enable_annex_i;
	gboolean enable_annex_j;
	gboolean enable_annex_k;
	gboolean enable_annex_t;
};


struct _GstImxVpuEncH263Class
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncH263, gst_imx_vpu_enc_h263, GST_TYPE_IMX_VPU_ENC)


static void gst_imx_vpu_enc_h263_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_h263_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

gboolean gst_imx_vpu_enc_h263_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
GstCaps* gst_imx_vpu_enc_h263_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_h263_class_init(GstImxVpuEncH263Class *klass)
{
	GObjectClass *object_class;
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_h263_debug, "imxvpuenc_h263", 0, "NXP i.MX VPU H263 video encoder");

	object_class = G_OBJECT_CLASS(klass);

	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);
	imx_vpu_enc_class->set_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h263_set_encoder_property);
	imx_vpu_enc_class->get_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h263_get_encoder_property);
	imx_vpu_enc_class->set_open_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h263_set_open_params);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h263_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_H263, TRUE, TRUE, TRUE);

	g_object_class_install_property(
		object_class,
		PROP_ENABLE_ANNEX_I,
		g_param_spec_boolean(
			"enable-annex-i",
			"Enable Annex.I",
			"Enable h.263 Annex.I support",
			DEFAULT_ENABLE_ANNEX_I,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_ANNEX_J,
		g_param_spec_boolean(
			"enable-annex-j",
			"Enable Annex.J",
			"Enable h.263 Annex.J support",
			DEFAULT_ENABLE_ANNEX_J,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_ANNEX_K,
		g_param_spec_boolean(
			"enable-annex-k",
			"Enable Annex.K",
			"Enable h.263 Annex.K support",
			DEFAULT_ENABLE_ANNEX_K,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_ANNEX_T,
		g_param_spec_boolean(
			"enable-annex-t",
			"Enable Annex.T",
			"Enable h.263 Annex.T support",
			DEFAULT_ENABLE_ANNEX_T,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_vpu_enc_h263_init(GstImxVpuEncH263 *imx_vpu_enc_h263)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_h263));

	imx_vpu_enc_h263->enable_annex_i = DEFAULT_ENABLE_ANNEX_I;
	imx_vpu_enc_h263->enable_annex_j = DEFAULT_ENABLE_ANNEX_J;
	imx_vpu_enc_h263->enable_annex_k = DEFAULT_ENABLE_ANNEX_K;
	imx_vpu_enc_h263->enable_annex_t = DEFAULT_ENABLE_ANNEX_T;
}


static void gst_imx_vpu_enc_h263_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncH263 *imx_vpu_enc_h263 = GST_IMX_VPU_ENC_H263(object);

	switch (prop_id)
	{
		case PROP_ENABLE_ANNEX_I:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			imx_vpu_enc_h263->enable_annex_i = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_J:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			imx_vpu_enc_h263->enable_annex_j = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_K:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			imx_vpu_enc_h263->enable_annex_k = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_T:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			imx_vpu_enc_h263->enable_annex_t = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


static void gst_imx_vpu_enc_h263_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncH263 *imx_vpu_enc_h263 = GST_IMX_VPU_ENC_H263(object);

	switch (prop_id)
	{
		case PROP_ENABLE_ANNEX_I:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			g_value_set_boolean(value, imx_vpu_enc_h263->enable_annex_i);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_J:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			g_value_set_boolean(value, imx_vpu_enc_h263->enable_annex_j);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_K:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			g_value_set_boolean(value, imx_vpu_enc_h263->enable_annex_k);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		case PROP_ENABLE_ANNEX_T:
			GST_OBJECT_LOCK(imx_vpu_enc_h263);
			g_value_set_boolean(value, imx_vpu_enc_h263->enable_annex_t);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h263);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


gboolean gst_imx_vpu_enc_h263_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params)
{
	GstImxVpuEncH263 *imx_vpu_enc_h263 = GST_IMX_VPU_ENC_H263_CAST(imx_vpu_enc);

	GST_OBJECT_LOCK(imx_vpu_enc_h263);
	open_params->format_specific_params.h263_params.enable_annex_i = imx_vpu_enc_h263->enable_annex_i;
	open_params->format_specific_params.h263_params.enable_annex_j = imx_vpu_enc_h263->enable_annex_j;
	open_params->format_specific_params.h263_params.enable_annex_k = imx_vpu_enc_h263->enable_annex_k;
	open_params->format_specific_params.h263_params.enable_annex_t = imx_vpu_enc_h263->enable_annex_t;
	GST_OBJECT_UNLOCK(imx_vpu_enc_h263);

	return TRUE;
}


GstCaps* gst_imx_vpu_enc_h263_get_output_caps(G_GNUC_UNUSED GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	return gst_caps_new_simple(
		"video/x-h263",
		"variant",   G_TYPE_STRING,     "itu",
		"parsed",    G_TYPE_BOOLEAN,    (gboolean)TRUE,
		"framerate", GST_TYPE_FRACTION, (gint)(stream_info->frame_rate_numerator), (gint)(stream_info->frame_rate_denominator),
		NULL
	);
}
