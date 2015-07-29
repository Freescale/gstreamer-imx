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




GST_DEBUG_CATEGORY_STATIC(imx_blitter_video_sink_debug);
#define GST_CAT_DEFAULT imx_blitter_video_sink_debug


enum
{
	PROP_0,
	PROP_FORCE_ASPECT_RATIO,
	PROP_FBDEV_NAME,
	PROP_USE_VSYNC,
	PROP_INPUT_CROP,
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
#define DEFAULT_USE_VSYNC FALSE
#define DEFAULT_INPUT_CROP TRUE
#define DEFAULT_OUTPUT_ROTATION GST_IMX_CANVAS_INNER_ROTATION_NONE
#define DEFAULT_WINDOW_X_COORD 0
#define DEFAULT_WINDOW_Y_COORD 0
#define DEFAULT_WINDOW_WIDTH 0
#define DEFAULT_WINDOW_HEIGHT 0
#define DEFAULT_LEFT_MARGIN 0
#define DEFAULT_TOP_MARGIN 0
#define DEFAULT_RIGHT_MARGIN 0
#define DEFAULT_BOTTOM_MARGIN 0


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterVideoSink, gst_imx_blitter_video_sink, GST_TYPE_VIDEO_SINK)


static void gst_imx_blitter_video_sink_dispose(GObject *object);
static void gst_imx_blitter_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_blitter_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_blitter_video_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_blitter_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_blitter_video_sink_event(GstBaseSink *sink, GstEvent *event);
static gboolean gst_imx_blitter_video_sink_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);

static gboolean gst_imx_blitter_video_sink_open_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink);
static void gst_imx_blitter_video_sink_close_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink);
static void gst_imx_blitter_video_sink_select_fb_page(GstImxBlitterVideoSink *blitter_video_sink, guint page);
static void gst_imx_blitter_video_sink_flip_to_selected_fb_page(GstImxBlitterVideoSink *blitter_video_sink);
static gboolean gst_imx_blitter_video_sink_set_virtual_fb_height(GstImxBlitterVideoSink *blitter_video_sink, guint32 virtual_fb_height);
static gboolean gst_imx_blitter_video_sink_reconfigure_fb(GstImxBlitterVideoSink *blitter_video_sink, guint num_pages);
static gboolean gst_imx_blitter_video_sink_restore_original_fb_config(GstImxBlitterVideoSink *blitter_video_sink);
static GstVideoFormat gst_imx_blitter_video_sink_get_format_from_fb(GstImxBlitterVideoSink *blitter_video_sink, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);

static void gst_imx_blitter_video_sink_update_canvas(GstImxBlitterVideoSink *blitter_video_sink, GstImxRegion const *source_region);
static gboolean gst_imx_blitter_video_sink_acquire_blitter(GstImxBlitterVideoSink *blitter_video_sink);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

