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
#include "blitter_video_sink.h"




GST_DEBUG_CATEGORY_STATIC(imx_blitter_video_sink_debug);
#define GST_CAT_DEFAULT imx_blitter_video_sink_debug


enum
{
	PROP_0,
	PROP_FORCE_ASPECT_RATIO,
	PROP_FBDEV_NAME,
	PROP_INPUT_CROP,
	PROP_WINDOW_X_COORD,
	PROP_WINDOW_Y_COORD,
	PROP_WINDOW_WIDTH,
	PROP_WINDOW_HEIGHT
};

#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_FBDEV_NAME "/dev/fb0"
#define DEFAULT_WINDOW_X_COORD 0
#define DEFAULT_WINDOW_Y_COORD 0
#define DEFAULT_WINDOW_WIDTH 0
#define DEFAULT_WINDOW_HEIGHT 0


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterVideoSink, gst_imx_blitter_video_sink, GST_TYPE_VIDEO_SINK)


static void gst_imx_blitter_video_sink_finalize(GObject *object);
static void gst_imx_blitter_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_blitter_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_blitter_video_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_blitter_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_blitter_video_sink_event(GstBaseSink *sink, GstEvent *event);
static gboolean gst_imx_blitter_video_sink_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);

static GstVideoFormat gst_imx_blitter_video_sink_get_format_from_fb(GstImxBlitterVideoSink *blitter_video_sink, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);
static gboolean gst_imx_blitter_video_sink_open_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink);
static void gst_imx_blitter_video_sink_close_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink);
static gboolean gst_imx_blitter_video_sink_init_framebuffer(GstImxBlitterVideoSink *blitter_video_sink);
static void gst_imx_blitter_video_sink_shutdown_framebuffer(GstImxBlitterVideoSink *blitter_video_sink);
static void gst_imx_blitter_video_sink_update_regions(GstImxBlitterVideoSink *blitter_video_sink);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

void gst_imx_blitter_video_sink_class_init(GstImxBlitterVideoSinkClass *klass)
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

	object_class->finalize         = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_finalize);
	object_class->set_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_set_property);
	object_class->get_property     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_get_property);
	element_class->change_state    = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_change_state);
	base_class->set_caps           = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_set_caps);
	base_class->event              = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_event);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_propose_allocation);
	parent_class->show_frame       = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_show_frame);

	klass->start      = NULL;
	klass->stop       = NULL;

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
		PROP_INPUT_CROP,
		g_param_spec_boolean(
			"enable-crop",
			"Enable input frame cropping",
			"Whether or not to crop input frames based on their video crop metadata",
			GST_IMX_BASE_BLITTER_CROP_DEFAULT,
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
}


void gst_imx_blitter_video_sink_init(GstImxBlitterVideoSink *blitter_video_sink)
{
	blitter_video_sink->initialized = FALSE;
	blitter_video_sink->blitter = NULL;
	blitter_video_sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
	blitter_video_sink->framebuffer_name = g_strdup(DEFAULT_FBDEV_NAME);
	blitter_video_sink->framebuffer = NULL;
	blitter_video_sink->framebuffer_fd = -1;
	blitter_video_sink->window_x_coord = DEFAULT_WINDOW_X_COORD;
	blitter_video_sink->window_y_coord = DEFAULT_WINDOW_Y_COORD;
	blitter_video_sink->window_width = DEFAULT_WINDOW_WIDTH;
	blitter_video_sink->window_height = DEFAULT_WINDOW_HEIGHT;

	gst_video_info_init(&(blitter_video_sink->input_video_info));

	blitter_video_sink->input_crop = GST_IMX_BASE_BLITTER_CROP_DEFAULT;

	g_mutex_init(&(blitter_video_sink->mutex));
}



/* base class functions */

