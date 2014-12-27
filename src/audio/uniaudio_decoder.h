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


#ifndef GST_IMX_AUDIO_UNIAUDIO_DECODER_H
#define GST_IMX_AUDIO_UNIAUDIO_DECODER_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/gstaudiodecoder.h>

#include <fsl_unia.h>

#include "uniaudio_codec.h"


G_BEGIN_DECLS


typedef struct _GstImxAudioUniaudioDec GstImxAudioUniaudioDec;
typedef struct _GstImxAudioUniaudioDecClass GstImxAudioUniaudioDecClass;


#define GST_TYPE_IMX_AUDIO_UNIAUDIO_DEC             (gst_imx_audio_uniaudio_dec_get_type())
#define GST_IMX_AUDIO_UNIAUDIO_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_AUDIO_UNIAUDIO_DEC,GstImxAudioUniaudioDec))
#define GST_IMX_AUDIO_UNIAUDIO_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_AUDIO_UNIAUDIO_DEC,GstImxAudioUniaudioDecClass))
#define GST_IS_IMX_AUDIO_UNIAUDIO_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_AUDIO_UNIAUDIO_DEC))
#define GST_IS_IMX_AUDIO_UNIAUDIO_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_AUDIO_UNIAUDIO_DEC))


struct _GstImxAudioUniaudioDec
{
	GstAudioDecoder parent;
	GstImxAudioUniaudioCodec *codec;
	UniACodec_Handle handle;
	gboolean has_audioinfo_set;
	GstAudioChannelPosition *original_channel_positions, *reordered_channel_positions;
	GstAudioFormat pcm_format;
	guint num_channels;
	GstAdapter *out_adapter;
	guint skip_header_counter, num_vorbis_headers;
	GstBuffer *codec_data;
};


struct _GstImxAudioUniaudioDecClass
{
	GstAudioDecoderClass parent_class;
};


GType gst_imx_audio_uniaudio_dec_get_type(void);


G_END_DECLS


#endif
