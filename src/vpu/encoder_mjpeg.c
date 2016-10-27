/* GStreamer motion JPEG video encoder using the Freescale VPU hardware video engine
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include "encoder_mjpeg.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_encoder_mjpeg_debug);
#define GST_CAT_DEFAULT imx_vpu_encoder_mjpeg_debug


enum
{
	PROP_0,
	PROP_QUALITY_FACTOR
};


#define DEFAULT_QUALITY_FACTOR     85


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) { I420, Y42B, Y444, NV12, NV16, NV24, GRAY8 }, "
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
		"image/jpeg; "
	)
);


G_DEFINE_TYPE(GstImxVpuEncoderMJPEG, gst_imx_vpu_encoder_mjpeg, GST_TYPE_IMX_VPU_ENCODER_BASE)

static void gst_imx_vpu_encoder_mjpeg_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_encoder_mjpeg_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_vpu_encoder_mjpeg_set_open_params(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecState *input_state, ImxVpuEncOpenParams *open_params);
static GstCaps* gst_imx_vpu_encoder_mjpeg_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base);




static void gst_imx_vpu_encoder_mjpeg_class_init(GstImxVpuEncoderMJPEGClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstImxVpuEncoderBaseClass *encoder_base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_encoder_mjpeg_debug, "imxvpuenc_mjpeg", 0, "Freescale i.MX VPU motion JPEG video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	encoder_base_class = GST_IMX_VPU_ENCODER_BASE_CLASS(klass);

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mjpeg_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mjpeg_get_property);

	encoder_base_class->codec_format = IMX_VPU_CODEC_FORMAT_MJPEG;

	encoder_base_class->set_open_params       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mjpeg_set_open_params);
	encoder_base_class->get_output_caps       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_mjpeg_get_output_caps);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	g_object_class_install_property(
		object_class,
		PROP_QUALITY_FACTOR,
		g_param_spec_uint(
			"quality-factor",
			"Quality factor",
			"Quality factor of encoding (1 = worst, 100 = best)",
			1, 100,
			DEFAULT_QUALITY_FACTOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU motion JPEG video encoder",
		"Codec/Encoder/Video",
		"hardware-accelerated motion JPEG video encoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


static void gst_imx_vpu_encoder_mjpeg_init(GstImxVpuEncoderMJPEG *vpu_encoder_mjpeg)
{
	vpu_encoder_mjpeg->quality_factor = DEFAULT_QUALITY_FACTOR;
}


static void gst_imx_vpu_encoder_mjpeg_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncoderMJPEG *vpu_encoder_mjpeg = GST_IMX_VPU_ENCODER_MJPEG(object);

	switch (prop_id)
	{
		case PROP_QUALITY_FACTOR:
			vpu_encoder_mjpeg->quality_factor = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_encoder_mjpeg_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncoderMJPEG *vpu_encoder_mjpeg = GST_IMX_VPU_ENCODER_MJPEG(object);

	switch (prop_id)
	{
		case PROP_QUALITY_FACTOR:
			g_value_set_uint(value, vpu_encoder_mjpeg->quality_factor);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_vpu_encoder_mjpeg_set_open_params(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecState *input_state, ImxVpuEncOpenParams *open_params)
{
	GstImxVpuEncoderMJPEG *vpu_encoder_mjpeg = GST_IMX_VPU_ENCODER_MJPEG(vpu_encoder_base);
	GstVideoFormat fmt = GST_VIDEO_INFO_FORMAT(&(input_state->info));

	switch (fmt)
	{
		case GST_VIDEO_FORMAT_I420:
		case GST_VIDEO_FORMAT_NV12:
			open_params->color_format = IMX_VPU_COLOR_FORMAT_YUV420;
			break;
		case GST_VIDEO_FORMAT_Y42B:
		case GST_VIDEO_FORMAT_NV16:
			open_params->color_format = IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL;
			break;
		case GST_VIDEO_FORMAT_Y444:
		case GST_VIDEO_FORMAT_NV24:
			open_params->color_format = IMX_VPU_COLOR_FORMAT_YUV444;
			break;
		case GST_VIDEO_FORMAT_GRAY8:
			open_params->color_format = IMX_VPU_COLOR_FORMAT_YUV400;
			break;
		default:
			GST_ERROR_OBJECT(vpu_encoder_mjpeg, "unsupported video format %s", gst_video_format_to_string(fmt));
			return FALSE;
	}

	open_params->codec_params.mjpeg_params.quality_factor = vpu_encoder_mjpeg->quality_factor;

	return TRUE;
}


static GstCaps* gst_imx_vpu_encoder_mjpeg_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base)
{
	ImxVpuEncOpenParams const *open_params = gst_imx_vpu_encoder_base_get_open_params(vpu_encoder_base);

	return gst_caps_new_simple(
		"image/jpeg",
		"width", G_TYPE_INT, (gint)(open_params->frame_width),
		"height", G_TYPE_INT, (gint)(open_params->frame_height),
		"framerate", GST_TYPE_FRACTION, open_params->frame_rate_numerator, open_params->frame_rate_denominator,
		NULL
	);
}
