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

#ifndef GST_IMX_2D_COMPOSITOR_H
#define GST_IMX_2D_COMPOSITOR_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "imx2d/imx2d.h"
#include "gst/imx/video/gstimxvideobufferpool.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_2D_COMPOSITOR             (gst_imx_2d_compositor_get_type())
#define GST_IMX_2D_COMPOSITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_2D_COMPOSITOR, GstImx2dCompositor))
#define GST_IMX_2D_COMPOSITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_2D_COMPOSITOR, GstImx2dCompositorClass))
#define GST_IMX_2D_COMPOSITOR_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_2D_COMPOSITOR, GstImx2dCompositorClass))
#define GST_IMX_2D_COMPOSITOR_CAST(obj)        ((GstImx2dCompositor *)(obj))
#define GST_IS_IMX_2D_COMPOSITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_2D_COMPOSITOR))
#define GST_IS_IMX_2D_COMPOSITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_2D_COMPOSITOR))


typedef struct _GstImx2dCompositor GstImx2dCompositor;
typedef struct _GstImx2dCompositorClass GstImx2dCompositorClass;


struct _GstImx2dCompositor
{
	GstVideoAggregator parent;

	/*< private >*/

	GstAllocator *imx_dma_buffer_allocator;

	GstImxVideoBufferPool *video_buffer_pool;

	Imx2dBlitter *blitter;

	GstVideoInfo output_video_info;
	Imx2dSurface *output_surface;

	guint32 background_color;
};


struct _GstImx2dCompositorClass
{
	GstVideoAggregatorClass parent_class;

	Imx2dBlitter* (*create_blitter)(GstImx2dCompositor *imx_2d_compositor);

	Imx2dHardwareCapabilities const *hardware_capabilities;
};


GType gst_imx_2d_compositor_get_type(void);


void gst_imx_2d_compositor_common_class_init(GstImx2dCompositorClass *klass, Imx2dHardwareCapabilities const *capabilities);


G_END_DECLS


#endif /* GST_IMX_2D_COMPOSITOR_H */
