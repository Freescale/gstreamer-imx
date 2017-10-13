/* Freescale G2D GStreamer 1.0 plugin definition
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
#include "video_sink.h"
#include "video_transform.h"
#include "compositor.h"

#ifdef WITH_G2D_PANGO_ELEMENTS
GST_DEBUG_CATEGORY(pango_debug);
#include "pango/textoverlay.h"
#include "pango/timeoverlay.h"
#include "pango/clockoverlay.h"
#include "pango/textrender.h"
#endif



static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;

	ret = ret && gst_element_register(plugin, "imxg2dvideosink", GST_RANK_PRIMARY + 1, gst_imx_g2d_video_sink_get_type());
	ret = ret && gst_element_register(plugin, "imxg2dvideotransform", GST_RANK_PRIMARY + 1, gst_imx_g2d_video_transform_get_type());
	ret = ret && gst_element_register(plugin, "imxg2dcompositor", GST_RANK_NONE, gst_imx_g2d_compositor_get_type());

#ifdef WITH_G2D_PANGO_ELEMENTS
	GST_DEBUG_CATEGORY_INIT (pango_debug, "imxg2dpango", 0, "IMX G2D Pango elements");

	ret = ret && gst_element_register(plugin, "imxg2dtextoverlay", GST_RANK_NONE, GST_TYPE_IMX_G2D_TEXT_OVERLAY);
	ret = ret && gst_element_register(plugin, "imxg2dtimeoverlay", GST_RANK_NONE, GST_TYPE_IMX_G2D_TIME_OVERLAY);
	ret = ret && gst_element_register(plugin, "imxg2dclockoverlay", GST_RANK_NONE, GST_TYPE_IMX_G2D_CLOCK_OVERLAY);
	ret = ret && gst_element_register(plugin, "imxg2dtextrender", GST_RANK_NONE, GST_TYPE_IMX_G2D_TEXT_RENDER);
#endif

	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxg2d,
	"video sink and image processing elements using the Freescale i.MX G2D API",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

