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

#ifndef GST_IMX_V4L2_VIDEO_SINK_H
#define GST_IMX_V4L2_VIDEO_SINK_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_V4L2_VIDEO_SINK             (gst_imx_v4l2_video_sink_get_type())
#define GST_IMX_V4L2_VIDEO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_V4L2_VIDEO_SINK, GstImxV4L2VideoSink))
#define GST_IMX_V4L2_VIDEO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_V4L2_VIDEO_SINK, GstImxV4L2VideoSinkClass))
#define GST_IMX_V4L2_VIDEO_SINK_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_V4L2_VIDEO_SINK, GstImxV4L2VideoSinkClass))
#define GST_IMX_V4L2_VIDEO_SINK_CAST(obj)        ((GstImxV4L2VideoSink *)(obj))
#define GST_IS_IMX_V4L2_VIDEO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_V4L2_VIDEO_SINK))
#define GST_IS_IMX_V4L2_VIDEO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_V4L2_VIDEO_SINK))


typedef struct _GstImxV4L2VideoSink GstImxV4L2VideoSink;
typedef struct _GstImxV4L2VideoSinkClass GstImxV4L2VideoSinkClass;


GType gst_imx_v4l2_video_sink_get_type(void);


G_END_DECLS


#endif /* GST_IMX_V4L2_VIDEO_SINK_H */