static void gst_imx_blitter_video_sink_class_init(GstImxBlitterVideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_video_sink_debug, "imxblittervideosink", 0, "Freescale i.MX blitter sink base class");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_BASE_SINK_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	object_class->dispose          = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_dispose);
	object_class->set_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_set_property);
	object_class->get_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_get_property);
	element_class->change_state    = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_change_state);
	base_class->set_caps           = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_set_caps);
	base_class->event              = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_event);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_propose_allocation);
	parent_class->show_frame       = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_show_frame);

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
		PROP_USE_VSYNC,
		g_param_spec_boolean(
			"use-vsync",
			"Use VSync",
			"Enable and use verticeal synchronization to eliminate tearing",
			DEFAULT_USE_VSYNC,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_INPUT_CROP,
		g_param_spec_boolean(
			"input-crop",
			"Input crop",
			"Whether or not to crop input frames based on their video crop metadata",
			DEFAULT_INPUT_CROP,
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


static void gst_imx_blitter_video_sink_init(GstImxBlitterVideoSink *blitter_video_sink)
{
	blitter_video_sink->blitter = NULL;
	blitter_video_sink->framebuffer_name = g_strdup(DEFAULT_FBDEV_NAME);
	blitter_video_sink->framebuffer = NULL;
	blitter_video_sink->framebuffer_fd = -1;
	blitter_video_sink->current_fb_page = 0;
	blitter_video_sink->use_vsync = DEFAULT_USE_VSYNC;
	blitter_video_sink->input_crop = DEFAULT_INPUT_CROP;
	blitter_video_sink->last_frame_with_cropdata = FALSE;

	gst_video_info_init(&(blitter_video_sink->input_video_info));
	gst_video_info_init(&(blitter_video_sink->output_video_info));

	blitter_video_sink->is_paused = FALSE;

	memset(&(blitter_video_sink->canvas), 0, sizeof(GstImxCanvas));
	blitter_video_sink->canvas.keep_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
	blitter_video_sink->canvas_needs_update = TRUE;
	blitter_video_sink->canvas.fill_color = 0xFF000000;

	g_mutex_init(&(blitter_video_sink->mutex));
}


static void gst_imx_blitter_video_sink_dispose(GObject *object)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(object);

	if (blitter_video_sink->framebuffer_name != NULL)
	{
		g_free(blitter_video_sink->framebuffer_name);
		blitter_video_sink->framebuffer_name = NULL;
	}

	if (blitter_video_sink->blitter != NULL)
	{
		gst_object_unref(blitter_video_sink->blitter);
		blitter_video_sink->blitter = NULL;
	}

	g_mutex_clear(&(blitter_video_sink->mutex));

	G_OBJECT_CLASS(gst_imx_blitter_video_sink_parent_class)->dispose(object);
}


static void gst_imx_blitter_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_FORCE_ASPECT_RATIO:
		{
			gboolean b = g_value_get_boolean(value);

			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			if (blitter_video_sink->canvas.keep_aspect_ratio != b)
			{
				blitter_video_sink->canvas_needs_update = TRUE;
				blitter_video_sink->canvas.keep_aspect_ratio = b;
			}
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_FBDEV_NAME:
		{
			gchar const *new_framebuffer_name = g_value_get_string(value);
			g_assert(new_framebuffer_name != NULL);

			/* Use mutex lock to ensure the Linux framebuffer switch doesn't
			 * interfere with any concurrent blitting operation */
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

			g_free(blitter_video_sink->framebuffer_name);
			blitter_video_sink->framebuffer_name = g_strdup(new_framebuffer_name);

			/* If the framebuffer is already open, reopen it to make use
			 * of the new device name */
			if (blitter_video_sink->framebuffer_fd != -1)
			{
				if (!gst_imx_blitter_video_sink_open_framebuffer_device(blitter_video_sink))
					GST_ELEMENT_ERROR(blitter_video_sink, RESOURCE, OPEN_READ_WRITE, ("reopening framebuffer failed"), (NULL));

				blitter_video_sink->canvas_needs_update = TRUE;
			}

			/* Done; now unlock */
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_USE_VSYNC:
		{
			gboolean b = g_value_get_boolean(value);

			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->use_vsync = b;
			if (!b && (blitter_video_sink->blitter != NULL))
			{
				blitter_video_sink->current_fb_page = 0;
				gst_imx_blitter_video_sink_select_fb_page(blitter_video_sink, 0);
				gst_imx_blitter_video_sink_flip_to_selected_fb_page(blitter_video_sink);
				gst_imx_blitter_set_num_output_pages(blitter_video_sink->blitter, b ? 2 : 1);
			}
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_INPUT_CROP:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->input_crop = g_value_get_boolean(value);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->canvas.inner_rotation = g_value_get_enum(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_x_coord = g_value_get_int(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_y_coord = g_value_get_int(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_width = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_height = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;
		}

		case PROP_LEFT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->canvas.margin_left = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_TOP_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->canvas.margin_top = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_RIGHT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->canvas.margin_right = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_BOTTOM_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->canvas.margin_bottom = g_value_get_uint(value);
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_blitter_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_FORCE_ASPECT_RATIO:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_boolean(value, blitter_video_sink->canvas.keep_aspect_ratio);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_FBDEV_NAME:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_string(value, blitter_video_sink->framebuffer_name);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_USE_VSYNC:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_boolean(value, blitter_video_sink->use_vsync);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_INPUT_CROP:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_boolean(value, blitter_video_sink->input_crop);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_enum(value, blitter_video_sink->canvas.inner_rotation);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_WINDOW_X_COORD:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_int(value, blitter_video_sink->window_x_coord);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_WINDOW_Y_COORD:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_int(value, blitter_video_sink->window_y_coord);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_WINDOW_WIDTH:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->window_width);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_WINDOW_HEIGHT:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->window_height);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_LEFT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->canvas.margin_left);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_TOP_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->canvas.margin_top);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_RIGHT_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->canvas.margin_right);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_BOTTOM_MARGIN:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_uint(value, blitter_video_sink->canvas.margin_bottom);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_blitter_video_sink_change_state(GstElement *element, GstStateChange transition)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(element);
	GstImxBlitterVideoSinkClass *klass = GST_IMX_BLITTER_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(element));
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(blitter_video_sink != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

			if (!gst_imx_blitter_video_sink_open_framebuffer_device(blitter_video_sink))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "opening framebuffer device failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
				return GST_STATE_CHANGE_FAILURE;
			}

			if ((klass->start != NULL) && !(klass->start(blitter_video_sink)))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "start() failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
				return GST_STATE_CHANGE_FAILURE;
			}

			if (!gst_imx_blitter_video_sink_acquire_blitter(blitter_video_sink))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "acquiring blitter failed");
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
				return GST_STATE_CHANGE_FAILURE;
			}

			/* switch to the first page in case only one is present
			 * this is done in case the framebuffer y offset was
			 * tampered with earlier, for example by running
			 * imxeglvivsink with FB_MULTI_BUFFER=2 */
			GST_DEBUG_OBJECT(blitter_video_sink, "cleaning up any previous vsync by flipping to page 0");
			gst_imx_blitter_video_sink_select_fb_page(blitter_video_sink, 0);
			gst_imx_blitter_video_sink_flip_to_selected_fb_page(blitter_video_sink);

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->is_paused = FALSE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_blitter_video_sink_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->is_paused = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->is_paused = FALSE;
			blitter_video_sink->last_frame_with_cropdata = FALSE;
			blitter_video_sink->canvas_needs_update = TRUE;
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

			if ((klass->stop != NULL) && !(klass->stop(blitter_video_sink)))
				GST_ERROR_OBJECT(blitter_video_sink, "stop() failed");

			gst_imx_blitter_video_sink_close_framebuffer_device(blitter_video_sink);

			if (blitter_video_sink->blitter != NULL)
			{
				gst_object_unref(blitter_video_sink->blitter);
				blitter_video_sink->blitter = NULL;
			}

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_blitter_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	gboolean ret;
	GstVideoInfo video_info;
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(sink);

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->blitter != NULL);

	GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not set caps %" GST_PTR_FORMAT, (gpointer)caps);
		ret = FALSE;
	}
	else
	{
		blitter_video_sink->input_video_info = video_info;
		blitter_video_sink->canvas_needs_update = TRUE;

		if (blitter_video_sink->blitter != NULL)
			gst_imx_blitter_set_input_video_info(blitter_video_sink->blitter, &video_info);

		ret = TRUE;
	}

	GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

	return ret;
}


