/* GStreamer video sink using the Vivante GPU's direct textures
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


#ifndef GST_IMX_EGL_VIV_SINK_H
#define GST_IMX_EGL_VIV_SINK_H


#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include "gles2_renderer.h"


G_BEGIN_DECLS


typedef struct _GstImxEglVivSink GstImxEglVivSink;
typedef struct _GstImxEglVivSinkClass GstImxEglVivSinkClass;
typedef struct _GstImxEglVivSinkPrivate GstImxEglVivSinkPrivate;


#define GST_TYPE_IMX_EGL_VIV_SINK             (gst_imx_egl_viv_sink_get_type())
#define GST_IMX_EGL_VIV_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_EGL_VIV_SINK, GstImxEglVivSink))
#define GST_IMX_EGL_VIV_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_EGL_VIV_SINK, GstImxEglVivSinkClass))
#define GST_IS_IMX_EGL_VIV_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_EGL_VIV_SINK))
#define GST_IS_IMX_EGL_VIV_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_EGL_VIV_SINK))


struct _GstImxEglVivSink
{
	GstVideoSink parent;
	GstImxEglVivSinkGLES2Renderer *gles2_renderer;
	GstVideoInfo video_info;
	guintptr window_handle;
	gboolean handle_events, fullscreen;
	gchar *native_display_name;
	GMutex renderer_access_mutex;
};


struct _GstImxEglVivSinkClass
{
	GstVideoSinkClass parent_class;
};


GType gst_imx_egl_viv_sink_get_type(void);


G_END_DECLS


#endif

