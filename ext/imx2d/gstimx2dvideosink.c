/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#include <gst/gst.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideosink.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_sink_debug);
#define GST_CAT_DEFAULT imx_2d_video_sink_debug


enum
{
	PROP_0,
	PROP_DROP_FRAMES,
	PROP_FRAMEBUFFER_NAME,
	PROP_INPUT_CROP,
	PROP_VIDEO_DIRECTION,
	PROP_CLEAR_AT_NULL,
	PROP_CLEAR_ON_RELOCATE,
	PROP_USE_VSYNC,
	PROP_FORCE_ASPECT_RATIO,
	PROP_WINDOW_X_COORD,
	PROP_WINDOW_Y_COORD,
	PROP_WINDOW_WIDTH,
	PROP_WINDOW_HEIGHT,
	PROP_LEFT_MARGIN,
	PROP_TOP_MARGIN,
	PROP_RIGHT_MARGIN,
	PROP_BOTTOM_MARGIN
};


#define DEFAULT_DROP_FRAMES FALSE
#define DEFAULT_FRAMEBUFFER_NAME "/dev/fb0"
#define DEFAULT_INPUT_CROP TRUE
#define DEFAULT_VIDEO_DIRECTION GST_VIDEO_ORIENTATION_IDENTITY
#define DEFAULT_CLEAR_AT_NULL FALSE
#define DEFAULT_CLEAR_ON_RELOCATE FALSE
#define DEFAULT_USE_VSYNC FALSE
#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_WINDOW_X_COORD 0
#define DEFAULT_WINDOW_Y_COORD 0
#define DEFAULT_WINDOW_WIDTH 0
#define DEFAULT_WINDOW_HEIGHT 0
#define DEFAULT_LEFT_MARGIN 0
#define DEFAULT_TOP_MARGIN 0
#define DEFAULT_RIGHT_MARGIN 0
#define DEFAULT_BOTTOM_MARGIN 0


static void gst_imx_2d_video_sink_video_direction_interface_init(G_GNUC_UNUSED GstVideoDirectionInterface *iface)
{
	/* We implement the video-direction property */
}


G_DEFINE_ABSTRACT_TYPE_WITH_CODE(
	GstImx2dVideoSink,
	gst_imx_2d_video_sink,
	GST_TYPE_VIDEO_SINK,
	G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_DIRECTION, gst_imx_2d_video_sink_video_direction_interface_init)
)


/* Base class function overloads. */

/* General element operations. */
static void gst_imx_2d_video_sink_dispose(GObject *object);
static void gst_imx_2d_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_2d_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_2d_video_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_2d_video_sink_event(GstBaseSink *sink, GstEvent *event);

/* Caps handling. */
static gboolean gst_imx_2d_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps);

/* Allocator. */
static gboolean gst_imx_2d_video_sink_propose_allocation(GstBaseSink *sink, GstQuery *query);

/* Frame output. */
static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer);

/* GstImx2dVideoSink specific functions. */
static gboolean gst_imx_2d_video_sink_start(GstImx2dVideoSink *self);
static void gst_imx_2d_video_sink_stop(GstImx2dVideoSink *self);
static gboolean gst_imx_2d_video_sink_create_blitter(GstImx2dVideoSink *self);
static GstVideoOrientationMethod gst_imx_2d_video_sink_get_current_video_direction(GstImx2dVideoSink *self);
static gboolean gst_imx_2d_video_sink_flip_pages(GstImx2dVideoSink *self);
static gboolean gst_imx_2d_video_clear_total_region(GstImx2dVideoSink *self, gboolean clear_on_all_pages);
static void gst_imx_2d_video_sink_recalculate_regions_if_needed(GstImx2dVideoSink *self);




