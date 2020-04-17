/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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
#include "gstimxmp3encoder.h"


GST_DEBUG_CATEGORY_STATIC(imx_audio_mp3_enc_debug);
#define GST_CAT_DEFAULT imx_audio_mp3_enc_debug


enum
{
	PROP_0,
	PROP_BITRATE,
	PROP_HIGH_QUALITY_MODE
};


#define DEFAULT_BITRATE (128)
#define DEFAULT_HIGH_QUALITY_MODE (TRUE)


#define ALIGN_TO(VALUE, ALIGN_SIZE)  ( ((guintptr)(((guint8*)(VALUE)) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )

/* for the NXP MP3 encoder, the bits per frame count is fixed (16-bit stereo -> 2*2 byte -> 4 byte) */
#define MP3_ENCODER_NUM_INPUT_BPF (4) /* BPF = bytes per frame */
#define MP3_ENCODER_NUM_INPUT_FRAMES MP3E_INPUT_BUFFER_SIZE
#define MP3_ENCODER_NUM_INPUT_BYTES (MP3_ENCODER_NUM_INPUT_FRAMES * MP3_ENCODER_NUM_INPUT_BPF) /* bytes = frames * bytes_per_frame */


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw, "
		"format = (string) S16LE, "
		"rate = (int) { 32000, 44100, 48000 }, "
		"channels = (int) 2, "
		"layout = (string) interleaved "
	)
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/mpeg, "
		"mpegversion = (int) 1, "
		"layer = (int) 3, "
		"rate = (int) { 32000, 44100, 48000 }, "
		"channels = (int) 2 "
	)
);


G_DEFINE_TYPE(GstImxAudioMp3Enc, gst_imx_audio_mp3_enc, GST_TYPE_AUDIO_ENCODER);


static void gst_imx_audio_mp3_enc_finalize(GObject *object);
static void gst_imx_audio_mp3_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_audio_mp3_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_audio_mp3_enc_start(GstAudioEncoder *audioencoder);
static gboolean gst_imx_audio_mp3_enc_stop(GstAudioEncoder *audioencoder);
static gboolean gst_imx_audio_mp3_enc_set_format(GstAudioEncoder *audioencoder, GstAudioInfo *info);
static GstFlowReturn gst_imx_audio_mp3_enc_handle_frame(GstAudioEncoder *audioencoder, GstBuffer *buffer);
static void gst_imx_audio_mp3_enc_flush(GstAudioEncoder *audioencoder);

static GstBuffer* gst_imx_audio_mp3_enc_encode_frame(GstImxAudioMp3Enc *imx_audio_mp3_enc, GstBuffer *input_buffer);
static gchar const * gst_imx_audio_mp3_enc_error_string(MP3E_RET_VAL ret);




GType gst_imx_audio_mp3_enc_bitrate_get_type(void)
{
	static GType gst_imx_audio_mp3_enc_bitrate_type = 0;

	if (!gst_imx_audio_mp3_enc_bitrate_type)
	{
		static GEnumValue bitrate_values[] =
		{
			{ 32, "32 kbps", "32" },
			{ 40, "40 kbps", "40" },
			{ 48, "48 kbps", "48" },
			{ 56, "56 kbps", "56" },
			{ 64, "64 kbps", "64" },
			{ 80, "80 kbps", "80" },
			{ 96, "96 kbps", "96" },
			{ 112, "112 kbps", "112" },
			{ 128, "128 kbps", "128" },
			{ 160, "160 kbps", "160" },
			{ 192, "192 kbps", "192" },
			{ 224, "224 kbps", "224" },
			{ 256, "256 kbps", "256" },
			{ 320, "320 kbps", "320" },
			{ 0, NULL, NULL },
		};

		gst_imx_audio_mp3_enc_bitrate_type = g_enum_register_static(
			"ImxAudioMp3EncBitrate",
			bitrate_values
		);
	}

	return gst_imx_audio_mp3_enc_bitrate_type;
}




