/* GStreamer video decoder using the Freescale VPU hardware video engine
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
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "decoder.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_meta.h"
#include "decoder_framebuffer_pool.h"




GST_DEBUG_CATEGORY_STATIC(imx_vpu_decoder_debug);
#define GST_CAT_DEFAULT imx_vpu_decoder_debug


enum
{
	PROP_0,
	PROP_NUM_ADDITIONAL_FRAMEBUFFERS
};


#define DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS 0


#define GST_IMX_VPU_DECODER_ALLOCATOR_MEM_TYPE "ImxVpuDecMemory2"


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		/* IMX_VPU_CODEC_FORMAT_H264 */
		"video/x-h264, "
		"parsed = (boolean) true, "
		"stream-format = (string) byte-stream, "
		"alignment = (string) au; "

		/* IMX_VPU_CODEC_FORMAT_MPEG2 */
		"video/mpeg, "
		"parsed = (boolean) true, "
		"systemstream = (boolean) false, "
		"mpegversion = (int) [ 1, 2 ]; "

		/* IMX_VPU_CODEC_FORMAT_MPEG4 */
		"video/mpeg, "
		"parsed = (boolean) true, "
		"mpegversion = (int) 4; "

		/* IMX_VPU_CODEC_FORMAT_MPEG4 */
		/* XXX: there is explicit support for DivX 3,5,6 for the
		 * VPU, but it is subject to licensing, and therefore
		 * the generic MPEG4 support is used (only for DivX 5 & 6,
		 * since it does not work with 3) */
		"video/x-divx, "
		"divxversion = (int) [ 5, 6 ]; "

		/* IMX_VPU_CODEC_FORMAT_MPEG4
		 * See above for why MPEG4 (similar reason) */
		"video/x-xvid; "

		/* IMX_VPU_CODEC_FORMAT_H263 */
		"video/x-h263, "
		"variant = (string) itu; "

		/* IMX_VPU_CODEC_FORMAT_MJPEG */
		"image/jpeg; "

		/* IMX_VPU_CODEC_FORMAT_WMV3 and IMX_VPU_CODEC_FORMAT_WVC1 */
		"video/x-wmv, "
		"wmvversion = (int) 3, "
		"format = (string) { WVC1, WMV3 }; "

		/* IMX_VPU_CODEC_FORMAT_VP8 */
		"video/x-vp8; "
	)
);

static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) { I420, Y42B, Y444, NV12, NV16, NV24, GRAY8 }, "
		"width = (int) [ 16, MAX ], "
		"height = (int) [ 16, MAX ], "
		"framerate = (fraction) [ 0, MAX ], "
		"interlace-mode = { progressive, interleaved } "
	)
);


G_DEFINE_TYPE(GstImxVpuDecoder, gst_imx_vpu_decoder, GST_TYPE_VIDEO_DECODER)


static gboolean gst_imx_vpu_decoder_start(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_decoder_stop(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_decoder_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state);
static gboolean gst_imx_vpu_decoder_sink_event(GstVideoDecoder *decoder, GstEvent *event);
static GstFlowReturn gst_imx_vpu_decoder_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *cur_frame);
static gboolean gst_imx_vpu_decoder_flush(GstVideoDecoder *decoder);
static GstFlowReturn gst_imx_vpu_decoder_finish(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_decoder_decide_allocation(GstVideoDecoder *decoder, GstQuery *query);

static void gst_imx_vpu_decoder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_imx_vpu_decoder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstStateChangeReturn gst_imx_vpu_decoder_change_state(GstElement *element, GstStateChange transition);

static void gst_imx_vpu_decoder_close_and_clear_decoder_context(GstImxVpuDecoder *vpu_decoder);
static gboolean gst_imx_vpu_decoder_fill_param_set(GstImxVpuDecoder *vpu_decoder, GstVideoCodecState *state, ImxVpuDecOpenParams *open_params, GstBuffer **codec_data);
static int gst_imx_vpu_decoder_initial_info_callback(ImxVpuDecoder *decoder, ImxVpuDecInitialInfo *new_initial_info, unsigned int output_code, void *user_data);

static void gst_imx_vpu_decoder_add_to_unfinished_frame_table(GstImxVpuDecoder *vpu_decoder, GstVideoCodecFrame *frame);
static void gst_imx_vpu_decoder_remove_from_unfinished_frame_table(GstImxVpuDecoder *vpu_decoder, GstVideoCodecFrame *frame);
static void gst_imx_vpu_decoder_release_all_unfinished_frames(GstImxVpuDecoder *vpu_decoder);




static void gst_imx_vpu_decoder_class_init(GstImxVpuDecoderClass *klass)
{
	GObjectClass *object_class;
	GstVideoDecoderClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_decoder_debug, "imxvpudecoder", 0, "Freescale i.MX VPU video decoder");

	imx_vpu_setup_logging();

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_VIDEO_DECODER_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_set_property);
	object_class->get_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_get_property);

	base_class->start             = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_start);
	base_class->stop              = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_stop);
	base_class->set_format        = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_set_format);
	base_class->sink_event        = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_sink_event);
	base_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_handle_frame);
	base_class->flush             = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_flush);
	base_class->finish            = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_finish);
	base_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_decide_allocation);

	element_class->change_state   = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_change_state);

	g_object_class_install_property(
		object_class,
		PROP_NUM_ADDITIONAL_FRAMEBUFFERS,
		g_param_spec_uint(
			"num-additional-framebuffers",
			"Number of additional output framebuffers",
			"Number of output framebuffers to allocate for decoding in addition to the minimum number indicated by the VPU and the necessary number of free buffers",
			0, 32767,
			DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU video decoder",
		"Codec/Decoder/Video",
		"hardware-accelerated video decoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


static void gst_imx_vpu_decoder_init(GstImxVpuDecoder *vpu_decoder)
{
	vpu_decoder->decoder = NULL;
	vpu_decoder->codec_data = NULL;
	vpu_decoder->bitstream_buffer = NULL;
	vpu_decoder->decoder_context = NULL;
	vpu_decoder->current_output_state = NULL;
	vpu_decoder->phys_mem_allocator = NULL;
	vpu_decoder->num_additional_framebuffers = DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS;
	vpu_decoder->unfinished_frames_table = NULL;
	vpu_decoder->fatal_error = FALSE;
}


static gboolean gst_imx_vpu_decoder_start(GstVideoDecoder *decoder)
{
	size_t bitstream_buffer_size;
	unsigned int bitstream_buffer_alignment;
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	GST_INFO_OBJECT(vpu_decoder, "starting VPU decoder");

	/* Make sure the firmware is loaded */
	if (!gst_imx_vpu_decoder_load())
		return FALSE;

	/* Set up a DMA buffer allocator for framebuffers and the bitstream buffer */
	if ((vpu_decoder->phys_mem_allocator = gst_imx_vpu_allocator_new(imx_vpu_dec_get_default_allocator(), GST_IMX_VPU_DECODER_ALLOCATOR_MEM_TYPE)) == NULL)
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not create physical memory allocator");
		return FALSE;
	}

	/* Allocate the bitstream buffer */
	imx_vpu_dec_get_bitstream_buffer_info(&bitstream_buffer_size, &bitstream_buffer_alignment);
	vpu_decoder->bitstream_buffer = gst_buffer_new_allocate(vpu_decoder->phys_mem_allocator, bitstream_buffer_size, NULL); // TODO: pass on alignment

	if (vpu_decoder->bitstream_buffer == NULL)
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not allocate bitstream buffer");
		return FALSE;
	}

	vpu_decoder->fatal_error = FALSE;
	vpu_decoder->unfinished_frames_table = g_hash_table_new(NULL, NULL);

	/* The decoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	GST_INFO_OBJECT(vpu_decoder, "VPU decoder started");

	return TRUE;
}


static gboolean gst_imx_vpu_decoder_stop(GstVideoDecoder *decoder)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	/* Cleanup any remaining GstVideoCodecFrame instances if there are any,
	 * and then destroy the unfinished_frames_table */
	if (vpu_decoder->unfinished_frames_table != NULL)
	{
		gst_imx_vpu_decoder_release_all_unfinished_frames(vpu_decoder);
		g_hash_table_destroy(vpu_decoder->unfinished_frames_table);
		vpu_decoder->unfinished_frames_table = NULL;
	}

	/* Cleanup the decoder context (= enable its no_wait mode, mark the
	 * decoder as gone in the context, and unref it), and close the
	 * decoder. If any gstbuffers downstream with framebuffers inside
	 * still exist, they keep the context alive, since they too have a
	 * reference to it. Once they are gone, so is the context. */
	gst_imx_vpu_decoder_close_and_clear_decoder_context(vpu_decoder);

	if (vpu_decoder->bitstream_buffer != NULL)
	{
		gst_buffer_unref(vpu_decoder->bitstream_buffer);
		vpu_decoder->bitstream_buffer = NULL;
	}

	if (vpu_decoder->codec_data != NULL)
	{
		gst_buffer_unref(vpu_decoder->codec_data);
		vpu_decoder->codec_data = NULL;
	}

	if (vpu_decoder->current_output_state != NULL)
	{
		gst_video_codec_state_unref(vpu_decoder->current_output_state);
		vpu_decoder->current_output_state = NULL;
	}

	GST_INFO_OBJECT(vpu_decoder, "VPU decoder stopped");

	if (vpu_decoder->phys_mem_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(vpu_decoder->phys_mem_allocator));
		vpu_decoder->phys_mem_allocator = NULL;
	}

	/* Make sure the firmware is unloaded */
	gst_imx_vpu_decoder_unload();

	return TRUE;
}


