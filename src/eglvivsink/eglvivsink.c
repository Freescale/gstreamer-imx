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


#include <config.h>

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/navigation.h>
#include <gst/video/videooverlay.h>

#include "eglvivsink.h"




GST_DEBUG_CATEGORY_STATIC(imx_egl_viv_sink_debug);
#define GST_CAT_DEFAULT imx_egl_viv_sink_debug


enum
{
	PROP_0,
	PROP_FULLSCREEN,
	PROP_FORCE_ASPECT_RATIO,
	PROP_NATIVE_DISPLAY,
	PROP_WINDOW_X_COORD,
	PROP_WINDOW_Y_COORD,
	PROP_WINDOW_WIDTH,
	PROP_WINDOW_HEIGHT,
	PROP_BORDERLESS_WINDOW
};


#define DEFAULT_FULLSCREEN FALSE
#define DEFAULT_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_NATIVE_DISPLAY NULL
#define DEFAULT_WINDOW_X_COORD 0
#define DEFAULT_WINDOW_Y_COORD 0
#define DEFAULT_WINDOW_WIDTH 0
#define DEFAULT_WINDOW_HEIGHT 0
#define DEFAULT_BORDERLESS_WINDOW FALSE


#ifdef HAVE_VIV_I420
	#define CAPS_VIV_I420 "I420, "
#else
	#define CAPS_VIV_I420 ""
#endif

#ifdef HAVE_VIV_YV12
	#define CAPS_VIV_YV12 "YV12, "
#else
	#define CAPS_VIV_YV12 ""
#endif

#ifdef HAVE_VIV_YV21
	#define CAPS_VIV_YV21 "YV21, "
#else
	#define CAPS_VIV_YV21 ""
#endif

#ifdef HAVE_VIV_NV12
	#define CAPS_VIV_NV12 "NV12, "
#else
	#define CAPS_VIV_NV12 ""
#endif

#ifdef HAVE_VIV_NV21
	#define CAPS_VIV_NV21 "NV21, "
#else
	#define CAPS_VIV_NV21 ""
#endif

#ifdef HAVE_VIV_UYVY
	#define CAPS_VIV_UYVY "UYVY, "
#else
	#define CAPS_VIV_UYVY ""
#endif


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ "
		CAPS_VIV_I420
		CAPS_VIV_YV12
		CAPS_VIV_YV21
		CAPS_VIV_NV12
		CAPS_VIV_NV21
		CAPS_VIV_UYVY
		"RGB16, RGBA, BGRA, RGBx, BGRx, BGR, ARGB, ABGR, xRGB, xBGR"
	" }"))
);


static void gst_imx_egl_viv_sink_navigation_init(GstNavigationInterface *iface);
static void gst_imx_egl_viv_sink_video_overlay_init(GstVideoOverlayInterface *iface);

static void gst_imx_egl_viv_sink_navigation_send_event(GstNavigation *navigation, GstStructure *structure);
static void gst_imx_egl_viv_sink_set_window_handle(GstVideoOverlay *overlay, guintptr window_handle);
static void gst_imx_egl_viv_sink_expose(GstVideoOverlay *overlay);
static void gst_imx_egl_viv_sink_set_event_handling(GstVideoOverlay *overlay, gboolean handle_events);



G_DEFINE_TYPE_WITH_CODE(
	GstImxEglVivSink, gst_imx_egl_viv_sink, GST_TYPE_VIDEO_SINK,
	G_IMPLEMENT_INTERFACE(GST_TYPE_NAVIGATION, gst_imx_egl_viv_sink_navigation_init);
	G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_OVERLAY, gst_imx_egl_viv_sink_video_overlay_init);
)


static void gst_imx_egl_viv_sink_finalize(GObject *object);
static void gst_imx_egl_viv_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_egl_viv_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_egl_viv_sink_change_state(GstElement *element, GstStateChange state_change);
static gboolean gst_imx_egl_viv_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_egl_viv_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_imx_egl_viv_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);




