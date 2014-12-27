/* GStreamer 1.0 audio decoder using the Freescale i.MX uniaudio codecs
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


#include <string.h>
#include <stdlib.h>
#include "uniaudio_decoder.h"
#include "uniaudio_codec.h"


GST_DEBUG_CATEGORY_STATIC(imx_audio_uniaudio_dec_debug);
#define GST_CAT_DEFAULT imx_audio_uniaudio_dec_debug


static uint32 uniaudio_channel_map_mono[] =
{
	UA_CHANNEL_FRONT_CENTER
};

static uint32 uniaudio_channel_map_2_0_stereo[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT
};

static uint32 uniaudio_channel_map_3_0_stereo[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_FRONT_CENTER
};

static uint32 uniaudio_channel_map_4_0_quad[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_REAR_LEFT,
	UA_CHANNEL_REAR_RIGHT,
};

static uint32 uniaudio_channel_map_4_1_quad[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_FRONT_CENTER,
	UA_CHANNEL_REAR_LEFT,
	UA_CHANNEL_REAR_RIGHT,
};

static uint32 uniaudio_channel_map_5_1_surround[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_FRONT_CENTER,
	UA_CHANNEL_LFE,
	UA_CHANNEL_REAR_LEFT,
	UA_CHANNEL_REAR_RIGHT
};

static uint32 uniaudio_channel_map_6_1_surround[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_FRONT_CENTER,
	UA_CHANNEL_REAR_CENTER,
	UA_CHANNEL_LFE,
	UA_CHANNEL_SIDE_LEFT,
	UA_CHANNEL_SIDE_RIGHT
};

static uint32 uniaudio_channel_map_7_1_surround[] =
{
	UA_CHANNEL_FRONT_LEFT,
	UA_CHANNEL_FRONT_RIGHT,
	UA_CHANNEL_FRONT_CENTER,
	UA_CHANNEL_LFE,
	UA_CHANNEL_REAR_LEFT,
	UA_CHANNEL_REAR_RIGHT,
	UA_CHANNEL_SIDE_LEFT,
	UA_CHANNEL_SIDE_RIGHT
};

static uint32 const * uniaudio_channel_maps[] =
{
	NULL, /* no 0-channel map */
	uniaudio_channel_map_mono,
	uniaudio_channel_map_2_0_stereo,
	uniaudio_channel_map_3_0_stereo,
	uniaudio_channel_map_4_0_quad,
	uniaudio_channel_map_4_1_quad,
	uniaudio_channel_map_5_1_surround,
	uniaudio_channel_map_6_1_surround,
	uniaudio_channel_map_7_1_surround
};

#define CHANNEL_MAPS_SIZE (sizeof(uniaudio_channel_maps) / sizeof(gint32 *))


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw, "
		"format = (string) { S32LE, S24LE, S16LE, S8 }, "
		"rate = [ 1, MAX ], "
		"channels = (int) [ 1, 8 ], "
		"layout = (string) interleaved "
	)
);


G_DEFINE_TYPE(GstImxAudioUniaudioDec, gst_imx_audio_uniaudio_dec, GST_TYPE_AUDIO_DECODER);


static void gst_imx_audio_uniaudio_dec_finalize(GObject *object);

static gboolean gst_imx_audio_uniaudio_dec_start(GstAudioDecoder *dec);
static gboolean gst_imx_audio_uniaudio_dec_stop(GstAudioDecoder *dec);
static gboolean gst_imx_audio_uniaudio_dec_set_format(GstAudioDecoder *dec, GstCaps *caps);
static GstFlowReturn gst_imx_audio_uniaudio_dec_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer);
static void gst_imx_audio_uniaudio_dec_flush(GstAudioDecoder *dec, gboolean hard);

static void gst_imx_audio_uniaudio_dec_clear_channel_positions(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec);
static void gst_imx_audio_uniaudio_dec_fill_channel_positions(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec, uint32 const *uniaudio_out_layout, guint num_channels);

static gboolean gst_imx_audio_uniaudio_dec_close_handle(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec);
static void* gst_imx_audio_uniaudio_dec_calloc(uint32 num_elements, uint32 size);
static void* gst_imx_audio_uniaudio_dec_malloc(uint32 size);
static void gst_imx_audio_uniaudio_dec_free(void *ptr);
static void* gst_imx_audio_uniaudio_dec_realloc(void *ptr, uint32 size);