static gboolean gst_imx_blitter_video_sink_event(GstBaseSink *sink, GstEvent *event)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(sink);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_STOP:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			if (blitter_video_sink->blitter != NULL)
				gst_imx_blitter_flush(blitter_video_sink->blitter);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		default:
			break;
	}

	return GST_BASE_SINK_CLASS(gst_imx_blitter_video_sink_parent_class)->event(sink, event);
}


static gboolean gst_imx_blitter_video_sink_propose_allocation(GstBaseSink *sink, GstQuery *query)
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


static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK_CAST(video_sink);
	GstVideoCropMeta *video_crop_meta;

	GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

	if (blitter_video_sink->input_crop && ((video_crop_meta = gst_buffer_get_video_crop_meta(buf)) != NULL))
	{
		/* Crop metadata present. Reconfigure canvas. */

		GstImxRegion source_region;
		source_region.x1 = video_crop_meta->x;
		source_region.y1 = video_crop_meta->y;
		source_region.x2 = video_crop_meta->x + video_crop_meta->width;
		source_region.y2 = video_crop_meta->y + video_crop_meta->height;

		/* Make sure the source region does not exceed valid bounds */
		source_region.x1 = MAX(0, source_region.x1);
		source_region.y1 = MAX(0, source_region.y1);
		source_region.x2 = MIN(GST_VIDEO_INFO_WIDTH(&(blitter_video_sink->input_video_info)), source_region.x2);
		source_region.y2 = MIN(GST_VIDEO_INFO_HEIGHT(&(blitter_video_sink->input_video_info)), source_region.y2);

		GST_LOG_OBJECT(blitter_video_sink, "retrieved crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));

		/* Canvas needs to be updated if either one of these applies:
		 * - the current frame has crop metadata, the last one didn't
		 * - the new crop rectangle and the last are different */
		if (!(blitter_video_sink->last_frame_with_cropdata) || !gst_imx_region_equal(&source_region, &(blitter_video_sink->last_source_region)))
		{
			GST_LOG_OBJECT(blitter_video_sink, "using new crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));
			blitter_video_sink->last_source_region = source_region;
			blitter_video_sink->canvas_needs_update = TRUE;
		}

		blitter_video_sink->last_frame_with_cropdata = TRUE;

		/* Update canvas and input region if necessary */
		if (blitter_video_sink->canvas_needs_update)
			gst_imx_blitter_video_sink_update_canvas(blitter_video_sink, &(blitter_video_sink->last_source_region));
	}
	else
	{
		/* Force an update if this frame has no crop metadata but the last one did */
		if (blitter_video_sink->last_frame_with_cropdata)
			blitter_video_sink->canvas_needs_update = TRUE;
		blitter_video_sink->last_frame_with_cropdata = FALSE;

		/* Update canvas and input region if necessary */
		if (blitter_video_sink->canvas_needs_update)
			gst_imx_blitter_video_sink_update_canvas(blitter_video_sink, NULL);
	}

	if (blitter_video_sink->canvas.visibility_mask == 0)
	{
		/* Visibility mask 0 -> nothing to blit */
		GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
		return GST_FLOW_OK;
	}

	gst_imx_blitter_set_input_frame(blitter_video_sink->blitter, buf);

	/* If using vsync, blit to the backbuffer, and flip
	 * The flipping is done by scrolling in Y direction
	 * by the same number of rows as there are on screen
	 * The scrolling is implicitely vsync'ed */
	if (blitter_video_sink->use_vsync)
	{
		/* Select which page to write/blit to */
		gst_imx_blitter_video_sink_select_fb_page(blitter_video_sink, blitter_video_sink->current_fb_page);

		/* The actual blitting */
		gst_imx_blitter_blit(blitter_video_sink->blitter, 255);

		/* Flip pages now */
		gst_imx_blitter_video_sink_flip_to_selected_fb_page(blitter_video_sink);

		blitter_video_sink->current_fb_page = 1 - blitter_video_sink->current_fb_page;
	}
	else
	{
		gst_imx_blitter_blit(blitter_video_sink->blitter, 255);
	}

	GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

	return GST_FLOW_OK;
}


static gboolean gst_imx_blitter_video_sink_open_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink)
{
	/* must be called with lock held */

	int fd;
	guint fb_width, fb_height;
	GstVideoFormat fb_format;
	GstBuffer *buffer;
	GstImxPhysMemMeta *phys_mem_meta;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->framebuffer_name != NULL);


	/* Close any currently open framebuffer first */
	if (blitter_video_sink->framebuffer_fd != -1)
		gst_imx_blitter_video_sink_close_framebuffer_device(blitter_video_sink);

	GST_INFO_OBJECT(blitter_video_sink, "opening framebuffer %s", blitter_video_sink->framebuffer_name);


	/* Open framebuffer and get its variable and fixed information */

	fd = open(blitter_video_sink->framebuffer_name, O_RDWR, 0);
	if (fd < 0)
	{
		GST_ELEMENT_ERROR(blitter_video_sink, RESOURCE, OPEN_READ_WRITE, ("could not open %s: %s", blitter_video_sink->framebuffer_name, strerror(errno)), (NULL));
		return FALSE;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
	{
		close(fd);
		GST_ERROR_OBJECT(blitter_video_sink, "could not open get fixed screen info: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
	{
		close(fd);
		GST_ERROR_OBJECT(blitter_video_sink, "could not open get variable screen info: %s", strerror(errno));
		return FALSE;
	}


	/* Copy FD, variable and fixed screen information structs
	 * These are also needed during the vsync setup below*/
	blitter_video_sink->framebuffer_fd = fd;
	blitter_video_sink->fb_var = fb_var;
	blitter_video_sink->fb_fix = fb_fix;


	/* Set up vsync (vsync is done via page flipping) */
	if (blitter_video_sink->use_vsync)
	{
		/* Check how many pages can currently be used. If this number is
		 * less than 2, reconfigure the framebuffer to allow for 2 pages. */

		guint cur_num_pages = fb_var.yres_virtual / fb_var.yres;
		if (cur_num_pages < 2)
		{
			GST_INFO_OBJECT(
				blitter_video_sink,
				"framebuffer configuration:  resolution is %u x %u , virtual %u x %u => need to reconfigure virtual height",
				fb_var.xres, fb_var.yres,
				fb_var.xres_virtual, fb_var.yres_virtual
			);
			if (!gst_imx_blitter_video_sink_reconfigure_fb(blitter_video_sink, 2))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "could not reconfigure framebuffer");
				close(fd);
				blitter_video_sink->framebuffer_fd = -1;
				return FALSE;
			}
		}
		else
		{
			GST_INFO_OBJECT(
				blitter_video_sink,
				"framebuffer configuration:  resolution is %u x %u , virtual %u x %u => don't need to reconfigure virtual height",
				fb_var.xres, fb_var.yres,
				fb_var.xres_virtual, fb_var.yres_virtual
			);
		}

		/* Fetch fixed screen info again in case it changed after the FB reconfiguration */
		if (ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
		{
			GST_ERROR_OBJECT(blitter_video_sink, "could not open get fixed screen info: %s", strerror(errno));
			close(fd);
			blitter_video_sink->framebuffer_fd = -1;
			return FALSE;
		}

		/* Update the fixed screen info copy */
		blitter_video_sink->fb_fix = fb_fix;
	}


	/* Get width, height, format for framebuffer */
	fb_width = fb_var.xres;
	fb_height = fb_var.yres;
	fb_format = gst_imx_blitter_video_sink_get_format_from_fb(blitter_video_sink, &fb_var, &fb_fix);


	/* Construct framebuffer and add meta to it
	 * Note: not adding any GstMemory blocks, since none are needed */
	buffer = gst_buffer_new();
	gst_buffer_add_video_meta(buffer, GST_VIDEO_FRAME_FLAG_NONE, fb_format, fb_width, fb_height);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_ADD(buffer);
	phys_mem_meta->phys_addr = (gst_imx_phys_addr_t)(fb_fix.smem_start);


	/* Set up framebuffer related information */
	blitter_video_sink->framebuffer = buffer;
	blitter_video_sink->framebuffer_fd = fd;
	blitter_video_sink->framebuffer_region.x1 = 0;
	blitter_video_sink->framebuffer_region.y1 = 0;
	blitter_video_sink->framebuffer_region.x2 = fb_width;
	blitter_video_sink->framebuffer_region.y2 = fb_height;
	GST_INFO_OBJECT(blitter_video_sink, "framebuffer FD is %d", blitter_video_sink->framebuffer_fd);


	/* Create videoinfo structure for the framebuffer */
	gst_video_info_set_format(&(blitter_video_sink->output_video_info), fb_format, fb_width, fb_height);


	/* New framebuffer means the canvas most likely changed -> update */
	blitter_video_sink->canvas_needs_update = TRUE;


	/* If a blitter is present, set its output video info
	 * and output frame, since these two items changed */
	if (blitter_video_sink->blitter != NULL)
	{
		gst_imx_blitter_set_output_video_info(blitter_video_sink->blitter, &(blitter_video_sink->output_video_info));
		gst_imx_blitter_set_output_frame(blitter_video_sink->blitter, blitter_video_sink->framebuffer);
		gst_imx_blitter_set_num_output_pages(blitter_video_sink->blitter, blitter_video_sink->use_vsync ? 2 : 1);
	}


	return TRUE;
}


static void gst_imx_blitter_video_sink_close_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink)
{
	/* must be called with lock held */

	g_assert(blitter_video_sink != NULL);

	if (blitter_video_sink->framebuffer_fd == -1)
		return;

	GST_INFO_OBJECT(blitter_video_sink, "closing framebuffer %s with FD %d", blitter_video_sink->framebuffer_name, blitter_video_sink->framebuffer_fd);

	if (blitter_video_sink->blitter != NULL)
	{
		/* switch back to the first page if vsync was used */
		if (blitter_video_sink->use_vsync)
		{
			GST_DEBUG_OBJECT(blitter_video_sink, "cleaning up page flipping by flipping to page 0");
			gst_imx_blitter_video_sink_select_fb_page(blitter_video_sink, 0);
			gst_imx_blitter_video_sink_flip_to_selected_fb_page(blitter_video_sink);

			if (blitter_video_sink->original_fb_virt_height != 0)
				gst_imx_blitter_video_sink_restore_original_fb_config(blitter_video_sink);
		}

		gst_imx_blitter_flush(blitter_video_sink->blitter);
	}

	gst_buffer_unref(blitter_video_sink->framebuffer);
	close(blitter_video_sink->framebuffer_fd);
	blitter_video_sink->framebuffer = NULL;
	blitter_video_sink->framebuffer_fd = -1;
}