static gboolean gst_imx_vpu_decoder_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state)
{
	ImxVpuDecReturnCodes ret;
	ImxVpuDecOpenParams open_params;
	GstBuffer *codec_data = NULL;
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	GST_INFO_OBJECT(decoder, "setting decoder format");

	/* Output frames that are already decoded but not yet displayed */
	GST_INFO_OBJECT(decoder, "draining remaining frames from decoder");
	gst_imx_vpu_decoder_finish(decoder);

	/* Cleanup the existing decoder context (= enable its no_wait mode,
	 * mark the decoder as gone in the context, and unref it), and close
	 * the existing decoder. If any gstbuffers downstream with framebuffers
	 * inside still exist, they keep the context alive, since they too
	 * have a reference to it. Once they are gone, so is the context.
	 * New gstbuffers however will make use of a new decoder context,
	 * which fits with the new caps in the new output state, and which
	 is created later. */
	gst_imx_vpu_decoder_close_and_clear_decoder_context(vpu_decoder);

	/* Clean up old codec data copy */
	if (vpu_decoder->codec_data != NULL)
	{
		GST_INFO_OBJECT(decoder, "cleaning up existing codec data");

		gst_buffer_unref(vpu_decoder->codec_data);
		vpu_decoder->codec_data = NULL;
	}

	/* Clean up old output state */
	if (vpu_decoder->current_output_state != NULL)
	{
		GST_INFO_OBJECT(decoder, "cleaning up existing output state");

		gst_video_codec_state_unref(vpu_decoder->current_output_state);
		vpu_decoder->current_output_state = NULL;
	}

	memset(&open_params, 0, sizeof(open_params));

	/* codec_data does not need to be unref'd after use; it is owned by the caps structure */
	if (!gst_imx_vpu_decoder_fill_param_set(vpu_decoder, state, &open_params, &codec_data))
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not fill open params: state info incompatible");
		return FALSE;
	}

	/* Find out what formats downstream supports, to determine the value for chroma_interleave */
	{
		GstCaps *allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(decoder));

		if (allowed_srccaps == NULL)
		{
			open_params.chroma_interleave = 0;
			GST_INFO_OBJECT(vpu_decoder, "srcpad not linked (yet), so no src caps set; using default chroma_interleave value %d", open_params.chroma_interleave);
		}
		else if (gst_caps_is_empty(allowed_srccaps))
		{
			GST_ERROR_OBJECT(vpu_decoder, "allowed_srccaps structure is empty");
			gst_caps_unref(allowed_srccaps);
			return FALSE;
		}
		else
		{
			gchar const *format_str;
			GValue const *format_value;
			GstVideoFormat format;

			/* Look at the sample format values from the first structure */
			GstStructure *structure = gst_caps_get_structure(allowed_srccaps, 0);
			format_value = gst_structure_get_value(structure, "format");

			if (format_value == NULL)
			{
				gst_caps_unref(allowed_srccaps);
				return FALSE;
			}
			else if (GST_VALUE_HOLDS_LIST(format_value))
			{
				/* if value is a format list, pick the first entry */
				GValue const *fmt_list_value = gst_value_list_get_value(format_value, 0);
				format_str = g_value_get_string(fmt_list_value);
			}
			else if (G_VALUE_HOLDS_STRING(format_value))
			{
				/* if value is a string, use it directly */
				format_str = g_value_get_string(format_value);
			}
			else
			{
				GST_ERROR_OBJECT(vpu_decoder, "unexpected type for 'format' field in allowed_srccaps structure %" GST_PTR_FORMAT, structure);
				gst_caps_unref(allowed_srccaps);
				return FALSE;
			}

			format = gst_video_format_from_string(format_str);
			g_assert(format != GST_VIDEO_FORMAT_UNKNOWN);

			switch (format)
			{
				case GST_VIDEO_FORMAT_I420:
				case GST_VIDEO_FORMAT_Y42B:
				case GST_VIDEO_FORMAT_Y444:
				case GST_VIDEO_FORMAT_GRAY8:
					open_params.chroma_interleave = 0;
					break;
				case GST_VIDEO_FORMAT_NV12:
				case GST_VIDEO_FORMAT_NV16:
				case GST_VIDEO_FORMAT_NV24:
					open_params.chroma_interleave = 1;
					break;
				default:
					g_assert_not_reached();
			}

			GST_INFO_OBJECT(vpu_decoder, "format %s detected in list of supported srccaps formats => setting chroma_interleave to %d", format_str, open_params.chroma_interleave);

			gst_caps_unref(allowed_srccaps);
		}
	}

	vpu_decoder->chroma_interleave = open_params.chroma_interleave;

	if ((ret = imx_vpu_dec_open(&(vpu_decoder->decoder), &open_params, gst_imx_vpu_get_dma_buffer_from(vpu_decoder->bitstream_buffer), gst_imx_vpu_decoder_initial_info_callback, vpu_decoder)) != IMX_VPU_DEC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not open decoder: %s", imx_vpu_dec_error_string(ret));
		return FALSE;
	}

	/* Ref the output state, to be able to add information from the init_info structure to it later
	 * by using the gst_video_decoder_set_output_state() function */
	vpu_decoder->current_output_state = gst_video_codec_state_ref(state);

	/* Copy the buffer, to make sure the codec_data lifetime does not depend on the caps */
	if (codec_data != NULL)
		vpu_decoder->codec_data = gst_buffer_copy(codec_data);

	GST_INFO_OBJECT(decoder, "setting format finished");

	return TRUE;
}