static void gst_imx_audio_uniaudio_dec_class_init(GstImxAudioUniaudioDecClass *klass)
{
	GObjectClass *object_class;
	GstAudioDecoderClass *base_class;
	GstElementClass *element_class;
	GstPadTemplate *sink_template;
	GstCaps *sink_template_caps;

	GST_DEBUG_CATEGORY_INIT(imx_audio_uniaudio_dec_debug, "imxuniaudiodec", 0, "Freescale i.MX uniaudio decoder");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_AUDIO_DECODER_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_imx_audio_uniaudio_codec_table_init();

	sink_template_caps = gst_imx_audio_uniaudio_codec_table_get_caps();
	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	GST_DEBUG("decoder sink caps: %" GST_PTR_FORMAT, (gpointer)sink_template_caps);

	object_class->finalize      = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_finalize);
	base_class->start           = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_start);
	base_class->stop            = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_stop);
	base_class->set_format      = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_set_format);
	base_class->handle_frame    = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_handle_frame);
	base_class->flush           = GST_DEBUG_FUNCPTR(gst_imx_audio_uniaudio_dec_flush);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale i.MX uniaudio decoder",
		"Codec/Decoder/Audio",
		"audio decoding using the Freescale i.MX uniaudio codecs",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_imx_audio_uniaudio_dec_init(GstImxAudioUniaudioDec *imx_decoder)
{
	GstAudioDecoder *base = GST_AUDIO_DECODER(imx_decoder);
	gst_audio_decoder_set_drainable(base, TRUE);
	gst_audio_decoder_set_plc_aware(base, FALSE);

	imx_decoder->codec = NULL;
	imx_decoder->handle = NULL;
	imx_decoder->original_channel_positions = NULL;
	imx_decoder->reordered_channel_positions = NULL;
	imx_decoder->out_adapter = gst_adapter_new();
	imx_decoder->skip_header_counter = 0;
	imx_decoder->codec_data = NULL;
}


static void gst_imx_audio_uniaudio_dec_finalize(GObject *object)
{
	GstImxAudioUniaudioDec *imx_audio_uniaudio_dec = GST_IMX_AUDIO_UNIAUDIO_DEC(object);

	g_object_unref(G_OBJECT(imx_audio_uniaudio_dec->out_adapter));
	if (imx_audio_uniaudio_dec->codec_data != NULL)
		gst_buffer_unref(imx_audio_uniaudio_dec->codec_data);
	gst_imx_audio_uniaudio_dec_clear_channel_positions(imx_audio_uniaudio_dec);

	G_OBJECT_CLASS(gst_imx_audio_uniaudio_dec_parent_class)->finalize(object);
}


static gboolean gst_imx_audio_uniaudio_dec_start(GstAudioDecoder *dec)
{
	GstImxAudioUniaudioDec *imx_audio_uniaudio_dec = GST_IMX_AUDIO_UNIAUDIO_DEC(dec);
	imx_audio_uniaudio_dec->has_audioinfo_set = FALSE;
	return TRUE;
}


static gboolean gst_imx_audio_uniaudio_dec_stop(GstAudioDecoder *dec)
{
	return gst_imx_audio_uniaudio_dec_close_handle(GST_IMX_AUDIO_UNIAUDIO_DEC(dec));
}