static void gst_imx_blitter_video_sink_finalize(GObject *object)
{
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(object);

	g_free(blitter_video_sink->framebuffer_name);

	g_mutex_clear(&(blitter_video_sink->mutex));

	G_OBJECT_CLASS(gst_imx_blitter_video_sink_parent_class)->finalize(object);
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
			blitter_video_sink->force_aspect_ratio = b;
			gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
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

			/* Reinitialize framebuffer only if the sink
			 * itself is initialized already */
			if (blitter_video_sink->initialized)
			{
				/* Shut down old framebuffer states */
				gst_imx_blitter_video_sink_shutdown_framebuffer(blitter_video_sink);

				/* Get the device name for the new framebuffer */
				g_free(blitter_video_sink->framebuffer_name);
				blitter_video_sink->framebuffer_name = g_strdup(new_framebuffer_name);

				/* Try to reinitialize using the new device name */
				if (!gst_imx_blitter_video_sink_init_framebuffer(blitter_video_sink))
					GST_ELEMENT_ERROR(blitter_video_sink, RESOURCE, OPEN_READ_WRITE, ("reinitializing framebuffer failed"), (NULL));

				/* Set the new framebuffer as the output buffer */
				if (!gst_imx_base_blitter_set_output_buffer(blitter_video_sink->blitter, blitter_video_sink->framebuffer))
					GST_ELEMENT_ERROR(blitter_video_sink, RESOURCE, OPEN_READ_WRITE, ("could not set framebuffer as output buffer"), (NULL));

				/* Update display ratio for the new framebuffer */
				gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
			}
			else
			{
				/* Get the device name for the new framebuffer, and do
				 * nothing else, since the sink isn't initialized yet */
				g_free(blitter_video_sink->framebuffer_name);
				blitter_video_sink->framebuffer_name = g_strdup(new_framebuffer_name);
			}

			/* Done; now unlock */
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_INPUT_CROP:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->input_crop = g_value_get_boolean(value);
			if (blitter_video_sink->blitter != NULL)
				gst_imx_base_blitter_enable_crop(blitter_video_sink->blitter, blitter_video_sink->input_crop);
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_x_coord = g_value_get_int(value);
			gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_y_coord = g_value_get_int(value);
			gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_width = g_value_get_uint(value);
			gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			blitter_video_sink->window_height = g_value_get_uint(value);
			gst_imx_blitter_video_sink_update_regions(blitter_video_sink);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

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
			g_value_set_boolean(value, blitter_video_sink->force_aspect_ratio);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_FBDEV_NAME:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_string(value, blitter_video_sink->framebuffer_name);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
			break;

		case PROP_INPUT_CROP:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);
			g_value_set_boolean(value, blitter_video_sink->input_crop);
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
	g_assert(klass->start != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

			/* Set this to TRUE here, since gst_imx_blitter_video_sink_update_regions()
			 * won't do anything if this value is FALSE */
			blitter_video_sink->initialized = TRUE;

			if (!gst_imx_blitter_video_sink_init_framebuffer(blitter_video_sink))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "initializing framebuffer failed");
				blitter_video_sink->initialized = FALSE;
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
				return GST_STATE_CHANGE_FAILURE;
			}

			if (!(klass->start(blitter_video_sink)))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "start() failed");
				blitter_video_sink->initialized = FALSE;
				GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);
				return GST_STATE_CHANGE_FAILURE;
			}

			/* start() must call gst_imx_blitter_video_sink_set_blitter(),
			 * otherwise the sink cannot function properly */
			g_assert(blitter_video_sink->blitter != NULL);

			gst_imx_base_blitter_enable_crop(blitter_video_sink->blitter, blitter_video_sink->input_crop);

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_blitter_video_sink_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(blitter_video_sink);

			blitter_video_sink->initialized = FALSE;

			if ((klass->stop != NULL) && !(klass->stop(blitter_video_sink)))
				GST_ERROR_OBJECT(blitter_video_sink, "stop() failed");

			gst_imx_blitter_video_sink_shutdown_framebuffer(blitter_video_sink);

			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(blitter_video_sink);

			if (blitter_video_sink->blitter != NULL)
			{
				gst_object_unref(GST_OBJECT(blitter_video_sink->blitter));
				blitter_video_sink->blitter = NULL;
			}

			break;
		}

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_blitter_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstVideoInfo video_info;
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(sink);

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->blitter != NULL);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not set caps %" GST_PTR_FORMAT, (gpointer)caps);
		return FALSE;
	}

	blitter_video_sink->input_video_info = video_info;

	gst_imx_blitter_video_sink_update_regions(blitter_video_sink);

	return gst_imx_base_blitter_set_input_video_info(blitter_video_sink->blitter, &video_info);
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
			{
				if (!gst_imx_base_blitter_flush(blitter_video_sink->blitter))
					GST_WARNING_OBJECT(sink, "could not flush blitter");
			}
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
	gboolean ret;
	GstImxBlitterVideoSink *blitter_video_sink = GST_IMX_BLITTER_VIDEO_SINK(video_sink);

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->framebuffer != NULL);

	GST_IMX_BLITTER_VIDEO_SINK_LOCK(video_sink);

	/* using early exit optimization here to avoid calls if necessary */
	ret = TRUE;
	ret = ret && gst_imx_base_blitter_set_input_buffer(blitter_video_sink->blitter, buf);
	ret = ret && gst_imx_base_blitter_blit(blitter_video_sink->blitter);

	GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(video_sink);

	return ret ? GST_FLOW_OK : GST_FLOW_ERROR;
}