static gboolean gst_imx_vpu_decoder_sink_event(GstVideoDecoder *decoder, GstEvent *event)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_START:
		{
			if (G_UNLIKELY(vpu_decoder->decoder_context == NULL))
				break;

			GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
			GST_DEBUG_OBJECT(vpu_decoder, "Enabling no_wait mode in decoder context after flushing started");
			gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, TRUE);
			GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

			break;
		}

		case GST_EVENT_FLUSH_STOP:
		{
			if (G_UNLIKELY(vpu_decoder->decoder_context == NULL))
				break;

			GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
			GST_DEBUG_OBJECT(vpu_decoder, "Disabling no_wait mode in decoder context after flushing ended");
			gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, FALSE);
			GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

			break;
		}

		default:
			break;
	}

	return GST_VIDEO_DECODER_CLASS(gst_imx_vpu_decoder_parent_class)->sink_event(decoder, event);
}


static GstFlowReturn gst_imx_vpu_decoder_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *input_frame)
{
	ImxVpuDecReturnCodes ret;
	ImxVpuEncodedFrame encoded_frame;
	unsigned int output_code;
	GstMapInfo in_map_info, codecdata_map_info;
	GstImxVpuDecoder *vpu_decoder;
	gboolean exit_early;

	ret = IMX_VPU_DEC_RETURN_CODE_OK;
	exit_early = FALSE;

	vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	if (vpu_decoder->decoder == NULL)
	{
		GST_ELEMENT_ERROR(decoder, LIBRARY, INIT, ("VPU decoder was not created"), (NULL));
		return GST_FLOW_ERROR;
	}

	memset(&encoded_frame, 0, sizeof(encoded_frame));

	if (input_frame != NULL)
	{
		/* Add this new input frame to the unfinished_frames_table,
		 * since it is new and hasn't been finished yet */
		gst_imx_vpu_decoder_add_to_unfinished_frame_table(vpu_decoder, input_frame);

		gst_buffer_map(input_frame->input_buffer, &in_map_info, GST_MAP_READ);

		GST_LOG_OBJECT(decoder, "input gstframe %p with input buffer %p number #%" G_GUINT32_FORMAT " and %z" G_GSIZE_FORMAT " byte", (gpointer)input_frame, (gpointer)(input_frame->input_buffer), input_frame->system_frame_number, in_map_info.size);

		encoded_frame.data = in_map_info.data;
		encoded_frame.data_size = in_map_info.size;
		/* The system frame number is necessary to correctly associate encoded
		 * frames and decoded frames. This is required, because some formats
		 * have a delay (= output frames only show up after N complete input
		 * frames), and others like h.264 even reorder frames. */
		encoded_frame.context = (void *)(input_frame->system_frame_number);
	}

	/* cur_frame is NULL if handle_frame() is being called inside finish(); in other words,
	 * when the decoder is shutting down, and output frames are being flushed.
	 * This requires the decoder's drain mode to be enabled, which is done in finish(). */

	if (vpu_decoder->codec_data != NULL)
	{
		gst_buffer_map(vpu_decoder->codec_data, &codecdata_map_info, GST_MAP_READ);
		imx_vpu_dec_set_codec_data(vpu_decoder->decoder, codecdata_map_info.data, codecdata_map_info.size);
		GST_LOG_OBJECT(vpu_decoder, "setting extra codec data (%d byte)", codecdata_map_info.size);
	}

	if (vpu_decoder->decoder_context != NULL)
	{
		/* Using a mutex here, since the imx_vpu_dec_decode() call internally picks an
		 * available framebuffer, and at the same time, the bufferpool release() function
		 * might be returning a framebuffer to the VPU pool; also, the decoder_context
		 * no_wait function would otherwise be potentially subject to data races */

		GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);

		GST_TRACE_OBJECT(vpu_decoder, "waiting until decoding can continue");
		/* Wait until decoding is possible (= until enough free framebuffers are in the
		 * VPU). When a framebuffer is free, an gst_imx_vpu_decoder_context_mark_as_displayed()
		 * call in the current decoder_framebuffer_pool's release function will
		 * unblock this function. See its documentation for details. */
		gst_imx_vpu_decoder_context_wait_until_decoding_possible(vpu_decoder->decoder_context);

		if (vpu_decoder->decoder_context->no_wait)
		{
			/* no_wait mode enabled in the current context. The earlier
			 * gst_imx_vpu_decoder_context_wait_until_decoding_possible() was
			 * interrupted. No decoding is possible. See the documentation of
			 * gst_imx_vpu_decoder_context_set_no_wait() for details. */

			GST_DEBUG_OBJECT(vpu_decoder, "aborting decode since no_wait mode is active");
			exit_early = TRUE;
		}
		else
		{
			/* no_wait mode disabled in the current context. Decoding is possible. */

			GST_TRACE_OBJECT(vpu_decoder, "decoding");
			/* The actual decoding */
			ret = imx_vpu_dec_decode(vpu_decoder->decoder, &encoded_frame, &output_code);
		}

		GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);
	}
	else
	{
		/* There is no decoder context yet. This occurs in the very beginning of the
		 * decoding, meaning that there is also no decoder_framebuffer_pool instance yet.
		 * Therefore, at this point, no concurrent VPU decoder access can happen, and
		 * thus, no mutex locks are needed. Once the decoder has enough input data,
		 * it will invoke a callback, in which the decoder context is created. */

		GST_TRACE_OBJECT(vpu_decoder, "decoding");
		ret = imx_vpu_dec_decode(vpu_decoder->decoder, &encoded_frame, &output_code);
	}

	/* Cleanup temporary input frame and codec data mapping, since they are not needed anymore */
	if (input_frame != NULL)
		gst_buffer_unmap(input_frame->input_buffer, &in_map_info);
	if (vpu_decoder->codec_data != NULL)
		gst_buffer_unmap(vpu_decoder->codec_data, &codecdata_map_info);

	/* This is the right time to exit early if it was requested or if decoding failed */
	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(vpu_decoder, "failed to decode: %s", imx_vpu_dec_error_string(ret));
		vpu_decoder->fatal_error = TRUE;
		return GST_FLOW_ERROR;
	}
	else if (exit_early)
		return GST_FLOW_EOS;

	GST_LOG_OBJECT(vpu_decoder, "decoding succeeded with output code %#x", output_code);

	if (output_code & IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA)
	{
		/* The input_frame does not contain enough data for decoding a frame;
		 * instead, it forms part of a set of GstVideoCodecFrame instances which
		 * together contain the data for a complete frame. Only the PTS and DTS of
		 * the last frame in such a set is needed. The rest does not have to be
		 * kept around once their payload has been fed into the decoder, and can
		 * be safely released and discarded here. Later, the decoded frame will
		 * have the PTS and DTS of the input GstVideoCodecFrame instance which
		 * completes the encoded input frame (the output_code won't have the
		 * IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA flag set then). */

		GST_DEBUG_OBJECT(vpu_decoder, "input gstframe is incomplete; discarding this gstframe");
		/* input_frame is no longer considered "unfinished"; we simply don't need
		 * it anymore, it will never be "finished" */
		gst_imx_vpu_decoder_remove_from_unfinished_frame_table(vpu_decoder, input_frame);
		/* Release the input_frame, removing it from internal base class lists,
		 * and unref'ing memory, thus avoiding leaks */
		gst_video_decoder_release_frame(decoder, input_frame);
	}
	else if (output_code & IMX_VPU_DEC_OUTPUT_CODE_DECODED_FRAME_AVAILABLE)
	{
		/* A complete encoded input frame has been fed into the decode, either
		 * in one go (= input_frame contains the entire encoded frame), or after
		 * several iterations (see above). Decode the frame and output it. */

		guint32 system_frame_number;
		GstVideoCodecFrame *out_frame;
		ImxVpuRawFrame decoded_frame;
		GstBuffer *out_buffer;

		/* Using mutex locks when retrieving the frame, to avoid data
		 * races; a GstBuffer with a framebuffer inside might be unref'd
		 * concurrently, leading to race conditions, since its release function
		 * then calls gst_imx_vpu_decoder_context_mark_as_displayed(). */
		GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
		ret = imx_vpu_dec_get_decoded_frame(vpu_decoder->decoder, &decoded_frame);
		GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(vpu_decoder, "could not get decoded frame: %s", imx_vpu_dec_error_string(ret));
			return GST_FLOW_ERROR;
		}

		if (vpu_decoder->decoder_context->uses_interlacing)
		{
			GST_LOG_OBJECT(
				vpu_decoder,
				"frame types for the retrieved frame's fields: %s %s",
				imx_vpu_frame_type_string(decoded_frame.frame_types[0]),
				imx_vpu_frame_type_string(decoded_frame.frame_types[1])
			);
		}
		else
		{
			GST_LOG_OBJECT(
				vpu_decoder,
				"frame type for the retrieved frame: %s",
				imx_vpu_frame_type_string(decoded_frame.frame_types[0])
			);
		}

		/* Retrieve the correct GstVideoCodecFrame for the decoded frame
		 * based on the decoded frame's context (see the "if (input_frame != NULL)"
		 * block above for more details) */
		system_frame_number = (guint32)(decoded_frame.context);
		out_frame = gst_video_decoder_get_frame(decoder, system_frame_number);

		if (out_frame != NULL)
		{
			/* We got the GstVideoCodecFrame that corresponds to the decoded
			 * frame. Get a GstBuffer from the memory pool, place the
			 * decoded frame's framebuffer in it, and finish the frame,
			 * pushing it downstream. */

			GST_LOG_OBJECT(vpu_decoder, "retrieved gstframe %p with number #%" G_GUINT32_FORMAT, (gpointer)out_frame, system_frame_number);

			out_buffer = gst_video_decoder_allocate_output_buffer(decoder);
			if (out_buffer == NULL)
			{
				/* Unref output frame, since get_frame() refs it */
				gst_video_codec_frame_unref(out_frame);

				/* Manually mark the framebuffer as "displayed". Since
				 * no buffer could be allocated, it cannot really be
				 * displayed, but the decoder doesn't know that. To
				 * clean up properly, return it to the VPU pool by marking
				 * it as displayed. */
				imx_vpu_dec_mark_framebuffer_as_displayed(vpu_decoder->decoder, decoded_frame.framebuffer);

				/* Frame is not "unfinished" anymore, but also can't be
				 * shown. Remove it from the unfinished frame table. */
				gst_imx_vpu_decoder_remove_from_unfinished_frame_table(vpu_decoder, out_frame);

				/* Drop the frame, since it cannot be shown */
				gst_video_decoder_drop_frame(decoder, out_frame);

				GST_ELEMENT_WARNING(decoder, STREAM, DECODE, ("could not allocate buffer for output frame, dropping frame"), ("output gstframe %p with number #%" G_GUINT32_FORMAT, (gpointer)out_frame, system_frame_number));

				return GST_FLOW_ERROR;
			}

			GST_LOG_OBJECT(vpu_decoder, "output gstbuffer: %p imxvpu framebuffer: %p", out_buffer, decoded_frame.framebuffer);

			gst_imx_vpu_framebuffer_array_set_framebuffer_in_gstbuffer(vpu_decoder->decoder_context->framebuffer_array, out_buffer, decoded_frame.framebuffer);

			/* The GST_BUFFER_FLAG_TAG_MEMORY flag will be set, because the
			 * buffer's memory was added after the buffer was acquired from
			 * the pool. (The framebufferpool produces empty buffers.)
			 * However, at this point, the buffer is ready for use,
			 * so just remove that flag to prevent unnecessary copies.
			 * (new in GStreamer >= 1.3.1 */
	#if GST_CHECK_VERSION(1, 3, 1)
			GST_BUFFER_FLAG_UNSET(out_buffer, GST_BUFFER_FLAG_TAG_MEMORY);
	#endif

			/* Add interlacing flags to the output buffer if necessary */
			if (vpu_decoder->decoder_context->uses_interlacing)
			{
				switch (decoded_frame.interlacing_mode)
				{
					case IMX_VPU_INTERLACING_MODE_NO_INTERLACING:
						GST_LOG_OBJECT(vpu_decoder, "bitstream has interlacing flag set, but this frame is progressive");
						break;

					case IMX_VPU_INTERLACING_MODE_TOP_FIELD_FIRST:
						GST_LOG_OBJECT(vpu_decoder, "interlaced frame, 1 field, top field first");
						GST_BUFFER_FLAG_SET(out_buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
						GST_BUFFER_FLAG_SET(out_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
						break;

					case IMX_VPU_INTERLACING_MODE_BOTTOM_FIELD_FIRST:
						GST_LOG_OBJECT(vpu_decoder, "interlaced frame, 1 field, bottom field first");
						GST_BUFFER_FLAG_SET(out_buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
						GST_BUFFER_FLAG_UNSET(out_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
						break;

					default:
						GST_LOG_OBJECT(vpu_decoder, "interlaced frame, but interlacing type is unsupported");
						break;
				}
			}

			/* Unref output frame, since get_frame() refs it */
			gst_video_codec_frame_unref(out_frame);

			out_frame->output_buffer = out_buffer;

			/* Remove this frame from the unfinished_frames_table,
			 * since we are finishing it here */
			gst_imx_vpu_decoder_remove_from_unfinished_frame_table(vpu_decoder, out_frame);

			/* Now finish the frame */
			gst_video_decoder_finish_frame(decoder, out_frame);
		}
		else
		{
			/* If this point is reached, something went wrong. It could be a
			 * severely broken and/or invalid video stream, or a bug in
			 * imxvpuapi. Try to continue to decode, but emit a warning.
			 * Unfortunately, the decoded frame cannot be used, since no
			 * corresponding output GstVideoCodecFrame could be found. */

			GST_WARNING_OBJECT(vpu_decoder, "no gstframe exists with number #%" G_GUINT32_FORMAT " - discarding decoded frame", system_frame_number);

			GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
			gst_imx_vpu_decoder_context_mark_as_displayed(vpu_decoder->decoder_context, decoded_frame.framebuffer);
			GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);
		}
	}
	else if (output_code & IMX_VPU_DEC_OUTPUT_CODE_DROPPED)
	{
		void* system_frame_number_ptr;
		guint32 system_frame_number_uint;
		GstVideoCodecFrame *out_frame;
		
		/* Using mutex locks when retrieving the dropped frame's context,
		 * to avoid data races; a GstBuffer with a framebuffer inside might be
		 * unref'd concurrently, leading to race conditions, since its release
		 * function then calls gst_imx_vpu_decoder_context_mark_as_displayed(). */
		GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
		imx_vpu_dec_get_dropped_frame_info(vpu_decoder->decoder, &system_frame_number_ptr, NULL, NULL);
		GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

		system_frame_number_uint = (guint32)((guintptr)system_frame_number_ptr);

		GST_DEBUG_OBJECT(vpu_decoder, "VPU dropped frame #%" G_GUINT32_FORMAT " internally", system_frame_number_uint);

		/* Get the corresponding GstVideoCodecFrame so we can drop it */
		out_frame = gst_video_decoder_get_frame(decoder, system_frame_number_uint);

		if (out_frame != NULL)
		{
			GST_DEBUG_OBJECT(vpu_decoder, "dropping gstframe %p with number #%" G_GUINT32_FORMAT, (gpointer)out_frame, system_frame_number_uint);
			/* Unref, since the gst_video_decoder_get_frame() call refs the out_frame */
			gst_video_codec_frame_unref(out_frame);
		}
		else
		{
			/* XXX Dropped frames with invalid numbers have been observed with a few mkv
			 * files and the fslwrapper backend. The vpulib backend never exhibited
			 * this behavior. Therefore, fslwrapper specific problems are suspected.
			 * Dropping the oldest frame instead works reliably, but should not be
			 * necessary. Try to find out why this is exactly happening. */
			out_frame = gst_video_decoder_get_oldest_frame(decoder);

			/* Unref, since the gst_video_decoder_get_oldest_frame() call refs the out_frame */
			gst_video_codec_frame_unref(out_frame);

			GST_WARNING_OBJECT(vpu_decoder, "didn't get a gstframe with number #%" G_GUINT32_FORMAT " - dropping oldest gstframe %p instead", system_frame_number_uint, (gpointer)out_frame);
		}

		/* A dropped frame isn't unfinished */
		gst_imx_vpu_decoder_remove_from_unfinished_frame_table(vpu_decoder, out_frame);

		/* Do the actual dropping */
		gst_video_decoder_drop_frame(decoder, out_frame);
	}

	if (output_code & IMX_VPU_DEC_OUTPUT_CODE_EOS)
		GST_TRACE_OBJECT(vpu_decoder, "decoder reports EOS");

	return (output_code & IMX_VPU_DEC_OUTPUT_CODE_EOS) ? GST_FLOW_EOS : GST_FLOW_OK;
}


static gboolean gst_imx_vpu_decoder_flush(GstVideoDecoder *decoder)
{
	ImxVpuDecReturnCodes ret;
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	if (vpu_decoder->decoder == NULL)
		return TRUE;

	if (vpu_decoder->decoder_context == NULL)
		return TRUE;

	GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
	ret = imx_vpu_dec_flush(vpu_decoder->decoder);
	GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not flush decoder: %s", imx_vpu_dec_error_string(ret));
		return FALSE;
	}

	/* After flushing, all pending unfinished frames are stale,
	 * so get rid of them */
	gst_imx_vpu_decoder_release_all_unfinished_frames(vpu_decoder);

	return TRUE;
}


