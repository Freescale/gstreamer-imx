/* Freescale VPU GStreamer 1.0 plugin definition
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


#include <config.h>
#include <gst/gst.h>
#include "decoder.h"
#include "encoder_h263.h"
#include "encoder_h264.h"
#include "encoder_mpeg4.h"
#include "encoder_mjpeg.h"



static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;
	ret = ret && gst_element_register(plugin, "imxvpudec", GST_RANK_PRIMARY + 1, gst_imx_vpu_decoder_get_type());
	ret = ret && gst_element_register(plugin, "imxvpuenc_h263", GST_RANK_PRIMARY + 1, gst_imx_vpu_encoder_h263_get_type());
	ret = ret && gst_element_register(plugin, "imxvpuenc_h264", GST_RANK_PRIMARY + 1, gst_imx_vpu_encoder_h264_get_type());
	ret = ret && gst_element_register(plugin, "imxvpuenc_mpeg4", GST_RANK_PRIMARY + 1, gst_imx_vpu_encoder_mpeg4_get_type());
	ret = ret && gst_element_register(plugin, "imxvpuenc_mjpeg", GST_RANK_PRIMARY + 1, gst_imx_vpu_encoder_mjpeg_get_type());
	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxvpu,
	"video en- and decoder elements using the Freescale i.MX VPU",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

