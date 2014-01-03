#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "egl_platform.h"
#include "egl_misc.h"
#include "gl_headers.h"


GST_DEBUG_CATEGORY_STATIC(eglplatform_fb_debug);
#define GST_CAT_DEFAULT eglplatform_fb_debug


struct _GstImxEglVivSinkEGLPlatform
{
	EGLNativeDisplayType native_display;
	EGLNativeWindowType native_window;
	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb;
	gpointer user_context;
	int ctrl_pipe[2];
};


static void init_debug_category(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(eglplatform_fb_debug, "eglplatform_fb", 0, "eglvivsink FB platform");
		initialized = TRUE;
	}
}




GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, gpointer user_context)
{
	gint64 display_index;
	EGLint ver_major, ver_minor;
	GstImxEglVivSinkEGLPlatform* platform;

	init_debug_category();

	platform = (GstImxEglVivSinkEGLPlatform *)g_new0(GstImxEglVivSinkEGLPlatform, 1);
	platform->window_resized_event_cb = window_resized_event_cb;
	platform->user_context = user_context;

	if (pipe(platform->ctrl_pipe) == -1)
	{
		GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
		g_free(platform);
		return NULL;
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
		return NULL;
	}

	if (!eglInitialize(platform->egl_display, &ver_major, &ver_minor))
	{
		GST_ERROR("eglInitialize failed: %s", gst_imx_egl_viv_sink_egl_platform_get_last_error_string());
		g_free(platform);
		return NULL;
	}

	GST_INFO("FB EGL platform initialized, using EGL %d.%d", ver_major, ver_minor);

	return platform;
}


void gst_imx_egl_viv_sink_egl_platform_destroy(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform != NULL)
	{
		if (platform->egl_display != EGL_NO_DISPLAY)
			eglTerminate(platform->egl_display);
		close(platform->ctrl_pipe[0]);
		close(platform->ctrl_pipe[1]);
		g_free(platform);
	}
}


gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, G_GNUC_UNUSED guintptr window_handle, G_GNUC_UNUSED gboolean event_handling, G_GNUC_UNUSED GstVideoInfo *video_info, G_GNUC_UNUSED gboolean fullscreen)
{
	EGLint num_configs;
	EGLConfig config;
	int x, y, width, height;

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

	platform->native_window = fbCreateWindow(platform->native_display, 0, 0, 0, 0);

	fbGetWindowGeometry(platform->native_window, &x, &y, &width, &height);
	GST_DEBUG("fbGetWindowGeometry: x/y %d/%d width/height %d/%d", x, y, width, height);

	eglBindAPI(EGL_OPENGL_ES_API);

	platform->egl_context = eglCreateContext(platform->egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
	platform->egl_surface = eglCreateWindowSurface(platform->egl_display, config, platform->native_window, NULL);

	eglMakeCurrent(platform->egl_display, platform->egl_surface, platform->egl_surface, platform->egl_context);

	if (platform->window_resized_event_cb != NULL)
		platform->window_resized_event_cb(platform, width, height, platform->user_context);
	else
		glViewport(x, y, width, height);

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


void gst_imx_egl_viv_sink_egl_platform_swap_buffers(GstImxEglVivSinkEGLPlatform *platform)
{
	if (platform->native_window != 0)
		eglSwapBuffers(platform->egl_display, platform->egl_surface);
}


GstImxEglVivSinkHandleEventsRetval gst_imx_egl_viv_sink_egl_platform_handle_events(GstImxEglVivSinkEGLPlatform *platform)
{
	struct pollfd fds[1];
	int const nfds = sizeof(fds) / sizeof(struct pollfd);
	gboolean expose_required = FALSE;

	memset(&fds[0], 0, sizeof(fds));
	fds[0].fd = platform->ctrl_pipe[0];
	fds[0].events = POLLIN;

	if (poll(&fds[0], nfds, -1) == -1)
	{
		GST_ERROR("error creating POSIX pipe: %s", strerror(errno));
		return GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_ERROR;
	}

	if (fds[0].revents & POLLIN)
	{
		char buf[256];
		read(fds[0].fd, buf, sizeof(buf));
		expose_required = TRUE;
	}

	return expose_required ? GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_EXPOSE_REQUIRED : GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_OK;
}

