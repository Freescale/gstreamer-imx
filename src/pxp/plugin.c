/* Freescale PxP GStreamer 1.0 plugin definition
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
#include "video_sink.h"
#include "video_transform.h"


GST_DEBUG_CATEGORY(imx_pxp_device_debug);



static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;

	GST_DEBUG_CATEGORY_INIT(imx_pxp_device_debug, "imxpxpdevice", 0, "Freescale i.MX PXP device");

	ret = ret && gst_element_register(plugin, "imxpxpvideosink", GST_RANK_PRIMARY + 1, gst_imx_pxp_video_sink_get_type());
	ret = ret && gst_element_register(plugin, "imxpxpvideotransform", GST_RANK_PRIMARY + 1, gst_imx_pxp_video_transform_get_type());

	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxpxp,
	"video sink and image processing elements using the Freescale i.MX PxP element",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
