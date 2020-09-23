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
#include "gstimx2dcompositor.h"
#include "gstimxg2dcompositor.h"


struct _GstImxG2DCompositor
{
	GstImx2dCompositor parent;
};


struct _GstImxG2DCompositorClass
{
	GstImx2dCompositorClass parent_class;
};


G_DEFINE_TYPE(GstImxG2DCompositor, gst_imx_g2d_compositor, GST_TYPE_IMX_2D_COMPOSITOR)


static Imx2dBlitter* gst_imx_g2d_compositor_create_blitter(GstImx2dCompositor *imx_2d_compositor);




static void gst_imx_g2d_compositor_class_init(GstImxG2DCompositorClass *klass)
{
	GstElementClass *element_class;
	GstImx2dCompositorClass *imx_2d_compositor_class;

	element_class = GST_ELEMENT_CLASS(klass);
	imx_2d_compositor_class = GST_IMX_2D_COMPOSITOR_CLASS(klass);

	imx_2d_compositor_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_g2d_compositor_create_blitter);

	gst_imx_2d_compositor_common_class_init(
		imx_2d_compositor_class,
		imx_2d_backend_g2d_get_hardware_capabilities()
	);

	gst_element_class_set_static_metadata(
		element_class,
		"i.MX G2D video transform",
		"Filter/Effect/Video/Compositor/Hardware",
		"Video transformation using the Vivante G2D API on i.MX platforms",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_g2d_compositor_init(G_GNUC_UNUSED GstImxG2DCompositor *self)
{
}


static Imx2dBlitter* gst_imx_g2d_compositor_create_blitter(G_GNUC_UNUSED GstImx2dCompositor *imx_2d_compositor)
{
	return imx_2d_backend_g2d_blitter_create();
}
