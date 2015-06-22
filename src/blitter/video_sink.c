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


#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>

#include "phys_mem_meta.h"
#include "video_sink.h"




GST_DEBUG_CATEGORY_STATIC(imx_blitter_video_sink_2_debug);
#define GST_CAT_DEFAULT imx_blitter_video_sink_2_debug


enum
{
	PROP_0,
	PROP_FORCE_ASPECT_RATIO,
	PROP_FBDEV_NAME,
	PROP_OUTPUT_ROTATION,
	PROP_WINDOW_X_COORD,
	PROP_WINDOW_Y_COORD,
	PROP_WINDOW_WIDTH,
	PROP_WINDOW_HEIGHT,
	PROP_LEFT_MARGIN,
	PROP_TOP_MARGIN,
	PROP_RIGHT_MARGIN,
	PROP_BOTTOM_MARGIN
};


#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_FBDEV_NAME "/dev/fb0"
#define DEFAULT_OUTPUT_ROTATION GST_IMX_CANVAS_INNER_ROTATION_NONE
#define DEFAULT_WINDOW_X_COORD 0
#define DEFAULT_WINDOW_Y_COORD 0
#define DEFAULT_WINDOW_WIDTH 0
#define DEFAULT_WINDOW_HEIGHT 0
#define DEFAULT_LEFT_MARGIN 0
#define DEFAULT_TOP_MARGIN 0
#define DEFAULT_RIGHT_MARGIN 0
#define DEFAULT_BOTTOM_MARGIN 0


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterVideoSink2, gst_imx_blitter_video_sink_2, GST_TYPE_VIDEO_SINK)


static void gst_imx_blitter_video_sink_2_dispose(GObject *object);
static void gst_imx_blitter_video_sink_2_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_blitter_video_sink_2_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_blitter_video_sink_2_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_blitter_video_sink_2_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_blitter_video_sink_2_event(GstBaseSink *sink, GstEvent *event);
static gboolean gst_imx_blitter_video_sink_2_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_imx_blitter_video_sink_2_show_frame(GstVideoSink *video_sink, GstBuffer *buf);

static gboolean gst_imx_blitter_video_sink_2_open_framebuffer_device(GstImxBlitterVideoSink2 *blitter_video_sink_2);
static void gst_imx_blitter_video_sink_2_close_framebuffer_device(GstImxBlitterVideoSink2 *blitter_video_sink_2);
static GstVideoFormat gst_imx_blitter_video_sink_2_get_format_from_fb(GstImxBlitterVideoSink2 *blitter_video_sink_2, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);
static void gst_imx_blitter_video_sink_2_update_canvas(GstImxBlitterVideoSink2 *blitter_video_sink_2);
static gboolean gst_imx_blitter_video_sink_2_acquire_blitter(GstImxBlitterVideoSink2 *blitter_video_sink_2);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