static void gst_imx_blitter_video_sink_select_fb_page(GstImxBlitterVideoSink *blitter_video_sink, guint page)
{
	GstImxPhysMemMeta *phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(blitter_video_sink->framebuffer);
	guint height = GST_VIDEO_INFO_HEIGHT(&(blitter_video_sink->output_video_info));
	guint page_size = GST_VIDEO_INFO_PLANE_STRIDE(&(blitter_video_sink->output_video_info), 0) * height;

	GST_LOG_OBJECT(blitter_video_sink, "switching to page %u", page);

	phys_mem_meta->phys_addr = (gst_imx_phys_addr_t)(blitter_video_sink->fb_fix.smem_start) + page_size * page;
	blitter_video_sink->fb_var.yoffset = height * page;

	gst_imx_blitter_set_output_frame(blitter_video_sink->blitter, blitter_video_sink->framebuffer);
}


static void gst_imx_blitter_video_sink_flip_to_selected_fb_page(GstImxBlitterVideoSink *blitter_video_sink)
{
	if (ioctl(blitter_video_sink->framebuffer_fd, FBIOPAN_DISPLAY, &(blitter_video_sink->fb_var)) == -1)
		GST_ERROR_OBJECT(blitter_video_sink, "FBIOPAN_DISPLAY error: %s", strerror(errno));
}


