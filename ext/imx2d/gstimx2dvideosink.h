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

#ifndef GST_IMX_2D_VIDEO_SINK_H
#define GST_IMX_2D_VIDEO_SINK_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferuploader.h"
#include "imx2d/imx2d.h"
#include "imx2d/linux_framebuffer.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_2D_VIDEO_SINK             (gst_imx_2d_video_sink_get_type())
#define GST_IMX_2D_VIDEO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_2D_VIDEO_SINK, GstImx2dVideoSink))
#define GST_IMX_2D_VIDEO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_2D_VIDEO_SINK, GstImx2dVideoSinkClass))
#define GST_IMX_2D_VIDEO_SINK_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_2D_VIDEO_SINK, GstImx2dVideoSinkClass))
#define GST_IMX_2D_VIDEO_SINK_CAST(obj)        ((GstImx2dVideoSink *)(obj))
#define GST_IS_IMX_2D_VIDEO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_2D_VIDEO_SINK))
#define GST_IS_IMX_2D_VIDEO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_2D_VIDEO_SINK))


typedef struct _GstImx2dVideoSink GstImx2dVideoSink;
typedef struct _GstImx2dVideoSinkClass GstImx2dVideoSinkClass;


struct _GstImx2dVideoSink
{
	GstVideoSink parent;

	/*< private >*/

	GstImxDmaBufferUploader *uploader;
	GstAllocator *imx_dma_buffer_allocator;

	Imx2dBlitter *blitter;

	GstVideoInfo input_video_info;
	Imx2dSurface *input_surface;
	Imx2dSurfaceDesc input_surface_desc;

	Imx2dLinuxFramebuffer *framebuffer;
	Imx2dSurface *framebuffer_surface;

	gboolean drop_frames;
	gchar *framebuffer_name;
	gboolean input_crop;
	GstVideoOrientationMethod video_direction;
	gboolean use_vsync;
	gboolean clear_at_null;
	gboolean force_aspect_ratio;

	GstVideoOrientationMethod tag_video_direction;

	gboolean drop_frames_changed;

	int write_fb_page;
	int display_fb_page;
	int num_fb_pages;
};


struct _GstImx2dVideoSinkClass
{
	GstVideoSinkClass parent_class;

	gboolean (*start)(GstImx2dVideoSink *imx_2d_video_sink);
	gboolean (*stop)(GstImx2dVideoSink *imx_2d_video_sink);

	Imx2dBlitter* (*create_blitter)(GstImx2dVideoSink *imx_2d_video_sink);
};


GType gst_imx_2d_video_sink_get_type(void);


void gst_imx_2d_video_sink_common_class_init(GstImx2dVideoSinkClass *klass, Imx2dHardwareCapabilities const *capabilities);


G_END_DECLS


#endif /* GST_IMX_2D_VIDEO_TRASINK*/