static void gst_imx_blitter_video_sink_2_class_init(GstImxBlitterVideoSink2Class *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_video_sink_2_debug, "imxblittervideosink2", 0, "Freescale i.MX blitter sink base class");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_BASE_SINK_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	object_class->dispose          = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_dispose);
	object_class->set_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_set_property);
	object_class->get_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_get_property);
	element_class->change_state    = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_change_state);
	base_class->set_caps           = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_set_caps);
	base_class->event              = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_event);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_propose_allocation);
	parent_class->show_frame       = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_2_show_frame);

	klass->start          = NULL;
	klass->stop           = NULL;
	klass->create_blitter = NULL;

	g_object_class_install_property(
		object_class,
		PROP_FORCE_ASPECT_RATIO,
		g_param_spec_boolean(
			"force-aspect-ratio",
			"Force aspect ratio",
			"When enabled, scaling will respect original aspect ratio",
			DEFAULT_FORCE_ASPECT_RATIO,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FBDEV_NAME,
		g_param_spec_string(
			"framebuffer",
			"Framebuffer device name",
			"The device name of the framebuffer to render to",
			DEFAULT_FBDEV_NAME,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Output rotation in 90-degree steps",
			gst_imx_canvas_inner_rotation_get_type(),
			DEFAULT_OUTPUT_ROTATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_WINDOW_X_COORD,
		g_param_spec_int(
			"window-x-coord",
			"Window x coordinate",
			"X coordinate of the window's top left corner, in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_WINDOW_X_COORD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_WINDOW_Y_COORD,
		g_param_spec_int(
			"window-y-coord",
			"Window y coordinate",
			"Y coordinate of the window's top left corner, in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_WINDOW_Y_COORD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_WINDOW_WIDTH,
		g_param_spec_uint(
			"window-width",
			"Window width",
			"Window width, in pixels (0 = automatically set to the video input width)",
			0, G_MAXINT,
			DEFAULT_WINDOW_WIDTH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_WINDOW_HEIGHT,
		g_param_spec_uint(
			"window-height",
			"Window height",
			"Window height, in pixels (0 = automatically set to the video input height)",
			0, G_MAXINT,
			DEFAULT_WINDOW_HEIGHT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_LEFT_MARGIN,
		g_param_spec_uint(
			"left-margin",
			"Left margin",
			"Left margin",
			0, G_MAXUINT,
			DEFAULT_LEFT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_TOP_MARGIN,
		g_param_spec_uint(
			"top-margin",
			"Top margin",
			"Top margin",
			0, G_MAXUINT,
			DEFAULT_TOP_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_RIGHT_MARGIN,
		g_param_spec_uint(
			"right-margin",
			"Right margin",
			"Right margin",
			0, G_MAXUINT,
			DEFAULT_RIGHT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_BOTTOM_MARGIN,
		g_param_spec_uint(
			"bottom-margin",
			"Bottom margin",
			"Bottom margin",
			0, G_MAXUINT,
			DEFAULT_BOTTOM_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_blitter_video_sink_2_init(GstImxBlitterVideoSink2 *blitter_video_sink_2)
{
	blitter_video_sink_2->blitter = NULL;
	blitter_video_sink_2->framebuffer_name = g_strdup(DEFAULT_FBDEV_NAME);
	blitter_video_sink_2->framebuffer = NULL;
	blitter_video_sink_2->framebuffer_fd = -1;

	gst_video_info_init(&(blitter_video_sink_2->input_video_info));
	gst_video_info_init(&(blitter_video_sink_2->output_video_info));

	blitter_video_sink_2->is_paused = FALSE;

	memset(&(blitter_video_sink_2->canvas), 0, sizeof(GstImxCanvas));
	blitter_video_sink_2->canvas.keep_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
	blitter_video_sink_2->canvas_needs_update = TRUE;
	blitter_video_sink_2->canvas.fill_color = 0xFF000000;

	g_mutex_init(&(blitter_video_sink_2->mutex));
}


static void gst_imx_blitter_video_sink_2_dispose(GObject *object)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(object);

	if (blitter_video_sink_2->framebuffer_name != NULL)
	{
		g_free(blitter_video_sink_2->framebuffer_name);
		blitter_video_sink_2->framebuffer_name = NULL;
	}

	if (blitter_video_sink_2->framebuffer_fd != -1)
	{
		close(blitter_video_sink_2->framebuffer_fd);
		blitter_video_sink_2->framebuffer_fd = -1;
	}

	if (blitter_video_sink_2->blitter != NULL)
	{
		gst_object_unref(blitter_video_sink_2->blitter);
		blitter_video_sink_2->blitter = NULL;
	}

	g_mutex_clear(&(blitter_video_sink_2->mutex));

	G_OBJECT_CLASS(gst_imx_blitter_video_sink_2_parent_class)->dispose(object);
}


static void gst_imx_blitter_video_sink_2_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_FORCE_ASPECT_RATIO:
		{
			gboolean b = g_value_get_boolean(value);

			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			if (blitter_video_sink_2->canvas.keep_aspect_ratio != b)
			{
				blitter_video_sink_2->canvas_needs_update = TRUE;
				blitter_video_sink_2->canvas.keep_aspect_ratio = b;
			}
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_FBDEV_NAME:
		{
			gchar const *new_framebuffer_name = g_value_get_string(value);
			g_assert(new_framebuffer_name != NULL);

			/* Use mutex lock to ensure the Linux framebuffer switch doesn't
			 * interfere with any concurrent blitting operation */
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);

			g_free(blitter_video_sink_2->framebuffer_name);
			blitter_video_sink_2->framebuffer_name = g_strdup(new_framebuffer_name);

			if (!gst_imx_blitter_video_sink_2_open_framebuffer_device(blitter_video_sink_2))
				GST_ELEMENT_ERROR(blitter_video_sink_2, RESOURCE, OPEN_READ_WRITE, ("reopening framebuffer failed"), (NULL));

			blitter_video_sink_2->canvas_needs_update = TRUE;

			/* Done; now unlock */
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->canvas.inner_rotation = g_value_get_enum(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->window_x_coord = g_value_get_int(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->window_y_coord = g_value_get_int(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->window_width = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->window_height = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case PROP_LEFT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->canvas.margin_left = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_TOP_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->canvas.margin_top = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_RIGHT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->canvas.margin_right = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_BOTTOM_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->canvas.margin_bottom = g_value_get_uint(value);
			blitter_video_sink_2->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_blitter_video_sink_2_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_FORCE_ASPECT_RATIO:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_boolean(value, blitter_video_sink_2->canvas.keep_aspect_ratio);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_FBDEV_NAME:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_string(value, blitter_video_sink_2->framebuffer_name);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_enum(value, blitter_video_sink_2->canvas.inner_rotation);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_WINDOW_X_COORD:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_int(value, blitter_video_sink_2->window_x_coord);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_WINDOW_Y_COORD:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_int(value, blitter_video_sink_2->window_y_coord);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_WINDOW_WIDTH:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->window_width);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_WINDOW_HEIGHT:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->window_height);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_LEFT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->canvas.margin_left);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_TOP_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->canvas.margin_top);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_RIGHT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->canvas.margin_right);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case PROP_BOTTOM_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			g_value_set_uint(value, blitter_video_sink_2->canvas.margin_bottom);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_blitter_video_sink_2_change_state(GstElement *element, GstStateChange transition)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(element);
	GstImxBlitterVideoSink2Class *klass = GST_IMX_BLITTER_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(element));
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(blitter_video_sink_2 != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);

			if (!gst_imx_blitter_video_sink_2_open_framebuffer_device(blitter_video_sink_2))
			{
				GST_ERROR_OBJECT(blitter_video_sink_2, "opening framebuffer device failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
				return GST_STATE_CHANGE_FAILURE;
			}

			if ((klass->start != NULL) && !(klass->start(blitter_video_sink_2)))
			{
				GST_ERROR_OBJECT(blitter_video_sink_2, "start() failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
				return GST_STATE_CHANGE_FAILURE;
			}

			if (!gst_imx_blitter_video_sink_2_acquire_blitter(blitter_video_sink_2))
			{
				GST_ERROR_OBJECT(blitter_video_sink_2, "acquiring blitter failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
				return GST_STATE_CHANGE_FAILURE;
			}

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->is_paused = FALSE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_blitter_video_sink_2_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->is_paused = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			blitter_video_sink_2->is_paused = FALSE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
			break;

		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);

			if ((klass->stop != NULL) && !(klass->stop(blitter_video_sink_2)))
				GST_ERROR_OBJECT(blitter_video_sink_2, "stop() failed");

			if (blitter_video_sink_2->blitter != NULL)
			{
				gst_object_unref(blitter_video_sink_2->blitter);
				blitter_video_sink_2->blitter = NULL;
			}

			gst_imx_blitter_video_sink_2_close_framebuffer_device(blitter_video_sink_2);

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_blitter_video_sink_2_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	gboolean ret;
	GstVideoInfo video_info;
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(sink);

	g_assert(blitter_video_sink_2 != NULL);
	g_assert(blitter_video_sink_2->blitter != NULL);

	GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
	{
		GST_ERROR_OBJECT(blitter_video_sink_2, "could not set caps %" GST_PTR_FORMAT, (gpointer)caps);
		ret = FALSE;
	}
	else
	{
		blitter_video_sink_2->input_video_info = video_info;
		blitter_video_sink_2->canvas_needs_update = TRUE;

		if (blitter_video_sink_2->blitter != NULL)
			gst_imx_blitter_set_input_video_info(blitter_video_sink_2->blitter, &video_info);

		ret = TRUE;
	}

	GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

	return ret;
}


static gboolean gst_imx_blitter_video_sink_2_event(GstBaseSink *sink, GstEvent *event)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK(sink);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_STOP:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);
			if (blitter_video_sink_2->blitter != NULL)
				gst_imx_blitter_flush(blitter_video_sink_2->blitter);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

			break;
		}

		default:
			break;
	}

	return GST_BASE_SINK_CLASS(gst_imx_blitter_video_sink_2_parent_class)->event(sink, event);
}


static gboolean gst_imx_blitter_video_sink_2_propose_allocation(GstBaseSink *sink, GstQuery *query)
{
	GstCaps *caps;
	GstVideoInfo info;
	GstBufferPool *pool;
	guint size;

	gst_query_parse_allocation(query, &caps, NULL);

	if (caps == NULL)
	{
		GST_DEBUG_OBJECT(sink, "no caps specified");
		return FALSE;
	}

	if (!gst_video_info_from_caps(&info, caps))
		return FALSE;

	size = GST_VIDEO_INFO_SIZE(&info);

	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GstStructure *structure;
		GstAllocationParams params;
		GstAllocator *allocator = NULL;

		memset(&params, 0, sizeof(params));
		params.flags = 0;
		params.align = 15;
		params.prefix = 0;
		params.padding = 0;

		if (gst_query_get_n_allocation_params(query) > 0)
			gst_query_parse_nth_allocation_param(query, 0, &allocator, &params);
		else
			gst_query_add_allocation_param(query, allocator, &params);

		pool = gst_video_buffer_pool_new();

		structure = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(structure, caps, size, 0, 0);
		gst_buffer_pool_config_set_allocator(structure, allocator, &params);

		if (allocator)
			gst_object_unref(allocator);

		if (!gst_buffer_pool_set_config(pool, structure))
		{
			GST_ERROR_OBJECT(sink, "failed to set config");
			gst_object_unref(pool);
			return FALSE;
		}

		gst_query_add_allocation_pool(query, pool, size, 0, 0);
		gst_object_unref(pool);
		gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
	}

	return TRUE;
}


static GstFlowReturn gst_imx_blitter_video_sink_2_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstImxBlitterVideoSink2 *blitter_video_sink_2 = GST_IMX_BLITTER_VIDEO_SINK_CAST(video_sink);

	GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink_2);

	/* Update canvas and input region if necessary */
	if (blitter_video_sink_2->canvas_needs_update)
		gst_imx_blitter_video_sink_2_update_canvas(blitter_video_sink_2);

	if (blitter_video_sink_2->canvas.visibility_mask == 0)
	{
		GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);
		return GST_FLOW_OK;
	}

	gst_imx_blitter_set_input_frame(blitter_video_sink_2->blitter, buf);
	gst_imx_blitter_blit(blitter_video_sink_2->blitter, 255);

	GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink_2);

	return GST_FLOW_OK;
}