static gboolean gst_imx_blitter_video_sink_set_virtual_fb_height(GstImxBlitterVideoSink *blitter_video_sink, guint32 virtual_fb_height)
{
	blitter_video_sink->fb_var.yres_virtual = virtual_fb_height;

	if (ioctl(blitter_video_sink->framebuffer_fd, FBIOPUT_VSCREENINFO, &(blitter_video_sink->fb_var)) == -1)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not set variable screen info with updated virtual y resolution: %s", strerror(errno));
		return FALSE;
	}
	else
		return TRUE;
}


static gboolean gst_imx_blitter_video_sink_reconfigure_fb(GstImxBlitterVideoSink *blitter_video_sink, guint num_pages)
{
	/* Make room for an integer number of pages by adjusting the virtual height */
	guint new_virtual_height = blitter_video_sink->fb_var.yres * num_pages;

	/* Save the original virtual height to be able to restore it later */
	blitter_video_sink->original_fb_virt_height = blitter_video_sink->fb_var.yres_virtual;

	GST_INFO_OBJECT(blitter_video_sink, "setting new configuration: original virtual height: %u new virtual height: %u", blitter_video_sink->original_fb_virt_height, new_virtual_height);

	return gst_imx_blitter_video_sink_set_virtual_fb_height(blitter_video_sink, new_virtual_height);
}