static gboolean gst_imx_audio_uniaudio_dec_set_format(GstAudioDecoder *dec, GstCaps *caps)
{
	UniACodecParameter parameter;
	UniACodecMemoryOps memory_ops;
	GstImxAudioUniaudioDec *imx_audio_uniaudio_dec = GST_IMX_AUDIO_UNIAUDIO_DEC(dec);

#define UNIA_SET_PARAMETER(PARAM_ID, DESC) \
	do \
	{ \
		if (imx_audio_uniaudio_dec->codec->set_parameter(imx_audio_uniaudio_dec->handle, (PARAM_ID), &parameter) != ACODEC_SUCCESS) \
		{ \
			GST_ERROR_OBJECT(dec, "setting %s parameter failed: %s", (DESC), imx_audio_uniaudio_dec->codec->get_last_error(imx_audio_uniaudio_dec->handle)); \
			gst_imx_audio_uniaudio_dec_close_handle(imx_audio_uniaudio_dec); \
			return FALSE; \
		} \
	} \
	while (0)

#define UNIA_SET_PARAMETER_EX(PARAM_ID, DESC, VALUE) \
	do \
	{ \
		if (imx_audio_uniaudio_dec->codec->set_parameter(imx_audio_uniaudio_dec->handle, (PARAM_ID), ((UniACodecParameter *)(VALUE))) != ACODEC_SUCCESS) \
		{ \
			GST_ERROR_OBJECT(dec, "setting %s parameter failed: %s", (DESC), imx_audio_uniaudio_dec->codec->get_last_error(imx_audio_uniaudio_dec->handle)); \
			gst_imx_audio_uniaudio_dec_close_handle(imx_audio_uniaudio_dec); \
			return FALSE; \
		} \
	} \
	while (0)

	if (imx_audio_uniaudio_dec->handle != NULL)
	{
		/* drain old decoder handle */
		gst_imx_audio_uniaudio_dec_handle_frame(dec, NULL);
		gst_imx_audio_uniaudio_dec_close_handle(imx_audio_uniaudio_dec);
	}

	if ((imx_audio_uniaudio_dec->codec = gst_imx_audio_uniaudio_codec_table_get_codec(caps)) == NULL)
	{
		GST_ERROR_OBJECT(dec, "found no suitable codec for caps %" GST_PTR_FORMAT, (gpointer)caps);
		return FALSE;
	}

	memory_ops.Calloc  = gst_imx_audio_uniaudio_dec_calloc;
	memory_ops.Malloc  = gst_imx_audio_uniaudio_dec_malloc;
	memory_ops.Free    = gst_imx_audio_uniaudio_dec_free;
	memory_ops.ReAlloc = gst_imx_audio_uniaudio_dec_realloc;

	if ((imx_audio_uniaudio_dec->handle = imx_audio_uniaudio_dec->codec->create_codec(&memory_ops)) == NULL)
	{
		GST_ERROR_OBJECT(dec, "creating codec handle for caps %" GST_PTR_FORMAT " failed", (gpointer)caps);
		return FALSE;
	}

	/* Get configuration parameters from caps */
	{
		int samplerate, channels, bitrate, block_align, wmaversion;
		gchar const *stream_format, *sample_format;
		GValue const *value;
		GstBuffer *codec_data = NULL;
		GstStructure *structure = gst_caps_get_structure(caps, 0);

		imx_audio_uniaudio_dec->skip_header_counter = 0;

		if (gst_structure_get_int(structure, "rate", &samplerate))
		{
			GST_DEBUG_OBJECT(dec, "input caps sample rate: %d Hz", samplerate);
			parameter.samplerate = samplerate;
			UNIA_SET_PARAMETER(UNIA_SAMPLERATE, "sample rate");
		}

		if (gst_structure_get_int(structure, "channels", &channels))
		{
			CHAN_TABLE table;

			GST_DEBUG_OBJECT(dec, "input caps channel count: %d", channels);
			parameter.channels = channels;
			UNIA_SET_PARAMETER(UNIA_CHANNEL, "channel");

			memset(&table, 0, sizeof(table));
			table.size = CHANNEL_MAPS_SIZE;
			memcpy(&table.channel_table, uniaudio_channel_maps, sizeof(uniaudio_channel_maps));
			UNIA_SET_PARAMETER_EX(UNIA_CHAN_MAP_TABLE, "channel map", &table);
		}

		if (gst_structure_get_int(structure, "bitrate", &bitrate))
		{
			GST_DEBUG_OBJECT(dec, "input caps channel count: %d", bitrate);
			parameter.bitrate = bitrate;
			UNIA_SET_PARAMETER(UNIA_BITRATE, "bitrate");
		}

		if (gst_structure_get_int(structure, "block_align", &block_align))
		{
			GST_DEBUG_OBJECT(dec, "block alignment: %d", block_align);
			parameter.blockalign = block_align;
			UNIA_SET_PARAMETER(UNIA_WMA_BlOCKALIGN, "blockalign");
		}

		if (gst_structure_get_int(structure, "wmaversion", &wmaversion))
		{
			GST_DEBUG_OBJECT(dec, "WMA version: %d", wmaversion);
			parameter.version = wmaversion;
			UNIA_SET_PARAMETER(UNIA_WMA_VERSION, "wmaversion");
		}

		if ((stream_format = gst_structure_get_string(structure, "stream-format")) != NULL)
		{
			GST_DEBUG_OBJECT(dec, "input caps stream format: %s", stream_format);
			if (g_strcmp0(stream_format, "raw") == 0)
				parameter.stream_type = STREAM_ADTS;
			if (g_strcmp0(stream_format, "adif") == 0)
				parameter.stream_type = STREAM_ADIF;
			if (g_strcmp0(stream_format, "raw") == 0)
				parameter.stream_type = STREAM_RAW;
			else
				parameter.stream_type = STREAM_UNKNOW;
			UNIA_SET_PARAMETER(UNIA_STREAM_TYPE, "stream type");
		}

		if ((sample_format = gst_structure_get_string(structure, "format")) != NULL)
		{
			GstAudioFormat fmt;
			GstAudioFormatInfo const * fmtinfo;

			GST_DEBUG_OBJECT(dec, "input caps stream sample format: %s", sample_format);
			if ((fmt = gst_audio_format_from_string(sample_format)) == GST_AUDIO_FORMAT_UNKNOWN)
			{
				GST_ERROR_OBJECT(dec, "format is unknown, cannot continue");
				return FALSE;
			}

			fmtinfo = gst_audio_format_get_info(fmt);
			g_assert(fmtinfo != NULL);

			parameter.depth = GST_AUDIO_FORMAT_INFO_DEPTH(fmtinfo);
			UNIA_SET_PARAMETER(UNIA_DEPTH, "depth");
		}

		/* Handle codec data, either directly from a codec_data caps,
		 * or assemble it from a list of buffers specified by the
		 * streamheader caps (typically used by Vorbis audio) */

		/* Cleanup old codec data first */
		if (imx_audio_uniaudio_dec->codec_data != NULL)
		{
			gst_buffer_unref(imx_audio_uniaudio_dec->codec_data);
			imx_audio_uniaudio_dec->codec_data = NULL;
		}

		/* Check if either codec_data or streamheader caps exist */
		if ((value = gst_structure_get_value(structure, "codec_data")) != NULL)
		{
			/* codec_data caps exist - simply make a copy of its buffer
			 * (this makes sure we own that buffer properly) */

			GstBuffer *caps_buffer;
			GST_DEBUG_OBJECT(dec, "reading codec_data value");
			caps_buffer = gst_value_get_buffer(value);
			g_assert(caps_buffer != NULL);
			codec_data = gst_buffer_copy(caps_buffer);
		}
		else if ((value = gst_structure_get_value(structure, "streamheader")) != NULL)
		{
			/* streamheader caps exist, which are a list of buffers
			 * these buffers need to be concatenated and then given as
			 * one consecutive codec data buffer to the decoder */

			guint i, num_buffers = gst_value_array_get_size(value);
			GstAdapter *streamheader_adapter = gst_adapter_new();

			GST_DEBUG_OBJECT(dec, "reading streamheader value (%u headers)", num_buffers);

			imx_audio_uniaudio_dec->num_vorbis_headers = num_buffers;

			/* Use the GstAdapter to stitch these buffers together */
			for (i = 0; i < num_buffers; ++i)
			{
				GValue const *array_value = gst_value_array_get_value(value, i);
				GstBuffer *buf = gst_value_get_buffer(array_value);
				GST_DEBUG_OBJECT(dec, "add streamheader buffer #%u with %" G_GSIZE_FORMAT " byte", i, gst_buffer_get_size(buf));
				gst_adapter_push(streamheader_adapter, gst_buffer_copy(buf));
			}
			codec_data = gst_adapter_take_buffer(streamheader_adapter, gst_adapter_available(streamheader_adapter));

			g_object_unref(G_OBJECT(streamheader_adapter));
		}

		/* At this point, if either codec_data or streamheader caps were found,
		 * the codec_data pointer will refer to a valid non-empty buffer with
		 * codec data inside. This buffer is owned by this audio decoder object,
		 * and must be kept around for as long as the decoder needs to be ran,
		 * since the set_parameter call below does *not* copy the codec data
		 * bytes into some internal buffer. Instead, the uniaudio decoder plugin
		 * expects the caller to keep the buffer valid. */
		if ((codec_data != NULL) && (gst_buffer_get_size(codec_data) != 0))
		{
			GstMapInfo map;
			gst_buffer_map(codec_data, &map, GST_MAP_READ);
			parameter.codecData.size = map.size;
			parameter.codecData.buf = (char *)(map.data);
			UNIA_SET_PARAMETER(UNIA_CODEC_DATA, "codec data");
			gst_buffer_unmap(codec_data, &map);

			GST_DEBUG_OBJECT(dec, "codec data: %lu byte", parameter.codecData.size);
		}
	}

	/* framed = TRUE works with mp3, AMR-NB/WB, and Vorbis, but does not seem to
	 * change anything; however, it does break WMA decoding, since the WMA decoder
	 * then expects some additional ASF headers, so just always set framed to FALSE */
	parameter.framed = FALSE;
	UNIA_SET_PARAMETER(UNIA_FRAMED, "framed");

	GST_DEBUG_OBJECT(dec, "decoder configured");

	imx_audio_uniaudio_dec->has_audioinfo_set = FALSE;

#undef UNIA_SET_PARAMETER

	return TRUE;
}


