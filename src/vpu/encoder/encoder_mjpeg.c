/* GStreamer motion JPEG video encoder using the Freescale VPU hardware video engine
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


#include <string.h>
#include "encoder_mjpeg.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_mjpeg_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_mjpeg_enc_debug


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
		"format = (string) { I420, I42B, Y444, GRAY8 }, "
		"width = (int) [ 48, 1920, 8 ], "
		"height = (int) [ 32, 1080, 8 ], "
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


G_DEFINE_TYPE(GstImxVpuMJPEGEnc, gst_imx_vpu_mjpeg_enc, GST_TYPE_IMX_VPU_BASE_ENC)


static gboolean gst_imx_vpu_mjpeg_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecState *input_state, VpuEncOpenParam *open_param);
static GstCaps* gst_imx_vpu_mjpeg_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc);
static gboolean gst_imx_vpu_mjpeg_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParam *open_param);
static void gst_imx_vpu_mjpeg_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_mjpeg_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);




void gst_imx_vpu_mjpeg_enc_class_init(GstImxVpuMJPEGEncClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstImxVpuBaseEncClass *base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_mjpeg_enc_debug, "imxvpumjpegenc", 0, "Freescale i.MX VPU motion JPEG video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_class = GST_IMX_VPU_BASE_ENC_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU motion JPEG video encoder",
		"Codec/Encoder/Video",
		"hardware-accelerated motion JPEG video encoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property       = GST_DEBUG_FUNCPTR(gst_imx_vpu_mjpeg_set_property);
	object_class->get_property       = GST_DEBUG_FUNCPTR(gst_imx_vpu_mjpeg_get_property);
	base_class->set_open_params      = GST_DEBUG_FUNCPTR(gst_imx_vpu_mjpeg_enc_set_open_params);
	base_class->get_output_caps      = GST_DEBUG_FUNCPTR(gst_imx_vpu_mjpeg_enc_get_output_caps);
	base_class->set_frame_enc_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_mjpeg_enc_set_frame_enc_params);

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
}


void gst_imx_vpu_mjpeg_enc_init(GstImxVpuMJPEGEnc *enc)
{
	enc->quant_param = DEFAULT_QUANT_PARAM;
}




static gboolean gst_imx_vpu_mjpeg_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecState *input_state, VpuEncOpenParam *open_param)
{
	GstVideoFormat fmt = GST_VIDEO_INFO_FORMAT(&(input_state->info));

	switch (fmt)
	{
		case GST_VIDEO_FORMAT_I420:
			open_param->eColorFormat = VPU_COLOR_420;
			break;
		case GST_VIDEO_FORMAT_Y42B:
			open_param->eColorFormat = VPU_COLOR_422H;
			break;
		case GST_VIDEO_FORMAT_Y444:
			open_param->eColorFormat = VPU_COLOR_444;
			break;
		case GST_VIDEO_FORMAT_GRAY8:
			open_param->eColorFormat = VPU_COLOR_400;
			break;
		default:
			GST_ERROR_OBJECT(vpu_base_enc, "unsupported video format %s", gst_video_format_to_string(fmt));
	}

	open_param->eFormat = VPU_V_MJPG;

	/* These are default settings from VPU_EncOpenSimp */
	open_param->VpuEncStdParam.mp4Param.mp4_dataPartitionEnable = 0;
	open_param->VpuEncStdParam.mp4Param.mp4_reversibleVlcEnable = 0;
	open_param->VpuEncStdParam.mp4Param.mp4_intraDcVlcThr = 0;
	open_param->VpuEncStdParam.mp4Param.mp4_hecEnable = 0;
	open_param->VpuEncStdParam.mp4Param.mp4_verid = 2;

	return TRUE;
}


static GstCaps* gst_imx_vpu_mjpeg_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc)
{
	int fps_n = vpu_base_enc->open_param.nFrameRate & 0xffff;
	int fps_d = ((vpu_base_enc->open_param.nFrameRate >> 16) & 0xffff) + 1;

	return gst_caps_new_simple(
		"image/jpeg",
		"width", G_TYPE_INT, (gint)(vpu_base_enc->open_param.nPicWidth),
		"height", G_TYPE_INT, (gint)(vpu_base_enc->open_param.nPicHeight),
		"framerate", GST_TYPE_FRACTION, fps_n, fps_d,
		NULL
	);
}


static gboolean gst_imx_vpu_mjpeg_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, G_GNUC_UNUSED VpuEncOpenParam *open_param)
{
	GstImxVpuMJPEGEnc *enc = GST_IMX_VPU_MJPEG_ENC(vpu_base_enc);

	enc_enc_param->eFormat = VPU_V_MJPG;
	enc_enc_param->nQuantParam = enc->quant_param;

	return TRUE;
}


static void gst_imx_vpu_mjpeg_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuMJPEGEnc *enc = GST_IMX_VPU_MJPEG_ENC(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			enc->quant_param = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_mjpeg_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuMJPEGEnc *enc = GST_IMX_VPU_MJPEG_ENC(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			g_value_set_uint(value, enc->quant_param);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}
