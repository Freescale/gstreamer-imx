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


#include <string.h>
#include "encoder_h264.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_h264_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_h264_enc_debug


enum
{
	PROP_0,
	PROP_QUANT_PARAM
};


#define DEFAULT_QUANT_PARAM     0


#define NALU_TYPE_IDR 0x05
#define NALU_TYPE_SPS 0x07
#define NALU_TYPE_PPS 0x08


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) I420, "
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
		"video/x-h264, "
		"stream-format = (string) byte-stream, "
		"alignment = (string) { au , nal }; "
	)
);


G_DEFINE_TYPE(GstImxVpuH264Enc, gst_imx_vpu_h264_enc, GST_TYPE_IMX_VPU_BASE_ENC)


static void gst_imx_vpu_h264_enc_finalize(GObject *object);
static gboolean gst_imx_vpu_h264_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParam *open_param);
static GstCaps* gst_imx_vpu_h264_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc);
static gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParam *open_param);
static gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, gsize output_offset, void *encoded_data_addr, gsize encoded_data_size, gboolean contains_header);
static void gst_imx_vpu_h264_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_h264_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);




void gst_imx_vpu_h264_enc_class_init(GstImxVpuH264EncClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstImxVpuBaseEncClass *base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_h264_enc_debug, "imxvpuh264enc", 0, "Freescale i.MX VPU h.264 video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_class = GST_IMX_VPU_BASE_ENC_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU h.264 video encoder",
		"Codec/Encoder/Video",
		"hardware-accelerated h.264 video encoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property       = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_property);
	object_class->get_property       = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_get_property);
	object_class->finalize           = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_finalize);
	base_class->set_open_params      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_open_params);
	base_class->get_output_caps      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_get_output_caps);
	base_class->set_frame_enc_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_frame_enc_params);
	base_class->fill_output_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_fill_output_buffer);

	g_object_class_install_property(
		object_class,
		PROP_QUANT_PARAM,
		g_param_spec_uint(
			"quant-param",
			"Quantization parameter",
			"Constant quantization quality parameter (ignored if bitrate is set to a nonzero value)",
			0, 51,
			DEFAULT_QUANT_PARAM,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_vpu_h264_enc_init(GstImxVpuH264Enc *enc)
{
	enc->quant_param = DEFAULT_QUANT_PARAM;
	enc->produce_access_units = FALSE;
}




static void gst_imx_vpu_h264_enc_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_imx_vpu_h264_enc_parent_class)->finalize(object);
}


static gboolean gst_imx_vpu_h264_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParam *open_param)
{
	GstCaps *template_caps, *allowed_caps;
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	open_param->eFormat = VPU_V_AVC;
	open_param->eColorFormat = VPU_COLOR_420;

	/* These are default settings from VPU_EncOpenSimp */
	open_param->VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag = 0;
	open_param->VpuEncStdParam.avcParam.avc_disableDeblk = 0;
	open_param->VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha = 6;
	open_param->VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta = 0;
	open_param->VpuEncStdParam.avcParam.avc_chromaQpOffset = 10;
	open_param->VpuEncStdParam.avcParam.avc_audEnable = 0;
	open_param->VpuEncStdParam.avcParam.avc_fmoEnable = 0;
	open_param->VpuEncStdParam.avcParam.avc_fmoType = 0;
	open_param->VpuEncStdParam.avcParam.avc_fmoSliceNum = 1;
	open_param->VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize = 32;

	/* Since this call is part of set_format, it is a suitable place for looking up whether or not
	 * downstream requires access units. The src caps are retrieved and examined for this purpose. */

	template_caps = gst_static_pad_template_get_caps(&static_src_template);
	allowed_caps = gst_pad_get_allowed_caps(GST_VIDEO_ENCODER_SRC_PAD(GST_VIDEO_ENCODER(vpu_base_enc)));

	if (allowed_caps == template_caps)
	{
		enc->produce_access_units = TRUE;
	}
	else if (allowed_caps != NULL)
	{
		GstStructure *s;
		gchar const *alignment_str;

		if (gst_caps_is_empty(allowed_caps))
		{
			GST_ERROR_OBJECT(enc, "src caps are empty");
			gst_caps_unref(allowed_caps);
			return FALSE;
		}

		allowed_caps = gst_caps_make_writable(allowed_caps);
		allowed_caps = gst_caps_fixate(allowed_caps);
		s = gst_caps_get_structure(allowed_caps, 0);

		alignment_str = gst_structure_get_string(s, "alignment");
		enc->produce_access_units = !g_strcmp0(alignment_str, "au");

		gst_caps_unref(allowed_caps);
	}

	if (enc->produce_access_units)
		open_param->VpuEncStdParam.avcParam.avc_audEnable = 1;

	GST_DEBUG_OBJECT(vpu_base_enc, "produce access unit: %s", enc->produce_access_units ? "yes" : "no");

	gst_caps_unref(template_caps);

	return TRUE;
}


static GstCaps* gst_imx_vpu_h264_enc_get_output_caps(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	return gst_caps_new_simple(
		"video/x-h264",
		"stream-format", G_TYPE_STRING, "byte-stream",
		"alignment", G_TYPE_STRING, enc->produce_access_units ? "au" : "nal",
		NULL
	);
}


static gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, G_GNUC_UNUSED VpuEncOpenParam *open_param)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	enc_enc_param->eFormat = VPU_V_AVC;
	enc_enc_param->nQuantParam = enc->quant_param;

	return TRUE;
}


static gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, gsize output_offset, void *encoded_data_addr, gsize encoded_data_size, G_GNUC_UNUSED gboolean contains_header)
{
	guint8 *in_data;
	static guint8 start_code[] = { 0x00, 0x00, 0x00, 0x01 };
	gsize start_code_size = sizeof(start_code);
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	/* If the first NAL unit is an SPS then this frame is a sync point */
	in_data = (guint8 *)encoded_data_addr;
	if (memcmp(in_data, start_code, start_code_size) == 0)
	{
		guint8 nalu_type;

		/* Retrieve the NAL unit type from the 5 lower bits of the first byte in the NAL unit */
		nalu_type = in_data[start_code_size] & 0x1F;
		if (nalu_type == NALU_TYPE_SPS)
		{
			GST_DEBUG_OBJECT(enc, "SPS NAL found, setting sync point");
			GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
		}
	}

	gst_buffer_fill(frame->output_buffer, output_offset, encoded_data_addr, encoded_data_size);

	return encoded_data_size;
}


static void gst_imx_vpu_h264_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(object);

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


static void gst_imx_vpu_h264_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(object);

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