static GstFlowReturn gst_imx_audio_uniaudio_dec_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer)
{
	GstMapInfo in_map;
	GstBuffer *out_buffer;
	gsize avail_out_size;
	GstImxAudioUniaudioDec *imx_audio_uniaudio_dec = GST_IMX_AUDIO_UNIAUDIO_DEC(dec);
	int32 dec_ret;
	uint32 offset = 0;
	uint8 *in_buf = NULL;
	uint32 in_size = 0;
	gboolean dec_loop = TRUE, flow_error = FALSE;

	/* With some formats such as Vorbis, the first few buffers are actually redundant,
	 * since they contain codec data that was already specified in codec_data or
	 * streamheader caps earlier. If this is the case, skip these buffers. */
	if (imx_audio_uniaudio_dec->skip_header_counter < imx_audio_uniaudio_dec->num_vorbis_headers)
	{
		GST_TRACE_OBJECT(dec, "skipping header buffer #%u", imx_audio_uniaudio_dec->skip_header_counter);
		++imx_audio_uniaudio_dec->skip_header_counter;
		return gst_audio_decoder_finish_frame(dec, NULL, 1);
	}

	if (buffer != NULL)
	{
		gst_buffer_map(buffer, &in_map, GST_MAP_READ);
		in_buf = in_map.data;
		in_size = in_map.size;
	}

	while (dec_loop)
	{
		GstBuffer *tmp_buf;
		uint8 *out_buf = NULL;
		uint32 out_size = 0;

		if (buffer != NULL)
			GST_TRACE_OBJECT(dec, "feeding %lu bytes to the decoder", in_size);
		else
			GST_TRACE_OBJECT(dec, "draining decoder");

		dec_ret = imx_audio_uniaudio_dec->codec->decode_frame(
			imx_audio_uniaudio_dec->handle,
			in_buf, in_size,
			&offset,
			&out_buf, &out_size
		);

		GST_TRACE_OBJECT(dec, "decode_frame:  return 0x%x  offset %lu  out_size %lu", (unsigned int)dec_ret, offset, out_size);

		if ((out_buf != NULL) && (out_size > 0))
		{
			tmp_buf = gst_audio_decoder_allocate_output_buffer(dec, out_size);
			tmp_buf = gst_buffer_make_writable(tmp_buf);
			gst_buffer_fill(tmp_buf, 0, out_buf, out_size);
			gst_adapter_push(imx_audio_uniaudio_dec->out_adapter, tmp_buf);
		}

		if ((buffer != NULL) && (offset == in_map.size))
		{
			dec_loop = FALSE;
		}

		switch (dec_ret)
		{
			case ACODEC_SUCCESS:
				break;
			case ACODEC_END_OF_STREAM:
				dec_loop = FALSE;
				break;
			case ACODEC_NOT_ENOUGH_DATA:
				break;
			case ACODEC_CAPIBILITY_CHANGE:
				break;
			default:
			{
				dec_loop = FALSE;
				flow_error = TRUE;
				GST_ELEMENT_ERROR(dec, STREAM, DECODE, ("could not decode"), ("error message: %s", imx_audio_uniaudio_dec->codec->get_last_error(imx_audio_uniaudio_dec->handle)));
			}
		}
	}

	if (buffer != NULL)
		gst_buffer_unmap(buffer, &in_map);

	if (flow_error)
		return GST_FLOW_ERROR;

	if (!(imx_audio_uniaudio_dec->has_audioinfo_set))
	{
		UniACodecParameter parameter;
		GstAudioFormat pcm_fmt;
		GstAudioInfo audio_info;

		imx_audio_uniaudio_dec->codec->get_parameter(imx_audio_uniaudio_dec->handle, UNIA_OUTPUT_PCM_FORMAT, &parameter);

		if ((parameter.outputFormat.width == 0) || (parameter.outputFormat.depth == 0))
		{
			GST_DEBUG_OBJECT(imx_audio_uniaudio_dec, "no output format available yet");
			return gst_audio_decoder_finish_frame(dec, NULL, 1);
		}

		GST_DEBUG_OBJECT(imx_audio_uniaudio_dec, "output sample width: %lu  depth: %lu", parameter.outputFormat.width, parameter.outputFormat.depth);
		pcm_fmt = gst_audio_format_build_integer(TRUE, G_BYTE_ORDER, parameter.outputFormat.width, parameter.outputFormat.depth);

		GST_DEBUG_OBJECT(imx_audio_uniaudio_dec, "setting output format to: %s  %d Hz  %d channels", gst_audio_format_to_string(pcm_fmt), (gint)(parameter.outputFormat.samplerate), (gint)(parameter.outputFormat.channels));

		gst_imx_audio_uniaudio_dec_clear_channel_positions(imx_audio_uniaudio_dec);
		gst_imx_audio_uniaudio_dec_fill_channel_positions(imx_audio_uniaudio_dec, parameter.outputFormat.layout, parameter.outputFormat.channels);

		imx_audio_uniaudio_dec->pcm_format = pcm_fmt;
		imx_audio_uniaudio_dec->num_channels = parameter.outputFormat.channels;

		gst_audio_info_set_format(
			&audio_info,
			pcm_fmt,
			parameter.outputFormat.samplerate,
			parameter.outputFormat.channels,
			imx_audio_uniaudio_dec->reordered_channel_positions
		);
		gst_audio_decoder_set_output_format(dec, &audio_info);

		imx_audio_uniaudio_dec->has_audioinfo_set = TRUE;
	}


	avail_out_size = gst_adapter_available(imx_audio_uniaudio_dec->out_adapter);

	if (avail_out_size > 0)
	{
		out_buffer = gst_adapter_take_buffer(imx_audio_uniaudio_dec->out_adapter, avail_out_size);
		if (imx_audio_uniaudio_dec->original_channel_positions != imx_audio_uniaudio_dec->reordered_channel_positions)
		{
			gst_audio_buffer_reorder_channels(
				out_buffer,
				imx_audio_uniaudio_dec->pcm_format,
				imx_audio_uniaudio_dec->num_channels,
				imx_audio_uniaudio_dec->original_channel_positions,
				imx_audio_uniaudio_dec->reordered_channel_positions
			);
		}
		return gst_audio_decoder_finish_frame(dec, out_buffer, 1);
	}
	else
	{
		return gst_audio_decoder_finish_frame(dec, NULL, 1);
	}
}


