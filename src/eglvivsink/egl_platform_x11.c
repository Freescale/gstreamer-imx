#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include "egl_platform.h"
#include "egl_misc.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(imx_egl_platform_x11_debug);
#define GST_CAT_DEFAULT imx_egl_platform_x11_debug


struct _GstImxEglVivSinkEGLPlatform
{
	EGLNativeDisplayType native_display;
	EGLNativeWindowType native_window;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	Window parent_window;
	Atom wm_delete_atom;
	GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb;
	GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb;
	gpointer user_context;
	GMutex mutex;
	gboolean fullscreen;
	guint fixed_window_width, fixed_window_height, video_width, video_height;
};
 
 
#define EGL_PLATFORM_LOCK(platform) g_mutex_lock(&((platform)->mutex))
#define EGL_PLATFORM_UNLOCK(platform) g_mutex_unlock(&((platform)->mutex))


typedef enum
{
	GSTIMX_EGLX11_CMD_EXPOSE = 1,
	GSTIMX_EGLX11_CMD_CALL_RESIZE_CB = 2,
	GSTIMX_EGLX11_CMD_STOP_MAINLOOP = 3
}
GstImxEGLX11Cmds;


static void gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling);


static void init_debug_category(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(imx_egl_platform_x11_debug, "imxeglplatform_x11", 0, "imxeglvivsink X11 platform");
		initialized = TRUE;

		XInitThreads();
	}
}


GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb, gpointer user_context)
{
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;
	Display *x11_display;

	g_assert(window_resized_event_cb != NULL);
	g_assert(render_frame_cb != NULL);

	init_debug_category();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->render_frame_cb = render_frame_cb;
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
		GST_ERROR("eglGetDisplay failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		XCloseDisplay(x11_display);
		g_free(platform);
		return NULL;
	}

	if (!eglInitialize(platform->egl_display, &ver_major, &ver_minor))
	{
		GST_ERROR("eglInitialize failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
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


gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, guintptr window_handle, gboolean event_handling, GstVideoInfo *video_info, gboolean fullscreen, gint x_coord, gint y_coord, guint width, guint height, gboolean borderless)
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
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	EGL_PLATFORM_LOCK(platform);

	{
		EGLint native_visual_id;
		XVisualInfo visual_info_template;
		XVisualInfo *visual_info;
		int num_matching_visuals;
		XSetWindowAttributes attr;
		int screen_num;
		Window root_window;
		Atom net_wm_state_atom, net_wm_state_fullscreen_atom;
		guint chosen_width, chosen_height;

		GST_INFO("Creating new X11 window with EGL context (parent window: %" G_GUINTPTR_FORMAT ")", window_handle);

		if (!eglGetConfigAttrib(platform->egl_display, config, EGL_NATIVE_VISUAL_ID, &native_visual_id))
		{
			GST_ERROR("eglGetConfigAttrib failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
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
		attr.override_redirect = borderless ? True : False;
		attr.cursor            = None;

		if (window_handle != 0)
		{
			platform->parent_window = (Window)window_handle;
			/* Out of the parent window events, only the structure
			 * notifications are of interest here */
			XSelectInput(x11_display, platform->parent_window, StructureNotifyMask);
		}

		// TODO: xlib error handler

		platform->fixed_window_width = width;
		platform->fixed_window_height = height;

		platform->video_width = GST_VIDEO_INFO_WIDTH(video_info);
		platform->video_height = GST_VIDEO_INFO_HEIGHT(video_info);

		platform->fullscreen = fullscreen;

		/* If either no fixed size is set, or fullscreen is requested, use the video frame size
		 * In the fullscreen case, the size is actually irrelevant, since it will be overwritten
		 * with the screen size. But passing zero for the width/height values is invalid, the
		 * video frame size is used. */
		chosen_width = ((width == 0) || fullscreen) ? platform->video_width : width;
		chosen_height = ((height == 0) || fullscreen) ? platform->video_height : height;

		/* This video output window can be embedded into other windows, for example inside
		 * media player user interfaces. This is done by making the specified window as
		 * the parent of the video playback window. */
		x11_window = XCreateWindow(
			x11_display, (window_handle != 0) ? platform->parent_window : root_window,
			x_coord,
			y_coord,
			chosen_width,
			chosen_height,
			0, visual_info->depth, InputOutput, visual_info->visual,
			CWBackPixel | CWColormap  | CWBorderPixel | CWBackingStore | CWOverrideRedirect,
			&attr
		);

		platform->native_window = (EGLNativeWindowType)x11_window;

		net_wm_state_atom = XInternAtom(x11_display, "_NET_WM_STATE", True);
		net_wm_state_fullscreen_atom = XInternAtom(x11_display, "_NET_WM_STATE_FULLSCREEN", True);

		platform->wm_delete_atom = XInternAtom(x11_display, "WM_DELETE_WINDOW", True);
		XSetWMProtocols(x11_display, x11_window, &(platform->wm_delete_atom), 1);

		XStoreName(x11_display, x11_window, "eglvivsink window");
		gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(platform, event_handling);

		XSizeHints sizehints;
		sizehints.x = 0;
		sizehints.y = 0;
		sizehints.width  = chosen_width;
		sizehints.height = chosen_height;
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

		if (fullscreen || (platform->fixed_window_width != 0) || (platform->fixed_window_height != 0))
		{
			platform->fixed_window_width = window_attr.width;
			platform->fixed_window_height = window_attr.height;
		}

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

	XDestroyWindow(x11_display, x11_window);

	platform->native_window = 0;

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


static void gst_imx_egl_viv_sink_egl_platform_set_event_handling_nolock(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling)
{
	Window x11_window;
	Display *x11_display = (Display *)(platform->native_display);

	/* Select user input events only if it is requested (= when event_handling is TRUE)
	 * Select the StructureNotifyMask only if this window is standalone, because otherwise,
	 * we are interested in structure notifications of the parent (for example, when it gets
	 * resized), to let the event handlers auto-resize this window to fit in the parent one */
	long user_input_mask = event_handling ? (PointerMotionMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask) : 0;
	long window_event_mask = (platform->parent_window != 0) ? 0 : StructureNotifyMask;

	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot set event handling");
		return;
	}
	x11_window = (Window)(platform->native_window);

	XSelectInput(x11_display, x11_window, ExposureMask | window_event_mask | user_input_mask);
}


static void gst_imx_egl_viv_sink_egl_platform_send_cmd(GstImxEglVivSinkEGLPlatform *platform, GstImxEGLX11Cmds cmd)
{
	/* must be called with lock */

	Window x11_window;
	Display *x11_display = (Display *)(platform->native_display);

	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot send cmd");
		EGL_PLATFORM_UNLOCK(platform);
		return;
	}
	x11_window = (Window)(platform->native_window);

	XClientMessageEvent event;
	memset(&event, 0, sizeof(event));
	event.type = ClientMessage;
	event.window = x11_window;
	event.format = 32;
	event.data.l[1] = cmd;
	XSendEvent(x11_display, x11_window, 0, 0, (XEvent *)(&event));
	XFlush(x11_display);
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

	platform->video_width = GST_VIDEO_INFO_WIDTH(video_info);
	platform->video_height = GST_VIDEO_INFO_HEIGHT(video_info);

	if (platform->fullscreen || (platform->fixed_window_width != 0) || (platform->fixed_window_height != 0) || (platform->parent_window != 0))
	{
		/* even though the window itself might not have been resized, the callback
		 * still needs to be invoked, because it depends on both the window and the
		 * video frame sizes */
		if (platform->window_resized_event_cb != NULL)
		{
			// do not call the resize callback here directly; instead, notify the main loop about this change
			// because here, the EGL context is not and cannot be set
			gst_imx_egl_viv_sink_egl_platform_send_cmd(platform, GSTIMX_EGLX11_CMD_CALL_RESIZE_CB);
		}
	}
	else
	{
		/* not calling the resize callback here, since the XResizeWindow() call
		 * creates a resize event that will be handled in the main loop */
		XResizeWindow((Display *)(platform->native_display), x11_window, GST_VIDEO_INFO_WIDTH(video_info), GST_VIDEO_INFO_HEIGHT(video_info));
	}

	EGL_PLATFORM_UNLOCK(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_expose(GstImxEglVivSinkEGLPlatform *platform)
{
	EGL_PLATFORM_LOCK(platform);
	gst_imx_egl_viv_sink_egl_platform_send_cmd(platform, GSTIMX_EGLX11_CMD_EXPOSE);
	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


GstImxEglVivSinkMainloopRetval gst_imx_egl_viv_sink_egl_platform_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	Display *x11_display = (Display *)(platform->native_display);
	gboolean expose_required = TRUE;
	gboolean continue_loop = TRUE;

	while (continue_loop)
	{
		XEvent xevent;
		XNextEvent(x11_display, &xevent);

		/* handle X11 events */
		EGL_PLATFORM_LOCK(platform);

		switch (xevent.type)
		{
			case Expose:
			{
				Window this_window = (Window)(platform->native_window);

				/* In case this window is child of another window that is not the root
				 * window, resize this window; sometimes, ConfigureNotify is not trigger
				 * when the windows show up for the first time*/
				if ((xevent.xexpose.count == 0) && (platform->parent_window != 0))
				{
					Window root_window;
					int x, y;
					unsigned int width, height, border_width, depth;

					XGetGeometry(
						x11_display,
						platform->parent_window,
						&root_window,
						&x, &y,
						&width, &height,
						&border_width,
						&depth
					);

					XResizeWindow(x11_display, this_window, width, height);

					if ((platform->fixed_window_width != 0) || (platform->fixed_window_height != 0))
					{
						platform->fixed_window_width = width;
						platform->fixed_window_height = height;
					}

					if (platform->window_resized_event_cb != NULL)
						platform->window_resized_event_cb(platform, width, height, platform->user_context);
				}

				/* Make sure no more expose events are there */
				while (XCheckTypedWindowEvent(x11_display, platform->parent_window, Expose, &xevent) == True);
				while (XCheckTypedWindowEvent(x11_display, this_window, Expose, &xevent) == True);

				expose_required = TRUE;

				break;
			}

			case ClientMessage:
				if ((xevent.xclient.format == 32) && (xevent.xclient.data.l[0] == (long)(platform->wm_delete_atom)))
				{
					GST_INFO("window got closed");
					EGL_PLATFORM_UNLOCK(platform);
					return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_WINDOW_CLOSED;
				}
				else if ((xevent.xclient.format == 32) && (xevent.xclient.data.l[0] == 0))
				{
					switch (xevent.xclient.data.l[1])
					{
						case GSTIMX_EGLX11_CMD_EXPOSE:
							expose_required = TRUE;
							break;
						case GSTIMX_EGLX11_CMD_CALL_RESIZE_CB:
							if (platform->window_resized_event_cb != NULL)
								platform->window_resized_event_cb(platform, platform->fixed_window_width, platform->fixed_window_height, platform->user_context);
							break;
						case GSTIMX_EGLX11_CMD_STOP_MAINLOOP:
							continue_loop = FALSE;
							break;
						default:
							break;
					}
				}
				break;

			case ConfigureNotify:
			{
				Window this_window = (Window)(platform->native_window);

				GST_TRACE("received ConfigureNotify event -> calling resize callback");

				/* Make sure no other ConfigureNotify events are there */
				while (XCheckTypedWindowEvent(x11_display, platform->parent_window, ConfigureNotify, &xevent) == True);

				/* Resize if this is a child window of a non-root parent window;
				 * this is usually the case when this window is embedded inside another one */
				if (platform->parent_window != 0)
					XResizeWindow(x11_display, this_window, xevent.xconfigure.width, xevent.xconfigure.height);

				if ((platform->fixed_window_width != 0) || (platform->fixed_window_height != 0))
				{
					platform->fixed_window_width = xevent.xconfigure.width;
					platform->fixed_window_height = xevent.xconfigure.height;
				}

				if (platform->window_resized_event_cb != NULL)
					platform->window_resized_event_cb(platform, xevent.xconfigure.width, xevent.xconfigure.height, platform->user_context);

				expose_required = TRUE;
				break;
			}

			default:
				break;
		}

		EGL_PLATFORM_UNLOCK(platform);

		if (expose_required)
		{
			platform->render_frame_cb(platform, platform->user_context);
			eglSwapBuffers(platform->egl_display, platform->egl_surface);
			expose_required = FALSE;
		}
	}

	return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_OK;
}


void gst_imx_egl_viv_sink_egl_platform_stop_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	GST_LOG("sending stop mainloop command");

	EGL_PLATFORM_LOCK(platform);
	gst_imx_egl_viv_sink_egl_platform_send_cmd(platform, GSTIMX_EGLX11_CMD_STOP_MAINLOOP);
	EGL_PLATFORM_UNLOCK(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_coords(GstImxEglVivSinkEGLPlatform *platform, gint x_coord, gint y_coord)
{
	EGL_PLATFORM_LOCK(platform);

	if (platform->parent_window != 0)
	{
		Display *x11_display = (Display *)(platform->native_display);
		Window this_window = (Window)(platform->native_window);
		XMoveWindow(x11_display, this_window, x_coord, y_coord);
	}

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_size(GstImxEglVivSinkEGLPlatform *platform, guint width, guint height)
{
	EGL_PLATFORM_LOCK(platform);

	platform->fixed_window_width = width;
	platform->fixed_window_height = height;

	/* not calling the resize callback here, since the XResizeWindow() call
	 * creates a resize event that will be handled in the main loop */

	if ((platform->fullscreen) || (platform->parent_window != 0))
	{
		// do nothing
	}
	else if ((width != 0) || (height != 0))
	{
		Display *x11_display = (Display *)(platform->native_display);
		Window this_window = (Window)(platform->native_window);
		XResizeWindow(x11_display, this_window, width, height);
	}
	else
	{
		Display *x11_display = (Display *)(platform->native_display);
		Window this_window = (Window)(platform->native_window);
		XResizeWindow(x11_display, this_window, platform->video_width, platform->video_height);
	}

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_borderless(GstImxEglVivSinkEGLPlatform *platform, gboolean borderless)
{
	XSetWindowAttributes attr;
	Display *x11_display = (Display *)(platform->native_display);
	Window this_window = (Window)(platform->native_window);

	attr.override_redirect = borderless ? True : False;

	EGL_PLATFORM_LOCK(platform);

	XChangeWindowAttributes(x11_display, this_window, CWOverrideRedirect, &attr);
	XRaiseWindow(x11_display, this_window);

	EGL_PLATFORM_UNLOCK(platform);

	return TRUE;
}