static gboolean gst_imx_blitter_video_sink_2_open_framebuffer_device(GstImxBlitterVideoSink2 *blitter_video_sink_2)
{
	/* must be called with lock held */

	int fd;
	guint fb_width, fb_height;
	GstVideoFormat fb_format;
	GstBuffer *buffer;
	GstImxPhysMemMeta *phys_mem_meta;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	g_assert(blitter_video_sink_2 != NULL);
	g_assert(blitter_video_sink_2->framebuffer_name != NULL);

	if (blitter_video_sink_2->framebuffer_fd != -1)
		gst_imx_blitter_video_sink_2_close_framebuffer_device(blitter_video_sink_2);

	GST_INFO_OBJECT(blitter_video_sink_2, "opening framebuffer %s", blitter_video_sink_2->framebuffer_name);

	fd = open(blitter_video_sink_2->framebuffer_name, O_RDWR, 0);
	if (fd < 0)
	{
		GST_ELEMENT_ERROR(blitter_video_sink_2, RESOURCE, OPEN_READ_WRITE, ("could not open %s: %s", blitter_video_sink_2->framebuffer_name, strerror(errno)), (NULL));
		return FALSE;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
	{
		close(fd);
		GST_ERROR_OBJECT(blitter_video_sink_2, "could not open get fixed screen info: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
	{
		close(fd);
		GST_ERROR_OBJECT(blitter_video_sink_2, "could not open get variable screen info: %s", strerror(errno));
		return FALSE;
	}

	blitter_video_sink_2->framebuffer_fd = fd;

	fb_width = fb_var.xres;
	fb_height = fb_var.yres;
	fb_format = gst_imx_blitter_video_sink_2_get_format_from_fb(blitter_video_sink_2, &fb_var, &fb_fix);

	GST_INFO_OBJECT(blitter_video_sink_2, "framebuffer resolution is %u x %u", fb_width, fb_height);

	buffer = gst_buffer_new();
	gst_buffer_add_video_meta(buffer, GST_VIDEO_FRAME_FLAG_NONE, fb_format, fb_width, fb_height);

	phys_mem_meta = GST_IMX_PHYS_MEM_META_ADD(buffer);
	phys_mem_meta->phys_addr = (gst_imx_phys_addr_t)(fb_fix.smem_start);

	blitter_video_sink_2->framebuffer = buffer;
	blitter_video_sink_2->framebuffer_fd = fd;
	blitter_video_sink_2->framebuffer_region.x1 = 0;
	blitter_video_sink_2->framebuffer_region.y1 = 0;
	blitter_video_sink_2->framebuffer_region.x2 = fb_width;
	blitter_video_sink_2->framebuffer_region.y2 = fb_height;
	GST_INFO_OBJECT(blitter_video_sink_2, "framebuffer FD is %d", blitter_video_sink_2->framebuffer_fd);

	gst_video_info_set_format(&(blitter_video_sink_2->output_video_info), fb_format, fb_width, fb_height);

	blitter_video_sink_2->canvas_needs_update = TRUE;

	if (blitter_video_sink_2->blitter != NULL)
	{
		gst_imx_blitter_set_output_video_info(blitter_video_sink_2->blitter, &(blitter_video_sink_2->output_video_info));
		gst_imx_blitter_set_output_frame(blitter_video_sink_2->blitter, blitter_video_sink_2->framebuffer);
	}

	return TRUE;
}


static void gst_imx_blitter_video_sink_2_close_framebuffer_device(GstImxBlitterVideoSink2 *blitter_video_sink_2)
{
	/* must be called with lock held */

	g_assert(blitter_video_sink_2 != NULL);

	if (blitter_video_sink_2->framebuffer_fd == -1)
		return;

	GST_INFO_OBJECT(blitter_video_sink_2, "closing framebuffer %s with FD %d", blitter_video_sink_2->framebuffer_name, blitter_video_sink_2->framebuffer_fd);

	if (blitter_video_sink_2->blitter != NULL)
		gst_imx_blitter_flush(blitter_video_sink_2->blitter);

	gst_buffer_unref(blitter_video_sink_2->framebuffer);
	close(blitter_video_sink_2->framebuffer_fd);
	blitter_video_sink_2->framebuffer = NULL;
	blitter_video_sink_2->framebuffer_fd = -1;
}


static GstVideoFormat gst_imx_blitter_video_sink_2_get_format_from_fb(GstImxBlitterVideoSink2 *blitter_video_sink_2, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
{
	GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;
	guint rlen = fb_var->red.length, glen = fb_var->green.length, blen = fb_var->blue.length, alen = fb_var->transp.length;
	guint rofs = fb_var->red.offset, gofs = fb_var->green.offset, bofs = fb_var->blue.offset, aofs = fb_var->transp.offset;

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		GST_DEBUG_OBJECT(blitter_video_sink_2, "unknown framebuffer type %d", fb_fix->type);
		return fmt;
	}

	switch (fb_var->bits_per_pixel)
	{
		case 15:
		{
			if ((rlen == 5) && (glen == 5) && (blen == 5))
				fmt = GST_VIDEO_FORMAT_RGB15;
			break;
		}
		case 16:
		{
			if ((rlen == 5) && (glen == 6) && (blen == 5))
				fmt = GST_VIDEO_FORMAT_RGB16;
			break;
		}
		case 24:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16))
					fmt = GST_VIDEO_FORMAT_RGB;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0))
					fmt = GST_VIDEO_FORMAT_BGR;
				else if ((rofs == 16) && (gofs == 0) && (bofs == 8))
					fmt = GST_VIDEO_FORMAT_GBR;
			}
			break;
		}
		case 32:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8) && (alen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16) && (aofs == 24))
					fmt = GST_VIDEO_FORMAT_RGBA;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0) && (aofs == 24))
					fmt = GST_VIDEO_FORMAT_BGRA;
				else if ((rofs == 24) && (gofs == 16) && (bofs == 8) && (aofs == 0))
					fmt = GST_VIDEO_FORMAT_ABGR;
			}
			break;
		}
		default:
			break;
	}

	GST_INFO_OBJECT(
		blitter_video_sink_2,
		"framebuffer uses %u bpp (sizes: r %u g %u b %u  offsets: r %u g %u b %u) => format %s",
		fb_var->bits_per_pixel,
		rlen, glen, blen,
		rofs, gofs, bofs,
		gst_video_format_to_string(fmt)
	);

	return fmt;
}