static GstFlowReturn gst_imx_vpu_decoder_finish(GstVideoDecoder *decoder)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);

	if (vpu_decoder->decoder == NULL)
		return GST_FLOW_OK;

	if (vpu_decoder->decoder_context == NULL)
		return GST_FLOW_OK;

	if (vpu_decoder->fatal_error)
		return GST_FLOW_ERROR;

	GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
	imx_vpu_dec_enable_drain_mode(vpu_decoder->decoder, 1);
	GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

	/* Get as many output frames as possible, until the decoder reports the EOS.
	 * This makes sure all frames which can be decoded but haven't been yet are
	 * output, draining the decoder. */
	GST_INFO_OBJECT(vpu_decoder, "pushing out all remaining unfinished frames");
	while (TRUE)
	{
		GstFlowReturn flow_ret = gst_imx_vpu_decoder_handle_frame(decoder, NULL);
		if (flow_ret == GST_FLOW_EOS)
		{
			GST_INFO_OBJECT(vpu_decoder, "last remaining unfinished frame pushed");
			break;
		}
		else if (flow_ret != GST_FLOW_OK)
			break;
		else
			GST_LOG_OBJECT(vpu_decoder, "unfinished frame pushed, others remain");
	}

	GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
	imx_vpu_dec_enable_drain_mode(vpu_decoder->decoder, 0);
	GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

	return GST_FLOW_OK;
}


