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

#ifndef GST_IMX_AUDIO_UNIAUDIO_CODEC_H
#define GST_IMX_AUDIO_UNIAUDIO_CODEC_H

#include <gst/gst.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <fsl_unia.h>
#pragma GCC diagnostic pop


G_BEGIN_DECLS


typedef struct _GstImxAudioUniaudioCodec GstImxAudioUniaudioCodec;


struct _GstImxAudioUniaudioCodec
{
	GstCaps *caps;
	void* dlhandle;

	tUniACodecQueryInterface query_interface;

	UniACodecVersionInfo get_version_info;
	UniACodecCreate create_codec;
	UniACodecDelete delete_codec;
	UniACodecSetParameter set_parameter;
	UniACodecGetParameter get_parameter;
	UniACodec_decode_frame decode_frame;
	UniACodecReset reset;
	UniACodec_get_last_error get_last_error;
};


void gst_imx_audio_uniaudio_codec_table_init(void);
GstCaps* gst_imx_audio_uniaudio_codec_table_get_caps(void);
GstImxAudioUniaudioCodec* gst_imx_audio_uniaudio_codec_table_get_codec(GstCaps *caps);


G_END_DECLS


#endif