static void gst_imx_2d_video_sink_class_init(GstImx2dVideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseSinkClass *base_sink_class;
	GstVideoSinkClass *video_sink_class;

	gst_imx_2d_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_sink_debug, "imx2dvideosink", 0, "NXP i.MX 2D video sink base class");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_sink_class = GST_BASE_SINK_CLASS(klass);
	video_sink_class = GST_VIDEO_SINK_CLASS(klass);

	object_class->dispose               = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_dispose);
	object_class->set_property          = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_set_property);
	object_class->get_property          = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_get_property);

	element_class->change_state         = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_change_state);

	base_sink_class->set_caps           = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_set_caps);
	base_sink_class->event              = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_event);
	base_sink_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_propose_allocation);

	video_sink_class->show_frame        = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_show_frame);

	g_object_class_install_property(
		object_class,
		PROP_DROP_FRAMES,
		g_param_spec_boolean(
			"drop-frames",
			"Drop frames",
			"Drop frames and output a black screen instead",
			DEFAULT_DROP_FRAMES,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FRAMEBUFFER_NAME,
		g_param_spec_string(
			"framebuffer",
			"Framebuffer device name",
			"The device name of the framebuffer to render to",
			DEFAULT_FRAMEBUFFER_NAME,
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
	g_object_class_override_property(object_class, PROP_VIDEO_DIRECTION, "video-direction");
	g_object_class_install_property(
		object_class,
		PROP_CLEAR_AT_NULL,
		g_param_spec_boolean(
			"clear-at-null",
			"Clear at null",
			"Clear the screen by filling it with black pixels when switching to the NULL state",
			DEFAULT_CLEAR_AT_NULL,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_CLEAR_ON_RELOCATE,
		g_param_spec_boolean(
			"clear-on-relocate",
			"Clear on relocate",
			"Clear the screen by filling it with black pixels when relocating the video window",
			DEFAULT_CLEAR_AT_NULL,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_USE_VSYNC,
		g_param_spec_boolean(
			"use-vsync",
			"Use VSync",
			"Enable and use vertical synchronization (based on page flipping) to eliminate tearing",
			DEFAULT_USE_VSYNC,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
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


static void gst_imx_2d_video_sink_init(GstImx2dVideoSink *self)
{
	self->uploader = NULL;
	self->imx_dma_buffer_allocator = NULL;

	self->blitter = NULL;

	gst_video_info_init(&(self->input_video_info));
	self->input_surface = NULL;

	self->framebuffer = NULL;

	self->drop_frames = DEFAULT_DROP_FRAMES;
	self->framebuffer_name = g_strdup(DEFAULT_FRAMEBUFFER_NAME);
	self->input_crop = DEFAULT_INPUT_CROP;
	self->video_direction = DEFAULT_VIDEO_DIRECTION;
	self->clear_at_null = DEFAULT_CLEAR_AT_NULL;
	self->clear_on_relocate = DEFAULT_CLEAR_ON_RELOCATE;
	self->use_vsync = DEFAULT_USE_VSYNC;
	self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
	self->window_x_coord = DEFAULT_WINDOW_X_COORD;
	self->window_y_coord = DEFAULT_WINDOW_Y_COORD;
	self->window_width = DEFAULT_WINDOW_WIDTH;
	self->window_height = DEFAULT_WINDOW_HEIGHT;
	self->extra_margin.left_margin = DEFAULT_LEFT_MARGIN;
	self->extra_margin.top_margin = DEFAULT_TOP_MARGIN;
	self->extra_margin.right_margin = DEFAULT_RIGHT_MARGIN;
	self->extra_margin.bottom_margin = DEFAULT_BOTTOM_MARGIN;

	self->tag_video_direction = DEFAULT_VIDEO_DIRECTION;

	self->drop_frames_changed = FALSE;

	self->region_coords_need_update = TRUE;
}


static void gst_imx_2d_video_sink_dispose(GObject *object)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(object);

	g_free(self->framebuffer_name);

	G_OBJECT_CLASS(gst_imx_2d_video_sink_parent_class)->dispose(object);
}


static void gst_imx_2d_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DROP_FRAMES:
		{
			GST_OBJECT_LOCK(self);
			self->drop_frames = g_value_get_boolean(value);
			self->drop_frames_changed = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_FRAMEBUFFER_NAME:
		{
			gchar const *new_framebuffer_name = g_value_get_string(value);

			if ((new_framebuffer_name == NULL) || (new_framebuffer_name[0] == '\0'))
			{
				GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("framebuffer device name must not be an empty string; using default framebuffer instead"), (NULL));
				break;
			}

			GST_OBJECT_LOCK(self);
			g_free(self->framebuffer_name);
			self->framebuffer_name = g_strdup(new_framebuffer_name);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			self->input_crop = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_VIDEO_DIRECTION:
		{
			GST_OBJECT_LOCK(self);
			self->video_direction = g_value_get_enum(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_CLEAR_AT_NULL:
		{
			GST_OBJECT_LOCK(self);
			self->clear_at_null = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_CLEAR_ON_RELOCATE:
		{
			GST_OBJECT_LOCK(self);
			self->clear_on_relocate = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_USE_VSYNC:
		{
			GST_OBJECT_LOCK(self);
			self->use_vsync = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_FORCE_ASPECT_RATIO:
		{
			GST_OBJECT_LOCK(self);
			self->force_aspect_ratio = g_value_get_boolean(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			GST_OBJECT_LOCK(self);
			self->window_x_coord = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			GST_OBJECT_LOCK(self);
			self->window_y_coord = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			GST_OBJECT_LOCK(self);
			self->window_width = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			GST_OBJECT_LOCK(self);
			self->window_height = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_LEFT_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			self->extra_margin.left_margin = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_TOP_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			self->extra_margin.top_margin = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_RIGHT_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			self->extra_margin.right_margin = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_BOTTOM_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			self->extra_margin.bottom_margin = g_value_get_uint(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_2d_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DROP_FRAMES:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->drop_frames);
			GST_OBJECT_UNLOCK(self);
			break;
		}


		case PROP_FRAMEBUFFER_NAME:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_string(value, self->framebuffer_name);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->input_crop);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_VIDEO_DIRECTION:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_enum(value, self->video_direction);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_CLEAR_AT_NULL:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->clear_at_null);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_CLEAR_ON_RELOCATE:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->clear_on_relocate);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_USE_VSYNC:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->use_vsync);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_FORCE_ASPECT_RATIO:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->force_aspect_ratio);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->window_x_coord);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->window_y_coord);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->window_width);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->window_height);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_LEFT_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->extra_margin.left_margin);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_TOP_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->extra_margin.top_margin);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_RIGHT_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->extra_margin.right_margin);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_BOTTOM_MARGIN:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->extra_margin.bottom_margin);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_2d_video_sink_change_state(GstElement *element, GstStateChange transition)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(self != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_imx_2d_video_sink_start(self))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_2d_video_sink_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
			gst_imx_2d_video_sink_stop(self);
			break;

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_2d_video_sink_event(GstBaseSink *sink, GstEvent *event)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(sink);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_TAG:
		{
			GstTagList *taglist;
			GstVideoOrientationMethod new_tag_video_direction;

			gst_event_parse_tag(event, &taglist);

			if (gst_imx_2d_orientation_from_image_direction_tag(taglist, &new_tag_video_direction))
			{
				GST_OBJECT_LOCK(self);
				self->tag_video_direction = new_tag_video_direction;
				GST_OBJECT_UNLOCK(self);
			}

			break;
		}

		default:
			break;
	}

	return GST_BASE_SINK_CLASS(gst_imx_2d_video_sink_parent_class)->event(sink, event);
}