static gboolean gst_imx_blitter_video_sink_restore_original_fb_config(GstImxBlitterVideoSink *blitter_video_sink)
{
	gboolean ret;

	GST_INFO_OBJECT(blitter_video_sink, "restoring configuration: virtual height %u", blitter_video_sink->original_fb_virt_height);

	ret = gst_imx_blitter_video_sink_set_virtual_fb_height(blitter_video_sink, blitter_video_sink->original_fb_virt_height);
	/* After restoring, reset original_fb_virt_height to 0, indicating
	 * that there is no longer any original virtual height to restore */
	blitter_video_sink->original_fb_virt_height = 0;

	return ret;
}


static GstVideoFormat gst_imx_blitter_video_sink_get_format_from_fb(GstImxBlitterVideoSink *blitter_video_sink, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
{
	GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;
	guint rlen = fb_var->red.length, glen = fb_var->green.length, blen = fb_var->blue.length, alen = fb_var->transp.length;
	guint rofs = fb_var->red.offset, gofs = fb_var->green.offset, bofs = fb_var->blue.offset, aofs = fb_var->transp.offset;

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		GST_DEBUG_OBJECT(blitter_video_sink, "unknown framebuffer type %d", fb_fix->type);
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
		blitter_video_sink,
		"framebuffer uses %u bpp (sizes: r %u g %u b %u  offsets: r %u g %u b %u) => format %s",
		fb_var->bits_per_pixel,
		rlen, glen, blen,
		rofs, gofs, bofs,
		gst_video_format_to_string(fmt)
	);

	return fmt;
}