/* miscellaneous functions */

gboolean gst_imx_blitter_video_sink_set_blitter(GstImxBlitterVideoSink *blitter_video_sink, GstImxBaseBlitter *blitter)
{
	g_assert(blitter_video_sink != NULL);
	g_assert(blitter != NULL);

	if (blitter == blitter_video_sink->blitter)
		return TRUE;

	if (blitter_video_sink->blitter != NULL)
		gst_object_unref(GST_OBJECT(blitter_video_sink->blitter));

	blitter_video_sink->blitter = blitter;

	gst_object_ref(GST_OBJECT(blitter_video_sink->blitter));

	gst_imx_blitter_video_sink_update_regions(blitter_video_sink);

	if (!gst_imx_base_blitter_set_output_buffer(blitter_video_sink->blitter, blitter_video_sink->framebuffer))
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not set framebuffer as output buffer");
		return FALSE;
	}

	return TRUE;
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


static gboolean gst_imx_blitter_video_sink_open_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink)
{
	int fd;

	g_assert(blitter_video_sink != NULL);
	g_assert(blitter_video_sink->framebuffer_name != NULL);

	/* the derived class' stop() vfunc must be called prior to this function
	 * (or, at startup, this function must be run before the start() vfunc is called) */

	if (blitter_video_sink->framebuffer_fd != -1)
		gst_imx_blitter_video_sink_close_framebuffer_device(blitter_video_sink);

	GST_INFO_OBJECT(blitter_video_sink, "opening framebuffer %s", blitter_video_sink->framebuffer_name);

	fd = open(blitter_video_sink->framebuffer_name, O_RDWR, 0);
	if (fd < 0)
	{
		GST_ELEMENT_ERROR(blitter_video_sink, RESOURCE, OPEN_READ_WRITE, ("could not open %s: %s", blitter_video_sink->framebuffer_name, strerror(errno)), (NULL));
		return FALSE;
	}

	blitter_video_sink->framebuffer_fd = fd;
	GST_INFO_OBJECT(blitter_video_sink, "framebuffer FD is %d", blitter_video_sink->framebuffer_fd);

	return TRUE;
}


static void gst_imx_blitter_video_sink_close_framebuffer_device(GstImxBlitterVideoSink *blitter_video_sink)
{
	g_assert(blitter_video_sink != NULL);

	if (blitter_video_sink->framebuffer_fd == -1)
		return;

	GST_INFO_OBJECT(blitter_video_sink, "closing framebuffer %s with FD %d", blitter_video_sink->framebuffer_name, blitter_video_sink->framebuffer_fd);

	close(blitter_video_sink->framebuffer_fd);
	blitter_video_sink->framebuffer_fd = -1;
}