static gboolean gst_imx_vpu_decoder_decide_allocation(GstVideoDecoder *decoder, GstQuery *query)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(decoder);
	GstCaps *outcaps;
	GstBufferPool *pool = NULL;
	guint size, min = 0, max = 0;
	GstStructure *config;
	GstVideoInfo vinfo;
	gboolean update_pool;

	g_assert(vpu_decoder->decoder_context != NULL);

	gst_query_parse_allocation(query, &outcaps, NULL);

	if (outcaps == NULL)
	{
		GST_DEBUG_OBJECT(decoder, "can't decide allocation since there are no output caps");
		return FALSE;
	}

	gst_video_info_init(&vinfo);
	gst_video_info_from_caps(&vinfo, outcaps);

	GST_INFO_OBJECT(decoder, "number of allocation pools in query: %d", gst_query_get_n_allocation_pools(query));

	/* Look for an allocator which can allocate VPU DMA buffers */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		for (guint i = 0; i < gst_query_get_n_allocation_pools(query); ++i)
		{
			gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
			if (gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER))
			{
				/* This pool can be used, since it does have the
				 * GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER option. Exit the
				 * loop *without* unref'ing the pool (since it is used
				 * later below). */
				GST_DEBUG_OBJECT(decoder, "video pool %p can be used - it does have the GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER", (gpointer)pool);
				break;
			}
			else
			{
				/* This pool cannot be used, since it doesn't have the
				 * GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER option.
				 * Unref it, since gst_query_parse_nth_allocation_pool() refs it. */
				GST_DEBUG_OBJECT(decoder, "video pool %p cannot be used - it does not have the GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER; unref'ing", (gpointer)pool);
				gst_object_unref(GST_OBJECT(pool));
				pool = NULL;
			}
		}

		size = MAX(size, (guint)(vpu_decoder->decoder_context->framebuffer_array->framebuffer_sizes.total_size));
		size = MAX(size, vinfo.size);
		update_pool = TRUE;
	}
	else
	{
		pool = NULL;
		size = MAX(vinfo.size, (guint)(vpu_decoder->decoder_context->framebuffer_array->framebuffer_sizes.total_size));
		min = max = 0;
		update_pool = FALSE;
	}

	/* Either no pool or no pool with the ability to allocate VPU DMA buffers
	 * has been found -> create a new pool */
	if ((pool == NULL) || !gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER))
	{
		if (pool == NULL)
			GST_INFO_OBJECT(decoder, "no pool present; creating new pool");
		else
		{
			gst_object_unref(pool);
			GST_INFO_OBJECT(decoder, "no pool supports VPU buffers; creating new pool");
		}
		pool = gst_imx_vpu_decoder_framebuffer_pool_new(vpu_decoder->decoder_context);
	}

	GST_INFO_OBJECT(
		pool,
		"pool config:  outcaps: %" GST_PTR_FORMAT "  size: %u  min buffers: %u  max buffers: %u",
		(gpointer)outcaps,
		size,
		min,
		max
	);

	/* Now configure the pool. */
	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_set_config(pool, config);

	if (update_pool)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	/* Unref the pool, since both gst_query_set_nth_allocation_pool() and
	 * gst_query_add_allocation_pool() ref it */
	if (pool != NULL)
		gst_object_unref(pool);

	return TRUE;
}


