#ifndef GST_IMX_EGL_VIV_SINK_EGL_PLATFORM_H
#define GST_IMX_EGL_VIV_SINK_EGL_PLATFORM_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


typedef struct _GstImxEglVivSinkEGLPlatform GstImxEglVivSinkEGLPlatform;


typedef enum
{
	GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_OK,
	GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_WINDOW_CLOSED,
	GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_EXPOSE_REQUIRED,
	GST_IMX_EGL_VIV_SINK_HANDLE_EVENTS_RETVAL_ERROR
} GstImxEglVivSinkHandleEventsRetval;


typedef void (*GstImxEglVivSinkWindowResizedEventCallback)(GstImxEglVivSinkEGLPlatform *platform, guint window_width, guint window_height, gpointer user_context);


GstImxEglVivSinkEGLPlatform* gst_imx_egl_viv_sink_egl_platform_create(gchar const *native_display_name, GstImxEglVivSinkWindowResizedEventCallback window_resized_event_cb, gpointer user_context);
void gst_imx_egl_viv_sink_egl_platform_destroy(GstImxEglVivSinkEGLPlatform *platform);

gboolean gst_imx_egl_viv_sink_egl_platform_init_window(GstImxEglVivSinkEGLPlatform *platform, guintptr window_handle, gboolean event_handling, GstVideoInfo *video_info, gboolean fullscreen, gint x_coord, gint y_coord, guint width, guint height, gboolean borderless);
gboolean gst_imx_egl_viv_sink_egl_platform_shutdown_window(GstImxEglVivSinkEGLPlatform *platform);

void gst_imx_egl_viv_sink_egl_platform_set_event_handling(GstImxEglVivSinkEGLPlatform *platform, gboolean event_handling);
void gst_imx_egl_viv_sink_egl_platform_set_video_info(GstImxEglVivSinkEGLPlatform *platform, GstVideoInfo *video_info);

gboolean gst_imx_egl_viv_sink_egl_platform_expose(GstImxEglVivSinkEGLPlatform *platform);
void gst_imx_egl_viv_sink_egl_platform_swap_buffers(GstImxEglVivSinkEGLPlatform *platform);

GstImxEglVivSinkHandleEventsRetval gst_imx_egl_viv_sink_egl_platform_handle_events(GstImxEglVivSinkEGLPlatform *platform);

gboolean gst_imx_egl_viv_sink_egl_platform_set_coords(GstImxEglVivSinkEGLPlatform *platform, gint x_coord, gint y_coord);
gboolean gst_imx_egl_viv_sink_egl_platform_set_size(GstImxEglVivSinkEGLPlatform *platform, guint width, guint height);
gboolean gst_imx_egl_viv_sink_egl_platform_set_borderless(GstImxEglVivSinkEGLPlatform *platform, gboolean borderless);


G_END_DECLS


#endif