/* NOTE: This function creates a GstBuffer that wraps the *entire* framebuffer,
 * not just a subsection. If the sink needs to blit to a subsection, and not
 * to the entire screen, it must instruct the blitter to use a subsection of
 * the framebuffer as its destination. It is an error to try and adjust the
 * GstBuffer's metadata to make it fit that subregion. The GstBuffer must
 * *always* encompass the entire framebuffer, to keep operations simple and
 * efficient.
 *
 * The created GstBuffer has no GstMemory blocks inside, just a phys mem meta.
 * Since the sink never writes with the CPU to that GstBuffer, it is pointless
 * to add GstMemory blocks, map/unmap logic etc.
 * But if this function one day gets reused by multiple components, this situation
 * will have to be revised.
 * */
static gboolean gst_imx_blitter_video_sink_init_framebuffer(GstImxBlitterVideoSink *blitter_video_sink)
{
	guint fb_width, fb_height;
	GstVideoFormat fb_format;
	GstBuffer *buffer;
	GstImxPhysMemMeta *phys_mem_meta;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	g_assert(blitter_video_sink != NULL);

	if (blitter_video_sink->framebuffer != NULL)
		gst_imx_blitter_video_sink_shutdown_framebuffer(blitter_video_sink);

	if (!gst_imx_blitter_video_sink_open_framebuffer_device(blitter_video_sink))
	{
		GST_ERROR_OBJECT(blitter_video_sink, "opening framebuffer device failed");
		return FALSE;
	}

	if (ioctl(blitter_video_sink->framebuffer_fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not open get fixed screen info: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(blitter_video_sink->framebuffer_fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not open get variable screen info: %s", strerror(errno));
		return FALSE;
	}

	fb_width = fb_var.xres;
	fb_height = fb_var.yres;
	fb_format = gst_imx_blitter_video_sink_get_format_from_fb(blitter_video_sink, &fb_var, &fb_fix);

	GST_INFO_OBJECT(blitter_video_sink, "framebuffer resolution is %u x %u", fb_width, fb_height);

	buffer = gst_buffer_new();
	gst_buffer_add_video_meta(buffer, GST_VIDEO_FRAME_FLAG_NONE, fb_format, fb_width, fb_height);

	phys_mem_meta = GST_IMX_PHYS_MEM_META_ADD(buffer);
	phys_mem_meta->phys_addr = (gst_imx_phys_addr_t)(fb_fix.smem_start);

	blitter_video_sink->framebuffer = buffer;

	return TRUE;
}


static void gst_imx_blitter_video_sink_shutdown_framebuffer(GstImxBlitterVideoSink *blitter_video_sink)
{
	if (blitter_video_sink->framebuffer == NULL)
		return;

	gst_buffer_unref(blitter_video_sink->framebuffer);

	blitter_video_sink->framebuffer = NULL;

	gst_imx_blitter_video_sink_close_framebuffer_device(blitter_video_sink);
}


static void gst_imx_blitter_video_sink_update_regions(GstImxBlitterVideoSink *blitter_video_sink)
{
	/* must be called with mutex lock */

	guint display_ratio_n, display_ratio_d;
	guint video_width, video_height;
	gboolean keep_aspect = TRUE;
	GstVideoMeta *fb_video_meta;
	GstImxBaseBlitterRegion output_region;

	if (!(blitter_video_sink->initialized))
		return;

	fb_video_meta = gst_buffer_get_video_meta(blitter_video_sink->framebuffer);

	/* Determine the display ratio to be used for blitting */
	if (blitter_video_sink->force_aspect_ratio)
	{
		guint video_par_n, video_par_d, window_par_n, window_par_d;

		video_width = GST_VIDEO_INFO_WIDTH(&(blitter_video_sink->input_video_info));
		video_height = GST_VIDEO_INFO_HEIGHT(&(blitter_video_sink->input_video_info));
		video_par_n = GST_VIDEO_INFO_PAR_N(&(blitter_video_sink->input_video_info));
		video_par_d = GST_VIDEO_INFO_PAR_D(&(blitter_video_sink->input_video_info));
		window_par_n = 1;
		window_par_d = 1;

		if ((video_width == 0) || (video_height == 0))
		{
			GST_INFO_OBJECT(blitter_video_sink, "video info in initial state -> using 1:1 display ratio");
			keep_aspect = FALSE;
		}
		else
		{
			if (!gst_video_calculate_display_ratio(
				&display_ratio_n, &display_ratio_d,
				video_width, video_height,
				video_par_n, video_par_d,
				window_par_n, window_par_d
			))
			{
				GST_ERROR_OBJECT(blitter_video_sink, "aspect ratio calculation failed -> using 1:1 display ratio");
				keep_aspect = FALSE;
			}
		}
	}
	else
	{
		GST_INFO_OBJECT(blitter_video_sink, "aspect ratio not forced -> using default 1:1 display ratio");
		keep_aspect = FALSE;
	}

	output_region.x1 = blitter_video_sink->window_x_coord;
	output_region.y1 = blitter_video_sink->window_y_coord;
	output_region.x2 = output_region.x1 + ((blitter_video_sink->window_width == 0) ? ((guint)(fb_video_meta->width)) : blitter_video_sink->window_width);
	output_region.y2 = output_region.y1 + ((blitter_video_sink->window_height == 0) ? ((guint)(fb_video_meta->height)) : blitter_video_sink->window_height);

	/* Calculate and set the blitter's output region */
	if (keep_aspect)
	{
		GstImxBaseBlitterRegion video_region;
		guint ratio_factor;
		guint outw, outh, videow, videoh;

		GST_INFO_OBJECT(blitter_video_sink, "calculated display ratio:  %u:%u", display_ratio_n, display_ratio_d);

		/* Fit the frame to the framebuffer, keeping display ratio
		 * This means that either its width or its height will be set to the
		 * framebuffer's width/height, and the other length will be shorter,
		 * scaled accordingly to retain the display ratio
		 *
		 * Setting dn = display_ratio_n , dd = display_ratio_d ,
		 * fw = fb_video_meta->width , fh = fb_video_meta->height ,
		 * we can identify cases:
		 *
		 * (1) Frame fits in framebuffer with its width maximized
		 *     In this case, this holds: fw/fh < dn/dd
		 * (1) Frame fits in framebuffer with its height maximized
		 *     In this case, this holds: fw/fh > dn/dd
		 *
		 * To simplify comparison, the inequality fw/fh > dn/dd is
		 * transformed to: fw*dd/fh > dn
		 * fw*dd/fh is the ratio_factor
		 */

		outw = output_region.x2 - output_region.x1;
		outh = output_region.y2 - output_region.y1;
		videow = video_region.x2 - video_region.x1;
		videoh = video_region.y2 - video_region.y1;

		ratio_factor = (guint)gst_util_uint64_scale_int(outw, display_ratio_d, outh);

		if (ratio_factor >= display_ratio_n)
		{
			GST_INFO_OBJECT(blitter_video_sink, "maximizing video height");
			videow = (guint)gst_util_uint64_scale_int(outh, display_ratio_n, display_ratio_d);
			videoh = outh;
		}
		else
		{
			GST_INFO_OBJECT(blitter_video_sink, "maximizing video width");
			videow = outw;
			videoh = (guint)gst_util_uint64_scale_int(outw, display_ratio_d, display_ratio_n);
		}

		/* Safeguard to ensure width/height aren't out of bounds
		 * (should not happen, but better safe than sorry) */
		videow = MIN(videow, outw);
		videoh = MIN(videoh, outh);

		video_region.x1 = output_region.x1 + (outw - videow) / 2;
		video_region.y1 = output_region.y1 + (outh - videoh) / 2;
		video_region.x2 = video_region.x1 + videow;
		video_region.y2 = video_region.y1 + videoh;

		GST_INFO_OBJECT(blitter_video_sink, "setting video region to (%d,%d - %d,%d)", video_region.x1, video_region.y1, video_region.x2, video_region.y2);

		gst_imx_base_blitter_set_output_regions(blitter_video_sink->blitter, &video_region, &output_region);
	}
	else
	{
		GST_INFO_OBJECT(blitter_video_sink, "not keeping aspect ratio");
		GST_INFO_OBJECT(blitter_video_sink, "setting video region to cover the entire window rectangle: (%d,%d - %d,%d)", output_region.x1, output_region.y1, output_region.x2, output_region.y2);
		gst_imx_base_blitter_set_output_regions(blitter_video_sink->blitter, NULL, &output_region);
	}
}