static void gst_imx_vpu_decoder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(object);

	switch (prop_id)
	{
		case PROP_NUM_ADDITIONAL_FRAMEBUFFERS:
		{
			guint num;

			if (vpu_decoder->decoder != NULL)
			{
				GST_ERROR_OBJECT(vpu_decoder, "cannot change number of additional framebuffers while a VPU decoder instance is open");
				return;
			}

			num = g_value_get_uint(value);
			vpu_decoder->num_additional_framebuffers = num;

			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_decoder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(object);

	switch (prop_id)
	{
		case PROP_NUM_ADDITIONAL_FRAMEBUFFERS:
			g_value_set_uint(value, vpu_decoder->num_additional_framebuffers);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_vpu_decoder_change_state(GstElement *element, GstStateChange transition)
{
	GstImxVpuDecoder *vpu_decoder = GST_IMX_VPU_DECODER(element);
	GstStateChangeReturn result;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			if (vpu_decoder->decoder_context != NULL)
			{
				GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
				GST_DEBUG_OBJECT(element, "Disabling no_wait mode in decoder context during PAUSED->READY state change");
				gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, FALSE);
				GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);
			}
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			/* This is done here, *before* the change_state of the base class is
			 * called, to make sure gst_imx_vpu_decoder_context_wait_until_decoding_possible()
			 * does not block inside handle_frame() */
			if (vpu_decoder->decoder_context != NULL)
			{
				GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
				GST_DEBUG_OBJECT(element, "Enabling no_wait mode in decoder context during PAUSED->READY state change");
				gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, TRUE);
				GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);
			}

			break;

		default:
			break;
	}

	result = GST_ELEMENT_CLASS(gst_imx_vpu_decoder_parent_class)->change_state(element, transition);

	switch (transition)
	{
		default:
			break;
	}

	return result;
}


static void gst_imx_vpu_decoder_close(GstImxVpuDecoder *vpu_decoder)
{
	ImxVpuDecReturnCodes ret;

	if (vpu_decoder->decoder == NULL)
		return;

	GST_DEBUG_OBJECT(vpu_decoder, "closing decoder");

	if ((ret = imx_vpu_dec_close(vpu_decoder->decoder)) != IMX_VPU_DEC_RETURN_CODE_OK)
		GST_ERROR_OBJECT(vpu_decoder, "error while closing decoder: %s", imx_vpu_dec_error_string(ret));

	vpu_decoder->decoder = NULL;
}


static void gst_imx_vpu_decoder_close_and_clear_decoder_context(GstImxVpuDecoder *vpu_decoder)
{
	if (vpu_decoder->decoder_context == NULL)
	{	
		gst_imx_vpu_decoder_close(vpu_decoder);
		return;
	}

	GST_INFO_OBJECT(vpu_decoder, "Clearing decoder context");

	/* Using mutexes here to prevent race conditions when the decoder is set as gone
	 * at the same time as it is checked in the buffer pool release() function.
	 * For similar reasons, the decoder is closed while the mutex is held. the decoder
	 * must be closed *before* the context is unref'd, since the internal imxvpuapi
	 * decoder might try to access entries from the context' framebuffer array in its
	 * imx_vpu_dec_close() function. To avoid an edge case (decoder is closed,
	 * and a buffer's release() function tries to mark the buffer as displayed
	 * *before* the decoder was marked as gone), the decoder is closed while the
	 * mutex is held. */
	GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_decoder->decoder_context);
	gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, TRUE);
	gst_imx_vpu_decoder_context_set_decoder_as_gone(vpu_decoder->decoder_context);
	gst_imx_vpu_decoder_close(vpu_decoder);
	GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_decoder->decoder_context);

	gst_object_unref(vpu_decoder->decoder_context);
	vpu_decoder->decoder_context = NULL;

}


