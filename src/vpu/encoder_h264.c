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


#include <string.h>
#include "encoder_h264.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_encoder_h264_debug);
#define GST_CAT_DEFAULT imx_vpu_encoder_h264_debug


enum
{
	PROP_0,
	PROP_QUANT_PARAM,
	PROP_IDR_INTERVAL
};


#define DEFAULT_QUANT_PARAM     0
#define DEFAULT_IDR_INTERVAL    0


#define NALU_TYPE_IDR 0x05
#define NALU_TYPE_SPS 0x07
#define NALU_TYPE_PPS 0x08


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
		"video/x-h264, "
		"stream-format = (string) byte-stream, "
		"alignment = (string) { au , nal }; "
	)
);


G_DEFINE_TYPE(GstImxVpuEncoderH264, gst_imx_vpu_encoder_h264, GST_TYPE_IMX_VPU_ENCODER_BASE)

static void gst_imx_vpu_encoder_h264_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_encoder_h264_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

gboolean gst_imx_vpu_encoder_h264_set_open_params(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecState *input_state, ImxVpuEncOpenParams *open_params);
GstCaps* gst_imx_vpu_encoder_h264_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base);
gboolean gst_imx_vpu_encoder_h264_set_frame_enc_params(GstImxVpuEncoderBase *vpu_encoder_base, ImxVpuEncParams *enc_params);
gboolean gst_imx_vpu_encoder_h264_process_output_buffer(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecFrame *frame, GstBuffer **output_buffer);