static void gst_imx_audio_mp3_enc_class_init(GstImxAudioMp3EncClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstAudioEncoderClass *audioencoder_class;

	GST_DEBUG_CATEGORY_INIT(imx_audio_mp3_enc_debug, "imxmp3audioenc", 0, "NXP i.MX MP3 encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	audioencoder_class = GST_AUDIO_ENCODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->finalize           = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_finalize);
	object_class->set_property       = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_set_property);
	object_class->get_property       = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_get_property);
	audioencoder_class->start        = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_start);
	audioencoder_class->stop         = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_stop);
	audioencoder_class->set_format   = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_set_format);
	audioencoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_handle_frame);
	audioencoder_class->flush        = GST_DEBUG_FUNCPTR(gst_imx_audio_mp3_enc_flush);

	GST_INFO("MP3 encoder version: %s", MP3ECodecVersionInfo());

	g_object_class_install_property(
		object_class,
		PROP_BITRATE,
		g_param_spec_enum(
			"bitrate",
			"Bitrate",
			"Bitrate of outgoing data, in kbps",
			gst_imx_audio_mp3_enc_bitrate_get_type(),
			DEFAULT_BITRATE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_HIGH_QUALITY_MODE,
		g_param_spec_boolean(
			"high-quality-mode",
			"High quality mode",
			"Use high quality encoding",
			DEFAULT_HIGH_QUALITY_MODE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"NXP i.MX MP3 encoder",
		"Codec/Encoder/Audio",
		"encodes PCM data to MP3 using the NXP i.MX MP3 encoder",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_imx_audio_mp3_enc_init(GstImxAudioMp3Enc *imx_audio_mp3_enc)
{
	GstAudioEncoder *audioencoder = GST_AUDIO_ENCODER(imx_audio_mp3_enc);

	imx_audio_mp3_enc->bitrate = DEFAULT_BITRATE;
	imx_audio_mp3_enc->high_quality_mode = DEFAULT_HIGH_QUALITY_MODE;

	memset(&(imx_audio_mp3_enc->config), 0, sizeof(MP3E_Encoder_Config));
	memset(&(imx_audio_mp3_enc->param), 0, sizeof(MP3E_Encoder_Parameter));
	memset(&(imx_audio_mp3_enc->allocated_blocks), 0, sizeof(gpointer) * ENC_NUM_MEM_BLOCKS);

	gst_audio_encoder_set_drainable(audioencoder, TRUE);
}


static void gst_imx_audio_mp3_enc_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_imx_audio_mp3_enc_parent_class)->finalize(object);
}


static void gst_imx_audio_mp3_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(object);

	switch (prop_id)
	{
		case PROP_BITRATE:
			imx_audio_mp3_enc->bitrate = g_value_get_enum(value);
			break;

		case PROP_HIGH_QUALITY_MODE:
			imx_audio_mp3_enc->high_quality_mode = g_value_get_boolean(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_audio_mp3_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(object);

	switch (prop_id)
	{
		case PROP_BITRATE:
			g_value_set_enum(value, imx_audio_mp3_enc->bitrate);
			break;

		case PROP_HIGH_QUALITY_MODE:
			g_value_set_boolean(value, imx_audio_mp3_enc->high_quality_mode);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_audio_mp3_enc_start(GstAudioEncoder *audioencoder)
{
	MP3E_RET_VAL ret;
	int i;
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(audioencoder);

	/* Make sure no leftover stale states are present */
	memset(&(imx_audio_mp3_enc->config), 0, sizeof(MP3E_Encoder_Config));
	memset(&(imx_audio_mp3_enc->param), 0, sizeof(MP3E_Encoder_Parameter));
	memset(&(imx_audio_mp3_enc->allocated_blocks), 0, sizeof(gpointer) * ENC_NUM_MEM_BLOCKS);

	ret = mp3e_query_mem(&(imx_audio_mp3_enc->config));
	if (ret != MP3E_SUCCESS)
	{
		GST_ERROR_OBJECT(audioencoder, "mp3e_query_mem() error: %s", gst_imx_audio_mp3_enc_error_string(ret));
		return FALSE;
	}

	for (i = 0; i < ENC_NUM_MEM_BLOCKS; ++i)
	{
		gpointer ptr;
		MP3E_Mem_Alloc_Info *alloc_info = &(imx_audio_mp3_enc->config.mem_info[i]);
		GST_DEBUG_OBJECT(audioencoder, "allocating memory block with %d byte and alignment %d", alloc_info->size, alloc_info->align);
		ptr = g_malloc(alloc_info->size + alloc_info->align);
		if (ptr == NULL)
		{
			GST_ERROR_OBJECT(audioencoder, "allocating memory block failed");
			return FALSE;
		}

		alloc_info->ptr = (MP3E_INT32 *)ALIGN_TO(ptr, alloc_info->align);
		imx_audio_mp3_enc->allocated_blocks[i] = ptr;
	}

	return TRUE;
}


static gboolean gst_imx_audio_mp3_enc_stop(GstAudioEncoder *audioencoder)
{
	int i;
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(audioencoder);

	for (i = 0; i < ENC_NUM_MEM_BLOCKS; ++i)
	{
		gpointer ptr = imx_audio_mp3_enc->allocated_blocks[i];
		if (ptr != NULL)
		{
			MP3E_Mem_Alloc_Info *alloc_info = &(imx_audio_mp3_enc->config.mem_info[i]);
			GST_DEBUG_OBJECT(audioencoder, "freeing memory block with %d byte and alignment %d", alloc_info->size, alloc_info->align);

			g_free(ptr);
			imx_audio_mp3_enc->allocated_blocks[i] = NULL;
		}
	}

	return TRUE;
}


static gboolean gst_imx_audio_mp3_enc_set_format(GstAudioEncoder *audioencoder, GstAudioInfo *info)
{
	MP3E_RET_VAL ret;
	GstCaps *output_caps, *allowed_srccaps;
	gint num_out_channels, out_sample_rate, stereo_mode, input_format, input_quality;
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(audioencoder);
	MP3E_Encoder_Parameter *param = &(imx_audio_mp3_enc->param);

	gst_audio_encoder_set_frame_samples_min(audioencoder, MP3_ENCODER_NUM_INPUT_FRAMES);
	gst_audio_encoder_set_frame_samples_max(audioencoder, MP3_ENCODER_NUM_INPUT_FRAMES);
	gst_audio_encoder_set_frame_max(audioencoder, 1);

	gst_imx_audio_mp3_enc_flush(audioencoder);

	allowed_srccaps = gst_pad_get_allowed_caps(GST_AUDIO_ENCODER_SRC_PAD(audioencoder));
	if (allowed_srccaps == NULL)
	{
		/* srcpad is not linked (yet), so no peer information is available;
		 * just use the default output channel count (stereo) */

		GST_DEBUG_OBJECT(audioencoder, "srcpad is not linked (yet) -> using default stereo mode");
		num_out_channels = 2;
	}
	else
	{
		/* Look at the channel count from the first structure */
		gboolean err = FALSE;
		GstStructure *structure = gst_caps_get_structure(allowed_srccaps, 0);
		GValue const *channels_value = gst_structure_get_value(structure, "channels");

		if (channels_value == NULL)
		{
			GST_INFO_OBJECT(audioencoder, "output caps structure has no channels field - using default stereo mode");
			num_out_channels = 2;
		}
		else if (!gst_value_is_fixed(channels_value))
		{
			num_out_channels = 2;
			GST_INFO_OBJECT(audioencoder, "output caps structure has no fixated channels field - using default stereo mode");
		}
		else if (G_VALUE_HOLDS_INT(channels_value))
		{
			num_out_channels = g_value_get_int(channels_value);
		}
		else
		{
			GST_ERROR_OBJECT(audioencoder, "unexpected type for 'channel' field in caps structure %" GST_PTR_FORMAT, (gpointer)structure);
			err = TRUE;
		}

		gst_caps_unref(allowed_srccaps);

		if (err)
			return TRUE;
	}

	out_sample_rate = GST_AUDIO_INFO_RATE(info);
	GST_DEBUG_OBJECT(audioencoder, "output channel count: %d  output sample rate: %d", num_out_channels, out_sample_rate);

	stereo_mode = (num_out_channels == 2) ? 0x00 : 0x01; /* 0x00 = joint stereo  0x00 = mono */
	input_format = 0x00; /* L/R interleaved */
	input_quality = imx_audio_mp3_enc->high_quality_mode ? 0x01 : 0x00; /* 0x00 = low quality  0x01 = high quality */

	param->app_sampling_rate = out_sample_rate;
	param->app_bit_rate = imx_audio_mp3_enc->bitrate;
	param->app_mode = (stereo_mode << 0) | (input_format << 8) | (input_quality << 16);

	ret = mp3e_encode_init(param, &(imx_audio_mp3_enc->config));
	if (ret != MP3E_SUCCESS)
	{
		GST_ERROR_OBJECT(audioencoder, "error while initializing encoder: %s", gst_imx_audio_mp3_enc_error_string(ret));
		return FALSE;
	}

	if (param->mp3e_outbuf_size == 0)
	{
		GST_ERROR_OBJECT(audioencoder, "output buffer size is zero");
		return FALSE;
	}

	GST_DEBUG_OBJECT(audioencoder, "output buffer size: %d byte", param->mp3e_outbuf_size);

	output_caps = gst_caps_new_simple(
		"audio/mpeg",
		"mpegversion", G_TYPE_INT, (gint)1,
		"layer", G_TYPE_INT, (gint)3,
		"rate", G_TYPE_INT, out_sample_rate,
		"channels", G_TYPE_INT, num_out_channels,
		NULL
	);
	gst_audio_encoder_set_output_format(audioencoder, output_caps);
	gst_caps_unref(output_caps);

	return TRUE;
}


static GstFlowReturn gst_imx_audio_mp3_enc_handle_frame(GstAudioEncoder *audioencoder, GstBuffer *buffer)
{
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(audioencoder);

	if (buffer == NULL)
	{
		gst_imx_audio_mp3_enc_flush(audioencoder);
		return GST_FLOW_EOS;
	}
	else
	{
		GstBuffer *output_buffer = gst_imx_audio_mp3_enc_encode_frame(imx_audio_mp3_enc, buffer);
		gst_audio_encoder_finish_frame(audioencoder, output_buffer, MP3_ENCODER_NUM_INPUT_FRAMES);
		return GST_FLOW_OK;
	}
}


static void gst_imx_audio_mp3_enc_flush(GstAudioEncoder *audioencoder)
{
	GstImxAudioMp3Enc *imx_audio_mp3_enc = GST_IMX_AUDIO_MP3_ENC(audioencoder);

	if (imx_audio_mp3_enc->param.mp3e_outbuf_size == 0)
	{
		GST_DEBUG_OBJECT(audioencoder, "encoder not initialized yet - nothing to flush");
		return;
	}

	GstMapInfo out_map_info;
	GstBuffer *output_buffer = gst_audio_encoder_allocate_output_buffer(audioencoder, imx_audio_mp3_enc->param.mp3e_outbuf_size);
	gst_buffer_map(output_buffer, &out_map_info, GST_MAP_WRITE);
	mp3e_flush_bitstream(&(imx_audio_mp3_enc->config), (MP3E_INT8 *)(out_map_info.data));
	gst_buffer_unmap(output_buffer, &out_map_info);

	if (imx_audio_mp3_enc->config.num_bytes > 0)
	{
		GST_TRACE_OBJECT(audioencoder, "flushed encoder, writing out %d bytes", imx_audio_mp3_enc->config.num_bytes);
		gst_buffer_resize(output_buffer, 0, imx_audio_mp3_enc->config.num_bytes);
		gst_audio_encoder_finish_frame(audioencoder, output_buffer, imx_audio_mp3_enc->config.num_bytes / MP3_ENCODER_NUM_INPUT_BPF);
	}
	else
	{
		GST_TRACE_OBJECT(audioencoder, "flushed encoder, but no bytes to write");
		gst_buffer_unref(output_buffer);
	}
}


static GstBuffer* gst_imx_audio_mp3_enc_encode_frame(GstImxAudioMp3Enc *imx_audio_mp3_enc, GstBuffer *input_buffer)
{
	gsize orig_input_size;
	GstMapInfo in_map_info, out_map_info;
	GstBuffer *output_buffer;

	gst_buffer_ref(input_buffer); /* necessary because of the gst_buffer_make_writable() call below */

	orig_input_size = gst_buffer_get_size(input_buffer);
	if (orig_input_size < MP3_ENCODER_NUM_INPUT_BYTES)
	{
		GstMapInfo tmp_map_info;
		GstBuffer *temp_in_buffer = gst_buffer_new_allocate(NULL, MP3_ENCODER_NUM_INPUT_BYTES, NULL);

		gsize num_padding_bytes = MP3_ENCODER_NUM_INPUT_BYTES - orig_input_size;

		GST_TRACE_OBJECT(imx_audio_mp3_enc, "adding %" G_GSIZE_FORMAT " padding null bytes to input buffer", num_padding_bytes);

		gst_buffer_map(temp_in_buffer, &tmp_map_info, GST_MAP_WRITE);
		gst_buffer_extract(input_buffer, 0, tmp_map_info.data, orig_input_size);
		memset(tmp_map_info.data + orig_input_size, 0, num_padding_bytes);
		gst_buffer_unmap(temp_in_buffer, &tmp_map_info);

		gst_buffer_replace(&input_buffer, temp_in_buffer);
	}

	output_buffer = gst_audio_encoder_allocate_output_buffer(GST_AUDIO_ENCODER_CAST(imx_audio_mp3_enc), imx_audio_mp3_enc->param.mp3e_outbuf_size);

	gst_buffer_map(input_buffer, &in_map_info, GST_MAP_READ);
	gst_buffer_map(output_buffer, &out_map_info, GST_MAP_WRITE);

	mp3e_encode_frame((MP3E_INT16 *)(in_map_info.data), &(imx_audio_mp3_enc->config), (MP3E_INT8 *)(out_map_info.data));

	gst_buffer_unmap(output_buffer, &out_map_info);
	gst_buffer_unmap(input_buffer, &in_map_info);

	gst_buffer_resize(output_buffer, 0, imx_audio_mp3_enc->config.num_bytes);

	GST_TRACE_OBJECT(
		imx_audio_mp3_enc,
		"input buffer size: %" G_GSIZE_FORMAT " bytes (%" G_GSIZE_FORMAT " with padding null bytes)  output buffer size: %" G_GSIZE_FORMAT,
		orig_input_size,
		gst_buffer_get_size(input_buffer),
		gst_buffer_get_size(output_buffer)
	);

	gst_buffer_unref(input_buffer);

	return output_buffer;
}


static gchar const * gst_imx_audio_mp3_enc_error_string(MP3E_RET_VAL ret)
{
	switch (ret)
	{
		case MP3E_SUCCESS: return "success";
		case MP3E_ERROR_INIT_BITRATE: return "invalid bitrate";
		case MP3E_ERROR_INIT_SAMPLING_RATE: return "invalid sample rate";
		case MP3E_ERROR_INIT_MODE: return "invalid stereo mode";
		case MP3E_ERROR_INIT_FORMAT: return "invalid input format";
		case MP3E_ERROR_INIT_QUALITY: return "invalid quality value";
		case MP3E_ERROR_INIT_QUERY_MEM: return "querying memory requirements failed";
		default: return "<unknown>";
	}
}
