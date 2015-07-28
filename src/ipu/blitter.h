/* Common Freescale IPU blitter code for GStreamer
 * Copyright (C) 2014  Carlos Rafael Giani
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


#ifndef GST_IMX_IPU_BLITTER_H
#define GST_IMX_IPU_BLITTER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "../blitter/blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxIpuBlitter GstImxIpuBlitter;
typedef struct _GstImxIpuBlitterClass GstImxIpuBlitterClass;
typedef struct _GstImxIpuBlitterPrivate GstImxIpuBlitterPrivate;


#define GST_TYPE_IMX_IPU_BLITTER             (gst_imx_ipu_blitter_get_type())
#define GST_IMX_IPU_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitter))
#define GST_IMX_IPU_BLITTER_CAST(obj)        ((GstImxIpuBlitter *)(obj))
#define GST_IMX_IPU_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitterClass))
#define GST_IMX_IPU_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitterClass))
#define GST_IS_IMX_IPU_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_IPU_BLITTER))
#define GST_IS_IMX_IPU_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_IPU_BLITTER))


#define GST_IMX_IPU_VIDEO_FORMATS \
	" { " \
	"   RGB16 " \
	" , BGR " \
	" , RGB " \
	" , BGRx " \
	" , BGRA " \
	" , RGBx " \
	" , RGBA " \
	" , ABGR " \
	" , UYVY " \
	" , v308 " \
	" , NV12 " \
	" , YV12 " \
	" , I420 " \
	" , Y42B " \
	" , Y444 " \
	" } "

#define GST_IMX_IPU_BLITTER_SINK_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_IPU_VIDEO_FORMATS ", " \
		"width = (int) [ 64, MAX ], " \
		"height = (int) [ 64, MAX ], " \
		"framerate = (fraction) [ 0, MAX ], " \
		"interlace-mode = (string) { progressive, mixed, interleaved }; " \
	)

#define GST_IMX_IPU_BLITTER_SRC_CAPS GST_IMX_IPU_BLITTER_SINK_CAPS


#define GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT  FALSE


struct _GstImxIpuBlitter
{
	GstImxBlitter parent;

	GstVideoInfo input_video_info, output_video_info;
	GstAllocator *allocator;
	GstBuffer *input_frame, *output_frame, *fill_frame;
	gboolean use_entire_input_frame;

	GstImxIpuBlitterPrivate *priv;
	guint8 visibility_mask;
	guint32 fill_color;
	GstImxRegion empty_regions[4];
	guint num_empty_regions;

	GstImxRegion clipped_outer_region;
	gboolean clipped_outer_region_updated;
	guint num_output_pages, num_cleared_output_pages;

	gboolean deinterlacing_enabled;
};


struct _GstImxIpuBlitterClass
{
	GstImxBlitterClass parent_class;
};


GType gst_imx_ipu_blitter_get_type(void);

GstImxIpuBlitter* gst_imx_ipu_blitter_new(void);

void gst_imx_ipu_blitter_enable_deinterlacing(GstImxIpuBlitter *ipu_blitter, gboolean enable);


G_END_DECLS


#endif
