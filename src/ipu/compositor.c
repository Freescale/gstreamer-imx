/* IPU-based i.MX compositor class
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


#include "compositor.h"




GST_DEBUG_CATEGORY_STATIC(imx_ipu_compositor_debug);
#define GST_CAT_DEFAULT imx_ipu_compositor_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink_%u",
	GST_PAD_SINK,
	GST_PAD_REQUEST,
	GST_IMX_IPU_BLITTER_SINK_CAPS
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_IMX_IPU_BLITTER_SRC_CAPS
);


G_DEFINE_TYPE(GstImxIpuCompositor, gst_imx_ipu_compositor, GST_TYPE_IMX_BLITTER_COMPOSITOR)


static GstImxBlitter* gst_imx_ipu_compositor_create_blitter(GstImxBlitterCompositor *blitter_compositor);




static void gst_imx_ipu_compositor_class_init(GstImxIpuCompositorClass *klass)
{
	GstImxBlitterCompositorClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_ipu_compositor_debug, "imxipucompositor", 0, "Freescale i.MX IPU compositor");

	base_class = GST_IMX_BLITTER_COMPOSITOR_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video compositor",
		"Filter/Editor/Video/Compositor",
		"Creates composite output stream out of multiple input video streams using the Freescale i.MX IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	base_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_ipu_compositor_create_blitter);
}


static void gst_imx_ipu_compositor_init(GstImxIpuCompositor *ipu_compositor)
{
	ipu_compositor->blitter = NULL;
}




static GstImxBlitter* gst_imx_ipu_compositor_create_blitter(GstImxBlitterCompositor *blitter_compositor)
{
	GstImxIpuCompositor *ipu_compositor = GST_IMX_IPU_COMPOSITOR(blitter_compositor);
	if (ipu_compositor->blitter == NULL)
	{
		if ((ipu_compositor->blitter = gst_imx_ipu_blitter_new()) == NULL)
		{
			GST_ERROR_OBJECT(blitter_compositor, "could not create IPU blitter");
			return NULL;
		}
	}

	return GST_IMX_BLITTER(ipu_compositor->blitter);
}