static gboolean gst_imx_2d_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstVideoInfo input_video_info;
	GstImx2dTileLayout tile_layout;
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(sink);

	g_assert(self->blitter != NULL);

	/* Convert the caps to video info structures for easier acess. */

	GST_DEBUG_OBJECT(self, "setting caps %" GST_PTR_FORMAT, (gpointer)caps);

	if (G_UNLIKELY(!gst_imx_video_info_from_caps(&input_video_info, caps, &tile_layout, NULL)))
	{
		GST_ERROR_OBJECT(self, "could not set caps %" GST_PTR_FORMAT, (gpointer)caps);
		goto error;
	}

	memcpy(&(self->input_video_info), &input_video_info, sizeof(GstVideoInfo));

	/* Fill the input surface description with values that can't change
	 * in between buffers. (Plane stride and offset values can change.
	 * This is unlikely to happen, but it is not impossible.) */
	self->input_surface_desc.width = GST_VIDEO_INFO_WIDTH(&input_video_info);
	self->input_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&input_video_info);
	self->input_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&input_video_info), &tile_layout);

	return TRUE;

error:
	return FALSE;
}


static gboolean gst_imx_2d_video_sink_propose_allocation(G_GNUC_UNUSED GstBaseSink *sink, GstQuery *query)
{
	/* Not chaining up to the base class since it does not have
	 * its own propose_allocation implementation - its vmethod
	 * propose_allocation pointer is set to NULL. */

	/* Let upstream know that we can handle GstVideoMeta and GstVideoCropMeta. */
	gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);
	gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, 0);

	return TRUE;
}


