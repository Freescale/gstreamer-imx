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

#include <gst/gst.h>
#include <gst/video/video.h>
#include "imx2d/backend/pxp/pxp_blitter.h"
#include "gstimx2dmisc.h"
#include "gstimx2dvideotransform.h"
#include "gstimxpxpvideotransform.h"


struct _GstImxPxPVideoTransform
{
	GstImx2dVideoTransform parent;
};


struct _GstImxPxPVideoTransformClass
{
	GstImx2dVideoTransformClass parent_class;
};


G_DEFINE_TYPE(GstImxPxPVideoTransform, gst_imx_pxp_video_transform, GST_TYPE_IMX_2D_VIDEO_TRANSFORM)


static Imx2dBlitter* gst_imx_pxp_video_transform_create_blitter(GstImx2dVideoTransform *imx_2d_video_transform);




static void gst_imx_pxp_video_transform_class_init(GstImxPxPVideoTransformClass *klass)
{
	GstElementClass *element_class;
	GstImx2dVideoTransformClass *imx_2d_video_transform_class;

	element_class = GST_ELEMENT_CLASS(klass);
	imx_2d_video_transform_class = GST_IMX_2D_VIDEO_TRANSFORM_CLASS(klass);

	imx_2d_video_transform_class->start = NULL;
	imx_2d_video_transform_class->stop = NULL;
	imx_2d_video_transform_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_create_blitter);

	gst_imx_2d_video_transform_common_class_init(
		imx_2d_video_transform_class,
		imx_2d_backend_pxp_get_hardware_capabilities()
	);

	gst_element_class_set_static_metadata(
		element_class,
		"i.MX PxP video transform",
		"Filter/Converter/Video/Scaler/Transform/Effect/Hardware",
		"Video transformation using the i.MX Pixel Pipeline (PxP)",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_pxp_video_transform_init(G_GNUC_UNUSED GstImxPxPVideoTransform *self)
{
}


static Imx2dBlitter* gst_imx_pxp_video_transform_create_blitter(G_GNUC_UNUSED GstImx2dVideoTransform *imx_2d_video_transform)
{
	return imx_2d_backend_pxp_blitter_create();
}
