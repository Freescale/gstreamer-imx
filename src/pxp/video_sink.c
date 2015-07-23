/* PxP-based i.MX video sink class
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


#include "video_sink.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_pxp_video_sink_debug);
#define GST_CAT_DEFAULT imx_pxp_video_sink_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_PXP_BLITTER_SINK_CAPS
);


G_DEFINE_TYPE(GstImxPxPVideoSink, gst_imx_pxp_video_sink, GST_TYPE_IMX_BLITTER_VIDEO_SINK)


static GstImxBlitter* gst_imx_pxp_video_sink_create_blitter(GstImxBlitterVideoSink *blitter_video_sink);




/* required functions declared by G_DEFINE_TYPE */

static void gst_imx_pxp_video_sink_class_init(GstImxPxPVideoSinkClass *klass)
{
	GstImxBlitterVideoSinkClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_pxp_video_sink_debug, "imxpxpvideosink", 0, "Freescale i.MX PxP video sink");

	base_class = GST_IMX_BLITTER_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale PxP video sink",
		"Sink/Video",
		"Video output using the Freescale PxP API",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));

	base_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_sink_create_blitter);
}


static void gst_imx_pxp_video_sink_init(G_GNUC_UNUSED GstImxPxPVideoSink *pxp_video_sink)
{
}



/* base class functions */

static GstImxBlitter* gst_imx_pxp_video_sink_create_blitter(GstImxBlitterVideoSink *blitter_video_sink)
{
	GstImxPxPBlitter *blitter = gst_imx_pxp_blitter_new();
	if (blitter == NULL)
		GST_ERROR_OBJECT(blitter_video_sink, "could not create PxP blitter");

	return (GstImxBlitter *)blitter;
}
