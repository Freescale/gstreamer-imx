/* Freescale v4l2video GStreamer 1.0 plugin definition
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
#ifdef WITH_IMXV4L2VIDEOSRC
#include "v4l2src.h"
#endif
#ifdef WITH_IMXV4L2VIDEOSINK
#include "v4l2sink.h"
#endif


static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;

#ifdef WITH_IMXV4L2VIDEOSRC
	ret = ret && gst_element_register(plugin, "imxv4l2videosrc", GST_RANK_PRIMARY, gst_imx_v4l2src_get_type());
#endif
#ifdef WITH_IMXV4L2VIDEOSINK
	ret = ret && gst_element_register(plugin, "imxv4l2videosink", GST_RANK_PRIMARY, gst_imx_v4l2sink_get_type());
#endif

	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxv4l2video,
	"GStreamer i.MX Video4Linux2 elements",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