gboolean gst_imx_vpu_decoder_fill_param_set(GstImxVpuDecoder *vpu_decoder, GstVideoCodecState *state, ImxVpuDecOpenParams *open_params, GstBuffer **codec_data)
{
	guint structure_nr;
	gboolean format_set;
	gboolean do_codec_data = FALSE;

	memset(open_params, 0, sizeof(ImxVpuDecOpenParams));

	for (structure_nr = 0; structure_nr < gst_caps_get_size(state->caps); ++structure_nr)
	{
		GstStructure *s;
		gchar const *name;

		format_set = TRUE;
		s = gst_caps_get_structure(state->caps, structure_nr);
		name = gst_structure_get_name(s);

		open_params->enable_frame_reordering = 1;

		if (g_strcmp0(name, "video/x-h264") == 0)
		{
			open_params->codec_format = IMX_VPU_CODEC_FORMAT_H264;
			GST_INFO_OBJECT(vpu_decoder, "setting h.264 as stream format");
		}
		else if (g_strcmp0(name, "video/mpeg") == 0)
		{
			gint mpegversion;
			if (gst_structure_get_int(s, "mpegversion", &mpegversion))
			{
				gboolean is_systemstream;
				switch (mpegversion)
				{
					case 1:
					case 2:
						if (gst_structure_get_boolean(s, "systemstream", &is_systemstream) && !is_systemstream)
						{
							open_params->codec_format = IMX_VPU_CODEC_FORMAT_MPEG2;
						}
						else
						{
							GST_WARNING_OBJECT(vpu_decoder, "MPEG-%d system stream is not supported", mpegversion);
							format_set = FALSE;
						}
						break;
					case 4:
						open_params->codec_format = IMX_VPU_CODEC_FORMAT_MPEG4;
						break;
					default:
						GST_WARNING_OBJECT(vpu_decoder, "unsupported MPEG version: %d", mpegversion);
						format_set = FALSE;
						break;
				}

				if (format_set)
					GST_INFO_OBJECT(vpu_decoder, "setting MPEG-%d as stream format", mpegversion);
			}

			do_codec_data = TRUE;
		}
		/* TODO: DivX 3 */
		else if (g_strcmp0(name, "video/x-divx") == 0)
		{
			gint divxversion;
			if (gst_structure_get_int(s, "divxversion", &divxversion))
			{
				switch (divxversion)
				{
					case 5:
					case 6:
						/* Using MPEG4 , since the full DivX 5/6
						 * support in the VPU needs to be licensed */
						open_params->codec_format = IMX_VPU_CODEC_FORMAT_MPEG4;
						break;
					default:
						format_set = FALSE;
						break;
				}

				if (format_set)
					GST_INFO_OBJECT(vpu_decoder, "setting DivX %d as stream format", divxversion);
			}
		}
		else if (g_strcmp0(name, "video/x-xvid") == 0)
		{
			open_params->codec_format = IMX_VPU_CODEC_FORMAT_MPEG4; // TODO: is this OK?
			GST_INFO_OBJECT(vpu_decoder, "setting xvid as stream format");
		}
		else if (g_strcmp0(name, "video/x-h263") == 0)
		{
			open_params->codec_format = IMX_VPU_CODEC_FORMAT_H263;
			GST_INFO_OBJECT(vpu_decoder, "setting h.263 as stream format");
		}
		else if (g_strcmp0(name, "image/jpeg") == 0)
		{
			open_params->codec_format = IMX_VPU_CODEC_FORMAT_MJPEG;
			GST_INFO_OBJECT(vpu_decoder, "setting motion JPEG as stream format");
		}
		else if (g_strcmp0(name, "video/x-wmv") == 0)
		{
			gint wmvversion;
			gchar const *format_str;

			if (!gst_structure_get_int(s, "wmvversion", &wmvversion))
			{
				GST_WARNING_OBJECT(vpu_decoder, "wmvversion caps is missing");
				format_set = FALSE;
				break;
			}
			if (wmvversion != 3)
			{
				GST_WARNING_OBJECT(vpu_decoder, "unsupported WMV version %d (only version 3 is supported)", wmvversion);
				format_set = FALSE;
				break;
			}

			format_str = gst_structure_get_string(s, "format");
			if ((format_str == NULL) || g_str_equal(format_str, "WMV3"))
			{
				GST_INFO_OBJECT(vpu_decoder, "setting VC1M (= WMV3, VC1-SPMP) as stream format");
				open_params->codec_format = IMX_VPU_CODEC_FORMAT_WMV3;
			}
			else if (g_str_equal(format_str, "WVC1"))
			{
				GST_INFO_OBJECT(vpu_decoder, "setting VC1 (= WVC1, VC1-AP) as stream format");
				open_params->codec_format = IMX_VPU_CODEC_FORMAT_WVC1;
			}
			else
			{
				GST_WARNING_OBJECT(vpu_decoder, "unsupported WMV format \"%s\"", format_str);
				format_set = FALSE;
			}

			do_codec_data = TRUE;
		}
		else if (g_strcmp0(name, "video/x-vp8") == 0)
		{
			open_params->codec_format = IMX_VPU_CODEC_FORMAT_VP8;
			GST_INFO_OBJECT(vpu_decoder, "setting VP8 as stream format");
		}

		if  (format_set)
		{
			if (do_codec_data)
			{
				GValue const *value = gst_structure_get_value(s, "codec_data");
				if (value != NULL)
				{
					GST_INFO_OBJECT(vpu_decoder, "codec data expected and found in caps");
					*codec_data = gst_value_get_buffer(value);
				}
				else
				{
					GST_WARNING_OBJECT(vpu_decoder, "codec data expected, but not found in caps");
					format_set = FALSE;
					*codec_data = NULL;
				}
			}
			else
				*codec_data = NULL;

			break;
		}
	}

	if (!format_set)
		return FALSE;

	open_params->frame_width = state->info.width;
	open_params->frame_height = state->info.height;

	return TRUE;
}


