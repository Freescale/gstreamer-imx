/* GStreamer video sink using the Freescale IPU
 * Copyright (C) 2013  Carlos Rafael Giani
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


#ifndef GST_FSL_IPU_SINK_H
#define GST_FSL_IPU_SINK_H

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>


G_BEGIN_DECLS


typedef struct _GstFslIpuSink GstFslIpuSink;
typedef struct _GstFslIpuSinkClass GstFslIpuSinkClass;
typedef struct _GstFslIpuSinkPrivate GstFslIpuSinkPrivate;


#define GST_TYPE_FSL_IPU_SINK             (gst_fsl_ipu_sink_get_type())
#define GST_FSL_IPU_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_IPU_SINK, GstFslIpuSink))
#define GST_FSL_IPU_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_IPU_SINK, GstFslIpuSinkClass))
#define GST_IS_FSL_IPU_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_IPU_SINK))
#define GST_IS_FSL_IPU_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_IPU_SINK))


struct _GstFslIpuSink
{
	GstVideoSink parent;
	GstFslIpuSinkPrivate *priv;
	GstVideoInfo video_info;
	GstBufferPool *pool;
};


struct _GstFslIpuSinkClass
{
	GstVideoSinkClass parent_class;
};


GType gst_fsl_ipu_sink_get_type(void);


G_END_DECLS


#endif