/* required function declared by G_DEFINE_TYPE */

void gst_imx_egl_viv_sink_class_init(GstImxEglVivSinkClass *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_egl_viv_sink_debug, "imxeglvivsink", 0, "Freescale i.MX EGL video sink");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_BASE_SINK_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));

	base_class->set_caps = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_set_caps);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_propose_allocation);
	parent_class->show_frame = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_show_frame);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_change_state);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_egl_viv_sink_get_property);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale EGL video sink",
		"Sink/Video",
		"Video output using the i.MX6 Vivante GPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	g_object_class_install_property(
		object_class,
		PROP_FULLSCREEN,
		g_param_spec_boolean(
			"fullscreen",
			"Fullscreen mode",
			"Whether or not to set the created window to fullscreen mode (ignored if application provides a window handle)",
			DEFAULT_FULLSCREEN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_NATIVE_DISPLAY,
		g_param_spec_string(
			"native-display",
			"Native display identifier",
			"String identifying the display to use (default value uses the default display)",
			DEFAULT_NATIVE_DISPLAY,
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
		PROP_BORDERLESS_WINDOW,
		g_param_spec_boolean(
			"borderless-window",
			"Borderless window",
			"Disable window borders, bypassing any window manager",
			DEFAULT_BORDERLESS_WINDOW,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_egl_viv_sink_init(GstImxEglVivSink *egl_viv_sink)
{
	egl_viv_sink->gles2_renderer = NULL;
	egl_viv_sink->window_handle = 0;
	egl_viv_sink->handle_events = TRUE;
	egl_viv_sink->fullscreen = DEFAULT_FULLSCREEN;
	egl_viv_sink->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
	egl_viv_sink->native_display_name = DEFAULT_NATIVE_DISPLAY;
	g_mutex_init(&(egl_viv_sink->renderer_access_mutex));
}


static void gst_imx_egl_viv_sink_finalize(GObject *object)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(object);
	g_mutex_clear(&(egl_viv_sink->renderer_access_mutex));
	g_free(egl_viv_sink->native_display_name);
	G_OBJECT_CLASS(gst_imx_egl_viv_sink_parent_class)->finalize(object);
}


static void gst_imx_egl_viv_sink_navigation_init(GstNavigationInterface *iface)
{
	iface->send_event = gst_imx_egl_viv_sink_navigation_send_event;
}


static void gst_imx_egl_viv_sink_video_overlay_init(GstVideoOverlayInterface *iface)
{
	iface->set_window_handle = gst_imx_egl_viv_sink_set_window_handle;
	iface->expose            = gst_imx_egl_viv_sink_expose;
	iface->handle_events     = gst_imx_egl_viv_sink_set_event_handling;
}


static void gst_imx_egl_viv_sink_navigation_send_event(G_GNUC_UNUSED GstNavigation *navigation, G_GNUC_UNUSED GstStructure *structure)
{
	/* TODO: navigation support in eglvivsink */
}


static void gst_imx_egl_viv_sink_set_window_handle(GstVideoOverlay *overlay, guintptr window_handle)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(overlay);

	g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

	egl_viv_sink->window_handle = window_handle;
	if (egl_viv_sink->gles2_renderer != NULL)
		gst_imx_egl_viv_sink_gles2_renderer_set_window_handle(egl_viv_sink->gles2_renderer, window_handle);

	g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
}


static void gst_imx_egl_viv_sink_expose(GstVideoOverlay *overlay)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(overlay);

	g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

	if ((egl_viv_sink->gles2_renderer != NULL) && (gst_imx_egl_viv_sink_gles2_renderer_is_started(egl_viv_sink->gles2_renderer)))
		gst_imx_egl_viv_sink_gles2_renderer_expose(egl_viv_sink->gles2_renderer);

	g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
}


