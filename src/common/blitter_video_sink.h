/* GStreamer base class for i.MX blitter based video sinks
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


#ifndef GST_IMX_COMMON_BLITTER_VIDEO_SINK_H
#define GST_IMX_COMMON_BLITTER_VIDEO_SINK_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include "base_blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxBlitterVideoSink GstImxBlitterVideoSink;
typedef struct _GstImxBlitterVideoSinkClass GstImxBlitterVideoSinkClass;
typedef struct _GstImxBlitterVideoSinkPrivate GstImxBlitterVideoSinkPrivate;


#define GST_TYPE_IMX_BLITTER_VIDEO_SINK             (gst_imx_blitter_video_sink_get_type())
#define GST_IMX_BLITTER_VIDEO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BLITTER_VIDEO_SINK, GstImxBlitterVideoSink))
#define GST_IMX_BLITTER_VIDEO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BLITTER_VIDEO_SINK, GstImxBlitterVideoSinkClass))
#define GST_IS_IMX_BLITTER_VIDEO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BLITTER_VIDEO_SINK))
#define GST_IS_IMX_BLITTER_VIDEO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BLITTER_VIDEO_SINK))


/* Macros for locking/unlocking the blitter video sink's mutex.
 * These should always be used when a property is set that affects
 * the blit operation. */
#define GST_IMX_BLITTER_VIDEO_SINK_LOCK(obj) do { g_mutex_lock(&(((GstImxBlitterVideoSink*)(obj))->mutex)); } while (0)
#define GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(obj) do { g_mutex_unlock(&(((GstImxBlitterVideoSink*)(obj))->mutex)); } while (0)


/**
 * GstImxBlitterVideoSink:
 *
 * The opaque #GstImxBlitterVideoSink data structure.
 */
struct _GstImxBlitterVideoSink
{
	GstVideoSink parent;

	/*< protected >*/

	/* Mutex protecting the set input frame - set output frame - blit
	 * sequence inside show_frame */
	GMutex mutex;

	/* Flag to indicate initialization status; FALSE if
	 * the sink is in the NULL state, TRUE otherwise */
	gboolean initialized;

	/* The blitter to be used; is unref'd in the READY->NULL state change */
	GstImxBaseBlitter *blitter;

	/* Whether or not to enforce the aspect ratio defined by the input caps
	 * and the output framebuffer */
	gboolean force_aspect_ratio;

	/* Device name of the Linux framebuffer */
	gchar *framebuffer_name;
	/* GstBuffer encapsulating the Linux framebuffer memory */
	GstBuffer *framebuffer;
	/* File descriptor of the Linux framebuffer */
	int framebuffer_fd;

	gint window_x_coord, window_y_coord;
	guint window_width, window_height;

	/* Copy of the GstVideoInfo structure generated from the input caps */
	GstVideoInfo input_video_info;

	/* Flag to indicate if the videocrop meta metadata shall be applied */
	gboolean input_crop;

	gboolean do_transpose;
};


/**
 * GstImxBlitterVideoSinkClass
 * @parent_class: The parent class structure
 * @start:        Required.
 *                Called during the NULL->READY state change, after the Linux
 *                framebuffer was acquired (but before it is set as the blitter's
 *                output buffer, to give the derived sink the chance to call
 *                @gst_imx_blitter_video_sink_set_blitter inside @start.
 *                (This function must be called in @start.)
 * @stop:         Optional.
 *                Called during the READY->NULL state change, before the Linux
 *                framebuffer is released, and the blitter unref'd.
 *
 * The blitter video sink is an abstract base class for defining blitter-based video sinks. It
 * implements aspect ratio control, and uses a blitter specified with
 * @gst_imx_blitter_video_sink_set_blitter. Derived classes must implement at least the @start
 * function, and this function must internally call @gst_imx_blitter_video_sink_set_blitter.
 *
 * If derived classes implement @set_property/@get_property functions, and these modify states
 * related to the blitter, these must surround the modifications with mutex locks. Use
 * @GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK and @GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK for this.
 */
struct _GstImxBlitterVideoSinkClass
{
	GstVideoSinkClass parent_class;

	gboolean (*start)(GstImxBlitterVideoSink *blitter_video_sink);
	gboolean (*stop)(GstImxBlitterVideoSink *blitter_video_sink);
};


GType gst_imx_blitter_video_sink_get_type(void);

/* Sets the blitter the video sink uses for blitting video frames on the
 * Linux framebuffer. The blitter is ref'd. If a pointer to another blitter
 * was set previously, this older blitter is first unref'd. If the new and
 * the old blitter pointer are the same, this function does nothing. This
 * function can be called anytime, but must be called at least once inside @start.
 * If something goes wrong, it returns FALSE, otherwise TRUE. (The blitter is
 * ref'd even if it returns FALSE.)
 *
 * NOTE: This function must be called with a mutex lock. Surround this call
 * with GST_IS_IMX_BLITTER_VIDEO_SINK_LOCK/UNLOCK calls.
 */
gboolean gst_imx_blitter_video_sink_set_blitter(GstImxBlitterVideoSink *blitter_video_sink, GstImxBaseBlitter *blitter);

void gst_imx_blitter_video_sink_transpose_frames(GstImxBlitterVideoSink *blitter_video_sink, gboolean do_transpose);


G_END_DECLS


#endif
