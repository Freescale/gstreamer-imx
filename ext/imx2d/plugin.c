/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#ifdef WITH_GST_IMX2D_COMPOSITOR
#include "gstimxg2dcompositor.h"
#endif

#ifdef WITH_GST_IMX2D_VIDEOSINK
#include "gstimxg2dvideosink.h"
#include "gstimxpxpvideosink.h"
#endif

#include "gstimxg2dvideotransform.h"
#include "gstimxipuvideotransform.h"
#include "gstimxpxpvideotransform.h"


static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;

#ifdef WITH_IMX2D_G2D_BACKEND
#ifdef WITH_GST_IMX2D_COMPOSITOR
	ret = ret && gst_element_register(plugin, "imxg2dcompositor", GST_RANK_NONE, gst_imx_g2d_compositor_get_type());
#endif
#ifdef WITH_GST_IMX2D_VIDEOSINK
	ret = ret && gst_element_register(plugin, "imxg2dvideosink", GST_RANK_NONE, gst_imx_g2d_video_sink_get_type());
#endif
	ret = ret && gst_element_register(plugin, "imxg2dvideotransform", GST_RANK_NONE, gst_imx_g2d_video_transform_get_type());
#endif

#ifdef WITH_IMX2D_IPU_BACKEND
	ret = ret && gst_element_register(plugin, "imxipuvideotransform", GST_RANK_NONE, gst_imx_ipu_video_transform_get_type());
#endif

#ifdef WITH_IMX2D_PXP_BACKEND
#ifdef WITH_GST_IMX2D_VIDEOSINK
	ret = ret && gst_element_register(plugin, "imxpxpvideosink", GST_RANK_NONE, gst_imx_pxp_video_sink_get_type());
#endif
	ret = ret && gst_element_register(plugin, "imxpxpvideotransform", GST_RANK_NONE, gst_imx_pxp_video_transform_get_type());
#endif

	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imx2d,
	"i.MX 2D graphics processing elements",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
