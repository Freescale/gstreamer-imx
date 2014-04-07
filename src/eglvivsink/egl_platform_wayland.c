#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <wayland-egl.h>

#include "egl_platform.h"
#include "egl_misc.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(imx_egl_platform_wl_debug);
#define GST_CAT_DEFAULT imx_egl_platform_wl_debug


//#define USE_CALLBACK_BASED_RENDERING


enum
{
	GSTIMX_EGLWAYLAND_STOP = 1,
	GSTIMX_EGLWAYLAND_RENDER = 2,
	GSTIMX_EGLWAYLAND_RESIZE = 3
};


struct _GstImxEglVivSinkEGLPlatform
{
	EGLNativeDisplayType native_display;
	EGLNativeWindowType native_window;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb;
	GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb;
	gpointer user_context;

	gboolean do_render;

	struct wl_display *display;
	struct wl_registry *registry;
	int display_fd;
	struct wl_compositor *compositor;
	struct wl_shell *shell;

	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;

	struct wl_callback *frame_cb;

	int ctrl_pipe[2];
};


static void log_handler(const char *format, va_list args)
{
	gst_debug_log_valist(imx_egl_platform_wl_debug, GST_LEVEL_LOG, __FILE__, __func__, __LINE__, NULL, format, args);
}


static void static_global_init(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(imx_egl_platform_wl_debug, "imxeglplatform_wl", 0, "imxeglvivsink Wayland platform");

		wl_log_set_handler_client(log_handler);

		initialized = TRUE;
	}
}


static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, char const *interface, G_GNUC_UNUSED /* TODO */ uint32_t version)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	if (g_strcmp0(interface, "wl_compositor") == 0)
		platform->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	else if (g_strcmp0(interface, "wl_shell") == 0)
		platform->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
}

static void registry_handle_global_remove(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_registry *registry, G_GNUC_UNUSED uint32_t name)
{
}

static const struct wl_registry_listener registry_listener =
{
	registry_handle_global,
	registry_handle_global_remove
};


