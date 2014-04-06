#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "egl_platform.h"
#include "egl_misc.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(imx_egl_platform_fb_debug);
#define GST_CAT_DEFAULT imx_egl_platform_fb_debug


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
	int ctrl_pipe[2];
	gboolean run_mainloop;
};


static void init_debug_category(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(imx_egl_platform_fb_debug, "imxeglplatform_fb", 0, "imxeglvivsink FB platform");
		initialized = TRUE;
	}
}




GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, GstImxEglVivSinkWindowRenderFrameCallback render_frame_cb, gpointer user_context)
{
	gint64 display_index;
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;

	init_debug_category();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->render_frame_cb = render_frame_cb;
	platform->user_context = user_context;

	if (pipe(platform->ctrl_pipe) == -1)
	{
		GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
		g_free(platform);
		goto cleanup;
	}

	if (native_display_name == NULL)
		display_index = 0;
	else
		display_index = g_ascii_strtoll(native_display_name, NULL, 10);
	platform->native_display = fbGetDisplayByIndex(display_index);

	platform->egl_display = eglGetDisplay(platform->native_display);
	if (platform->egl_display == EGL_NO_DISPLAY)
	{
		GST_ERROR("eglGetDisplay failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		g_free(platform);
		goto cleanup;
	}

	if (!eglInitialize(platform->egl_display, &ver_major, &ver_minor))
	{
		GST_ERROR("eglInitialize failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		g_free(platform);
		goto cleanup;
	}

	GST_INFO("FB EGL platform initialized, using EGL %d.%d", ver_major, ver_minor);

	return platform;


cleanup:
	/* either both are set, or none is */
	if (platform->ctrl_pipe[0] != -1)
	{
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
	}

	g_free(platform);
	return NULL;
}


void gst_imx_egl_viv_sink_egl_platform_destroy(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform == NULL)
		return;

	if (platform->egl_display != EGL_NO_DISPLAY)
		eglTerminate(platform->egl_display);

	/* either both are set, or none is */
	if (platform->ctrl_pipe[0] != -1)
	{
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
		g_free(platform);
	}
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

	if (!eglChooseConfig(platform->egl_display, eglconfig_attribs, &config, 1, &num_configs))
	{
		GST_ERROR("eglChooseConfig failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		return FALSE;
	}

	platform->native_window = fbCreateWindow(platform->native_display, x_coord, y_coord, width, height);

	fbGetWindowGeometry(platform->native_window, &actual_x, &actual_y, &actual_width, &actual_height);
	GST_LOG("fbGetWindowGeometry: x/y %d/%d width/height %d/%d", actual_x, actual_y, actual_width, actual_height);

	eglBindAPI(EGL_OPENGL_ES_API);

	platform->egl_context = eglCreateContext(platform->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
	platform->egl_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_window, NULL);

	eglMakeCurrent(platform->egl_display, platform->egl_surface, platform->egl_surface, platform->egl_context);

	if (platform->window_resized_event_cb != NULL)
		platform->window_resized_event_cb(platform, actual_width, actual_height, platform->user_context);
	else
		glViewport(actual_x, actual_y, actual_width, actual_height);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_shutdown_window(GstImxEglVivSinkEGLPlatform *platform)
{
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

	platform->native_window = 0;

	return TRUE;
}


void gst_imx_egl_viv_sink_egl_platform_set_event_handling(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gboolean event_handling)
{
}


void gst_imx_egl_viv_sink_egl_platform_set_video_info(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED GstVideoInfo *video_info)
{
}


gboolean gst_imx_egl_viv_sink_egl_platform_expose(GstImxEglVivSinkEGLPlatform *platform)
{
	char dummy = 1;
	write(platform->ctrl_pipe[1], &dummy, 1);
	return TRUE;
}


GstImxEglVivSinkMainloopRetval gst_imx_egl_viv_sink_egl_platform_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	struct pollfd fds[1];
	int const nfds = sizeof(fds) / sizeof(struct pollfd);
	gboolean expose_required = FALSE;

	platform->run_mainloop = TRUE; // TODO: lock

	while (platform->run_mainloop)
	{
		memset(&fds[0], 0, sizeof(fds));
		fds[0].fd = platform->ctrl_pipe[0];
		fds[0].events = POLLIN;

		if (poll(&fds[0], nfds, -1) == -1)
		{
			GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
			return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_ERROR;
		}

		if (fds[0].revents & POLLIN)
		{
			char buf[256];
			read(fds[0].fd, buf, sizeof(buf));
			expose_required = TRUE;
		}

		if (expose_required)
		{
			if (platform->render_frame_cb != NULL)
			{
				platform->render_frame_cb(platform, platform->user_context);
				eglSwapBuffers(platform->egl_display, platform->egl_surface);
			}

			expose_required = FALSE;
		}
	}

	return GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_OK;
}


void gst_imx_egl_viv_sink_egl_platform_stop_mainloop(GstImxEglVivSinkEGLPlatform *platform)
{
	platform->run_mainloop = FALSE; // TODO: lock
	gst_imx_egl_viv_sink_egl_platform_expose(platform);
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_coords(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gint x_coord, G_GNUC_UNUSED gint y_coord)
{
	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_size(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED guint width, G_GNUC_UNUSED guint height)
{
	return TRUE;
}


gboolean gst_imx_egl_viv_sink_egl_platform_set_borderless(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED gboolean borderless)
{
	return TRUE;
}

