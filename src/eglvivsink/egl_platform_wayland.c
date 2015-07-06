#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

#include <wayland-egl.h>

#include "egl_platform.h"
#include "egl_misc.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(imx_egl_platform_wl_debug);
#define GST_CAT_DEFAULT imx_egl_platform_wl_debug


struct _GstImxEglVivSinkEGLPlatform
{
	EGLNativeDisplayType native_display;
	EGLNativeWindowType native_main_window;
	EGLNativeWindowType native_window;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_main_surface;
	EGLSurface egl_surface;

	GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb;
	GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb;

	gpointer user_context;

	gboolean fullscreen;
	guint video_par_n, video_par_d;
	guint fixed_window_width, fixed_window_height, video_width, video_height;
	guint current_width, current_height;
	guint screen_width, screen_height;
	gint pending_x_coord, pending_y_coord;
	gint x_coord, y_coord;
	gboolean pending_subsurface_desync;

	GMutex mutex;

	struct wl_display *display;
	struct wl_registry *registry;
	int display_fd;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shell *shell;
	struct wl_output *output;

	struct wl_surface *main_surface;
	struct wl_surface *surface;
	struct wl_subsurface *subsurface;
	struct wl_shell_surface *shell_surface;

	struct wl_callback *frame_cb;
	gboolean frame_callback_invoked;

	int ctrl_pipe[2];

	gboolean configured, do_render;
};


#define EGL_PLATFORM_LOCK(platform) g_mutex_lock(&((platform)->mutex))
#define EGL_PLATFORM_UNLOCK(platform) g_mutex_unlock(&((platform)->mutex))


typedef enum
{
	GSTIMX_EGLWL_CMD_REDRAW,
	GSTIMX_EGLWL_CMD_CALL_RESIZE_CB,
	GSTIMX_EGLWL_CMD_STOP_MAINLOOP
}
GstImxEGLWLCmds;




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




static void calculate_adjusted_window_size(GstImxEglVivSinkEGLPlatform *platform, guint *actual_width, guint *actual_height)
{
	gboolean b;
	guint window_par_n, window_par_d, display_ratio_n, display_ratio_d;

	window_par_n = 4;
	window_par_d = 3;

	b = gst_video_calculate_display_ratio(
		&display_ratio_n, &display_ratio_d,
		platform->video_width, platform->video_height,
		platform->video_par_n, platform->video_par_d,
		window_par_n, window_par_d
	);

	if (b)
	{
		*actual_width = platform->video_width * platform->video_par_n / platform->video_par_d;
		*actual_height = platform->video_height;
	}
	else
	{
		*actual_width = platform->video_width;
		*actual_height = platform->video_height;
	}

	GST_LOG(
		"calculate_adjusted_window_size:  video size: %dx%d  video ratio: %d/%d  display ratio: %d/%d  actual size: %ux%u",
		platform->video_width, platform->video_height,
		platform->video_par_n, platform->video_par_d,
		display_ratio_n, display_ratio_d,
		*actual_width, *actual_height
	);
}


static void resize_window_to_video(GstImxEglVivSinkEGLPlatform *platform)
{
	guint actual_width, actual_height;

	calculate_adjusted_window_size(platform, &actual_width, &actual_height);
	platform->current_width = ((platform->screen_width == 0) || (actual_width < platform->screen_width)) ? actual_width : platform->screen_width;
	platform->current_height = ((platform->screen_height == 0) || (actual_height < platform->screen_height)) ? actual_height : platform->screen_height;
	GST_LOG("final size: %dx%d", platform->current_width, platform->current_height);

	wl_egl_window_resize(platform->native_window, platform->current_width, platform->current_height, 0, 0);
	platform->pending_subsurface_desync = TRUE;
}




static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, char const *interface, G_GNUC_UNUSED uint32_t version)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	if (g_strcmp0(interface, "wl_compositor") == 0)
		platform->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	else if (g_strcmp0(interface, "wl_shell") == 0)
		platform->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
	else if (g_strcmp0(interface, "wl_output") == 0)
		platform->output = wl_registry_bind(registry, id, &wl_output_interface, 2);
	else if (g_strcmp0(interface, "wl_subcompositor") == 0)
		platform->subcompositor = wl_registry_bind(registry, id,
			&wl_subcompositor_interface, 1);
}

