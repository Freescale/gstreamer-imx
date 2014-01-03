#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include "egl_platform.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(eglplatform_x11_debug);
#define GST_CAT_DEFAULT eglplatform_x11_debug


struct _GstImxEglVivSinkEGLPlatform
{
	EGLNativeDisplayType native_display;
	EGLNativeWindowType native_window;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	gboolean internal_window;
	Atom wm_delete_atom;
	GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb;
	gpointer user_context;
	GMutex mutex;
};
 
 
#define EGL_PLATFORM_LOCK(platform) g_mutex_lock(&((platform)->mutex))
#define EGL_PLATFORM_UNLOCK(platform) g_mutex_unlock(&((platform)->mutex))


static void gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling);


static void init_debug_category(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(eglplatform_x11_debug, "eglplatform_x11", 0, "eglvivsink X11 platform");
		initialized = TRUE;
	}

	XInitThreads();
}


static char const *gst_imx_egl_viv_sink_egl_platform_get_error_string(void)
{
	EGLint err = eglGetError();
	if (err == EGL_SUCCESS)
		return "success";

	switch (err)
	{
		case EGL_NOT_INITIALIZED: return "not initialized";
		case EGL_BAD_ACCESS: return "bad access";
		case EGL_BAD_ALLOC: return "bad alloc";
		case EGL_BAD_ATTRIBUTE: return "bad attribute";
		case EGL_BAD_CONTEXT: return "bad context";
		case EGL_BAD_CONFIG: return "bad config";
		case EGL_BAD_CURRENT_SURFACE: return "bad current surface";
		case EGL_BAD_DISPLAY: return "bad display";
		case EGL_BAD_SURFACE: return "bad surface";
		case EGL_BAD_MATCH: return "bad match";
		case EGL_BAD_PARAMETER: return "bad parameter";
		case EGL_BAD_NATIVE_PIXMAP: return "bad native pixmap";
		case EGL_BAD_NATIVE_WINDOW: return "bad native window";
		case EGL_CONTEXT_LOST: return "context lost";
		default: return "<unknown error>";
	}
}


GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, gpointer user_context)
{
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;
	Display *x11_display;

	init_debug_category();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->user_context = user_context;

	g_mutex_init(&(platform->mutex));

	x11_display = XOpenDisplay(native_display_name);
	if (x11_display == NULL)
	{
		GST_ERROR("could not open X display");
		g_free(platform);
		return NULL;
	}

	platform->native_display = (EGLNativeDisplayType)x11_display;

	platform->egl_display = eglGetDisplay(platform->native_display);
	if (platform->egl_display == EGL_NO_DISPLAY)
	{
		GST_ERROR("eglGetDisplay failed: %s", gst_imx_egl_viv_sink_egl_platform_get_error_string());
		XCloseDisplay(x11_display);
		g_free(platform);
		return NULL;
	}

	if (!eglInitialize(platform->egl_display, &ver_major, &ver_minor))
	{
		GST_ERROR("eglInitialize failed: %s", gst_imx_egl_viv_sink_egl_platform_get_error_string());
		XCloseDisplay(x11_display);
		g_free(platform);
		return NULL;
	}

	GST_INFO("X11 EGL platform initialized, using EGL %d.%d", ver_major, ver_minor);

	return platform;
}


void gst_imx_egl_viv_sink_egl_platform_destroy(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform != NULL)
	{
		g_mutex_clear(&(platform->mutex));

		if (platform->egl_display != EGL_NO_DISPLAY)
			eglTerminate(platform->egl_display);
		if (platform->native_display != NULL)
			XCloseDisplay((Display*)(platform->native_display));
		g_free(platform);
	}
}


gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, guintptr window_handle, gboolean event_handling, GstVideoInfo *video_info, gboolean fullscreen)
{
	EGLint num_configs;
	EGLConfig config;
	Window x11_window;

	Display *x11_display = (Display *)(platform->native_display);

	static EGLint const eglconfig_attribs[] =
	{
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	static EGLint const ctx_attribs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	if (!eglChooseConfig(platform->egl_display, eglconfig_attribs, &config, 1, &num_configs))
	{
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_error_string());
		return FALSE;
	}

	EGL_PLATFORM_LOCK(platform);

	if (window_handle != 0)
	{
		platform->native_window = window_handle;
		platform->internal_window = FALSE;
		/* TODO: select EGL config with matching visual */

		gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(platform, event_handling);
	}
	else
	{
		EGLint native_visual_id;
		XVisualInfo visual_info_template;
		XVisualInfo *visual_info;
		int num_matching_visuals;
		XSetWindowAttributes attr;
		int screen_num;
		Window root_window;
		Atom net_wm_state_atom, net_wm_state_fullscreen_atom;

		if (!eglGetConfigAttrib(platform->egl_display, config, EGL_NATIVE_VISUAL_ID, &native_visual_id))
		{
			GST_ERROR("eglGetConfigAttrib failed: %s", gst_imx_egl_viv_sink_egl_platform_get_error_string());
			EGL_PLATFORM_UNLOCK(platform);
			return FALSE;
		}

		screen_num = DefaultScreen(x11_display);
		root_window = RootWindow(x11_display, screen_num);

		memset(&visual_info_template, 0, sizeof(visual_info_template));
		visual_info_template.visualid = native_visual_id;

		visual_info = XGetVisualInfo(x11_display, VisualIDMask, &visual_info_template, &num_matching_visuals);
		if (visual_info == NULL)
		{
			GST_ERROR("Could not get visual info for native visual ID %d", native_visual_id);
			EGL_PLATFORM_UNLOCK(platform);
			return FALSE;
		}

		memset(&attr, 0, sizeof(attr));
		attr.background_pixmap = None;
		attr.background_pixel  = BlackPixel(x11_display, screen_num);
		attr.border_pixmap     = CopyFromParent;
		attr.border_pixel      = BlackPixel(x11_display, screen_num);
		attr.backing_store     = NotUseful;
		attr.override_redirect = False;
		attr.cursor            = None;

		// TODO: xlib error handler

		x11_window = XCreateWindow(
			x11_display, root_window,
			0, 0, GST_VIDEO_INFO_WIDTH(video_info), GST_VIDEO_INFO_HEIGHT(video_info),
			0, visual_info->depth, InputOutput, visual_info->visual,
			CWBackPixel | CWColormap  | CWBorderPixel | CWBackingStore | CWOverrideRedirect,
			&attr
		);

		platform->native_window = (EGLNativeWindowType)x11_window;
		platform->internal_window = TRUE;

		net_wm_state_atom = XInternAtom(x11_display, "_NET_WM_STATE", True);
		net_wm_state_fullscreen_atom = XInternAtom(x11_display, "_NET_WM_STATE_FULLSCREEN", True);

		platform->wm_delete_atom = XInternAtom(x11_display, "WM_DELETE_WINDOW", True);
		XSetWMProtocols(x11_display, x11_window, &(platform->wm_delete_atom), 1);

		XStoreName(x11_display, x11_window, "eglvivsink window");
		gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(platform, event_handling);

		XSizeHints sizehints;
		sizehints.x = 0;
		sizehints.y = 0;
		sizehints.width  = GST_VIDEO_INFO_WIDTH(video_info);
		sizehints.height = GST_VIDEO_INFO_HEIGHT(video_info);
		sizehints.flags = PPosition | PSize;
		XSetNormalHints(x11_display, x11_window, &sizehints);

		if (fullscreen)
		{
			XChangeProperty(
				x11_display, x11_window,
				net_wm_state_atom,
				XA_ATOM, 32, PropModeReplace,
				(unsigned char*)&net_wm_state_fullscreen_atom, 1
			);
		}

		XClearWindow(x11_display, x11_window);
		XMapRaised(x11_display, x11_window);

		if (fullscreen)
		{
			XEvent event;
			event.type = ClientMessage;
			event.xclient.window = x11_window;
			event.xclient.message_type = net_wm_state_atom;
			event.xclient.format = 32;
			event.xclient.data.l[0] = 1;
			event.xclient.data.l[1] = net_wm_state_fullscreen_atom;
			event.xclient.data.l[3] = 0l;

			XSendEvent(
				x11_display,
				root_window,
				0,
				SubstructureNotifyMask,
				&event
			);
		}

		XSync(x11_display, False);
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	platform->egl_context = eglCreateContext(platform->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
	platform->egl_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_window, NULL);

	eglMakeCurrent(platform->egl_display, platform->egl_surface, platform->egl_surface, platform->egl_context);

	{
		XWindowAttributes window_attr;
		XGetWindowAttributes(x11_display, x11_window, &window_attr);

		if (platform->window_resized_event_cb != NULL)
			platform->window_resized_event_cb(platform, window_attr.width, window_attr.height, platform->user_context);
		else
			glViewport(0, 0, window_attr.width, window_attr.height);
	}

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_shutdown_window(GstImxEglVivSinkEGLPlatform *platform)
{
	Display *x11_display = (Display *)(platform->native_display);
	Window x11_window = (Window)(platform->native_window);

	if (platform->native_window == 0)
		return TRUE;

	eglMakeCurrent(platform->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (platform->egl_context != EGL_NO_CONTEXT)
		eglDestroyContext(platform->egl_display, platform->egl_context);

	if (platform->egl_surface != EGL_NO_SURFACE)
		eglDestroySurface(platform->egl_display, platform->egl_surface);

	if (platform->egl_display != EGL_NO_DISPLAY)
		eglTerminate(platform->egl_display);

	platform->egl_display = EGL_NO_DISPLAY;
	platform->egl_context = EGL_NO_CONTEXT;
	platform->egl_surface = EGL_NO_SURFACE;

	EGL_PLATFORM_LOCK(platform);

	XSelectInput(x11_display, x11_window, 0);

	while (XPending(x11_display))
	{
		XEvent xevent;
		XNextEvent(x11_display, &xevent);
	}

	if (platform->internal_window)
		XDestroyWindow(x11_display, x11_window);

	platform->native_window = 0;

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


static void gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling)
{
	Window x11_window;
	Display *x11_display = (Display *)(platform->native_display);

	static long const basic_event_mask = ExposureMask | StructureNotifyMask | PointerMotionMask | KeyPressMask | KeyReleaseMask;

	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot set event handling");
		return;
	}
	x11_window = (Window)(platform->native_window);

	if (event_handling)
	{
		if (platform->internal_window)
		{
			XSelectInput(x11_display, x11_window, basic_event_mask | ButtonPressMask | ButtonReleaseMask);
		}
		else
		{
			XSelectInput(x11_display, x11_window, basic_event_mask);
		}
	}
	else
	{ 
		XSelectInput(x11_display, x11_window, 0);
	}
}


void gst_imx_egl_viv_sink_egl_platform_set_event_handling(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling)
{
	EGL_PLATFORM_LOCK(platform);
	gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(platform, event_handling);
	EGL_PLATFORM_UNLOCK(platform);
}


void gst_imx_egl_viv_sink_egl_platform_set_video_info(GstImxEglVivSinkEGLPlatform *platform, GstVideoInfo *video_info)
{
	Window x11_window;

	EGL_PLATFORM_LOCK(platform);
	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot set video info");
		EGL_PLATFORM_UNLOCK(platform);
		return;
	}

	x11_window = (Window)(platform->native_window);

	XResizeWindow((Display *)(platform->native_display), x11_window, GST_VIDEO_INFO_WIDTH(video_info), GST_VIDEO_INFO_HEIGHT(video_info));

	EGL_PLATFORM_UNLOCK(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_expose(GstImxEglVivSinkEGLPlatform *platform)
{
	Window x11_window;
	Display *x11_display = (Display *)(platform->native_display);

	EGL_PLATFORM_LOCK(platform);

	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot expose");
		EGL_PLATFORM_UNLOCK(platform);
		return TRUE;
	}
	x11_window = (Window)(platform->native_window);

	XClientMessageEvent dummy_event;
	memset(&dummy_event, 0, sizeof(dummy_event));
	dummy_event.type = ClientMessage;
	dummy_event.window = x11_window;
	dummy_event.format = 32;
	XSendEvent(x11_display, x11_window, 0, 0, (XEvent *)(&dummy_event));
	XFlush(x11_display);

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


GstImxEglVivSinkHandleEventsRetval gst_imx_egl_viv_sink_egl_platform_handle_events(GstImxEglVivSinkEGLPlatform *platform)
{
	Display *x11_display = (Display *)(platform->native_display);
	gboolean expose_required = FALSE;

	{
		/* handle X11 events */

		{
			XEvent xevent;

			XNextEvent(x11_display, &xevent);

			switch (xevent.type)
			{
				case Expose:
					expose_required = TRUE;
					break;

				case ClientMessage:
					if ((xevent.xclient.format == 32) && (xevent.xclient.data.l[0] == (long)(platform->wm_delete_atom)))
						return GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_WINDOW_CLOSED;
					else if ((xevent.xclient.format == 32) && (xevent.xclient.data.l[0] == 0))
						expose_required = TRUE;
					break;

				case ConfigureNotify:
					GST_DEBUG("received ConfigureNotify event -> calling resize callback");
					if (platform->window_resized_event_cb != NULL)
						platform->window_resized_event_cb(platform, xevent.xconfigure.width, xevent.xconfigure.height, platform->user_context);
					expose_required = TRUE;
					break;

				case ResizeRequest:
					GST_DEBUG("received ResizeRequest event -> calling resize callback");
					if (platform->window_resized_event_cb != NULL)
						platform->window_resized_event_cb(platform, xevent.xresizerequest.width, xevent.xresizerequest.height, platform->user_context);
					expose_required = TRUE;
					break;

				default:
					break;
			}
		}
	}

	return expose_required ? GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_EXPOSE_REQUIRED : GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_OK;
}


void gst_imx_egl_viv_sink_egl_platform_swap_buffers(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform->native_window != 0)
		eglSwapBuffers(platform->egl_display, platform->egl_surface);
}