static int gst_imx_vpu_decoder_initial_info_callback(G_GNUC_UNUSED ImxVpuDecoder *d, ImxVpuDecInitialInfo *new_initial_info, G_GNUC_UNUSED unsigned int output_code, void *user_data)
{
	/* This function is called from within imx_vpu_dec_decode(), which in
	 * turn is called with a locked decoder context mutex. For this reason,
	 * this mutex isn't locked here again. */

	GstImxVpuDecoder *vpu_decoder = (GstImxVpuDecoder *)user_data;

	GST_DEBUG_OBJECT(
		vpu_decoder,
		"initial info:  color format: %s  size: %ux%u pixel  rate: %u/%u  min num required framebuffers: %u  interlacing: %d  framebuffer alignment: %u",
		imx_vpu_color_format_string(new_initial_info->color_format),
		new_initial_info->frame_width,
		new_initial_info->frame_height,
		new_initial_info->frame_rate_numerator,
		new_initial_info->frame_rate_denominator,
		new_initial_info->min_num_required_framebuffers,
		new_initial_info->interlacing,
		new_initial_info->framebuffer_alignment
	);

	/* Clear old framebuffer array first */
	if (vpu_decoder->decoder_context != NULL)
	{
		gst_imx_vpu_decoder_context_set_no_wait(vpu_decoder->decoder_context, TRUE);
		gst_imx_vpu_decoder_context_set_decoder_as_gone(vpu_decoder->decoder_context);
	}

	new_initial_info->min_num_required_framebuffers += vpu_decoder->num_additional_framebuffers;
	vpu_decoder->decoder_context = gst_imx_vpu_decoder_context_new(vpu_decoder->decoder, new_initial_info, vpu_decoder->chroma_interleave, (GstImxPhysMemAllocator *)(vpu_decoder->phys_mem_allocator));

	if (vpu_decoder->decoder_context == NULL)
	{
		GST_ERROR_OBJECT(vpu_decoder, "could not create new decoder context");
		return 0;
	}

	if (vpu_decoder->current_output_state != NULL)
	{
		GstVideoFormat fmt;
		GstVideoCodecState *state = vpu_decoder->current_output_state;

		/* XXX: color format IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL - what is this supposed to be in GStreamer? */
		if (vpu_decoder->chroma_interleave)
		{
			switch (new_initial_info->color_format)
			{
				case IMX_VPU_COLOR_FORMAT_YUV420: fmt = GST_VIDEO_FORMAT_NV12; break;
				case IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL: fmt = GST_VIDEO_FORMAT_NV16; break;
				case IMX_VPU_COLOR_FORMAT_YUV444: fmt = GST_VIDEO_FORMAT_NV24; break;
				case IMX_VPU_COLOR_FORMAT_YUV400: fmt = GST_VIDEO_FORMAT_GRAY8; break;
				default:
					GST_ERROR_OBJECT(vpu_decoder, "unsupported color format %d", new_initial_info->color_format);
					return 0;
			}
		}
		else
		{
			switch (new_initial_info->color_format)
			{
				case IMX_VPU_COLOR_FORMAT_YUV420: fmt = GST_VIDEO_FORMAT_I420; break;
				case IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL: fmt = GST_VIDEO_FORMAT_Y42B; break;
				case IMX_VPU_COLOR_FORMAT_YUV444: fmt = GST_VIDEO_FORMAT_Y444; break;
				case IMX_VPU_COLOR_FORMAT_YUV400: fmt = GST_VIDEO_FORMAT_GRAY8; break;
				default:
					GST_ERROR_OBJECT(vpu_decoder, "unsupported color format %d", new_initial_info->color_format);
					return 0;
			}
		}

		/* Check if the output format is actually supported by downstream. If not,
		 * emit an element error and exit. */
		{
			gboolean format_supported;
			gchar const *format_str = gst_video_format_to_string(fmt);
			GstCaps *fmt_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, NULL);
			GstCaps *allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(vpu_decoder));

			format_supported = gst_caps_can_intersect(fmt_caps, allowed_srccaps);

			if (!format_supported)
			{
				GST_ELEMENT_ERROR(
					vpu_decoder,
					STREAM,
					FORMAT,
					("downstream elements do not support output format"),
					("output format: %s allowed srccaps: %" GST_PTR_FORMAT, format_str, allowed_srccaps)
				);
			}

			gst_caps_unref(allowed_srccaps);
			gst_caps_unref(fmt_caps);

			if (!format_supported)
				return 0;
		}

		/* In some corner cases, width & height are not set in the input caps. If this happens, use the
		 * width & height from the current_framebuffers object that was initialized earlier. It receives
		 * width and height information from the bitstream itself (through the init_info structure). */
		if (state->info.width == 0)
		{
			state->info.width = new_initial_info->frame_width;
			GST_INFO_OBJECT(vpu_decoder, "output state width is 0 - using the value %u from the initial info instead", state->info.width);
		}
		if (state->info.height == 0)
		{
			state->info.height = new_initial_info->frame_height;
			GST_INFO_OBJECT(vpu_decoder, "output state height is 0 - using the value %u from the initial info instead", state->info.height);
		}

		GST_VIDEO_INFO_INTERLACE_MODE(&(state->info)) = new_initial_info->interlacing ? GST_VIDEO_INTERLACE_MODE_INTERLEAVED : GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
		state = gst_video_decoder_set_output_state(GST_VIDEO_DECODER(vpu_decoder), fmt, state->info.width, state->info.height, state);
		gst_video_codec_state_unref(state);

		gst_video_codec_state_unref(vpu_decoder->current_output_state);
		vpu_decoder->current_output_state = NULL;
	}

	vpu_decoder->decoder_context->uses_interlacing = new_initial_info->interlacing;

	return 1;
}


static void gst_imx_vpu_decoder_add_to_unfinished_frame_table(GstImxVpuDecoder *vpu_decoder, GstVideoCodecFrame *frame)
{
	g_assert(vpu_decoder->unfinished_frames_table != NULL);
	g_hash_table_add(vpu_decoder->unfinished_frames_table, frame);
}


static void gst_imx_vpu_decoder_remove_from_unfinished_frame_table(GstImxVpuDecoder *vpu_decoder, GstVideoCodecFrame *frame)
{
	g_assert(vpu_decoder->unfinished_frames_table != NULL);
	g_hash_table_remove(vpu_decoder->unfinished_frames_table, frame);
}


static void gst_imx_vpu_decoder_release_all_unfinished_frames(GstImxVpuDecoder *vpu_decoder)
{
	GHashTableIter iter;
	gpointer key;

	GST_DEBUG_OBJECT(vpu_decoder, "clearing %u frames", g_hash_table_size(vpu_decoder->unfinished_frames_table));

	g_hash_table_iter_init(&iter, vpu_decoder->unfinished_frames_table);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		GstVideoCodecFrame *frame = (GstVideoCodecFrame *)key;
		if (frame != NULL)
			gst_video_decoder_release_frame(GST_VIDEO_DECODER(vpu_decoder), frame);
	}

	g_hash_table_remove_all(vpu_decoder->unfinished_frames_table);
}
