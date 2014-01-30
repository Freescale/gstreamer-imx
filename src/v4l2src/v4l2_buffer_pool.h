/*
 * Copyright (c) 2013-2014, Black Moth Technologies
 *   Author: Philip Craig <phil@blackmoth.com.au>
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

#ifndef GST_IMX_V4L2_BUFFER_POOL_H
#define GST_IMX_V4L2_BUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

G_BEGIN_DECLS

typedef struct _GstImxV4l2BufferPool GstImxV4l2BufferPool;
typedef struct _GstImxV4l2BufferPoolClass GstImxV4l2BufferPoolClass;
typedef struct _GstImxV4l2Meta GstImxV4l2Meta;

#define GST_TYPE_IMX_V4L2_BUFFER_POOL             (gst_imx_v4l2_buffer_pool_get_type())
#define GST_IMX_V4L2_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_V4L2_BUFFER_POOL, GstImxV4l2BufferPool))
#define GST_IMX_V4L2_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_V4L2_BUFFER_POOL, GstImxV4l2BufferPoolClass))

struct _GstImxV4l2BufferPool
{
	GstBufferPool bufferpool;

	int fd;
	GstBuffer **buffers;
	guint num_buffers;
	guint num_allocated;
	GstVideoInfo video_info;
	gboolean add_videometa;
};

struct _GstImxV4l2BufferPoolClass
{
	GstBufferPoolClass parent_class;
};

GType gst_imx_v4l2_buffer_pool_get_type(void);

GstBufferPool *gst_imx_v4l2_buffer_pool_new(int fd);

struct _GstImxV4l2Meta {
  GstMeta meta;

  gpointer mem;
  struct v4l2_buffer vbuffer;
};

GType gst_imx_v4l2_meta_api_get_type (void);
const GstMetaInfo * gst_imx_v4l2_meta_get_info (void);
#define GST_IMX_V4L2_META_GET(buf) ((GstImxV4l2Meta *)gst_buffer_get_meta(buf,gst_imx_v4l2_meta_api_get_type()))
#define GST_IMX_V4L2_META_ADD(buf) ((GstImxV4l2Meta *)gst_buffer_add_meta(buf,gst_imx_v4l2_meta_get_info(),NULL))

G_END_DECLS

#endif

