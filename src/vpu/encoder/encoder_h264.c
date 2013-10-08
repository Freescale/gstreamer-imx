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
		"alignment = (string) nal; "
	)
);


G_DEFINE_TYPE(GstImxVpuH264Enc, gst_imx_vpu_h264_enc, GST_TYPE_IMX_VPU_BASE_ENC)


static void gst_imx_vpu_h264_enc_finalize(GObject *object);
static gboolean gst_imx_vpu_h264_enc_set_open_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParamSimp *open_param);
static GstCaps* gst_imx_vpu_h264_enc_get_output_caps(GstImxVpuBaseEnc *vpu_base_enc);
static gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, VpuEncOpenParamSimp *open_param);
static void gst_imx_vpu_h264_enc_copy_nalu(guint8 *in_data, guint8 **out_data_cur, guint8 *out_data_end, gsize nalu_size);
static gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, void *encoded_data_addr, gsize encoded_data_size, gboolean contains_header);
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
	enc->sps_buffer = NULL;
	enc->pps_buffer = NULL;
	enc->last_nalu_types[0] = 0;
	enc->last_nalu_types[1] = 0;
	enc->quant_param = DEFAULT_QUANT_PARAM;
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


static gboolean gst_imx_vpu_h264_enc_set_open_params(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc, VpuEncOpenParamSimp *open_param)
{
	open_param->eFormat = VPU_V_AVC;
	open_param->eColorFormat = VPU_COLOR_420;

	return TRUE;
}


static GstCaps* gst_imx_vpu_h264_enc_get_output_caps(G_GNUC_UNUSED GstImxVpuBaseEnc *vpu_base_enc)
{
	return gst_caps_from_string("video/x-h264, stream-format = (string) byte-stream, alignment = (string) nal");
}


static gboolean gst_imx_vpu_h264_enc_set_frame_enc_params(GstImxVpuBaseEnc *vpu_base_enc, VpuEncEncParam *enc_enc_param, G_GNUC_UNUSED VpuEncOpenParamSimp *open_param)
{
	GstImxVpuH264Enc *enc = GST_IMX_VPU_H264_ENC(vpu_base_enc);

	enc_enc_param->eFormat = VPU_V_AVC;
	enc_enc_param->nQuantParam = enc->quant_param;

	return TRUE;
}


static void gst_imx_vpu_h264_enc_copy_nalu(guint8 *in_data, guint8 **out_data_cur, guint8 *out_data_end, gsize nalu_size)
{
	g_assert((*out_data_cur + 4 + nalu_size) <= out_data_end);

	**out_data_cur = 0x00; ++(*out_data_cur);
	**out_data_cur = 0x00; ++(*out_data_cur);
	**out_data_cur = 0x00; ++(*out_data_cur);
	**out_data_cur = 0x01; ++(*out_data_cur);

	memcpy(*out_data_cur, in_data, nalu_size);
	(*out_data_cur) += nalu_size;
}


