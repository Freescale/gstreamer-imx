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

#ifndef GST_IMX_V4L2_VIDEO_SRC_H
#define GST_IMX_V4L2_VIDEO_SRC_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_V4L2_VIDEO_SRC             (gst_imx_v4l2_video_src_get_type())
#define GST_IMX_V4L2_VIDEO_SRC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_V4L2_VIDEO_SRC, GstImxV4L2VideoSrc))
#define GST_IMX_V4L2_VIDEO_SRC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_V4L2_VIDEO_SRC, GstImxV4L2VideoSrcClass))
#define GST_IMX_V4L2_VIDEO_SRC_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_V4L2_VIDEO_SRC, GstImxV4L2VideoSrcClass))
#define GST_IMX_V4L2_VIDEO_SRC_CAST(obj)        ((GstImxV4L2VideoSrc *)(obj))
#define GST_IS_IMX_V4L2_VIDEO_SRC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_V4L2_VIDEO_SRC))
#define GST_IS_IMX_V4L2_VIDEO_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_V4L2_VIDEO_SRC))


typedef struct _GstImxV4L2VideoSrc GstImxV4L2VideoSrc;
typedef struct _GstImxV4L2VideoSrcClass GstImxV4L2VideoSrcClass;


GType gst_imx_v4l2_video_src_get_type(void);


G_END_DECLS


#endif /* GST_IMX_V4L2_VIDEO_SRC_H */
