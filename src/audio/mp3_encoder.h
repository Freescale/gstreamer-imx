/* GStreamer 1.0 audio decoder using the Freescale i.MX MP3 encoder
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


#ifndef GST_IMX_AUDIO_MP3_ENCODER_H
#define GST_IMX_AUDIO_MP3_ENCODER_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/gstaudioencoder.h>
#include <mp3_enc_interface.h>


G_BEGIN_DECLS


typedef struct _GstImxAudioMp3Enc GstImxAudioMp3Enc;
typedef struct _GstImxAudioMp3EncClass GstImxAudioMp3EncClass;


#define GST_TYPE_IMX_AUDIO_MP3_ENC             (gst_imx_audio_mp3_enc_get_type())
#define GST_IMX_AUDIO_MP3_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_AUDIO_MP3_ENC,GstImxAudioMp3Enc))
#define GST_IMX_AUDIO_MP3_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_AUDIO_MP3_ENC,GstImxAudioMp3EncClass))
#define GST_IS_IMX_AUDIO_MP3_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_AUDIO_MP3_ENC))
#define GST_IS_IMX_AUDIO_MP3_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_AUDIO_MP3_ENC))


struct _GstImxAudioMp3Enc
{
	GstAudioEncoder parent;
	MP3E_Encoder_Config config;
	MP3E_Encoder_Parameter param;
	guint bitrate;
	gboolean high_quality_mode;
	gpointer allocated_blocks[ENC_NUM_MEM_BLOCKS];
};


struct _GstImxAudioMp3EncClass
{
	GstAudioDecoderClass parent_class;
};


GType gst_imx_audio_mp3_enc_get_type(void);
GType gst_imx_audio_mp3_enc_bitrate_get_type(void);


G_END_DECLS


#endif
