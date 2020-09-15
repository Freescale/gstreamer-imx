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

#include <gst/gst.h>
#include <gst/video/video.h>
#include "imx2d/backend/g2d/g2d_blitter.h"
#include "gstimx2dmisc.h"
#include "gstimx2dvideotransform.h"
#include "gstimxg2dvideotransform.h"


struct _GstImxG2DVideoTransform
{
	GstImx2dVideoTransform parent;
};


struct _GstImxG2DVideoTransformClass
{
	GstImx2dVideoTransformClass parent_class;
};


G_DEFINE_TYPE(GstImxG2DVideoTransform, gst_imx_g2d_video_transform, GST_TYPE_IMX_2D_VIDEO_TRANSFORM)


static Imx2dBlitter* gst_imx_g2d_video_transform_create_blitter(GstImx2dVideoTransform *imx_2d_video_transform);




static void gst_imx_g2d_video_transform_class_init(GstImxG2DVideoTransformClass *klass)
{
	GstElementClass *element_class;
	GstImx2dVideoTransformClass *imx_2d_video_transform_class;

	element_class = GST_ELEMENT_CLASS(klass);
	imx_2d_video_transform_class = GST_IMX_2D_VIDEO_TRANSFORM_CLASS(klass);

	imx_2d_video_transform_class->start = NULL;
	imx_2d_video_transform_class->stop = NULL;
	imx_2d_video_transform_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_g2d_video_transform_create_blitter);

	gst_imx_2d_video_transform_common_class_init(
		imx_2d_video_transform_class,
		imx_2d_backend_g2d_get_hardware_capabilities()
	);

	gst_element_class_set_static_metadata(
		element_class,
		"i.MX G2D video transform",
		"Filter/Converter/Video/Scaler/Transform/Effect/Hardware",
		"Video transformation using the Vivante G2D API on i.MX platforms",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_g2d_video_transform_init(G_GNUC_UNUSED GstImxG2DVideoTransform *self)
{
}


static Imx2dBlitter* gst_imx_g2d_video_transform_create_blitter(G_GNUC_UNUSED GstImx2dVideoTransform *imx_2d_video_transform)
{
	return imx_2d_backend_g2d_blitter_create();
}
