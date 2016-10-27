/* GStreamer MPEG-4 video encoder using the Freescale VPU hardware video engine
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


#include "encoder_mpeg4.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_encoder_mpeg4_debug);
#define GST_CAT_DEFAULT imx_vpu_encoder_mpeg4_debug


enum
{
	PROP_0,
	PROP_QUANT_PARAM
};


#define DEFAULT_QUANT_PARAM     1


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) { I420, NV12, GRAY8 }, "
		"width = (int) [ 48, 1920 ], "
		"height = (int) [ 32, 1080 ], "
		"framerate = (fraction) [ 0, MAX ]"
	)
);

static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/mpeg, "
		"mpegversion = (int) 4," 
		"systemstream = (boolean) false, "
		"width = (int) [ 48, 1920 ], "
		"height = (int) [ 32, 1080 ], "
		"framerate = (fraction) [ 0, MAX ]; "
	)
);


G_DEFINE_TYPE(GstImxVpuEncoderMPEG4, gst_imx_vpu_encoder_mpeg4, GST_TYPE_IMX_VPU_ENCODER_BASE)

static void gst_imx_vpu_encoder_mpeg4_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_encoder_mpeg4_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstCaps* gst_imx_vpu_encoder_mpeg4_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base);
static gboolean gst_imx_vpu_encoder_mpeg4_set_frame_enc_params(GstImxVpuEncoderBase *vpu_encoder_base, ImxVpuEncParams *enc_params);




static void gst_imx_vpu_encoder_mpeg4_class_init(GstImxVpuEncoderMPEG4Class *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstImxVpuEncoderBaseClass *encoder_base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_encoder_mpeg4_debug, "imxvpuenc_mpeg4", 0, "Freescale i.MX VPU h.263 video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	encoder_base_class = GST_IMX_VPU_ENCODER_BASE_CLASS(klass);

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mpeg4_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mpeg4_get_property);

	encoder_base_class->codec_format = IMX_VPU_CODEC_FORMAT_MPEG4;

	encoder_base_class->get_output_caps       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mpeg4_get_output_caps);
	encoder_base_class->set_frame_enc_params  = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mpeg4_set_frame_enc_params);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	g_object_class_install_property(
		object_class,
		PROP_QUANT_PARAM,
		g_param_spec_uint(
			"quant-param",
			"Quantization parameter",
			"Constant quantization quality parameter (ignored if bitrate is set to a nonzero value)",
			1, 31,
			DEFAULT_QUANT_PARAM,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU MPEG-4 video encoder",
		"Codec/Encoder/Video",
		"hardware-accelerated MPEG-4 part 2 video encoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


static void gst_imx_vpu_encoder_mpeg4_init(GstImxVpuEncoderMPEG4 *vpu_encoder_mpeg4)
{
	vpu_encoder_mpeg4->quant_param = DEFAULT_QUANT_PARAM;
}


static void gst_imx_vpu_encoder_mpeg4_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncoderMPEG4 *vpu_encoder_mpeg4 = GST_IMX_VPU_ENCODER_MPEG4(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			vpu_encoder_mpeg4->quant_param = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_encoder_mpeg4_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncoderMPEG4 *vpu_encoder_mpeg4 = GST_IMX_VPU_ENCODER_MPEG4(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			g_value_set_uint(value, vpu_encoder_mpeg4->quant_param);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstCaps* gst_imx_vpu_encoder_mpeg4_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base)
{
	ImxVpuEncOpenParams const *open_params = gst_imx_vpu_encoder_base_get_open_params(vpu_encoder_base);

	return gst_caps_new_simple(
		"video/mpeg",
		"mpegversion", G_TYPE_INT, (gint)4,
		"systemstream", G_TYPE_BOOLEAN, (gboolean)FALSE,
		"width", G_TYPE_INT, (gint)(open_params->frame_width),
		"height", G_TYPE_INT, (gint)(open_params->frame_height),
		"framerate", GST_TYPE_FRACTION, open_params->frame_rate_numerator, open_params->frame_rate_denominator,
		"parsed", G_TYPE_BOOLEAN, TRUE,
		NULL
	);
}


static gboolean gst_imx_vpu_encoder_mpeg4_set_frame_enc_params(GstImxVpuEncoderBase *vpu_encoder_base, ImxVpuEncParams *enc_params)
{
	GstImxVpuEncoderMPEG4 *vpu_encoder_mpeg4 = GST_IMX_VPU_ENCODER_MPEG4(vpu_encoder_base);

	enc_params->quant_param = vpu_encoder_mpeg4->quant_param;

	return TRUE;
}
