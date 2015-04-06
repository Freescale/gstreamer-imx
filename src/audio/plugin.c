/* Freescale GStreamer 1.0 audio plugin definition
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


#include <config.h>
#include <gst/gst.h>
#include "uniaudio_decoder.h"
#include "mp3_encoder.h"



static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;
#ifdef WITH_UNIAUDIO_DECODER
	ret = ret && gst_element_register(plugin, "imxuniaudiodec", GST_RANK_PRIMARY + 1, gst_imx_audio_uniaudio_dec_get_type());
#endif
#ifdef WITH_MP3_ENCODER
	ret = ret && gst_element_register(plugin, "imxmp3audioenc", GST_RANK_PRIMARY + 1, gst_imx_audio_mp3_enc_get_type());
#endif
	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxaudio,
	"audio elements for the Freescale i.MX",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