static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer)
{
	Imx2dBlitParams blit_params;
	GstFlowReturn flow_ret;
	gboolean input_crop;
	gboolean drop_frames, drop_frames_changed;
	Imx2dRegion inner_region;
	Imx2dBlitMargin combined_margin;
	Imx2dRegion crop_rectangle;
	GstVideoOrientationMethod video_direction;
	GstBuffer *uploaded_input_buffer = NULL;
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK_CAST(video_sink);

	g_assert(self->blitter != NULL);


	/* Create local copies of the property values so that we can use them
	 * without risking race conditions if another thread is setting new
	 * values while this function is running. */
	GST_OBJECT_LOCK(self);

	input_crop = self->input_crop;
	video_direction = gst_imx_2d_video_sink_get_current_video_direction(self);
	drop_frames = self->drop_frames;
	drop_frames_changed = self->drop_frames_changed;
	self->drop_frames_changed = FALSE;

	/* This must be called with the object lock held. */
	gst_imx_2d_video_sink_recalculate_regions_if_needed(self);

	memcpy(&inner_region, &(self->inner_region), sizeof(inner_region));
	memcpy(&combined_margin, &(self->combined_margin), sizeof(combined_margin));
	/* NOTE: Alpha is 0xFF. If it were 0x00, the imx2d blitter code would
	 * assume that the margin were invisible and skip it. */
	combined_margin.color = 0xFF000000;

	GST_OBJECT_UNLOCK(self);


	/* Check if the drop-frames property changed. If it changed
	 * from false to true, paint the output region black. */
	if (drop_frames)
	{
		GST_LOG_OBJECT(self, "drop-frames is currently set to TRUE; dropping frame by not showing it and instead filling output with black pixels");

		if (drop_frames_changed)
		{
			if (!gst_imx_2d_video_clear_total_region(self, TRUE))
				goto error;

			if (!gst_imx_2d_video_sink_flip_pages(self))
				goto error;
		}

		return GST_FLOW_OK;
	}


	/* Upload the input buffer. The uploader creates a deep
	 * copy if necessary, but tries to avoid that if possible
	 * by passing through the buffer (if it consists purely
	 * of imxdmabuffer backend gstmemory blocks) or by
	 * duplicating DMA-BUF FDs with dup(). */
	flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploaded_input_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		return flow_ret;


	/* Set up the input surface. */

	gst_imx_2d_assign_input_buffer_to_surface(
		input_buffer, uploaded_input_buffer,
		self->input_surface,
		&(self->input_surface_desc),
		&(self->input_video_info)
	);

	imx_2d_surface_set_desc(self->input_surface, &(self->input_surface_desc));


	/* Fill the blit parameters. */

	memset(&blit_params, 0, sizeof(blit_params));
	blit_params.margin = &combined_margin;
	blit_params.source_region = NULL;
	blit_params.dest_region = &inner_region;
	blit_params.rotation = gst_imx_2d_convert_from_video_orientation_method(video_direction);
	blit_params.alpha = 255;

	if (input_crop)
	{
		GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta(input_buffer);

		if (crop_meta != NULL)
		{
			crop_rectangle.x1 = crop_meta->x;
			crop_rectangle.y1 = crop_meta->y;
			crop_rectangle.x2 = crop_meta->x + crop_meta->width;
			crop_rectangle.y2 = crop_meta->y + crop_meta->height;

			blit_params.source_region = &crop_rectangle;

			GST_LOG_OBJECT(
				self,
				"using crop rectangle (%d, %d) - (%d, %d)",
				crop_rectangle.x1, crop_rectangle.y1,
				crop_rectangle.x2, crop_rectangle.y2
			);
		}
	}


	/* Now perform the actual blit. */

	GST_LOG_OBJECT(self, "beginning blitting procedure to transform the frame");

	if (!imx_2d_blitter_start(self->blitter, self->framebuffer_surface))
	{
		GST_ERROR_OBJECT(self, "starting blitter failed");
		goto error;
	}

	if (!imx_2d_blitter_do_blit(self->blitter, self->input_surface, &blit_params))
	{
		GST_ERROR_OBJECT(self, "blitting failed");
		goto error;
	}

	if (!imx_2d_blitter_finish(self->blitter))
	{
		GST_ERROR_OBJECT(self, "finishing blitter failed");
		goto error;
	}


	if (!gst_imx_2d_video_sink_flip_pages(self))
		goto error;


	GST_LOG_OBJECT(self, "blitting procedure finished successfully; frame output complete");


finish:
	/* Discard the uploaded version of the input buffer. */
	if (uploaded_input_buffer != NULL)
		gst_buffer_unref(uploaded_input_buffer);
	return flow_ret;

error:
	if (flow_ret != GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static gboolean gst_imx_2d_video_sink_start(GstImx2dVideoSink *self)
{
	gboolean ret = TRUE;
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));
	gboolean use_vsync;
	gchar *framebuffer_name;

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);

	self->tag_video_direction = DEFAULT_VIDEO_DIRECTION;
	self->drop_frames_changed = TRUE;

	self->region_coords_need_update = TRUE;
	self->total_region_valid = FALSE;

	GST_OBJECT_LOCK(self);
	framebuffer_name = g_strdup(self->framebuffer_name);
	use_vsync = self->use_vsync;
	GST_OBJECT_UNLOCK(self);

	/* We call start _after_ the allocator & uploader were
	 * set up in case these might be needed. Currently,
	 * this is not the case, but it may be in the future. */
	if ((klass->start != NULL) && !(klass->start(self)))
	{
		GST_ERROR_OBJECT(self, "start() failed");
		goto error;
	}

	if (!gst_imx_2d_video_sink_create_blitter(self))
	{
		GST_ERROR_OBJECT(self, "creating blitter failed");
		goto error;
	}

	self->input_surface = imx_2d_surface_create(NULL);
	if (self->input_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating input surface failed");
		goto error;
	}

	self->framebuffer = imx_2d_linux_framebuffer_create(framebuffer_name, use_vsync);
	if (self->framebuffer == NULL)
	{
		GST_ERROR_OBJECT(self, "creating output framebuffer using device \"%s\" failed", framebuffer_name);
		goto error;
	}

	if (use_vsync)
	{
		self->write_fb_page = 1;
		self->display_fb_page = 0;

		imx_2d_linux_framebuffer_set_write_fb_page(self->framebuffer, self->write_fb_page);
		if (!imx_2d_linux_framebuffer_set_display_fb_page(self->framebuffer, self->display_fb_page))
		{
			GST_ERROR_OBJECT(self, "could not set initial framebuffer display page");
			goto error;
		}
	}
	else
	{
		self->write_fb_page = 0;
		self->display_fb_page = 0;
	}

	self->num_fb_pages = imx_2d_linux_framebuffer_get_num_fb_pages(self->framebuffer);

	self->framebuffer_surface = imx_2d_linux_framebuffer_get_surface(self->framebuffer);
	g_assert(self->framebuffer_surface != NULL);

	self->framebuffer_surface_desc = imx_2d_surface_get_desc(self->framebuffer_surface);

	GST_INFO_OBJECT(self, "framebuffer using device \"%s\" set up", framebuffer_name);