static void gst_imx_egl_viv_sink_set_event_handling(GstVideoOverlay *overlay, gboolean handle_events)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(overlay);

	g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

	egl_viv_sink->handle_events = handle_events;
	if (egl_viv_sink->gles2_renderer != NULL)
		gst_imx_egl_viv_sink_gles2_renderer_set_event_handling(egl_viv_sink->gles2_renderer, handle_events);

	g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
}


static void gst_imx_egl_viv_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(object);

	switch (prop_id)
	{
		case PROP_FULLSCREEN:
		{
			gboolean new_fs = g_value_get_boolean(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (new_fs != egl_viv_sink->fullscreen)
				{
					egl_viv_sink->fullscreen = new_fs;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_fullscreen(egl_viv_sink->gles2_renderer, new_fs);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_FORCE_ASPECT_RATIO:
		{
			gboolean b = g_value_get_boolean(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (b != egl_viv_sink->force_aspect_ratio)
				{
					egl_viv_sink->force_aspect_ratio = b;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_force_aspect_ratio(egl_viv_sink->gles2_renderer, b);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_NATIVE_DISPLAY:
		{
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				gchar const *new_display_name = g_value_get_string(value);
				g_free(egl_viv_sink->native_display_name);
				egl_viv_sink->native_display_name = g_strdup(new_display_name);
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;
		}

		case PROP_WINDOW_X_COORD:
		{
			gint i = g_value_get_int(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (i != egl_viv_sink->window_x_coord)
				{
					egl_viv_sink->window_x_coord = i;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_window_coords(egl_viv_sink->gles2_renderer, i, egl_viv_sink->window_y_coord);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_WINDOW_Y_COORD:
		{
			gint i = g_value_get_int(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (i != egl_viv_sink->window_y_coord)
				{
					egl_viv_sink->window_y_coord = i;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_window_coords(egl_viv_sink->gles2_renderer, egl_viv_sink->window_x_coord, i);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_WINDOW_WIDTH:
		{
			guint i = g_value_get_uint(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (i != egl_viv_sink->window_width)
				{
					egl_viv_sink->window_width = i;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_window_size(egl_viv_sink->gles2_renderer, i, egl_viv_sink->window_height);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_WINDOW_HEIGHT:
		{
			guint i = g_value_get_uint(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (i != egl_viv_sink->window_height)
				{
					egl_viv_sink->window_height = i;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_window_size(egl_viv_sink->gles2_renderer, egl_viv_sink->window_width, i);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case PROP_BORDERLESS_WINDOW:
		{
			gboolean b = g_value_get_boolean(value);

			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			{
				if (b != egl_viv_sink->borderless_window)
				{
					egl_viv_sink->borderless_window = b;
					if (egl_viv_sink->gles2_renderer != NULL)
						gst_imx_egl_viv_sink_gles2_renderer_set_borderless_window(egl_viv_sink->gles2_renderer, b);
				}
			}
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_egl_viv_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(object);

	switch (prop_id)
	{
		case PROP_FULLSCREEN:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_boolean(value, egl_viv_sink->fullscreen);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_FORCE_ASPECT_RATIO:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_boolean(value, egl_viv_sink->force_aspect_ratio);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_NATIVE_DISPLAY:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_string(value, egl_viv_sink->native_display_name);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_WINDOW_X_COORD:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_int(value, egl_viv_sink->window_x_coord);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_WINDOW_Y_COORD:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_int(value, egl_viv_sink->window_y_coord);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_WINDOW_WIDTH:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_uint(value, egl_viv_sink->window_width);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_WINDOW_HEIGHT:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_uint(value, egl_viv_sink->window_height);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		case PROP_BORDERLESS_WINDOW:
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			g_value_set_boolean(value, egl_viv_sink->borderless_window);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_egl_viv_sink_change_state(GstElement *element, GstStateChange state_change)
{
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	switch (state_change)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
			egl_viv_sink->gles2_renderer = gst_imx_egl_viv_sink_gles2_renderer_create(egl_viv_sink->native_display_name);
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			if (egl_viv_sink->gles2_renderer == NULL)
				return GST_STATE_CHANGE_FAILURE;

			break;
		}

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_egl_viv_sink_parent_class)->change_state(element, state_change);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (state_change)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

			if (egl_viv_sink->gles2_renderer != NULL)
			{
				gst_imx_egl_viv_sink_gles2_renderer_destroy(egl_viv_sink->gles2_renderer);
				egl_viv_sink->gles2_renderer = NULL;
			}

			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

			break;
		}

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			break;

		default:
			break;
	}


	return ret;
}


static gboolean gst_imx_egl_viv_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	gboolean ret = TRUE;
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(sink);

	if (!gst_video_info_from_caps(&(egl_viv_sink->video_info), caps))
	{
		GST_ERROR_OBJECT(sink, "could not get video info from caps");
		return FALSE;
	}

	g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

	/* The code below relies on short-circuit evaluation; if one of the functions fail,
	 * ret will be FALSE, and therefore subsequent functions will not be evaluated
	 * (the C standard defines && as a sequence point) */
	if (!gst_imx_egl_viv_sink_gles2_renderer_is_started(egl_viv_sink->gles2_renderer))
	{
		/* Unlock renderer access mutex for the prepare_window_handle call.
		 * This call will post a "prepare-window-handle" bus message, which
		 * in may cause the application to call gst_video_overlay_set_window_handle().
		 * This in turn will eventually call gst_imx_egl_viv_sink_set_window_handle().
		 * That function locks the renderer access mutex internally, so if it is
		 * already locked, it causes a deadlock. Furthermore, while the application
		 * handles the message, it may want to set some of the renderer's properties,
		 * which again would deadlock with an already-locked renderer access mutex. */
		{
			g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));
			gst_video_overlay_prepare_window_handle(GST_VIDEO_OVERLAY(egl_viv_sink));
			g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));
		}

		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_window_handle(egl_viv_sink->gles2_renderer, egl_viv_sink->window_handle);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_event_handling(egl_viv_sink->gles2_renderer, egl_viv_sink->handle_events);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_video_info(egl_viv_sink->gles2_renderer, &(egl_viv_sink->video_info));
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_fullscreen(egl_viv_sink->gles2_renderer, egl_viv_sink->fullscreen);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_force_aspect_ratio(egl_viv_sink->gles2_renderer, egl_viv_sink->force_aspect_ratio);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_window_coords(egl_viv_sink->gles2_renderer, egl_viv_sink->window_x_coord, egl_viv_sink->window_y_coord);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_window_size(egl_viv_sink->gles2_renderer, egl_viv_sink->window_width, egl_viv_sink->window_height);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_borderless_window(egl_viv_sink->gles2_renderer, egl_viv_sink->borderless_window);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_start(egl_viv_sink->gles2_renderer);
	}
	else
	{
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_set_video_info(egl_viv_sink->gles2_renderer, &(egl_viv_sink->video_info));
	}

	g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

	return ret;
}


static gboolean gst_imx_egl_viv_propose_allocation(GstBaseSink *sink, GstQuery *query)
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


static GstFlowReturn gst_imx_egl_viv_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstFlowReturn ret = GST_FLOW_OK;
	GstImxEglVivSink *egl_viv_sink = GST_IMX_EGL_VIV_SINK(video_sink);

	g_mutex_lock(&(egl_viv_sink->renderer_access_mutex));

	if ((egl_viv_sink->gles2_renderer != NULL) && (gst_imx_egl_viv_sink_gles2_renderer_is_started(egl_viv_sink->gles2_renderer)))
		ret = gst_imx_egl_viv_sink_gles2_renderer_show_frame(egl_viv_sink->gles2_renderer, buf);

	g_mutex_unlock(&(egl_viv_sink->renderer_access_mutex));

	return ret;
}

