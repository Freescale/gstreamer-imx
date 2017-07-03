/*
 * GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_IMX_V4L2SINK_H__
#define __GST_IMX_V4L2SINK_H__

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>

#include <linux/videodev2.h>

#include "../common/fd_object.h"

G_BEGIN_DECLS

#define GST_TYPE_IMX_V4L2SINK            (gst_imx_v4l2sink_get_type())
#define GST_IMX_V4L2SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMX_V4L2SINK,GstImxV4l2VideoSink))
#define GST_IS_IMX_V4L2SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMX_V4L2SINK))
#define GST_IMX_V4L2SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_IMX_V4L2SINK,GstImxV4l2VideoSinkClass))
#define GST_IS_IMX_V4L2SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_IMX_V4L2SINK))
#define GST_IMX_V4L2SINK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_IMX_V4L2SINK,GstImxV4l2VideoSinkClass))

typedef struct _GstImxV4l2VideoSink      GstImxV4l2VideoSink;
typedef struct _GstImxV4l2VideoSinkClass GstImxV4l2VideoSinkClass;

struct _GstImxV4l2VideoSink {
  GstVideoSink parent;

  gchar *device;

  GstImxFDObject *fd_obj_v4l;
  gboolean streamon;
  gint current, allocated, queued;
  GQueue last_buffers;

  struct v4l2_format fmt;
};

struct _GstImxV4l2VideoSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_imx_v4l2sink_get_type (void);

G_END_DECLS

#endif /* __GST_IMX_V4L2SINK_H__ */