finish:
	g_free(framebuffer_name);
	return ret;

error:
	gst_imx_2d_video_sink_stop(self);
	ret = FALSE;
	goto finish;
}


static void gst_imx_2d_video_sink_stop(GstImx2dVideoSink *self)
{
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));

	if ((klass->stop != NULL) && !(klass->stop(self)))
		GST_ERROR_OBJECT(self, "stop() failed");

	if (self->input_surface != NULL)
	{
		imx_2d_surface_destroy(self->input_surface);
		self->input_surface = NULL;
	}

	if (self->framebuffer != NULL)
	{
		gboolean clear_at_null;

		GST_OBJECT_LOCK(self);
		clear_at_null = self->clear_at_null;
		GST_OBJECT_UNLOCK(self);

		if (clear_at_null && (self->blitter != NULL))
		{
			GST_DEBUG_OBJECT(self, "clearing window in framebuffer with black pixels at the READY->NULL state change as requested");
			gst_imx_2d_video_clear_total_region(self, FALSE);
		}

		imx_2d_linux_framebuffer_destroy(self->framebuffer);
		self->framebuffer = NULL;
	}

	if (self->blitter != NULL)
	{
		imx_2d_blitter_destroy(self->blitter);
		self->blitter = NULL;
	}

	if (self->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(self->uploader));
		self->uploader = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}
}


