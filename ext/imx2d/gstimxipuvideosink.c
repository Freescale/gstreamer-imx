/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2023  Carlos Rafael Giani
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
#include "imx2d/backend/ipu/ipu_blitter.h"
#include "gstimx2dmisc.h"
#include "gstimx2dvideosink.h"
#include "gstimxipuvideosink.h"


struct _GstImxIPUVideoSink
{
	GstImx2dVideoSink parent;
};


struct _GstImxIPUVideoSinkClass
{
	GstImx2dVideoSinkClass parent_class;
};


G_DEFINE_TYPE(GstImxIPUVideoSink, gst_imx_ipu_video_sink, GST_TYPE_IMX_2D_VIDEO_SINK)


static Imx2dBlitter* gst_imx_ipu_video_sink_create_blitter(GstImx2dVideoSink *imx_2d_video_sink);




static void gst_imx_ipu_video_sink_class_init(GstImxIPUVideoSinkClass *klass)
{
	GstElementClass *element_class;
	GstImx2dVideoSinkClass *imx_2d_video_sink_class;

	element_class = GST_ELEMENT_CLASS(klass);
	imx_2d_video_sink_class = GST_IMX_2D_VIDEO_SINK_CLASS(klass);

	imx_2d_video_sink_class->start = NULL;
	imx_2d_video_sink_class->stop = NULL;
	imx_2d_video_sink_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_create_blitter);

	gst_imx_2d_video_sink_common_class_init(
		imx_2d_video_sink_class,
		imx_2d_backend_ipu_get_hardware_capabilities()
	);

	gst_element_class_set_static_metadata(
		element_class,
		"i.MX IPU video sink",
		"Sink/Video/Hardware",
		"Video output using the i.MX IPU",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_ipu_video_sink_init(G_GNUC_UNUSED GstImxIPUVideoSink *self)
{
}


static Imx2dBlitter* gst_imx_ipu_video_sink_create_blitter(G_GNUC_UNUSED GstImx2dVideoSink *imx_2d_video_sink)
{
	return imx_2d_backend_ipu_blitter_create();
}