static gsize gst_imx_vpu_h264_enc_fill_output_buffer(GstImxVpuBaseEnc *vpu_base_enc, GstVideoCodecFrame *frame, void *encoded_data_addr, gsize encoded_data_size, G_GNUC_UNUSED gboolean contains_header)
{
	GstMapInfo map_info;
	guint num_found_starts;
	guint32 nal_start_code_offsets[2];
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

	/* This loop searches for NAL units. It does so by looking for start codes, which are the
	 * "boundaries" or delimiters of NAL units (in Annex.B format, which is what the VPU encoder
	 * produces).
	 *
	 * The offsets of the locations inside the encoded data which follow the start codes are kept.
	 * So, for example, if at offset 441 a 4-byte start code is found, the offset 445 is stored. 
	 * The most recently found two offsets are kept. Older offsets are discarded. The older offset
	 * is always found by start code. The newer one may be found by start code, or may simply be
	 * the end of the encoded data buffer. This is the case when only one NAL unit is contained;
	 * in that case, there will be only one start code inside, marking the beginning of the NAL unit.
	 * If the newer offset is found by start code, it is decremented by the start code size, to make
	 * sure both offsets exclude the start code bytes.
	 * The older offset then becomes the start offset, the newer offset the end one.
	 *
	 * Once the start and end offsets of the NAL unit are found, its type is retrieved.
	 * SPS/PPS NAL units are read, their headers stored in buffers. With IDR NAL units, the loop
	 * prepends the SPS/PPS headers (unless they are already present right before the IDR unit).
	 * This makes sure proper playback is possible even with discontinuous streams such as Apple HLS.
	 */

	for (ofs = 0; ofs < encoded_data_size;)
	{
		gsize nalu_size;
		guint32 nalu_start_ofs, nalu_end_ofs;

		/* Start code is found by appending the current byte to the existing code;
		 * the start code (usually a 3- or 4-byte code, 0x000001 / 0x00000001) is then detected
		 * by simple AND masking & comparison */
		start_code <<= 8;
		start_code |= in_data[ofs++];

		/* Store current offset if a 3- or 4-byte start code was found or if the end of the encoded data is reached */
		if (((start_code & 0x00FFFFFF) == 0x000001) || (ofs == encoded_data_size))
		{
			/* offset #1 is the older one, #0 the newer one */
			nal_start_code_offsets[1] = nal_start_code_offsets[0];
			nal_start_code_offsets[0] = ofs;
			num_found_starts = (num_found_starts < 2) ? (num_found_starts + 1) : 2;

			/* Two offsets are known; computing the NAL unit size is now possible */
			if (num_found_starts == 2)
			{
				guint num_start_code_bytes;

				/* Subtract either 3 or 4 byte (depending on the start code length) from the offset
				 * difference to get the NAL unit size without including the start code bytes.
				 * If no start code was found, then it means the end of the encoded data was reached;
				 * do not subtract in that case. */
				if (start_code == 0x00000001)
					num_start_code_bytes = 4;
				else if ((start_code & 0x00FFFFFF) == 0x000001)
					num_start_code_bytes = 3;
				else
					num_start_code_bytes = 0; /* end-of-encoded-data case */

				nalu_start_ofs = nal_start_code_offsets[1];
				nalu_end_ofs = nal_start_code_offsets[0];
				nalu_size = nalu_end_ofs - nalu_start_ofs - num_start_code_bytes;

				GST_DEBUG_OBJECT(enc, "Found NAL unit from offset %u to %u, size %u (minus %u start code bytes)", nal_start_code_offsets[1], nal_start_code_offsets[0], nalu_size, num_start_code_bytes);
			}

			start_code = 0;
		}

		/* Start and end of the NAL unit are known; the NAL unit can be analyzed */
		if (num_found_starts == 2)
		{
			gboolean headers_already_present, copy_headers;
			guint8 nalu_type;

			/* If header generation is forced, set initial copy_headers value to TRUE */
			copy_headers = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME_HEADERS(frame) || GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(frame);

			/* Retrieve the NAL unit type from the 5 lower bits of the first byte in the NAL unit */
			nalu_type = in_data[nalu_start_ofs] & 0x1F;

			/* If the last two NAL units were SPS and PPS ones, then adding headers is unnecessary */
			headers_already_present = (
			                           ((enc->last_nalu_types[0] == NALU_TYPE_SPS) && (enc->last_nalu_types[1] == NALU_TYPE_PPS)) ||
			                           ((enc->last_nalu_types[0] == NALU_TYPE_PPS) && (enc->last_nalu_types[1] == NALU_TYPE_SPS))
			                         );

			/* Save the last two NAL unit types */
			enc->last_nalu_types[1] = enc->last_nalu_types[0];
			enc->last_nalu_types[0] = nalu_type;

			GST_DEBUG_OBJECT(enc, "NAL unit is of type %02x", nalu_type);

			switch (nalu_type)
			{
				case NALU_TYPE_SPS:
				{
					GST_DEBUG_OBJECT(enc, "New SPS header found, size %u", nalu_size);
					if (enc->sps_buffer != NULL)
						gst_buffer_unref(enc->sps_buffer);
					enc->sps_buffer = gst_buffer_new_allocate(NULL, nalu_size, NULL);
					copy_headers = FALSE;
					gst_buffer_fill(enc->sps_buffer, 0, in_data + nalu_start_ofs, nalu_size);
					break;
				}
				case NALU_TYPE_PPS:
				{
					GST_DEBUG_OBJECT(enc, "New PPS header found, size %u", nalu_size);
					if (enc->pps_buffer != NULL)
						gst_buffer_unref(enc->pps_buffer);
					enc->pps_buffer = gst_buffer_new_allocate(NULL, nalu_size, NULL);
					copy_headers = FALSE;
					gst_buffer_fill(enc->pps_buffer, 0, in_data + nalu_start_ofs, nalu_size);
					break;
				}
				case NALU_TYPE_IDR:
				{
					GST_DEBUG_OBJECT(enc, "IDR NAL found, size %u, setting sync point", nalu_size);
					/* This is an IDR unit -> prepend SPS/PPS headers and set the frame as a sync point */
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
					/* Copying the SPS/PPS headers was requested -> insert them before the previously found NAL unit */

					if ((enc->sps_buffer != NULL) && (enc->pps_buffer != NULL))
					{
						GstMapInfo hdr_map_info;

						GST_DEBUG_OBJECT(enc, "Inserting SPS & PPS headers");

						gst_buffer_map(enc->sps_buffer, &hdr_map_info, GST_MAP_READ);
						gst_imx_vpu_h264_enc_copy_nalu(hdr_map_info.data, &out_data_cur, out_data_end, hdr_map_info.size);
						gst_buffer_unmap(enc->sps_buffer, &hdr_map_info);

						gst_buffer_map(enc->pps_buffer, &hdr_map_info, GST_MAP_READ);
						gst_imx_vpu_h264_enc_copy_nalu(hdr_map_info.data, &out_data_cur, out_data_end, hdr_map_info.size);
						gst_buffer_unmap(enc->pps_buffer, &hdr_map_info);
					}
					else
						GST_WARNING_OBJECT(enc, "Cannot insert SPS & PPS headers, since no headers were previously found");
				}
				else if (headers_already_present)
				{
					/* Copying the SPS/PPS headers was requested, but these are present already -> don't copy, and log this */
					GST_DEBUG_OBJECT(enc, "Not inserting SPS & PPS headers since they are already present right before this NAL unit");
				}
			}

			GST_DEBUG_OBJECT(enc, "Copying input NAL unit to output");
			gst_imx_vpu_h264_enc_copy_nalu(in_data + nalu_start_ofs, &out_data_cur, out_data_end, nalu_size);

			/* The older offset is no longer necessary, since the NAL unit was processed.
			 * Setting num_found_starts to 1 causes the code to not process anything until another start code is
			 * found (or the end of encoded data is reached. */
			num_found_starts = 1;
		}
	}

	/* Compute the actual output size, which may be bigger than the initial encoded data
	 * (because the loop above always writes 4-byte start codes, and sometimes inserts
	 * SPS/PPS headers) */
	actual_output_size = out_data_cur - map_info.data;

	gst_buffer_unmap(frame->output_buffer, &map_info);

	return actual_output_size;
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