static void gst_imx_blitter_video_sink_2_update_canvas(GstImxBlitterVideoSink2 *blitter_video_sink_2)
{
	/* must be called with lock held */

	GstImxRegion *outer_region = &(blitter_video_sink_2->canvas.outer_region);
	GstImxRegion source_subset;

	/* Define the outer region */
	if ((blitter_video_sink_2->window_width == 0) || (blitter_video_sink_2->window_height == 0))
	{
		/* If either window_width or window_height is 0, then just use the
		 * entire framebuffer as the outer region */
		*outer_region = blitter_video_sink_2->framebuffer_region;
	}
	else
	{
		/* Use the defined window as the outer region */
		outer_region->x1 = blitter_video_sink_2->window_x_coord;
		outer_region->y1 = blitter_video_sink_2->window_y_coord;
		outer_region->x2 = blitter_video_sink_2->window_x_coord + blitter_video_sink_2->window_width;
		outer_region->y2 = blitter_video_sink_2->window_y_coord + blitter_video_sink_2->window_height;
	}

	gst_imx_canvas_calculate_inner_region(&(blitter_video_sink_2->canvas), &(blitter_video_sink_2->input_video_info));
	gst_imx_canvas_clip(
		&(blitter_video_sink_2->canvas),
		&(blitter_video_sink_2->framebuffer_region),
		&(blitter_video_sink_2->input_video_info),
		&source_subset
	);

	gst_imx_blitter_set_input_region(blitter_video_sink_2->blitter, &source_subset);
	gst_imx_blitter_set_output_canvas(blitter_video_sink_2->blitter, &(blitter_video_sink_2->canvas));

	blitter_video_sink_2->canvas_needs_update = FALSE;
}


static gboolean gst_imx_blitter_video_sink_2_acquire_blitter(GstImxBlitterVideoSink2 *blitter_video_sink_2)
{
	/* must be called with lock held */

	GstImxBlitterVideoSink2Class *klass = GST_IMX_BLITTER_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(blitter_video_sink_2));

	g_assert(blitter_video_sink_2 != NULL);
	g_assert(blitter_video_sink_2->framebuffer != NULL);
	g_assert(klass->create_blitter != NULL);

	/* Do nothing if the blitter is already acquired */
	if (blitter_video_sink_2->blitter != NULL)
		return TRUE;

	if ((blitter_video_sink_2->blitter = klass->create_blitter(blitter_video_sink_2)) == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_sink_2, "could not acquire blitter");
		return FALSE;
	}

	return gst_imx_blitter_set_output_frame(blitter_video_sink_2->blitter, blitter_video_sink_2->framebuffer) &&
	       gst_imx_blitter_set_output_canvas(blitter_video_sink_2->blitter, &(blitter_video_sink_2->canvas)) &&
	       gst_imx_blitter_set_output_video_info(blitter_video_sink_2->blitter, &(blitter_video_sink_2->output_video_info));
}