static void registry_handle_global_remove(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_registry *registry, G_GNUC_UNUSED uint32_t name)
{
}

static const struct wl_registry_listener registry_listener =
{
	registry_handle_global,
	registry_handle_global_remove
};




static void output_geometry(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_output *wl_output, G_GNUC_UNUSED int x, G_GNUC_UNUSED int y, G_GNUC_UNUSED int w, G_GNUC_UNUSED int h, G_GNUC_UNUSED int subpixel, G_GNUC_UNUSED const char *make, G_GNUC_UNUSED const char *model, G_GNUC_UNUSED int transform)
{
}

static void output_mode(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_output *wl_output, G_GNUC_UNUSED unsigned int flags, int w, int h, G_GNUC_UNUSED int refresh)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	if (flags & WL_OUTPUT_MODE_CURRENT)
	{
		GST_LOG("reported screen size: %dx%d", w, h);

		platform->screen_width = w;
		platform->screen_height = h;

#if 0 /* This becomes unnecessary as the callback is dispatched before configuring the window. */
		/* resize again in case the window is set to the video size
		 * (this makes sure the window is not larger than the screen) */
		if (
			   !platform->fullscreen
			&& (platform->fixed_window_width == 0) && (platform->fixed_window_height == 0)
			&& (platform->video_width != 0) && (platform->video_height != 0)
			//&& (platform->parent_window != 0) // TODO
		)
		{
			resize_window_to_video(platform);

			if (platform->window_resized_event_cb != NULL)
			{
				char const cmd = GSTIMX_EGLWL_CMD_CALL_RESIZE_CB;
				write(platform->ctrl_pipe[1], &cmd, 1);
			}
		}
#endif
	}
}

static void output_done(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_output *output)
{
}

static void output_scale(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_output *output, G_GNUC_UNUSED int scale)
{
}

static const struct wl_output_listener output_listener =
{
	output_geometry,
	output_mode,
	output_done,
	output_scale
};