static void handle_ping(G_GNUC_UNUSED void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void handle_configure(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_shell_surface *shell_surface, G_GNUC_UNUSED uint32_t edges, G_GNUC_UNUSED int32_t width, G_GNUC_UNUSED int32_t height)
{
}

static void handle_popup_done(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener =
{
	handle_ping,
	handle_configure,
	handle_popup_done
};


static void frame_callback(void *data, struct wl_callback *callback, uint32_t time);

static const struct wl_callback_listener frame_listener =
{
	frame_callback
};

static void frame_callback(void *data, struct wl_callback *callback, G_GNUC_UNUSED uint32_t time)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	/* Cleanup old callback */
	if (callback)
		wl_callback_destroy(callback);

	if (!platform->do_render)
		return;

	/* The actual rendering */
	if (platform->render_frame_cb != NULL)
		platform->render_frame_cb(platform, platform->user_context);

	/* Setup new callback */
	platform->frame_cb = wl_surface_frame(platform->surface);
	wl_callback_add_listener(platform->frame_cb, &frame_listener, platform);

	/* Finally, do the actual commit to the server */
	if (platform->render_frame_cb != NULL)
		eglSwapBuffers(platform->egl_display, platform->egl_surface);
}





GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(G_GNUC_UNUSED /* TODO */ gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb, gpointer user_context)
{
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;

	static_global_init();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->render_frame_cb = render_frame_cb;
	platform->user_context = user_context;

	platform->ctrl_pipe[0] = -1;
	platform->ctrl_pipe[1] = -1;
	if (pipe(platform->ctrl_pipe) == -1)
	{
		GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
		goto cleanup;
	}

	platform->display = wl_display_connect(NULL);
	if (platform->display == NULL)
	{
		GST_ERROR("wl_display_connect failed: %s", strerror(errno));
		goto cleanup;
	}

	platform->registry = wl_display_get_registry(platform->display);
	wl_registry_add_listener(platform->registry, &registry_listener, platform);

	if (wl_display_dispatch(platform->display) == -1)
	{
		GST_ERROR("wl_display_dispatch failed: %s", strerror(errno));
		goto cleanup;
	}

	platform->display_fd = wl_display_get_fd(platform->display);

	platform->egl_display = eglGetDisplay(platform->display);
	if (platform->egl_display == EGL_NO_DISPLAY)
	{
		GST_ERROR("eglGetDisplay failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto cleanup;
	}

	if (!eglInitialize(platform->egl_display, &ver_major, &ver_minor))
	{
		GST_ERROR("eglInitialize failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		g_free(platform);
		goto cleanup;
	}

	GST_INFO("Wayland EGL platform initialized, using EGL %d.%d", ver_major, ver_minor);

	return platform;


cleanup:
	/* either both are set, or none is */
	if (platform->ctrl_pipe[0] != -1)
	{
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
	}

	if (platform->display != NULL)
	{
		wl_display_flush(platform->display);
		wl_display_disconnect(platform->display);
	}

	g_free(platform);
	return NULL;
}


void gst_imx_egl_viv_sink_egl_platform_destroy(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform == NULL)
		return;

	eglMakeCurrent(platform->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (platform->egl_display != EGL_NO_DISPLAY)
		eglTerminate(platform->egl_display);

	// TODO: should eglReleaseThread() be called?

	if (platform->shell != NULL)
		wl_shell_destroy(platform->shell);

	if (platform->compositor != NULL)
		wl_compositor_destroy(platform->compositor);

	wl_display_flush(platform->display);
	wl_display_disconnect(platform->display);

	/* either both are set, or none is */
	if (platform->ctrl_pipe[0] != -1)
	{
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
	}

	g_free(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED guintptr window_handle, G_GNUC_UNUSED gboolean event_handling, G_GNUC_UNUSED GstVideoInfo *video_info, G_GNUC_UNUSED gboolean fullscreen, gint x_coord, gint y_coord, guint width, guint height, G_GNUC_UNUSED gboolean borderless)
{
	EGLint num_configs;
	EGLConfig config;
	int actual_x, actual_y, actual_width, actual_height;

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


	if (platform->compositor == NULL)
	{
		GST_ERROR("compositor pointer is NULL");
		return FALSE;
	}

	if (platform->shell == NULL)
	{
		GST_ERROR("shell pointer is NULL");
		return FALSE;
	}


	if ((platform->surface = wl_compositor_create_surface(platform->compositor)) == NULL)
	{
		GST_ERROR("creating Wayland surface failed");
		return FALSE;
	}

#ifdef USE_CALLBACK_BASED_RENDERING
	platform->frame_cb = wl_surface_frame(platform->surface);
	wl_callback_add_listener(platform->frame_cb, &frame_listener, platform);
#endif

	if ((platform->shell_surface = wl_shell_get_shell_surface(platform->shell, platform->surface)) == NULL)
	{
		GST_ERROR("creating Wayland shell surface failed");
		return FALSE;
	}


	if (!eglChooseConfig(platform->egl_display, eglconfig_attribs, &config, 1, &num_configs))
	{
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	wl_shell_surface_add_listener(platform->shell_surface, &shell_surface_listener, platform);

	wl_shell_surface_set_toplevel(platform->shell_surface);

	if (!eglChooseConfig(platform->egl_display, eglconfig_attribs, &config, 1, &num_configs))
	{
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	width = (width != 0) ? (gint)width : GST_VIDEO_INFO_WIDTH(video_info);
	height = (height != 0) ? (gint)height : GST_VIDEO_INFO_HEIGHT(video_info);

	platform->native_window = wl_egl_window_create(platform->surface, width, height);
	if (platform->native_window == NULL)
	{
		GST_ERROR("wl_egl_window_create failed %d %d", width, height);
		return FALSE;
	}

	actual_x = x_coord;
	actual_y = y_coord;
	actual_width = width;
	actual_height = height;

	if (!eglBindAPI(EGL_OPENGL_ES_API))
	{
		GST_ERROR("eglBindAPI failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	platform->egl_context = eglCreateContext(platform->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
	if (platform->egl_context == EGL_NO_CONTEXT)
	{
		GST_ERROR("eglCreateContext failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	platform->egl_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_window, NULL);
	if (platform->egl_surface == EGL_NO_SURFACE)
	{
		GST_ERROR("eglCreateWindowSurface failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	if (!eglMakeCurrent(platform->egl_display, platform->egl_surface, platform->egl_surface, platform->egl_context))
	{
		GST_ERROR("eglMakeCurrent failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	if (platform->window_resized_event_cb != NULL)
		platform->window_resized_event_cb(platform, actual_width, actual_height, platform->user_context);
	else
		glViewport(actual_x, actual_y, actual_width, actual_height);


	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_shutdown_window(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform->native_window == NULL)
		return TRUE;


	if (platform->frame_cb != NULL)
	{
		wl_callback_destroy(platform->frame_cb);
		platform->frame_cb = NULL;
	}


	eglMakeCurrent(platform->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (platform->egl_context != EGL_NO_CONTEXT)
		eglDestroyContext(platform->egl_display, platform->egl_context);

	if (platform->egl_surface != EGL_NO_SURFACE)
		eglDestroySurface(platform->egl_display, platform->egl_surface);


	wl_egl_window_destroy(platform->native_window);

	if (platform->shell_surface != NULL)
	{
		wl_shell_surface_destroy(platform->shell_surface);
		platform->shell_surface = NULL;
	}

	if (platform->surface != NULL)
	{
		wl_surface_destroy(platform->surface);
		platform->surface = NULL;
	}


	platform->egl_context = EGL_NO_CONTEXT;
	platform->egl_surface = EGL_NO_SURFACE;

	platform->native_window = NULL;

	return TRUE;
}


void gst_imx_egl_viv_sink_egl_platform_set_event_handling(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gboolean event_handling)
{
}


void gst_imx_egl_viv_sink_egl_platform_set_video_info(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED GstVideoInfo *video_info)
{
}


gboolean gst_imx_egl_viv_sink_egl_platform_expose(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform)
{
#ifndef USE_CALLBACK_BASED_RENDERING
	char const msg = GSTIMX_EGLWAYLAND_RENDER;
	write(platform->ctrl_pipe[1], &msg, 1);
#endif
	return TRUE;
}


static void start_rendering(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform->render_frame_cb == NULL)
		return;

	platform->do_render = TRUE;
	platform->render_frame_cb(platform, platform->user_context);
	eglSwapBuffers(platform->egl_display, platform->egl_surface);
}


GstImxEglVivSinkMainloopRetval gst_imx_egl_viv_sink_egl_platform_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	struct pollfd fds[2];
	int const nfds = sizeof(fds) / sizeof(struct pollfd);
	gboolean run_loop = TRUE;

	/* This is necessary to trigger the frame render callback, which in turns makes sure
	 * it is triggered again for the next frame; in other words, this starts a continuous
	 * callback-based playback loop */
	start_rendering(platform);

	while (run_loop)
	{
		int ret;

		/* Watch the display FD and a pipe that is used when poll() shall wake up
		 * (for example, when the pipeline is being shut down and run_mainloop has been set to FALSE) */
		memset(&fds[0], 0, sizeof(fds));
		fds[0].fd = platform->ctrl_pipe[0];
		fds[0].events = POLLIN | POLLERR | POLLHUP;
		fds[1].fd = platform->display_fd;
		fds[1].events = POLLIN | POLLERR | POLLHUP;

		/* Start event handling; wl_display_prepare_read() announces the intention
		 * to read all events, taking care of race conditions that otherwise occur */
		while (wl_display_prepare_read(platform->display) != 0)
			wl_display_dispatch_pending(platform->display);

		/* Flush requests, sending them to the server; if not all data could be sent to
		 * the server, have poll() also let it wait until it the display FD is writable again */
		ret = wl_display_flush(platform->display);
		if (ret < 0)
		{
			if (errno == EAGAIN)
			{
				fds[1].events |= POLLOUT;
			}
			else
			{
				GST_ERROR("error while flushing display: %s", strerror(errno));
				break;
			}
		}

		/* Wait for activity */
		if (poll(&fds[0], nfds, -1) == -1)
		{
			GST_ERROR("error in poll() call: %s", strerror(errno));
			wl_display_cancel_read(platform->display);
			return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_ERROR;
		}

		/* If there is something to read from the display FD, handle events */
		if (fds[1].revents & POLLIN)
		{
			GST_LOG("There is something to read from the display FD - handling events");
			wl_display_read_events(platform->display);
			wl_display_dispatch(platform->display);
		}
		else
		{
			GST_LOG("Nothing to read from the display FD - canceling read");
			wl_display_cancel_read(platform->display);
		}

		/* Read messages from the control pipe
		 * Note that this is done *after* reading from the display FD
		 * above, to make sure the event read block is finished by the
		 * time this place is reached */
		if (fds[0].revents & POLLIN)
		{
			char msg;
			read(fds[0].fd, &msg, sizeof(msg));

			/* Stop if requested */
			switch (msg)
			{
				case GSTIMX_EGLWAYLAND_STOP:
					run_loop = FALSE;
					GST_LOG("Mainloop stop requested");
					break;

				case GSTIMX_EGLWAYLAND_RESIZE:
				{
					guint w, h;
					read(fds[0].fd, &w, sizeof(w));
					read(fds[0].fd, &h, sizeof(h));
					GST_LOG("Resizing EGL window to %dx%d pixels", w, h);
					wl_egl_window_resize(platform->native_window, w, h, 0, 0);
					break;
				}

#ifndef USE_CALLBACK_BASED_RENDERING
				case GSTIMX_EGLWAYLAND_RENDER:
					platform->render_frame_cb(platform, platform->user_context);
					eglSwapBuffers(platform->egl_display, platform->egl_surface);
					break;
#endif

				default:
					break;
			}
		}
	}

	/* At this point, the sink is shutting down. Disable rendering in the frame callback. */
	platform->do_render = FALSE;

	return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_OK;
}


void gst_imx_egl_viv_sink_egl_platform_stop_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	char msg = GSTIMX_EGLWAYLAND_STOP;
	write(platform->ctrl_pipe[1], &msg, 1);
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_coords(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gint x_coord, G_GNUC_UNUSED gint y_coord)
{
	/* Since windows cannot be positioned explicitely in Wayland, nothing can be done here */
	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_size(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED guint width, G_GNUC_UNUSED guint height)
{
	char msg = GSTIMX_EGLWAYLAND_RESIZE;
	write(platform->ctrl_pipe[1], &msg, 1);
	write(platform->ctrl_pipe[1], &width, sizeof(width));
	write(platform->ctrl_pipe[1], &height, sizeof(height));
	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_borderless(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gboolean borderless)
{
	/* Since borders are client-side in Wayland, nothing needs to be done here */
	return TRUE;
}

