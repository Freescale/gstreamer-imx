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


static void gst_imx_vpu_h264_enc_finalize(GObject *object);
gboolean gst_imx_vpu_h264_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParamSimp *open_param);
GstCaps* gst_imx_vpu_h264_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc);
gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParamSimp *open_param);
gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, void *encoded_data_addr, gsize encoded_data_size, gboolean contains_header);




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

	object_class->finalize           = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_finalize);
	base_class->set_open_params      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_open_params);
	base_class->get_output_caps      = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_get_output_caps);
	base_class->set_frame_enc_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_set_frame_enc_params);
	base_class->fill_output_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_h264_enc_fill_output_buffer);
}


void gst_imx_vpu_h264_enc_init(GstImxVpuH264Enc *enc)
{
	enc->sps_buffer = NULL;
	enc->pps_buffer = NULL;
	enc->last_nalu_types[0] = 0;
	enc->last_nalu_types[1] = 0;
}




static void gst_imx_vpu_h264_enc_finalize(GObject *object)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(object);

	if (enc->sps_buffer != NULL)
		gst_buffer_unref(enc->sps_buffer);
	if (enc->pps_buffer != NULL)
		gst_buffer_unref(enc->pps_buffer);

	G_OBJECT_CLASS(gst_imx_vpu_h264_enc_parent_class)->finalize(object);
}


gboolean gst_imx_vpu_h264_enc_set_open_params(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParamSimp *open_param)
{
	open_param->eFormat = VPU_V_AVC;
	open_param->eColorFormat = VPU_COLOR_420;

	return TRUE;
}


GstCaps* gst_imx_vpu_h264_enc_get_output_caps(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc)
{
	return gst_caps_from_string("video/x-h264, stream-format = (string) byte-stream, alignment = (string) au");
}


gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, G_GNUC_UNUSED VpuEncOpenParamSimp *open_param)
{
	enc_enc_param->eFormat = VPU_V_AVC;
	enc_enc_param->nQuantParam = 0;

	return TRUE;
}


gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, void *encoded_data_addr, gsize encoded_data_size, gboolean contains_header)
{
	GstMapInfo map_info;
	guint num_found_starts;
	guint32 nal_start_offsets[2];
	guint8 *in_data, *out_data_end, *out_data_cur;
	guint32 ofs;
	guint32 start_code;
	gsize actual_output_size;
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	gst_buffer_map(frame->output_buffer, &map_info, GST_MAP_WRITE);

	in_data = (guint8 *)encoded_data_addr;
	out_data_cur = map_info.data;
	out_data_end = map_info.data + map_info.size;
	num_found_starts = 0;
	start_code = 0;

	for (ofs = 0; ofs < encoded_data_size;)
	{
		gsize nalu_size;

		start_code <<= 8;
		start_code |= in_data[ofs++];

		if (((start_code & 0x00FFFFFF) == 0x000001) || (ofs == encoded_data_size))
		{
			nal_start_offsets[1] = nal_start_offsets[0];
			nal_start_offsets[0] = ofs;
			num_found_starts = (num_found_starts < 2) ? (num_found_starts + 1) : 2;

			if (num_found_starts == 2)
			{
				guint num_start_code_bytes = 0;

				if (start_code == 0x00000001)
					num_start_code_bytes = 4;
				else if ((start_code & 0x00FFFFFF) == 0x000001)
					num_start_code_bytes = 3;

				nalu_size = nal_start_offsets[0] - nal_start_offsets[1] - num_start_code_bytes;

				GST_DEBUG_OBJECT(enc, "Found NAL unit from offset %u to %u, size %u (minus %u start code bytes)", nal_start_offsets[1], nal_start_offsets[0], nalu_size, num_start_code_bytes);
			}

			start_code = 0;
		}

		if (num_found_starts == 2)
		{
			gboolean headers_already_present;
			gboolean copy_headers = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME_HEADERS(frame);
			guint8 nalu_type = in_data[nal_start_offsets[1]] & 0x1F;

			headers_already_present = (
			                           ((enc->last_nalu_types[0] == 0x07) && (enc->last_nalu_types[1] == 0x08)) ||
			                           ((enc->last_nalu_types[1] == 0x07) && (enc->last_nalu_types[0] == 0x08))
			                         );

			enc->last_nalu_types[1] = enc->last_nalu_types[0];
			enc->last_nalu_types[0] = nalu_type;

			GST_DEBUG_OBJECT(enc, "NAL unit is of type %02x", nalu_type);

			switch (nalu_type)
			{
				case 0x07: /* SPS */
				{
					GST_DEBUG_OBJECT(enc, "New SPS header found, size %u", nalu_size);
					if (enc->sps_buffer != NULL)
						gst_buffer_unref(enc->sps_buffer);
					enc->sps_buffer = gst_buffer_new_allocate(NULL, nalu_size, NULL);
					copy_headers = FALSE;
					gst_buffer_fill(enc->sps_buffer, 0, in_data + nal_start_offsets[1], nalu_size);
					break;
				}
				case 0x08: /* PPS */
				{
					GST_DEBUG_OBJECT(enc, "New PPS header found, size %u", nalu_size);
					if (enc->pps_buffer != NULL)
						gst_buffer_unref(enc->pps_buffer);
					enc->pps_buffer = gst_buffer_new_allocate(NULL, nalu_size, NULL);
					copy_headers = FALSE;
					gst_buffer_fill(enc->pps_buffer, 0, in_data + nal_start_offsets[1], nalu_size);
					break;
				}
				case 0x05: /* IDR */
				{
					GST_DEBUG_OBJECT(enc, "IDR NAL found, size %u, setting sync point", nalu_size);
					copy_headers = TRUE;
					GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);

					break;
				}
				default:
					break;
			}

			if (copy_headers)
			{
				if (!headers_already_present)
				{
					GstMapInfo hdr_map_info;

					GST_DEBUG_OBJECT(enc, "Inserting SPS & PPS headers");

					g_assert((out_data_cur + 4 + gst_buffer_get_size(enc->sps_buffer)) <= out_data_end);

					gst_buffer_map(enc->sps_buffer, &hdr_map_info, GST_MAP_READ);
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x01;
					memcpy(out_data_cur, hdr_map_info.data, hdr_map_info.size);
					out_data_cur += hdr_map_info.size;
					gst_buffer_unmap(enc->sps_buffer, &hdr_map_info);

					g_assert((out_data_cur + 4 + gst_buffer_get_size(enc->pps_buffer)) <= out_data_end);

					gst_buffer_map(enc->pps_buffer, &hdr_map_info, GST_MAP_READ);
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x00;
					*out_data_cur++ = 0x01;
					memcpy(out_data_cur, hdr_map_info.data, hdr_map_info.size);
					out_data_cur += hdr_map_info.size;
					gst_buffer_unmap(enc->pps_buffer, &hdr_map_info);
				}
				else if (headers_already_present)
				{
					GST_DEBUG_OBJECT(enc, "Not inserting SPS & PPS headers since they are already present right before this NAL unit");
				}
			}

			GST_DEBUG_OBJECT(enc, "Copying input NAL unit to output");

			g_assert((out_data_cur + 4 + nalu_size) <= out_data_end);

			*out_data_cur++ = 0x00;
			*out_data_cur++ = 0x00;
			*out_data_cur++ = 0x00;
			*out_data_cur++ = 0x01;
			memcpy(out_data_cur, in_data + nal_start_offsets[1], nalu_size);
			out_data_cur += nalu_size;

			num_found_starts = 1;
		}
	}

	actual_output_size = out_data_cur - map_info.data;

	gst_buffer_unmap(frame->output_buffer, &map_info);

	return actual_output_size;
}

