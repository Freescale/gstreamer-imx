/* PxP-based i.MX video transform class
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


#include "video_transform.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_pxp_video_transform_debug);
#define GST_CAT_DEFAULT imx_pxp_video_transform_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_PXP_BLITTER_SINK_CAPS
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_IMX_PXP_BLITTER_SRC_CAPS
);


G_DEFINE_TYPE(GstImxPxPVideoTransform, gst_imx_pxp_video_transform, GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM)


static GstImxBlitter* gst_imx_pxp_video_transform_create_blitter(GstImxBlitterVideoTransform *blitter_video_transform);

gboolean gst_imx_pxp_video_transform_are_video_infos_equal(GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info);




/* required functions declared by G_DEFINE_TYPE */

static void gst_imx_pxp_video_transform_class_init(GstImxPxPVideoTransformClass *klass)
{
	GstImxBlitterVideoTransformClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_pxp_video_transform_debug, "imxpxpvideotransform", 0, "Freescale i.MX PxP video transform");

	base_class = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale PxP video transform",
		"Filter/Converter/Video/Scaler",
		"Video transformation using the PxP API",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	base_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_create_blitter);

	base_class->are_video_infos_equal = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_are_video_infos_equal);
}


void gst_imx_pxp_video_transform_init(G_GNUC_UNUSED GstImxPxPVideoTransform *pxp_video_transform)
{
}



static GstImxBlitter* gst_imx_pxp_video_transform_create_blitter(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstImxPxPBlitter *blitter = gst_imx_pxp_blitter_new();
	if (blitter == NULL)
		GST_ERROR_OBJECT(blitter_video_transform, "could not create PxP blitter");

	return (GstImxBlitter *)blitter;
}


gboolean gst_imx_pxp_video_transform_are_video_infos_equal(G_GNUC_UNUSED GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info)
{
	return
		(GST_VIDEO_INFO_WIDTH(in_info) == GST_VIDEO_INFO_WIDTH(out_info)) &&
		(GST_VIDEO_INFO_HEIGHT(in_info) == GST_VIDEO_INFO_HEIGHT(out_info)) &&
		(GST_VIDEO_INFO_FORMAT(in_info) == GST_VIDEO_INFO_FORMAT(out_info))
		;
}