static void gst_imx_vpu_encoder_h264_class_init(GstImxVpuEncoderH264Class *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstImxVpuEncoderBaseClass *encoder_base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_encoder_h264_debug, "imxvpuenc_h264", 0, "Freescale i.MX VPU h.264 video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	encoder_base_class = GST_IMX_VPU_ENCODER_BASE_CLASS(klass);

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_get_property);

	encoder_base_class->codec_format = IMX_VPU_CODEC_FORMAT_H264;

	encoder_base_class->set_open_params       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_set_open_params);
	encoder_base_class->get_output_caps       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_get_output_caps);
	encoder_base_class->set_frame_enc_params  = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_set_frame_enc_params);
	encoder_base_class->process_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_h264_process_output_buffer);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

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
	g_object_class_install_property(
		object_class,
		PROP_IDR_INTERVAL,
		g_param_spec_uint(
			"idr-interval",
			"IDR interval",
			"Interval between IDR frames",
			0, G_MAXUINT,
			DEFAULT_IDR_INTERVAL,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU h.264 video encoder",
		"Codec/Encoder/Video",
		"hardware-accelerated h.264 video encoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


static void gst_imx_vpu_encoder_h264_init(GstImxVpuEncoderH264 *vpu_encoder_h264)
{
	vpu_encoder_h264->quant_param = DEFAULT_QUANT_PARAM;
	vpu_encoder_h264->idr_interval = DEFAULT_IDR_INTERVAL;
	vpu_encoder_h264->produce_access_units = FALSE;
}


static void gst_imx_vpu_encoder_h264_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncoderH264 *vpu_encoder_h264 = GST_IMX_VPU_ENCODER_H264(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			vpu_encoder_h264->quant_param = g_value_get_uint(value);
			break;
		case PROP_IDR_INTERVAL:
			vpu_encoder_h264->idr_interval = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_encoder_h264_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncoderH264 *vpu_encoder_h264 = GST_IMX_VPU_ENCODER_H264(object);

	switch (prop_id)
	{
		case PROP_QUANT_PARAM:
			g_value_set_uint(value, vpu_encoder_h264->quant_param);
			break;
		case PROP_IDR_INTERVAL:
			g_value_set_uint(value, vpu_encoder_h264->idr_interval);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


gboolean gst_imx_vpu_encoder_h264_set_open_params(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecState *input_state, ImxVpuEncOpenParams *open_params)
{
	GstCaps *template_caps, *allowed_caps;
	GstImxVpuEncoderH264 *vpu_encoder_h264 = GST_IMX_VPU_ENCODER_H264(vpu_encoder_base);

	/* Default h.264 open params are already set by the imx_vpu_enc_set_default_open_params()
	 * call in the base class */

	/* Since this call is part of set_format, it is a suitable place for looking up whether or not
	 * downstream requires access units. The src caps are retrieved and examined for this purpose. */

	template_caps = gst_static_pad_template_get_caps(&static_src_template);
	allowed_caps = gst_pad_get_allowed_caps(GST_VIDEO_ENCODER_SRC_PAD(GST_VIDEO_ENCODER(vpu_encoder_base)));

	if (allowed_caps == template_caps)
	{
		vpu_encoder_h264->produce_access_units = TRUE;
	}
	else if (allowed_caps != NULL)
	{
		GstStructure *s;
		gchar const *alignment_str;

		if (gst_caps_is_empty(allowed_caps))
		{
			GST_ERROR_OBJECT(vpu_encoder_h264, "src caps are empty");
			gst_caps_unref(allowed_caps);
			return FALSE;
		}

		allowed_caps = gst_caps_make_writable(allowed_caps);
		allowed_caps = gst_caps_fixate(allowed_caps);
		s = gst_caps_get_structure(allowed_caps, 0);

		alignment_str = gst_structure_get_string(s, "alignment");
		vpu_encoder_h264->produce_access_units = !g_strcmp0(alignment_str, "au");

		gst_caps_unref(allowed_caps);
	}

	vpu_encoder_h264->frame_count = 0;
	if (vpu_encoder_h264->produce_access_units)
		open_params->codec_params.h264_params.enable_access_unit_delimiters = 1;

	GST_INFO_OBJECT(vpu_encoder_h264, "produce h.264 access units: %s", vpu_encoder_h264->produce_access_units ? "yes" : "no");

	gst_caps_unref(template_caps);

	return TRUE;
}


GstCaps* gst_imx_vpu_encoder_h264_get_output_caps(GstImxVpuEncoderBase *vpu_encoder_base)
{
	GstImxVpuEncoderH264 *vpu_encoder_h264 = GST_IMX_VPU_ENCODER_H264(vpu_encoder_base);

	return gst_caps_new_simple(
		"video/x-h264",
		"stream-format", G_TYPE_STRING, "byte-stream",
		"alignment", G_TYPE_STRING, vpu_encoder_h264->produce_access_units ? "au" : "nal",
		"parsed", G_TYPE_BOOLEAN, TRUE,
		NULL
	);
}


gboolean gst_imx_vpu_encoder_h264_set_frame_enc_params(GstImxVpuEncoderBase *vpu_encoder_base, ImxVpuEncParams *enc_params)
{
	GstImxVpuEncoderH264 *vpu_encoder_h264 = GST_IMX_VPU_ENCODER_H264(vpu_encoder_base);

	enc_params->quant_param = vpu_encoder_h264->quant_param;
	if (vpu_encoder_h264->idr_interval > 0)
	{
		/* Force IDR frame if either force_I_frame is already set, or if
		 * an IDR interval is configured and this happens to be the first
		 * frame of such an interval */
		enc_params->force_I_frame = enc_params->force_I_frame || ((vpu_encoder_h264->frame_count % vpu_encoder_h264->idr_interval) == 0);
	}
	vpu_encoder_h264->frame_count++;

	return TRUE;
}


gboolean gst_imx_vpu_encoder_h264_process_output_buffer(GstImxVpuEncoderBase *vpu_encoder_base, GstVideoCodecFrame *frame, GstBuffer **output_buffer)
{
	GstMapInfo map_info;
	static guint8 start_code[] = { 0x00, 0x00, 0x00, 0x01 };
	gsize start_code_size = sizeof(start_code);

	gst_buffer_map(*output_buffer, &map_info, GST_MAP_READ);

	/* If the first NAL unit is an SPS then this frame is a sync point */
	if (memcmp(map_info.data, start_code, start_code_size) == 0)
	{
		guint8 nalu_type;

		/* Retrieve the NAL unit type from the 5 lower bits of the first byte in the NAL unit */
		nalu_type = map_info.data[start_code_size] & 0x1F;
		if (nalu_type == NALU_TYPE_SPS)
		{
			GST_LOG_OBJECT(vpu_encoder_base, "SPS NAL found, setting sync point");
			GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
		}
	}

	gst_buffer_unmap(*output_buffer, &map_info);

	return TRUE;
}