static void gst_imx_audio_uniaudio_dec_flush(GstAudioDecoder *dec, gboolean G_GNUC_UNUSED hard)
{
	GstImxAudioUniaudioDec *imx_audio_uniaudio_dec = GST_IMX_AUDIO_UNIAUDIO_DEC(dec);
	imx_audio_uniaudio_dec->codec->reset(imx_audio_uniaudio_dec->handle);
}


static void gst_imx_audio_uniaudio_dec_fill_channel_positions(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec, uint32 const *uniaudio_out_layout, guint num_channels)
{
	guint i;
	gsize num_chanpos_bytes = num_channels * sizeof(GstAudioChannelPosition);

	imx_audio_uniaudio_dec->original_channel_positions = g_malloc0(num_chanpos_bytes);
	imx_audio_uniaudio_dec->reordered_channel_positions = g_malloc0(num_chanpos_bytes);

	if (num_channels == 1)
	{
		imx_audio_uniaudio_dec->original_channel_positions[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
	}
	else
	{
		for (i = 0; i < num_channels; ++i)
		{
			GstAudioChannelPosition *pos = &(imx_audio_uniaudio_dec->original_channel_positions[i]);
			switch (uniaudio_out_layout[i])
			{
				case UA_CHANNEL_FRONT_LEFT:         *pos = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT; break;
				case UA_CHANNEL_FRONT_RIGHT:        *pos = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT; break;
				case UA_CHANNEL_REAR_CENTER:        *pos = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER; break;
				case UA_CHANNEL_REAR_LEFT:          *pos = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT; break;
				case UA_CHANNEL_REAR_RIGHT:         *pos = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT; break;
				case UA_CHANNEL_LFE:                *pos = GST_AUDIO_CHANNEL_POSITION_LFE1; break;
				case UA_CHANNEL_FRONT_CENTER:       *pos = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER; break;
				case UA_CHANNEL_FRONT_LEFT_CENTER:  *pos = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT; break;
				case UA_CHANNEL_FRONT_RIGHT_CENTER: *pos = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT; break;
				case UA_CHANNEL_SIDE_LEFT:          *pos = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT; break;
				case UA_CHANNEL_SIDE_RIGHT:         *pos = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT; break;

				default: *pos = GST_AUDIO_CHANNEL_POSITION_INVALID;
			}
		}
	}

	if (gst_audio_check_valid_channel_positions(imx_audio_uniaudio_dec->original_channel_positions, num_channels, TRUE))
	{
		GST_DEBUG_OBJECT(imx_audio_uniaudio_dec, "channel positions are in valid order, no need to reorder channels");
		imx_audio_uniaudio_dec->reordered_channel_positions = imx_audio_uniaudio_dec->original_channel_positions;
	}
	else
	{
		GST_DEBUG_OBJECT(imx_audio_uniaudio_dec, "channel positions are not in valid order -> need to reorder channels");
		memcpy(imx_audio_uniaudio_dec->reordered_channel_positions, imx_audio_uniaudio_dec->original_channel_positions, num_chanpos_bytes);
		gst_audio_channel_positions_to_valid_order(imx_audio_uniaudio_dec->reordered_channel_positions, num_channels);
	}
}


static void gst_imx_audio_uniaudio_dec_clear_channel_positions(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec)
{
	g_free(imx_audio_uniaudio_dec->original_channel_positions);
	if (imx_audio_uniaudio_dec->reordered_channel_positions != imx_audio_uniaudio_dec->original_channel_positions)
		g_free(imx_audio_uniaudio_dec->reordered_channel_positions);

	imx_audio_uniaudio_dec->original_channel_positions = NULL;
	imx_audio_uniaudio_dec->reordered_channel_positions = NULL;
}


static gboolean gst_imx_audio_uniaudio_dec_close_handle(GstImxAudioUniaudioDec *imx_audio_uniaudio_dec)
{
	gboolean ret = TRUE;

	if (imx_audio_uniaudio_dec->codec == NULL)
		return TRUE;

	if (imx_audio_uniaudio_dec->handle == NULL)
		return TRUE;

	if (imx_audio_uniaudio_dec->codec->delete_codec(imx_audio_uniaudio_dec->handle) != ACODEC_SUCCESS)
	{
		GST_ERROR_OBJECT(imx_audio_uniaudio_dec, "deleting codec handle produced an error: %s", imx_audio_uniaudio_dec->codec->get_last_error(imx_audio_uniaudio_dec->handle));
		ret = FALSE;
	}

	/* Setting this to NULL even if an error was produced, since
	 * there is nothing else that can be done at this point */
	imx_audio_uniaudio_dec->handle = NULL;

	return ret;
}


/* The memory allocation callbacks do not use the GLib memory functions,
 * since these lack a calloc implementation, and it is genrally not
 * recommended to use size*num and malloc as replacement (size may
 * overflow in some fringe cases) */


static void* gst_imx_audio_uniaudio_dec_calloc(uint32 num_elements, uint32 size)
{
	return calloc(num_elements, size);
}


static void* gst_imx_audio_uniaudio_dec_malloc(uint32 size)
{
	return malloc(size);
}


static void gst_imx_audio_uniaudio_dec_free(void *ptr)
{
	free(ptr);
}


static void* gst_imx_audio_uniaudio_dec_realloc(void *ptr, uint32 size)
{
	return realloc(ptr, size);
}
