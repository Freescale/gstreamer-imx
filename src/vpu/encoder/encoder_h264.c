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


#include "encoder_h264.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_h264_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_h264_enc_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) I420, "
		"width = (int) [ 16, 1920, 8 ], "
		"height = (int) [ 16, 1080, 8 ], "
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
		"alignment = (string) au; "
	)
);


G_DEFINE_TYPE(GstImxVpuH264Enc, gst_imx_vpu_h264_enc, GST_TYPE_IMX_VPU_BASE_ENC)


/* functions for the base class */
gboolean gst_imx_vpu_h264_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParam *open_param);
GstCaps* gst_imx_vpu_h264_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc);
gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParam *open_param);




/* required function declared by G_DEFINE_TYPE */

void gst_imx_vpu_h264_enc_class_init(GstImxVpuH264EncClass *klass)
{
	GstElementClass *element_class;
	GstImxVpuBaseEncClass *base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_h264_enc_debug, "imxvpuh264enc", 0, "Freescale i.MX VPU h.264 video encoder");

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

	base_class->set_open_params      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_open_params);
	base_class->get_output_caps      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_get_output_caps);
	base_class->set_frame_enc_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_frame_enc_params);
}


void gst_imx_vpu_h264_enc_init(G_GNUC_UNUSED GstImxVpuH264Enc *vpu_base_enc)
{
}




/********************************/
/* functions for the base class */

gboolean gst_imx_vpu_h264_enc_set_open_params(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParam *open_param)
{
	open_param->eFormat = VPU_V_AVC;
	open_param->eColorFormat = VPU_COLOR_420;

	return TRUE;
}


GstCaps* gst_imx_vpu_h264_enc_get_output_caps(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc)
{
	return gst_caps_from_string("video/x-h264, stream-format = (string) byte-stream, alignment = (string) au");
}


gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, G_GNUC_UNUSED VpuEncOpenParam *open_param)
{
	enc_enc_param->eFormat = VPU_V_AVC;
	enc_enc_param->nQuantParam = 0;

	return TRUE;
}