static void handle_ping(G_GNUC_UNUSED void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void handle_configure(void *data, G_GNUC_UNUSED struct wl_shell_surface *shell_surface, G_GNUC_UNUSED uint32_t edges, int32_t width, int32_t height)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	GST_LOG("reconfiguring window size to %dx%d pixels", width, height);

	platform->current_width = width;
	platform->current_height = height;

	if (platform->native_window != NULL)
		wl_egl_window_resize(platform->native_window, width, height, 0, 0);

	if (platform->window_resized_event_cb != NULL)
		platform->window_resized_event_cb(platform, width, height, platform->user_context);
	else
		glViewport(0, 0, width, height);

	platform->pending_subsurface_desync = TRUE;
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




static void frame_callback(void *data, struct wl_callback *callback, G_GNUC_UNUSED uint32_t time);

static const struct wl_callback_listener frame_listener =
{
	frame_callback
};

static void background_draw(GstImxEglVivSinkEGLPlatform *platform);

static void frame_callback(void *data, struct wl_callback *callback, G_GNUC_UNUSED uint32_t time)
{
	GstImxEglVivSinkEGLPlatform *platform = data;

	/* Cleanup old callback */
	if (callback != NULL)
		wl_callback_destroy(callback);

	platform->frame_callback_invoked = TRUE;
	GST_LOG("frame_callback_invoked set to TRUE");

	/* Setup new callback */
	platform->frame_cb = wl_surface_frame(platform->surface);
	wl_callback_add_listener(platform->frame_cb, &frame_listener, platform);
}




static void configure_callback(void *data, struct wl_callback *callback, uint32_t time)
{
	GstImxEglVivSinkEGLPlatform *platform = data;
	struct wl_region *input_region;

	wl_callback_destroy(callback);

	/* Position sub-surface. */
	if (!platform->fullscreen &&
		(platform->pending_x_coord != platform->x_coord ||
		 platform->pending_y_coord != platform->y_coord)) {

		platform->x_coord = platform->pending_x_coord;
		platform->y_coord = platform->pending_y_coord;
		wl_subsurface_set_position(platform->subsurface,
			platform->x_coord,
			platform->y_coord);
	}

	/* Set the input region carefully so that we only receive events on the sub-surface. */
	input_region = wl_compositor_create_region(platform->compositor);
	wl_region_add(input_region, platform->x_coord, platform->y_coord,
		platform->current_width, platform->current_height);
	wl_surface_set_input_region(platform->main_surface, input_region);
	wl_region_destroy(input_region);

	platform->configured = TRUE;
	background_draw(platform);
	if (platform->frame_cb == NULL)
		frame_callback(data, NULL, time);
}

static struct wl_callback_listener configure_callback_listener =
{
	configure_callback,
};


static void background_draw(GstImxEglVivSinkEGLPlatform *platform)
{
	if (!platform->configured || !platform->do_render)
		return;

	eglMakeCurrent(platform->egl_display,
		platform->egl_main_surface,
		platform->egl_main_surface,
		platform->egl_context);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(platform->egl_display, platform->egl_main_surface);
}

static void redraw(GstImxEglVivSinkEGLPlatform *platform)
{
	struct wl_region *region;

	if (!platform->configured || !platform->do_render)
		return;

	eglMakeCurrent(platform->egl_display, platform->egl_surface, platform->egl_surface, platform->egl_context);
	/* The actual rendering */
	if (platform->render_frame_cb != NULL)
		platform->render_frame_cb(platform, platform->user_context);

	/* Define opaque region */
	region = wl_compositor_create_region(platform->compositor);
	wl_region_add(
		region,
		0, 0,
		platform->current_width,
		platform->current_height
	);
	wl_surface_set_opaque_region(platform->surface, region);
	wl_region_destroy(region);

	/* Finally, do the actual commit to the server */
	wl_surface_commit(platform->main_surface);
	eglSwapBuffers(platform->egl_display, platform->egl_surface);
}


GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb, gpointer user_context)
{
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;

	g_assert(window_resized_event_cb != NULL);
	g_assert(render_frame_cb != NULL);

	static_global_init();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->render_frame_cb = render_frame_cb;
	platform->user_context = user_context;

	g_mutex_init(&(platform->mutex));

	platform->ctrl_pipe[0] = -1;
	platform->ctrl_pipe[1] = -1;
	if (pipe(platform->ctrl_pipe) == -1)
	{
		GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
		goto cleanup;
	}

	platform->display = wl_display_connect(native_display_name);
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

	wl_output_add_listener(platform->output, &output_listener, platform);
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

	g_mutex_clear(&(platform->mutex));

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

	if (platform->shell != NULL)
		wl_shell_destroy(platform->shell);

	if (platform->subcompositor != NULL)
		wl_subcompositor_destroy(platform->subcompositor);

	if (platform->compositor != NULL)
		wl_compositor_destroy(platform->compositor);

	if (platform->output != NULL)
		wl_output_destroy(platform->output);

	wl_display_flush(platform->display);
	wl_display_disconnect(platform->display);

	/* either both are set, or none is */
	if (platform->ctrl_pipe[0] != -1)
	{
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
	}

	g_mutex_clear(&(platform->mutex));

	g_free(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, guintptr window_handle, gboolean event_handling, GstVideoInfo *video_info, gboolean fullscreen, gint x_coord, gint y_coord, guint width, guint height, G_GNUC_UNUSED gboolean borderless)
{
	EGLint num_configs;
	EGLConfig config;
	guint chosen_width, chosen_height;
	int actual_width, actual_height;

	static EGLint const eglconfig_attribs[] =
	{
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	static EGLint const ctx_attribs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};


	EGL_PLATFORM_LOCK(platform);


	if (!eglChooseConfig(platform->egl_display, eglconfig_attribs, &config, 1, &num_configs))
	{
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}


	if (platform->compositor == NULL)
	{
		GST_ERROR("compositor pointer is NULL");
		goto fail;
	}

	if (platform->subcompositor == NULL)
	{
		GST_ERROR("subcompositor pointer is NULL");
		goto fail;
	}

	if (platform->shell == NULL)
	{
		GST_ERROR("shell pointer is NULL");
		goto fail;
	}

	if ((platform->main_surface = wl_compositor_create_surface(platform->compositor)) == NULL)
	{
		GST_ERROR("creating main Wayland surface failed");
		goto fail;
	}

	if ((platform->surface = wl_compositor_create_surface(platform->compositor)) == NULL)
	{
		GST_ERROR("creating Wayland surface failed");
		goto fail;
	}

	if ((platform->subsurface = wl_subcompositor_get_subsurface(platform->subcompositor,
		platform->surface, platform->main_surface)) == NULL)
	{
		GST_ERROR("creating Wayland subsurface failed");
		goto fail;
	}

	if ((platform->shell_surface = wl_shell_get_shell_surface(platform->shell,
		platform->main_surface)) == NULL)
	{
		GST_ERROR("creating Wayland shell surface failed");
		goto fail;
	}

	wl_shell_surface_add_listener(platform->shell_surface, &shell_surface_listener, platform);

	platform->pending_subsurface_desync = TRUE;

	platform->fixed_window_width = width;
	platform->fixed_window_height = height;

	platform->video_par_n = GST_VIDEO_INFO_PAR_N(video_info);
	platform->video_par_d = GST_VIDEO_INFO_PAR_D(video_info);
	platform->video_width = GST_VIDEO_INFO_WIDTH(video_info);
	platform->video_height = GST_VIDEO_INFO_HEIGHT(video_info);
	platform->pending_x_coord = x_coord;
	platform->pending_y_coord = y_coord;
	platform->x_coord = -1;
	platform->y_coord = -1;

	platform->fullscreen = fullscreen;

	/* If either no fixed size is set, or fullscreen is requested, use the video frame size
	 * In the fullscreen case, the size is actually irrelevant, since it will be overwritten
	 * with the screen size. But passing zero for the width/height values is invalid, the
	 * video frame size is used. */
	if ((width == 0) || (height == 0) || fullscreen)
	{
		calculate_adjusted_window_size(platform, &chosen_width, &chosen_height);
		/*chosen_width = platform->video_width;
		chosen_height = platform->video_height;*/
	}
	else
	{
		chosen_width = width;
		chosen_height = height;
	}

	platform->native_main_window = wl_egl_window_create(platform->main_surface,
		platform->screen_width, platform->screen_height);
	if (platform->native_main_window == NULL)
	{
		GST_ERROR("wl_egl_window_create failed to create the background window");
		goto fail;
	}

	platform->native_window = wl_egl_window_create(platform->surface, chosen_width, chosen_height);
	if (platform->native_window == NULL)
	{
		GST_ERROR("wl_egl_window_create failed to create a  %dx%d window", width, height);
		goto fail;
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API))
	{
		GST_ERROR("eglBindAPI failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}



	platform->egl_context = eglCreateContext(platform->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
	if (platform->egl_context == EGL_NO_CONTEXT)
	{
		GST_ERROR("eglCreateContext failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}

	platform->egl_main_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_main_window, NULL);
	if (platform->egl_main_surface == EGL_NO_SURFACE)
	{
		GST_ERROR("eglCreateWindowSurface failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}

	platform->egl_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_window, NULL);
	if (platform->egl_surface == EGL_NO_SURFACE)
	{
		GST_ERROR("eglCreateWindowSurface failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}

	if (!eglMakeCurrent(platform->egl_display, platform->egl_main_surface, platform->egl_main_surface, platform->egl_context))
	{
		GST_ERROR("eglMakeCurrent failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		goto fail;
	}


	if (fullscreen)
		wl_shell_surface_set_fullscreen(platform->shell_surface, WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, NULL);
	else
		wl_shell_surface_set_toplevel(platform->shell_surface);

	{
		struct wl_callback *callback = wl_display_sync(platform->display);
		wl_callback_add_listener(callback, &configure_callback_listener, platform);
	}


	actual_width = chosen_width;
	actual_height = chosen_height;

	platform->current_width = actual_width;
	platform->current_height = actual_height;

	if (fullscreen || (platform->fixed_window_width != 0) || (platform->fixed_window_height != 0))
	{
		platform->fixed_window_width = actual_width;
		platform->fixed_window_height = actual_height;
	}

	if (platform->window_resized_event_cb != NULL)
		platform->window_resized_event_cb(platform, actual_width, actual_height, platform->user_context);
	else
		glViewport(0, 0, actual_width, actual_height);


	EGL_PLATFORM_UNLOCK(platform);


	return TRUE;


fail:
	EGL_PLATFORM_UNLOCK(platform);
	return FALSE;
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


	EGL_PLATFORM_LOCK(platform);


	eglMakeCurrent(platform->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (platform->egl_context != EGL_NO_CONTEXT)
		eglDestroyContext(platform->egl_display, platform->egl_context);

	if (platform->egl_surface != EGL_NO_SURFACE)
		eglDestroySurface(platform->egl_display, platform->egl_surface);
	if (platform->egl_main_surface != EGL_NO_SURFACE)
		eglDestroySurface(platform->egl_display, platform->egl_main_surface);

	platform->egl_context = EGL_NO_CONTEXT;
	platform->egl_surface = EGL_NO_SURFACE;


	wl_egl_window_destroy(platform->native_window);
	wl_egl_window_destroy(platform->native_main_window);

	if (platform->shell_surface != NULL)
	{
		wl_shell_surface_destroy(platform->shell_surface);
		platform->shell_surface = NULL;
	}

	if (platform->subsurface != NULL)
	{
		wl_subsurface_destroy(platform->subsurface);
		platform->subsurface = NULL;
	}

	if (platform->surface != NULL)
	{
		wl_surface_destroy(platform->surface);
		platform->surface = NULL;
	}

	if (platform->main_surface != NULL)
	{
		wl_surface_destroy(platform->main_surface);
		platform->main_surface = NULL;
	}

	platform->native_window = NULL;
	platform->native_main_window = NULL;


	EGL_PLATFORM_UNLOCK(platform);


	return TRUE;
}


void gst_imx_egl_viv_sink_egl_platform_set_event_handling(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling)
{
}


void gst_imx_egl_viv_sink_egl_platform_set_video_info(GstImxEglVivSinkEGLPlatform *platform, GstVideoInfo *video_info)
{
	EGL_PLATFORM_LOCK(platform);


	if (platform->native_window == 0)
	{
		GST_LOG("window not open - cannot set video info");
		EGL_PLATFORM_UNLOCK(platform);
		return;
	}


	platform->video_par_n = GST_VIDEO_INFO_PAR_N(video_info);
	platform->video_par_d = GST_VIDEO_INFO_PAR_D(video_info);
	platform->video_width = GST_VIDEO_INFO_WIDTH(video_info);
	platform->video_height = GST_VIDEO_INFO_HEIGHT(video_info);


	if (platform->fullscreen || (platform->fixed_window_width != 0) || (platform->fixed_window_height != 0)/* || (platform->parent_window != 0)*/) // TODO
	{
	}
	else
	{
		resize_window_to_video(platform);
	}


	EGL_PLATFORM_UNLOCK(platform);


	/* even though the window itself might not have been resized, the callback
	 * still needs to be invoked, because it depends on both the window and the
	 * video frame sizes */
	if (platform->window_resized_event_cb != NULL)
	{
		// do not call the resize callback here directly; instead, notify the main loop about this change
		// because here, the EGL context is not and cannot be set
		char const cmd = GSTIMX_EGLWL_CMD_CALL_RESIZE_CB;
		write(platform->ctrl_pipe[1], &cmd, 1);
	}
}


gboolean gst_imx_egl_viv_sink_egl_platform_expose(GstImxEglVivSinkEGLPlatform *platform)
{
	char const cmd = GSTIMX_EGLWL_CMD_REDRAW;
	write(platform->ctrl_pipe[1], &cmd, 1);
	return TRUE;
}


GstImxEglVivSinkMainloopRetval gst_imx_egl_viv_sink_egl_platform_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	struct pollfd fds[2];
	int const nfds = sizeof(fds) / sizeof(struct pollfd);
	gboolean continue_loop = TRUE;

	platform->do_render = TRUE;
	while (continue_loop)
	{
		int ret;
		gboolean do_redraw = FALSE;

		/* Watch the display FD and a pipe that is used when poll() shall wake up
		 * (for example, when the pipeline is being shut down and run_mainloop has been set to FALSE) */
		memset(&fds[0], 0, sizeof(fds));
		fds[0].fd = platform->ctrl_pipe[0];
		fds[0].events = POLLIN | POLLERR | POLLHUP;
		fds[1].fd = platform->display_fd;
		fds[1].events = POLLIN | POLLERR | POLLHUP;

		//EGL_PLATFORM_LOCK(platform);

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
				EGL_PLATFORM_UNLOCK(platform);
				GST_ERROR("error while flushing display: %s", strerror(errno));
				break;
			}
		}

		if (poll(&fds[0], nfds, -1) == -1)
		{
			GST_ERROR("error in poll() call: %s", strerror(errno));
			wl_display_cancel_read(platform->display);
			EGL_PLATFORM_UNLOCK(platform);
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
			char cmd;
			read(fds[0].fd, &cmd, sizeof(cmd));

			GST_LOG("received cmd: %d", (int)cmd);

			/* Stop if requested */
			switch (cmd)
			{
				case GSTIMX_EGLWL_CMD_REDRAW:
					do_redraw = TRUE;
					break;

				case GSTIMX_EGLWL_CMD_STOP_MAINLOOP:
					continue_loop = FALSE;
					GST_LOG("Mainloop stop requested");
					break;

				case GSTIMX_EGLWL_CMD_CALL_RESIZE_CB:
					GST_LOG("Resize callback requested");
					if (platform->window_resized_event_cb != NULL)
						platform->window_resized_event_cb(platform, platform->current_width, platform->current_height, platform->user_context);
					break;

				default:
					break;
			}
		}

		if (do_redraw && platform->frame_callback_invoked)
		{
			redraw(platform);
			platform->frame_callback_invoked = FALSE;
			GST_LOG("frame_callback_invoked set to FALSE");
		}
		if (platform->pending_subsurface_desync)
		{
			wl_subsurface_set_desync(platform->subsurface);
			platform->pending_subsurface_desync = FALSE;
		}

		//EGL_PLATFORM_UNLOCK(platform);
	}

	/* At this point, the sink is shutting down. Disable rendering in the frame callback. */
	platform->do_render = FALSE;

	return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_OK;
}


void gst_imx_egl_viv_sink_egl_platform_stop_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	char cmd = GSTIMX_EGLWL_CMD_STOP_MAINLOOP;
	write(platform->ctrl_pipe[1], &cmd, 1);
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_coords(GstImxEglVivSinkEGLPlatform *platform, gint x_coord, gint y_coord)
{
	EGL_PLATFORM_LOCK(platform);

	platform->pending_x_coord = x_coord;
	platform->pending_y_coord = y_coord;

	EGL_PLATFORM_UNLOCK(platform);
	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_size(GstImxEglVivSinkEGLPlatform *platform, guint width, guint height)
{
	EGL_PLATFORM_LOCK(platform);

	/* Only allow overwriting values if the window size can actually be modified */
	if ((platform->fullscreen)/* || (platform->parent_window != 0)*/) // TODO
	{
		platform->fixed_window_width = width;
		platform->fixed_window_height = height;
	}

	if ((platform->fullscreen)/* || (platform->parent_window != 0)*/) // TODO
	{
		// do nothing
	}
	else if ((width != 0) || (height != 0))
	{
		wl_egl_window_resize(platform->native_window, width, height, 0, 0);
		platform->pending_subsurface_desync = TRUE;
	}
	else
	{
		resize_window_to_video(platform);
	}

	EGL_PLATFORM_UNLOCK(platform);

	if (platform->window_resized_event_cb != NULL)
	{
		// do not call the resize callback here directly; instead, notify the main loop about this change
		// because here, the EGL context is not and cannot be set
		char const cmd = GSTIMX_EGLWL_CMD_CALL_RESIZE_CB;
		write(platform->ctrl_pipe[1], &cmd, 1);
	}

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_borderless(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gboolean borderless)
{
	/* Since borders are client-side in Wayland, nothing needs to be done here */
	return TRUE;
}