static gboolean gst_imx_2d_video_sink_create_blitter(GstImx2dVideoSink *self)
{
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));

	g_assert(klass->create_blitter != NULL);
	g_assert(self->blitter == NULL);

	if (G_UNLIKELY((self->blitter = klass->create_blitter(self)) == NULL))
	{
		GST_ERROR_OBJECT(self, "could not create blitter");
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "created new blitter %" GST_PTR_FORMAT, (gpointer)(self->blitter));

	return TRUE;
}


static GstVideoOrientationMethod gst_imx_2d_video_sink_get_current_video_direction(GstImx2dVideoSink *self)
{
	return (self->video_direction == GST_VIDEO_ORIENTATION_AUTO) ? self->tag_video_direction : self->video_direction;
}


static gboolean gst_imx_2d_video_sink_flip_pages(GstImx2dVideoSink *self)
{
	if (!self->use_vsync)
		return TRUE;

	self->display_fb_page = self->write_fb_page;
	self->write_fb_page = (self->write_fb_page + 1) % self->num_fb_pages;

	imx_2d_linux_framebuffer_set_write_fb_page(self->framebuffer, self->write_fb_page);
	if (!imx_2d_linux_framebuffer_set_display_fb_page(self->framebuffer, self->display_fb_page))
	{
		GST_ERROR_OBJECT(self, "could not set new framebuffer display page");
		return FALSE;
	}
	else
		return TRUE;
}


static gboolean gst_imx_2d_video_clear_total_region(GstImx2dVideoSink *self, gboolean clear_on_all_pages)
{
	int page_index;
	int num_pages;

	if (!self->total_region_valid)
		return TRUE;

	num_pages = clear_on_all_pages ? imx_2d_linux_framebuffer_get_num_fb_pages(self->framebuffer) : 1;

	for (page_index = 0; page_index < num_pages; ++page_index)
	{
		if (self->use_vsync && clear_on_all_pages)
		{
			GST_DEBUG_OBJECT(self, "clearing FB page %d", page_index);
			imx_2d_linux_framebuffer_set_write_fb_page(self->framebuffer, page_index);
		}

		if (!imx_2d_blitter_start(self->blitter, self->framebuffer_surface))
		{
			GST_ERROR_OBJECT(self, "starting blitter failed");
			return FALSE;
		}

		if (!imx_2d_blitter_fill_region(self->blitter, &(self->total_region), 0xFF000000))
		{
			GST_ERROR_OBJECT(self, "blitting failed");
			return FALSE;
		}

		if (!imx_2d_blitter_finish(self->blitter))
		{
			GST_ERROR_OBJECT(self, "finishing blitter failed");
			return FALSE;
		}
	}

	self->write_fb_page = 0;
	gst_imx_2d_video_sink_flip_pages(self);

	return TRUE;
}


