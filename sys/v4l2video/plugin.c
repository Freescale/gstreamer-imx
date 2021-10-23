/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
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
#include "gstimxv4l2videosrc.h"
#include "gstimxv4l2videosink.h"
#include "gstimxv4l2videotransform.h"


GST_DEBUG_CATEGORY(imx_v4l2_utils_debug);
GST_DEBUG_CATEGORY(imx_v4l2_format_debug);


static gboolean plugin_init(GstPlugin *plugin)
{
	GST_DEBUG_CATEGORY_INIT(imx_v4l2_utils_debug, "imxv4l2utils", 0, "NXP i.MX V4L2 utility functions");
	GST_DEBUG_CATEGORY_INIT(imx_v4l2_format_debug, "imxv4l2format", 0, "NXP i.MX V4L2 formats functions");

	gboolean ret = TRUE;
	ret = ret && gst_element_register(plugin, "imxv4l2videosrc", GST_RANK_PRIMARY, gst_imx_v4l2_video_src_get_type());
	ret = ret && gst_element_register(plugin, "imxv4l2videosink", GST_RANK_NONE, gst_imx_v4l2_video_sink_get_type());
	ret = ret && gst_element_register(plugin, "imxv4l2videotransform", GST_RANK_NONE, gst_imx_v4l2_video_transform_get_type());
	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxv4l2video,
	"Video capture and output elements using the Video4Linux2 API on the NXP i.MX 6 platforms",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
