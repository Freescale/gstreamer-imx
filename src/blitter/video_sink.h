/* GStreamer base class for i.MX blitter based video sinks
 * Copyright (C) 2015  Carlos Rafael Giani
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


#ifndef GST_IMX_BLITTER_VIDEO_SINK_H
#define GST_IMX_BLITTER_VIDEO_SINK_H


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <linux/fb.h>

#include "blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxBlitterVideoSink GstImxBlitterVideoSink;
typedef struct _GstImxBlitterVideoSinkClass GstImxBlitterVideoSinkClass;


#define GST_TYPE_IMX_BLITTER_VIDEO_SINK             (gst_imx_blitter_video_sink_get_type())
#define GST_IMX_BLITTER_VIDEO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BLITTER_VIDEO_SINK, GstImxBlitterVideoSink))
#define GST_IMX_BLITTER_VIDEO_SINK_CAST(obj)        ((GstImxBlitterVideoSink *)(obj))
#define GST_IMX_BLITTER_VIDEO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BLITTER_VIDEO_SINK, GstImxBlitterVideoSinkClass))
#define GST_IS_IMX_BLITTER_VIDEO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BLITTER_VIDEO_SINK))
#define GST_IS_IMX_BLITTER_VIDEO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BLITTER_VIDEO_SINK))


/* Macros for locking/unlocking the blitter video sink's mutex.
 * These should always be used when a property is set that affects
 * the blit operation. */
#define GST_IMX_BLITTER_VIDEO_SINK_LOCK(obj) do { g_mutex_lock(&(((GstImxBlitterVideoSink*)(obj))->mutex)); } while (0)
#define GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(obj) do { g_mutex_unlock(&(((GstImxBlitterVideoSink*)(obj))->mutex)); } while (0)


struct _GstImxBlitterVideoSink
{
	GstVideoSink parent;

	/*< protected >*/

	GMutex mutex;

	GstImxBlitter *blitter;

	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	/* Device name of the Linux framebuffer */
	gchar *framebuffer_name;
	/* GstBuffer encapsulating the Linux framebuffer memory */
	GstBuffer *framebuffer;
	/* File descriptor of the Linux framebuffer */
	int framebuffer_fd;
	GstImxRegion framebuffer_region;
	guint32 original_fb_virt_height;
	/* Pages for page flipping and vsync */
	guint current_fb_page;
	gboolean use_vsync;
	gboolean input_crop;
	gboolean last_frame_with_cropdata;

	gint window_x_coord, window_y_coord;
	guint window_width, window_height;

	GstVideoInfo input_video_info, output_video_info;

	gboolean is_paused;

	/* The canvas encompasses the visible screen, or the window
	 * defined by the window_* values above
	 * It is updated only on-demand */
	GstImxCanvas canvas;
	GstImxRegion input_region;
	GstImxRegion last_source_region;
	gboolean canvas_needs_update;
};


/**
 * GstImxBlitterVideoSinkClass:
 *
 * This is a base class for blitter-based video sinks. It takes care of drawing video
 * frames on the Linux framebuffer by using a blitter. All a derived class has to do
 * is to create a blitter in the @create_blitter vfunc. The derived class does not
 * have to concern itself with keeping the blitter alive. GstImxBlitterVideoSinkClass
 * acquires a blitter by calling @create_blitter in the NULL->READY state change, and
 * unrefs that blitter in the READY->NULL state change.
 *
 * @parent_class:   The parent class structure
 * @start:          Optional.
 *                  Called during the NULL->READY state change. Note that this is
 *                  called before @create_blitter.
 *                  If this returns FALSE, then the state change is considered to
 *                  have failed, and the sink's change_state function will return
 *                  GST_STATE_CHANGE_FAILURE.
 * @stop:           Optional.
 *                  Called during the READY->NULL state change.
 *                  Returns FALSE if stopping failed, TRUE otherwise.
 * @create_blitter: Required.
 *                  Instructs the subclass to create a new blitter instance and
 *                  return it. If the subclass is supposed to create the blitter
 *                  only once, then create it in @start, ref it here, and then
 *                  return it. It will be unref'd in the READY->NULL state change.
 *
 * If derived classes implement @set_property/@get_property functions, and these modify states
 * related to the blitter, these must surround the modifications with mutex locks. Use
 * @GST_IMX_BLITTER_VIDEO_SINK_LOCK and @GST_IMX_BLITTER_VIDEO_SINK_UNLOCK for this.
 */
struct _GstImxBlitterVideoSinkClass
{
	GstVideoSinkClass parent_class;

	gboolean (*start)(GstImxBlitterVideoSink *blitter_video_sink);
	gboolean (*stop)(GstImxBlitterVideoSink *blitter_video_sink);
	GstImxBlitter* (*create_blitter)(GstImxBlitterVideoSink *blitter_video_sink);
};


GType gst_imx_blitter_video_sink_get_type(void);


#endif