static void gst_imx_2d_video_sink_recalculate_regions_if_needed(GstImx2dVideoSink *self)
{
	/* This must be called with the object lock held. */

	gint input_width, input_height;
	gint window_width, window_height;

	if (!self->region_coords_need_update)
		return;

	if (self->clear_on_relocate)
	{
		GST_TRACE_OBJECT(self, "need to clear total region %" IMX_2D_REGION_FORMAT " before relocating it", IMX_2D_REGION_ARGS(&(self->total_region)));
		gst_imx_2d_video_clear_total_region(self, TRUE);
	}

	input_width = GST_VIDEO_INFO_WIDTH(&(self->input_video_info));
	input_height = GST_VIDEO_INFO_HEIGHT(&(self->input_video_info));

	window_width = (self->window_width != 0) ? (gint)(self->window_width) : self->framebuffer_surface_desc->width;
	window_height = (self->window_height != 0) ? (gint)(self->window_height) : self->framebuffer_surface_desc->height;

	/* Relations between regions and margins:
	 *
	 * total_region = outer_region + extra_margin.
	 * outer_region = inner_region + letterbox_margin.
	 * combined_margin = extra_margin + letterbox_margin.
	 *
	 * Also:
	 * window_x_coord, window_y_coord, window_width, window_height
	 * define the total_region boundaries.
	 */

	self->total_region.x1 = self->window_x_coord;
	self->total_region.y1 = self->window_y_coord;
	self->total_region.x2 = self->window_x_coord + window_width;
	self->total_region.y2 = self->window_y_coord + window_height;

	self->total_region_valid = TRUE;

	self->outer_region.x1 = self->total_region.x1 + self->extra_margin.left_margin;
	self->outer_region.y1 = self->total_region.y1 + self->extra_margin.top_margin;
	self->outer_region.x2 = self->total_region.x2 - self->extra_margin.right_margin;
	self->outer_region.y2 = self->total_region.y2 - self->extra_margin.bottom_margin;

	GST_DEBUG_OBJECT(
		self,
		"window x/y coordinates: %d/%d  window width/height: %d/%d",
		self->window_x_coord, self->window_y_coord,
		window_width, window_height
	);

	/* This should not happen, and typically indicates invalid user
	 * defined extra margins. */
	if (G_UNLIKELY(self->outer_region.x1 > self->outer_region.x2))
		GST_ERROR_OBJECT(self, "calculated outer region X coordinates are invalid: x1 = %d x2 = %d (x1 must be <= x2)", self->outer_region.x1, self->outer_region.x2);
	if (G_UNLIKELY(self->outer_region.y1 > self->outer_region.y2))
		GST_ERROR_OBJECT(self, "calculated outer region Y coordinates are invalid: y1 = %d y2 = %d (y1 must be <= y2)", self->outer_region.y1, self->outer_region.y2);

	GST_DEBUG_OBJECT(self, "calculated outer region: %" IMX_2D_REGION_FORMAT, IMX_2D_REGION_ARGS(&(self->outer_region)));

	self->combined_margin.left_margin = self->extra_margin.left_margin;
	self->combined_margin.top_margin = self->extra_margin.top_margin;
	self->combined_margin.right_margin = self->extra_margin.right_margin;
	self->combined_margin.bottom_margin = self->extra_margin.bottom_margin;

	/* Calculate a letterbox_margin if necessary.
	 *
	 * If force_aspect_ratio is FALSE, then the frame will always
	 * be scaled to fill the outer_region. In other words, in that
	 * case, inner_region == outer_region.
	 *
	 * In rare cases where width and height are initially 0 (can happen
	 * with some broken video input), we cannot calculate letterbox
	 * margins, because this would lead to divisions by zero.
	 */
	if (self->force_aspect_ratio && ((self->outer_region.x1 < self->outer_region.x2))
	                             && ((self->outer_region.y1 < self->outer_region.y2))
	                             && ((input_width > 0))
	                             && ((input_height > 0)))
	{
		gboolean transposed = FALSE;

		switch (gst_imx_2d_video_sink_get_current_video_direction(self))
		{
			case GST_VIDEO_ORIENTATION_90L:
			case GST_VIDEO_ORIENTATION_90R:
			case GST_VIDEO_ORIENTATION_UL_LR:
			case GST_VIDEO_ORIENTATION_UR_LL:
				transposed = TRUE;
				break;

			default:
				break;
		}

		gst_imx_2d_canvas_calculate_letterbox_margin(
			&(self->letterbox_margin),
			&(self->inner_region),
			&(self->outer_region),
			transposed,
			input_width,
			input_height,
			GST_VIDEO_INFO_PAR_N(&(self->input_video_info)),
			GST_VIDEO_INFO_PAR_D(&(self->input_video_info))
		);

		self->combined_margin.left_margin += self->letterbox_margin.left_margin;
		self->combined_margin.top_margin += self->letterbox_margin.top_margin;
		self->combined_margin.right_margin += self->letterbox_margin.right_margin;
		self->combined_margin.bottom_margin += self->letterbox_margin.bottom_margin;
	}
	else
		memcpy(&(self->inner_region), &(self->outer_region), sizeof(Imx2dRegion));

	GST_DEBUG_OBJECT(self, "calculated inner region: %" IMX_2D_REGION_FORMAT, IMX_2D_REGION_ARGS(&(self->inner_region)));

	/* Mark the coordinates as updated so they are not
	 * needlessly recalculated later. */
	self->region_coords_need_update = FALSE;
}


void gst_imx_2d_video_sink_common_class_init(GstImx2dVideoSinkClass *klass, Imx2dHardwareCapabilities const *capabilities)
{
	GstElementClass *element_class;
	GstCaps *sink_template_caps;
	GstPadTemplate *sink_template;

	element_class = GST_ELEMENT_CLASS(klass);

	sink_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SINK);
	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	gst_element_class_add_pad_template(element_class, sink_template);
}