static void gst_imx_blitter_video_sink_update_canvas(GstImxBlitterVideoSink *blitter_video_sink, GstImxRegion const *source_region)
{
	/* must be called with lock held */

	GstImxRegion *outer_region = &(blitter_video_sink->canvas.outer_region);
	GstImxRegion source_subset;

	/* Define the outer region */
	if ((blitter_video_sink->window_width == 0) || (blitter_video_sink->window_height == 0))
	{
		/* If either window_width or window_height is 0, then just use the
		 * entire framebuffer as the outer region */
		*outer_region = blitter_video_sink->framebuffer_region;
	}
	else
	{
		/* Use the defined window as the outer region */
		outer_region->x1 = blitter_video_sink->window_x_coord;
		outer_region->y1 = blitter_video_sink->window_y_coord;
		outer_region->x2 = blitter_video_sink->window_x_coord + blitter_video_sink->window_width;
		outer_region->y2 = blitter_video_sink->window_y_coord + blitter_video_sink->window_height;
	}

	gst_imx_canvas_calculate_inner_region(&(blitter_video_sink->canvas), &(blitter_video_sink->input_video_info));
	gst_imx_canvas_clip(
		&(blitter_video_sink->canvas),
		&(blitter_video_sink->framebuffer_region),
		&(blitter_video_sink->input_video_info),
		source_region,
		&source_subset
	);

	gst_imx_blitter_set_input_region(blitter_video_sink->blitter, &source_subset);
	gst_imx_blitter_set_output_canvas(blitter_video_sink->blitter, &(blitter_video_sink->canvas));

	blitter_video_sink->canvas_needs_update = FALSE;
}


static gboolean gst_imx_blitter_video_sink_acquire_blitter(GstImxBlitterVideoSink *blitter_video_sink)
{
	/* must be called with lock held */

	GstImxBlitterVideoSinkClass *klass = GST_IMX_BLITTER_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(blitter_video_sink));

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->framebuffer != NULL);
	g_assert(klass->create_blitter != NULL);

	/* Do nothing if the blitter is already acquired */
	if (blitter_video_sink->blitter != NULL)
		return TRUE;

	if ((blitter_video_sink->blitter = klass->create_blitter(blitter_video_sink)) == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not acquire blitter");
		return FALSE;
	}

	return gst_imx_blitter_set_output_frame(blitter_video_sink->blitter, blitter_video_sink->framebuffer) &&
	       gst_imx_blitter_set_output_canvas(blitter_video_sink->blitter, &(blitter_video_sink->canvas)) &&
	       gst_imx_blitter_set_output_video_info(blitter_video_sink->blitter, &(blitter_video_sink->output_video_info)) &&
	       gst_imx_blitter_set_num_output_pages(blitter_video_sink->blitter, blitter_video_sink->use_vsync ? 2 : 1);
}
