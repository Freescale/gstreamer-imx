/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *   @author: Carlos Rafael Giani <dv@pseudoterminal.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-eglvivsink
 *
 * EglVivSink renders video frames on a EGL surface it sets up
 * from a window it either creates (on X11) or gets a handle to
 * through it's xOverlay interface. All the display/surface logic
 * in this sink uses EGL to interact with the native window system.
 * The rendering logic, in turn, uses OpenGL ES v2.
 *
 * This sink has been tested to work on X11/Mesa and on Android
 * (From Gingerbread on to Jelly Bean) and while it's currently
 * using an slow copy-over rendering path it has proven to be fast
 * enough on the devices we have tried it on. 
 *
 * <refsect2>
 * <title>Supported EGL/OpenGL ES versions</title>
 * <para>
 * This Sink uses EGLv1 and GLESv2
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! eglvivsink
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with internal window creation disabled</title>
 * <para>
 * By setting the can_create_window property to FALSE you can force the
 * sink to wait for a window handle through it's xOverlay interface even
 * if internal window creation is supported by the platform. Window creation
 * is only supported in X11 right now but it should be trivial to add support
 * for different platforms.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglvivsink can_create_window=FALSE
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * The sink will try it's best to consider the incoming frame's and display's
 * pixel aspect ratio and fill the corresponding surface without altering the
 * decoded frame's geometry when scaling. You can disable this logic by setting
 * the force_aspect_ratio property to FALSE, in which case the sink will just
 * fill the entire surface it has access to regardles of the PAR/DAR relationship.
 * </para>
 * <para>
 * Querying the display aspect ratio is only supported with EGL versions >= 1.2.
 * The sink will just assume the DAR to be 1/1 if it can't get access to this
 * information.
 * </para>
 * <para>
 * Here is an example launch line with the PAR/DAR aware scaling disabled:
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglvivsink force_aspect_ratio=FALSE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-frame.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/videooverlay.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include "video_platform_wrapper.h"

#include "gsteglvivsink.h"

#include "../common/phys_mem_meta.h"

/* Some EGL implementations are reporting wrong
 * values for the display's EGL_PIXEL_ASPECT_RATIO.
 * They are required by the khronos specs to report
 * this value as w/h * EGL_DISPLAY_SCALING (Which is
 * a constant with value 10000) but at least the
 * Galaxy SIII (Android) is reporting just 1 when
 * w = h. We use these two to bound returned values to
 * sanity.
 */
#define EGL_SANE_DAR_MIN ((EGL_DISPLAY_SCALING)/10)
#define EGL_SANE_DAR_MAX ((EGL_DISPLAY_SCALING)*10)

GST_DEBUG_CATEGORY_STATIC (gst_eglvivsink_debug);
#define GST_CAT_DEFAULT gst_eglvivsink_debug

/* GLESv2 GLSL Shaders
 *
 * OpenGL ES Standard does not mandate YUV support. This is
 * why most of these shaders deal with Packed/Planar YUV->RGB
 * conversion.
 */

/* *INDENT-OFF* */
/* Direct vertex copy */
static const char *vert_COPY_prog = {
      "attribute vec3 position;"
      "attribute vec2 texpos;"
      "varying vec2 opos;"
      "void main(void)"
      "{"
      " opos = texpos;"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

static const char *vert_COPY_prog_no_tex = {
      "attribute vec3 position;"
      "void main(void)"
      "{"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

/* Paint all black */
static const char *frag_BLACK_prog = {
  "precision mediump float;"
      "void main(void)"
      "{"
      " gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
      "}"
};

/* Direct fragments copy */
static const char *frag_COPY_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.rgb, 1.0);"
      "}"
};

/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
      "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv  = texture2D(tex,opos / tex_scale0).gba;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/* Planar YUV converters */

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,Utex,Vtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.y=texture2D(Utex,opos / tex_scale1).r;"
      "  yuv.z=texture2D(Vtex,opos / tex_scale2).r;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,UVtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.yz=texture2D(UVtex,opos / tex_scale1).%c%c;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};
/* *INDENT-ON* */

static const EGLint eglvivsink_config_attribs[] = {
  EGL_RED_SIZE, 1,
  EGL_GREEN_SIZE, 1,
  EGL_BLUE_SIZE, 1,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

/* Input capabilities. */
static GstStaticPadTemplate gst_eglvivsink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (
            "{ "
#ifdef HAVE_VIV_I420
            "I420, "
#endif
#ifdef HAVE_VIV_YV12
            "YV12, "
#endif
#ifdef HAVE_VIV_NV12
            "YV21, "
#endif
#ifdef HAVE_VIV_NV21
            "NV12, "
#endif
/*#ifdef HAVE_VIV_YUY2
            "NV21, "
#endif
#ifdef HAVE_VIV_UYVY
            "UYVY, "
#endif*/
            "RGB16, RGB, RGBA, BGRA, RGBx, BGRx, "
            "BGR, ARGB, ABGR, xRGB, xBGR, AYUV, Y444, Y41B"
            " }"
    )));
    /* TODO: YUY2 and UYVY are supported by the Vivante direct textures,
     * but not by the fallback fragment shaders. Add such shaders for
     * these formats, then put them back in the caps */

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
};

static void gst_eglvivsink_finalize (GObject * object);
static void gst_eglvivsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_eglvivsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_eglvivsink_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_eglvivsink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static gboolean gst_eglvivsink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_eglvivsink_getcaps (GstBaseSink * bsink, GstCaps * filter);
static gboolean gst_eglvivsink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

/* VideoOverlay interface cruft */
static void gst_eglvivsink_videooverlay_init (GstVideoOverlayInterface *
    iface);

/* Actual VideoOverlay interface funcs */
static void gst_eglvivsink_expose (GstVideoOverlay * overlay);
static void gst_eglvivsink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_eglvivsink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height);

/* Utility */
static EGLNativeWindowType gst_eglvivsink_create_window (GstEglVivSink *
    eglvivsink, gint width, gint height);
static gboolean gst_eglvivsink_fill_supported_fbuffer_configs (GstEglVivSink *
    eglvivsink);
static gboolean gst_eglvivsink_init_egl_display (GstEglVivSink * eglvivsink);
static gboolean gst_eglvivsink_choose_config (GstEglVivSink * eglvivsink);
static gboolean gst_eglvivsink_init_egl_surface (GstEglVivSink * eglvivsink);
static void gst_eglvivsink_init_egl_exts (GstEglVivSink * eglvivsink);
static gboolean gst_eglvivsink_setup_vbo (GstEglVivSink * eglvivsink,
    gboolean reset);
static gboolean
gst_eglvivsink_configure_caps (GstEglVivSink * eglvivsink, GstCaps * caps);
static GstFlowReturn gst_eglvivsink_upload (GstEglVivSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_eglvivsink_render (GstEglVivSink * sink);
static GstFlowReturn gst_eglvivsink_queue_object (GstEglVivSink * sink,
    GstMiniObject * obj);
static inline gboolean got_gl_error (const char *wtf);
static inline gboolean got_egl_error (const char *wtf);
static inline gboolean egl_init (GstEglVivSink * eglvivsink);
static gboolean gst_eglvivsink_context_make_current (GstEglVivSink *
    eglvivsink, gboolean bind);
static void gst_eglvivsink_wipe_eglglesctx (GstEglVivSink * eglvivsink);

static GLenum gst_eglvivsink_is_format_supported (GstVideoFormat format);
static GLenum gst_eglvivsink_get_viv_format (GstVideoFormat format);

#define parent_class gst_eglvivsink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstEglVivSink, gst_eglvivsink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_eglvivsink_videooverlay_init))

static GstCaps *
_gst_video_format_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

static gboolean
gst_eglvivsink_fill_supported_fbuffer_configs (GstEglVivSink * eglvivsink)
{
  gboolean ret = FALSE;
  EGLint cfg_number;
  GstCaps *caps;

  GST_DEBUG_OBJECT (eglvivsink,
      "Building initial list of wanted eglattribs per format");

  /* Init supported format/caps list */
  caps = gst_caps_new_empty ();

  if (eglChooseConfig (eglvivsink->eglglesctx.display,
          eglvivsink_config_attribs, NULL, 1, &cfg_number) != EGL_FALSE) {
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YUY2));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_UYVY));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
    gst_caps_append (caps,
        _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
    ret = TRUE;
  } else {
    GST_INFO_OBJECT (eglvivsink,
        "EGL display doesn't support config");
  }

  GST_OBJECT_LOCK (eglvivsink);
  gst_caps_replace (&eglvivsink->sinkcaps, caps);
  GST_OBJECT_UNLOCK (eglvivsink);
  gst_caps_unref (caps);

  return ret;
}

static inline gboolean
egl_init (GstEglVivSink * eglvivsink)
{
  if (!platform_wrapper_init ()) {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't init EGL platform wrapper");
    goto HANDLE_ERROR;
  }

  if (!gst_eglvivsink_init_egl_display (eglvivsink)) {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't init EGL display");
    goto HANDLE_ERROR;
  }

  if (!gst_eglvivsink_fill_supported_fbuffer_configs (eglvivsink)) {
    GST_ERROR_OBJECT (eglvivsink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  }

  eglvivsink->egl_started = TRUE;

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Failed to perform EGL init");
  return FALSE;
}

static gpointer
render_thread_func (GstEglVivSink * eglvivsink)
{
  GstMessage *message;
  GValue val = { 0 };
  EGLGstDataQueueItem *item = NULL;
  GstFlowReturn last_flow = GST_FLOW_OK;

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglvivsink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (eglvivsink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglvivsink, "posting ENTER stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglvivsink), message);
  g_value_unset (&val);

  eglBindAPI (EGL_OPENGL_ES_API);

  while (egl_gst_data_queue_pop (eglvivsink->queue, &item)) {
    GstMiniObject *object = item->object;

    GST_DEBUG_OBJECT (eglvivsink, "Handling object %" GST_PTR_FORMAT, object);

    if (GST_IS_CAPS (object)) {
      GstCaps *caps = GST_CAPS_CAST (object);

      if (caps != eglvivsink->configured_caps) {
        if (!gst_eglvivsink_configure_caps (eglvivsink, caps)) {
          last_flow = GST_FLOW_NOT_NEGOTIATED;
        }
      }
    } else if (GST_IS_BUFFER (object) || !object) {
      GstBuffer *buf = GST_BUFFER_CAST (object);

      if (eglvivsink->configured_caps) {
        last_flow = gst_eglvivsink_upload (eglvivsink, buf);
        if (last_flow == GST_FLOW_OK)
          last_flow = gst_eglvivsink_render (eglvivsink);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglvivsink,
            "No caps configured yet, not drawing anything");
      }
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);
    g_mutex_lock (&eglvivsink->render_lock);
    eglvivsink->last_flow = last_flow;
    g_cond_broadcast (&eglvivsink->render_cond);
    g_mutex_unlock (&eglvivsink->render_lock);

    if (last_flow != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (eglvivsink, "Successfully handled object");
  }

  if (last_flow == GST_FLOW_OK) {
    g_mutex_lock (&eglvivsink->render_lock);
    eglvivsink->last_flow = GST_FLOW_FLUSHING;
    g_cond_broadcast (&eglvivsink->render_cond);
    g_mutex_unlock (&eglvivsink->render_lock);
  }

  GST_DEBUG_OBJECT (eglvivsink, "Shutting down thread");

  /* EGL/GLES cleanup */
  gst_eglvivsink_wipe_eglglesctx (eglvivsink);

  if (eglvivsink->configured_caps) {
    gst_caps_unref (eglvivsink->configured_caps);
    eglvivsink->configured_caps = NULL;
  }

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglvivsink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (eglvivsink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglvivsink, "posting LEAVE stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglvivsink), message);
  g_value_unset (&val);

  return NULL;
}

static void
gst_eglvivsink_wipe_eglglesctx (GstEglVivSink * eglvivsink)
{
  gint i;

  glUseProgram (0);

  if (eglvivsink->have_vbo) {
    glDeleteBuffers (1, &eglvivsink->eglglesctx.position_buffer);
    glDeleteBuffers (1, &eglvivsink->eglglesctx.index_buffer);
    eglvivsink->have_vbo = FALSE;
  }

  if (eglvivsink->have_texture) {
    glDeleteTextures (eglvivsink->eglglesctx.n_textures,
        eglvivsink->eglglesctx.texture);
    eglvivsink->have_texture = FALSE;
    eglvivsink->eglglesctx.n_textures = 0;
  }

  for (i = 0; i < 2; i++) {
    if (eglvivsink->eglglesctx.glslprogram[i]) {
      glDetachShader (eglvivsink->eglglesctx.glslprogram[i],
          eglvivsink->eglglesctx.fragshader[i]);
      glDetachShader (eglvivsink->eglglesctx.glslprogram[i],
          eglvivsink->eglglesctx.vertshader[i]);
      glDeleteProgram (eglvivsink->eglglesctx.glslprogram[i]);
      glDeleteShader (eglvivsink->eglglesctx.fragshader[i]);
      glDeleteShader (eglvivsink->eglglesctx.vertshader[i]);
      eglvivsink->eglglesctx.glslprogram[i] = 0;
      eglvivsink->eglglesctx.fragshader[i] = 0;
      eglvivsink->eglglesctx.vertshader[i] = 0;
    }
  }

  gst_eglvivsink_context_make_current (eglvivsink, FALSE);

  if (eglvivsink->eglglesctx.surface) {
    eglDestroySurface (eglvivsink->eglglesctx.display,
        eglvivsink->eglglesctx.surface);
    eglvivsink->eglglesctx.surface = NULL;
    eglvivsink->have_surface = FALSE;
  }

  if (eglvivsink->eglglesctx.eglcontext) {
    eglDestroyContext (eglvivsink->eglglesctx.display,
        eglvivsink->eglglesctx.eglcontext);
    eglvivsink->eglglesctx.eglcontext = NULL;
  }
}

static GLenum gst_eglvivsink_is_format_supported (GstVideoFormat format)
{
  return gst_eglvivsink_get_viv_format(format) != 0;
}

static GLenum gst_eglvivsink_get_viv_format (GstVideoFormat format)
{
  switch (format) {
#ifdef HAVE_VIV_I420
    case GST_VIDEO_FORMAT_I420:  return GL_VIV_I420;
#endif
#ifdef HAVE_VIV_YV12
    case GST_VIDEO_FORMAT_YV12:  return GL_VIV_YV12;
#endif
#ifdef HAVE_VIV_NV12
    case GST_VIDEO_FORMAT_NV12:  return GL_VIV_NV12;
#endif
#ifdef HAVE_VIV_NV21
    case GST_VIDEO_FORMAT_NV21:  return GL_VIV_NV21;
#endif
#ifdef HAVE_VIV_YUY2
    case GST_VIDEO_FORMAT_YUY2:  return GL_VIV_YUY2;
#endif
#ifdef HAVE_VIV_UYVY
    case GST_VIDEO_FORMAT_UYVY:  return GL_VIV_UYVY;
#endif
    case GST_VIDEO_FORMAT_RGB16: return GL_RGB565;
    case GST_VIDEO_FORMAT_RGB:   return GL_RGB;
    case GST_VIDEO_FORMAT_RGBA:  return GL_RGBA;
    case GST_VIDEO_FORMAT_BGRA:  return GL_BGRA_EXT;
    case GST_VIDEO_FORMAT_RGBx:  return GL_RGBA;
    case GST_VIDEO_FORMAT_BGRx:  return GL_BGRA_EXT;
    default: return 0;
  }
}

static gint gst_eglvivsink_video_bpp (GstVideoFormat fmt)
{
  switch (fmt)
  {
    case GST_VIDEO_FORMAT_RGB16: return 2;
    case GST_VIDEO_FORMAT_RGB: return 3;
    case GST_VIDEO_FORMAT_RGBA: return 4;
    case GST_VIDEO_FORMAT_BGRA: return 4;
    case GST_VIDEO_FORMAT_RGBx: return 4;
    case GST_VIDEO_FORMAT_BGRx: return 4;
    case GST_VIDEO_FORMAT_UYVY: return 2;
    default: return 1;
  }
}

static gboolean
gst_eglvivsink_start (GstEglVivSink * eglvivsink)
{
  GError *error = NULL;

  GST_DEBUG_OBJECT (eglvivsink, "Starting");

  if (!eglvivsink->egl_started) {
    GST_ERROR_OBJECT (eglvivsink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  /* Ask for a window to render to */
  if (!eglvivsink->have_window)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (eglvivsink));

  if (!eglvivsink->have_window && !eglvivsink->create_window) {
    GST_ERROR_OBJECT (eglvivsink, "Window handle unavailable and we "
        "were instructed not to create an internal one. Bailing out.");
    goto HANDLE_ERROR;
  }

  eglvivsink->last_flow = GST_FLOW_OK;
  eglvivsink->display_region.w = 0;
  eglvivsink->display_region.h = 0;

  egl_gst_data_queue_set_flushing (eglvivsink->queue, FALSE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  eglvivsink->thread =
      g_thread_create ((GThreadFunc) render_thread_func, eglvivsink, TRUE,
      &error);
#else
  eglvivsink->thread = g_thread_try_new ("eglvivsink-render",
      (GThreadFunc) render_thread_func, eglvivsink, &error);
#endif

  if (!eglvivsink->thread || error != NULL)
    goto HANDLE_ERROR;

  GST_DEBUG_OBJECT (eglvivsink, "Started");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Couldn't start");
  g_clear_error (&error);
  return FALSE;
}

static gboolean
gst_eglvivsink_stop (GstEglVivSink * eglvivsink)
{
  GST_DEBUG_OBJECT (eglvivsink, "Stopping");

  egl_gst_data_queue_set_flushing (eglvivsink->queue, TRUE);
  g_mutex_lock (&eglvivsink->render_lock);
  g_cond_broadcast (&eglvivsink->render_cond);
  g_mutex_unlock (&eglvivsink->render_lock);

  if (eglvivsink->thread) {
    g_thread_join (eglvivsink->thread);
    eglvivsink->thread = NULL;
  }
  eglvivsink->last_flow = GST_FLOW_FLUSHING;

  if (eglvivsink->using_own_window) {
    platform_destroy_native_window (eglvivsink->eglglesctx.display,
        eglvivsink->eglglesctx.used_window, &eglvivsink->own_window_data);
    eglvivsink->eglglesctx.used_window = 0;
    eglvivsink->have_window = FALSE;
  }
  eglvivsink->eglglesctx.used_window = 0;
  if (eglvivsink->current_caps) {
    gst_caps_unref (eglvivsink->current_caps);
    eglvivsink->current_caps = NULL;
  }

  GST_DEBUG_OBJECT (eglvivsink, "Stopped");

  return TRUE;
}

static void
gst_eglvivsink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_eglvivsink_set_window_handle;
  iface->expose = gst_eglvivsink_expose;
  iface->set_render_rectangle = gst_eglvivsink_set_render_rectangle;
}

static inline gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}

static EGLNativeWindowType
gst_eglvivsink_create_window (GstEglVivSink * eglvivsink, gint width,
    gint height)
{
  EGLNativeWindowType window = 0;

  if (!eglvivsink->create_window) {
    GST_ERROR_OBJECT (eglvivsink, "This sink can't create a window by itself");
    return window;
  } else
    GST_INFO_OBJECT (eglvivsink, "Attempting internal window creation");

  window =
      platform_create_native_window (width, height,
      &eglvivsink->own_window_data);
  if (!window) {
    GST_ERROR_OBJECT (eglvivsink, "Could not create window");
    return window;
  }
  return window;
}

static void
gst_eglvivsink_expose (GstVideoOverlay * overlay)
{
  GstEglVivSink *eglvivsink;
  GstFlowReturn ret;

  eglvivsink = GST_EGLVIVSINK (overlay);
  GST_DEBUG_OBJECT (eglvivsink, "Expose catched, redisplay");

  /* Render from last seen buffer */
  ret = gst_eglvivsink_queue_object (eglvivsink, NULL);
  if (ret == GST_FLOW_ERROR)
    GST_ERROR_OBJECT (eglvivsink, "Redisplay failed");
}

/* Prints available EGL/GLES extensions 
 * If another rendering path is implemented this is the place
 * where you want to check for the availability of its supporting
 * EGL/GLES extensions.
 */
static void
gst_eglvivsink_init_egl_exts (GstEglVivSink * eglvivsink)
{
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (eglvivsink->eglglesctx.display, EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (eglvivsink, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (eglvivsink, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));
}

static gboolean
gst_eglvivsink_setup_vbo (GstEglVivSink * eglvivsink, gboolean reset)
{
  gdouble render_width, render_height;
  gdouble texture_width, texture_height;
  gdouble x1, x2, y1, y2;
  gdouble tx1, tx2, ty1, ty2;

  GST_INFO_OBJECT (eglvivsink, "VBO setup. have_vbo:%d, should reset %d",
      eglvivsink->have_vbo, reset);

  if (eglvivsink->have_vbo && reset) {
    glDeleteBuffers (1, &eglvivsink->eglglesctx.position_buffer);
    glDeleteBuffers (1, &eglvivsink->eglglesctx.index_buffer);
    eglvivsink->have_vbo = FALSE;
  }

  render_width = eglvivsink->render_region.w;
  render_height = eglvivsink->render_region.h;

  texture_width = eglvivsink->configured_info.width;
  texture_height = eglvivsink->configured_info.height;

  GST_DEBUG_OBJECT (eglvivsink, "Performing VBO setup");

  x1 = (eglvivsink->display_region.x / render_width) * 2.0 - 1;
  y1 = (eglvivsink->display_region.y / render_height) * 2.0 - 1;
  x2 = ((eglvivsink->display_region.x +
          eglvivsink->display_region.w) / render_width) * 2.0 - 1;
  y2 = ((eglvivsink->display_region.y +
          eglvivsink->display_region.h) / render_height) * 2.0 - 1;

  tx1 = (eglvivsink->crop.x / texture_width);
  tx2 = ((eglvivsink->crop.x + eglvivsink->crop.w) / texture_width);
  ty1 = (eglvivsink->crop.y / texture_height);
  ty2 = ((eglvivsink->crop.y + eglvivsink->crop.h) / texture_height);

  eglvivsink->eglglesctx.position_array[0].x = x2;
  eglvivsink->eglglesctx.position_array[0].y = y2;
  eglvivsink->eglglesctx.position_array[0].z = 0;
  eglvivsink->eglglesctx.position_array[0].a = tx2;
  eglvivsink->eglglesctx.position_array[0].b = ty1;

  eglvivsink->eglglesctx.position_array[1].x = x2;
  eglvivsink->eglglesctx.position_array[1].y = y1;
  eglvivsink->eglglesctx.position_array[1].z = 0;
  eglvivsink->eglglesctx.position_array[1].a = tx2;
  eglvivsink->eglglesctx.position_array[1].b = ty2;

  eglvivsink->eglglesctx.position_array[2].x = x1;
  eglvivsink->eglglesctx.position_array[2].y = y2;
  eglvivsink->eglglesctx.position_array[2].z = 0;
  eglvivsink->eglglesctx.position_array[2].a = tx1;
  eglvivsink->eglglesctx.position_array[2].b = ty1;

  eglvivsink->eglglesctx.position_array[3].x = x1;
  eglvivsink->eglglesctx.position_array[3].y = y1;
  eglvivsink->eglglesctx.position_array[3].z = 0;
  eglvivsink->eglglesctx.position_array[3].a = tx1;
  eglvivsink->eglglesctx.position_array[3].b = ty2;

  if (eglvivsink->display_region.x == 0) {
    /* Borders top/bottom */

    eglvivsink->eglglesctx.position_array[4 + 0].x = 1;
    eglvivsink->eglglesctx.position_array[4 + 0].y = 1;
    eglvivsink->eglglesctx.position_array[4 + 0].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 1].x = x2;
    eglvivsink->eglglesctx.position_array[4 + 1].y = y2;
    eglvivsink->eglglesctx.position_array[4 + 1].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 2].x = -1;
    eglvivsink->eglglesctx.position_array[4 + 2].y = 1;
    eglvivsink->eglglesctx.position_array[4 + 2].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 3].x = x1;
    eglvivsink->eglglesctx.position_array[4 + 3].y = y2;
    eglvivsink->eglglesctx.position_array[4 + 3].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 0].x = 1;
    eglvivsink->eglglesctx.position_array[8 + 0].y = y1;
    eglvivsink->eglglesctx.position_array[8 + 0].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 1].x = 1;
    eglvivsink->eglglesctx.position_array[8 + 1].y = -1;
    eglvivsink->eglglesctx.position_array[8 + 1].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 2].x = x1;
    eglvivsink->eglglesctx.position_array[8 + 2].y = y1;
    eglvivsink->eglglesctx.position_array[8 + 2].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 3].x = -1;
    eglvivsink->eglglesctx.position_array[8 + 3].y = -1;
    eglvivsink->eglglesctx.position_array[8 + 3].z = 0;
  } else {
    /* Borders left/right */

    eglvivsink->eglglesctx.position_array[4 + 0].x = x1;
    eglvivsink->eglglesctx.position_array[4 + 0].y = 1;
    eglvivsink->eglglesctx.position_array[4 + 0].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 1].x = x1;
    eglvivsink->eglglesctx.position_array[4 + 1].y = -1;
    eglvivsink->eglglesctx.position_array[4 + 1].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 2].x = -1;
    eglvivsink->eglglesctx.position_array[4 + 2].y = 1;
    eglvivsink->eglglesctx.position_array[4 + 2].z = 0;

    eglvivsink->eglglesctx.position_array[4 + 3].x = -1;
    eglvivsink->eglglesctx.position_array[4 + 3].y = -1;
    eglvivsink->eglglesctx.position_array[4 + 3].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 0].x = 1;
    eglvivsink->eglglesctx.position_array[8 + 0].y = 1;
    eglvivsink->eglglesctx.position_array[8 + 0].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 1].x = 1;
    eglvivsink->eglglesctx.position_array[8 + 1].y = -1;
    eglvivsink->eglglesctx.position_array[8 + 1].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 2].x = x2;
    eglvivsink->eglglesctx.position_array[8 + 2].y = y2;
    eglvivsink->eglglesctx.position_array[8 + 2].z = 0;

    eglvivsink->eglglesctx.position_array[8 + 3].x = x2;
    eglvivsink->eglglesctx.position_array[8 + 3].y = -1;
    eglvivsink->eglglesctx.position_array[8 + 3].z = 0;
  }

  eglvivsink->eglglesctx.index_array[0] = 0;
  eglvivsink->eglglesctx.index_array[1] = 1;
  eglvivsink->eglglesctx.index_array[2] = 2;
  eglvivsink->eglglesctx.index_array[3] = 3;

  glGenBuffers (1, &eglvivsink->eglglesctx.position_buffer);
  glGenBuffers (1, &eglvivsink->eglglesctx.index_buffer);
  if (got_gl_error ("glGenBuffers"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ARRAY_BUFFER, eglvivsink->eglglesctx.position_buffer);
  if (got_gl_error ("glBindBuffer position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ARRAY_BUFFER,
      sizeof (eglvivsink->eglglesctx.position_array),
      eglvivsink->eglglesctx.position_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, eglvivsink->eglglesctx.index_buffer);
  if (got_gl_error ("glBindBuffer index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ELEMENT_ARRAY_BUFFER,
      sizeof (eglvivsink->eglglesctx.index_array),
      eglvivsink->eglglesctx.index_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  eglvivsink->have_vbo = TRUE;
  GST_DEBUG_OBJECT (eglvivsink, "VBO setup done");

  return TRUE;

HANDLE_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglvivsink, "Unable to perform VBO setup");
  return FALSE;
}

/* XXX: Lock eglgles context? */
static gboolean
gst_eglvivsink_update_surface_dimensions (GstEglVivSink * eglvivsink)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (eglvivsink->eglglesctx.display,
      eglvivsink->eglglesctx.surface, EGL_WIDTH, &width);
  eglQuerySurface (eglvivsink->eglglesctx.display,
      eglvivsink->eglglesctx.surface, EGL_HEIGHT, &height);

  if (width != eglvivsink->eglglesctx.surface_width ||
      height != eglvivsink->eglglesctx.surface_height) {
    eglvivsink->eglglesctx.surface_width = width;
    eglvivsink->eglglesctx.surface_height = height;
    GST_INFO_OBJECT (eglvivsink, "Got surface of %dx%d pixels", width, height);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_eglvivsink_context_make_current (GstEglVivSink * eglvivsink,
    gboolean bind)
{
  g_assert (eglvivsink->eglglesctx.display != NULL);

  if (bind && eglvivsink->eglglesctx.surface &&
      eglvivsink->eglglesctx.eglcontext) {
    EGLContext *ctx = eglGetCurrentContext ();

    if (ctx == eglvivsink->eglglesctx.eglcontext) {
      GST_DEBUG_OBJECT (eglvivsink,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (eglvivsink, "Attaching context to thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (eglvivsink->eglglesctx.display,
            eglvivsink->eglglesctx.surface, eglvivsink->eglglesctx.surface,
            eglvivsink->eglglesctx.eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (eglvivsink, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (eglvivsink, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (eglvivsink->eglglesctx.display, EGL_NO_SURFACE,
            EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (eglvivsink, "Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
create_shader_program (GstEglVivSink * eglvivsink, GLuint * prog,
    GLuint * vert, GLuint * frag, const gchar * vert_text,
    const gchar * frag_text)
{
  GLint test;
  GLchar *info_log;

  /* Build shader program for video texture rendering */
  *vert = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (eglvivsink, "Sending %s to handle %d", vert_text, *vert);
  glShaderSource (*vert, 1, &vert_text, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (*vert);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (*vert, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglvivsink, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't compile vertex shader");
    glGetShaderiv (*vert, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*vert, test, NULL, info_log);
    GST_INFO_OBJECT (eglvivsink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *frag = glCreateShader (GL_FRAGMENT_SHADER);
  GST_DEBUG_OBJECT (eglvivsink, "Sending %s to handle %d", frag_text, *frag);
  glShaderSource (*frag, 1, &frag_text, NULL);
  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (*frag);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (*frag, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglvivsink, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't compile fragment shader");
    glGetShaderiv (*frag, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*frag, test, NULL, info_log);
    GST_INFO_OBJECT (eglvivsink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *prog = glCreateProgram ();
  if (got_gl_error ("glCreateProgram"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *vert);
  if (got_gl_error ("glAttachShader vertices"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *frag);
  if (got_gl_error ("glAttachShader fragments"))
    goto HANDLE_ERROR;
  glLinkProgram (*prog);
  glGetProgramiv (*prog, GL_LINK_STATUS, &test);
  if (test != GL_FALSE) {
    GST_DEBUG_OBJECT (eglvivsink, "GLES: Successfully linked program");
  } else {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't link program");
    goto HANDLE_ERROR;
  }

  return TRUE;

HANDLE_ERROR:
  {
    if (*frag && *prog)
      glDetachShader (*prog, *frag);
    if (*vert && *prog)
      glDetachShader (*prog, *vert);
    if (*prog)
      glDeleteProgram (*prog);
    if (*frag)
      glDeleteShader (*frag);
    if (*vert)
      glDeleteShader (*vert);
    *prog = 0;
    *frag = 0;
    *vert = 0;

    return FALSE;
  }
}

static gboolean
gst_eglvivsink_init_egl_surface (GstEglVivSink * eglvivsink)
{
  GLboolean ret;
  EGLint display_par;
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;
  gboolean free_frag_prog = FALSE;
  EGLint swap_behavior;
  gint i;

  GST_DEBUG_OBJECT (eglvivsink, "Enter EGL surface setup");

  eglvivsink->eglglesctx.surface =
      eglCreateWindowSurface (eglvivsink->eglglesctx.display,
      eglvivsink->eglglesctx.config, eglvivsink->eglglesctx.used_window,
      NULL);

  if (eglvivsink->eglglesctx.surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (eglvivsink, "Can't create surface");
    goto HANDLE_EGL_ERROR_LOCKED;
  }

  eglvivsink->eglglesctx.buffer_preserved = FALSE;
  if (eglQuerySurface (eglvivsink->eglglesctx.display,
          eglvivsink->eglglesctx.surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (eglvivsink, "Buffer swap behavior %x", swap_behavior);
    eglvivsink->eglglesctx.buffer_preserved =
        swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (eglvivsink, "Can't query buffer swap behavior");
  }

  if (!gst_eglvivsink_context_make_current (eglvivsink, TRUE))
    goto HANDLE_EGL_ERROR_LOCKED;

  gst_eglvivsink_init_egl_exts (eglvivsink);

  /* Save display's pixel aspect ratio
   *
   * DAR is reported as w/h * EGL_DISPLAY_SCALING wich is
   * a constant with value 10000. This attribute is only
   * supported if the EGL version is >= 1.2
   * XXX: Setup this as a property.
   * or some other one time check. Right now it's being called once
   * per frame.
   */
  if (eglvivsink->eglglesctx.egl_major == 1 &&
      eglvivsink->eglglesctx.egl_minor < 2) {
    GST_DEBUG_OBJECT (eglvivsink, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    eglvivsink->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (eglvivsink->eglglesctx.display,
        eglvivsink->eglglesctx.surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (eglvivsink, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d",
          eglvivsink->eglglesctx.pixel_aspect_ratio, EGL_DISPLAY_SCALING,
          EGL_DISPLAY_SCALING);
      eglvivsink->eglglesctx.pixel_aspect_ratio = EGL_DISPLAY_SCALING;
    } else {
      eglvivsink->eglglesctx.pixel_aspect_ratio = display_par;
    }
  }

  /* Save surface dims */
  gst_eglvivsink_update_surface_dimensions (eglvivsink);

  /* We have a surface! */
  eglvivsink->have_surface = TRUE;

  /* Init vertex and fragment GLSL shaders. 
   * Note: Shader compiler support is optional but we currently rely on it.
   */

  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (eglvivsink, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  /* Build shader program for video texture rendering */

  /* If the video frame is stored in a physically contiguous buffer and
   * uses a format that can be used with glTexDirectVIVMap, then the COPY
   * shader is used, since the GPU does the color space conversion
   * internally */
  // TODO: what if the incoming video frame is not using a phys buffer?
  if (gst_eglvivsink_is_format_supported (eglvivsink->configured_info.finfo->format)) {
    frag_prog = (gchar *) frag_COPY_prog;
    free_frag_prog = FALSE;
    eglvivsink->eglglesctx.n_textures = 1;
    texnames[0] = "tex";
  } else {
    switch (eglvivsink->configured_info.finfo->format) {
      case GST_VIDEO_FORMAT_AYUV:
        frag_prog = (gchar *) frag_AYUV_prog;
        free_frag_prog = FALSE;
        eglvivsink->eglglesctx.n_textures = 1;
        texnames[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_Y444:
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_Y42B:
      case GST_VIDEO_FORMAT_Y41B:
        frag_prog = (gchar *) frag_PLANAR_YUV_prog;
        free_frag_prog = FALSE;
        eglvivsink->eglglesctx.n_textures = 3;
        texnames[0] = "Ytex";
        texnames[1] = "Utex";
        texnames[2] = "Vtex";
        break;
      case GST_VIDEO_FORMAT_NV12:
        frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
        free_frag_prog = TRUE;
        eglvivsink->eglglesctx.n_textures = 2;
        texnames[0] = "Ytex";
        texnames[1] = "UVtex";
        break;
      case GST_VIDEO_FORMAT_NV21:
        frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
        free_frag_prog = TRUE;
        eglvivsink->eglglesctx.n_textures = 2;
        texnames[0] = "Ytex";
        texnames[1] = "UVtex";
        break;
      case GST_VIDEO_FORMAT_BGR:
      case GST_VIDEO_FORMAT_BGRx:
      case GST_VIDEO_FORMAT_BGRA:
        frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
        free_frag_prog = TRUE;
        eglvivsink->eglglesctx.n_textures = 1;
        texnames[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_xRGB:
      case GST_VIDEO_FORMAT_ARGB:
        frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
        free_frag_prog = TRUE;
        eglvivsink->eglglesctx.n_textures = 1;
        texnames[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_xBGR:
      case GST_VIDEO_FORMAT_ABGR:
        frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
        free_frag_prog = TRUE;
        eglvivsink->eglglesctx.n_textures = 1;
        texnames[0] = "tex";
        break;
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_RGB16:
        frag_prog = (gchar *) frag_COPY_prog;
        free_frag_prog = FALSE;
        eglvivsink->eglglesctx.n_textures = 1;
        texnames[0] = "tex";
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  if (!create_shader_program (eglvivsink,
          &eglvivsink->eglglesctx.glslprogram[0],
          &eglvivsink->eglglesctx.vertshader[0],
          &eglvivsink->eglglesctx.fragshader[0], vert_COPY_prog, frag_prog)) {
    if (free_frag_prog)
      g_free (frag_prog);
    frag_prog = NULL;
    goto HANDLE_ERROR;
  }
  if (free_frag_prog)
    g_free (frag_prog);
  frag_prog = NULL;

  eglvivsink->eglglesctx.position_loc[0] =
      glGetAttribLocation (eglvivsink->eglglesctx.glslprogram[0], "position");
  eglvivsink->eglglesctx.texpos_loc[0] =
      glGetAttribLocation (eglvivsink->eglglesctx.glslprogram[0], "texpos");
  eglvivsink->eglglesctx.tex_scale_loc[0][0] =
      glGetUniformLocation (eglvivsink->eglglesctx.glslprogram[0],
      "tex_scale0");
  eglvivsink->eglglesctx.tex_scale_loc[0][1] =
      glGetUniformLocation (eglvivsink->eglglesctx.glslprogram[0],
      "tex_scale1");
  eglvivsink->eglglesctx.tex_scale_loc[0][2] =
      glGetUniformLocation (eglvivsink->eglglesctx.glslprogram[0],
      "tex_scale2");

  glEnableVertexAttribArray (eglvivsink->eglglesctx.position_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  glEnableVertexAttribArray (eglvivsink->eglglesctx.texpos_loc[0]);
  if (got_gl_error ("glEnableVertexAttribArray"))
    goto HANDLE_ERROR;

  for (i = 0; i < eglvivsink->eglglesctx.n_textures; i++) {
    eglvivsink->eglglesctx.tex_loc[0][i] =
        glGetUniformLocation (eglvivsink->eglglesctx.glslprogram[0],
        texnames[i]);
  }

  if (!eglvivsink->eglglesctx.buffer_preserved) {
    /* Build shader program for black borders */
    if (!create_shader_program (eglvivsink,
            &eglvivsink->eglglesctx.glslprogram[1],
            &eglvivsink->eglglesctx.vertshader[1],
            &eglvivsink->eglglesctx.fragshader[1], vert_COPY_prog_no_tex,
            frag_BLACK_prog))
      goto HANDLE_ERROR;

    eglvivsink->eglglesctx.position_loc[1] =
        glGetAttribLocation (eglvivsink->eglglesctx.glslprogram[1],
        "position");

    glEnableVertexAttribArray (eglvivsink->eglglesctx.position_loc[1]);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR;
  }

  /* Generate textures */
  if (!eglvivsink->have_texture) {
    GST_INFO_OBJECT (eglvivsink, "Performing initial texture setup");

    glGenTextures (eglvivsink->eglglesctx.n_textures,
        eglvivsink->eglglesctx.texture);
    if (got_gl_error ("glGenTextures"))
      goto HANDLE_ERROR_LOCKED;

    for (i = 0; i < eglvivsink->eglglesctx.n_textures; i++) {
      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[i]);
      if (got_gl_error ("glBindTexture"))
        goto HANDLE_ERROR;

      /* Set 2D resizing params */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      /* If these are not set the texture image unit will return
       * (R, G, B, A) = black on glTexImage2D for non-POT width/height
       * frames. For a deeper explanation take a look at the OpenGL ES
       * documentation for glTexParameter */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (got_gl_error ("glTexParameteri"))
        goto HANDLE_ERROR_LOCKED;
    }

    eglvivsink->have_texture = TRUE;
  }

  glUseProgram (0);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglvivsink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR_LOCKED:
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Couldn't setup EGL surface");
  return FALSE;
}

static gboolean
gst_eglvivsink_init_egl_display (GstEglVivSink * eglvivsink)
{
  GST_DEBUG_OBJECT (eglvivsink, "Enter EGL initial configuration");

#ifdef USE_EGL_RPI
  /* See https://github.com/raspberrypi/firmware/issues/99 */
  if (!eglMakeCurrent ((EGLDisplay) 1, EGL_NO_SURFACE, EGL_NO_SURFACE,
          EGL_NO_CONTEXT)) {
    got_egl_error ("eglMakeCurrent");
    GST_ERROR_OBJECT (eglvivsink, "Couldn't unbind context");
    return FALSE;
  }
#endif
  eglvivsink->eglglesctx.display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  if (eglvivsink->eglglesctx.display == EGL_NO_DISPLAY) {
    GST_ERROR_OBJECT (eglvivsink, "Could not get EGL display connection");
    goto HANDLE_ERROR;          /* No EGL error is set by eglGetDisplay() */
  }

  if (!eglInitialize (eglvivsink->eglglesctx.display,
          &eglvivsink->eglglesctx.egl_major,
          &eglvivsink->eglglesctx.egl_minor)) {
    got_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (eglvivsink, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version
   * XXX: Need to review the version requirement in terms of the needed API
   */
  if (eglvivsink->eglglesctx.egl_major < GST_EGLVIVSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (eglvivsink, "EGL v%d needed, but you only have v%d.%d",
        GST_EGLVIVSINK_EGL_MIN_VERSION, eglvivsink->eglglesctx.egl_major,
        eglvivsink->eglglesctx.egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (eglvivsink, "System reports supported EGL version v%d.%d",
      eglvivsink->eglglesctx.egl_major, eglvivsink->eglglesctx.egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Couldn't setup window/surface from handle");
  return FALSE;
}

static gboolean
gst_eglvivsink_choose_config (GstEglVivSink * eglvivsink)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  GLint egl_configs;

  if ((eglChooseConfig (eglvivsink->eglglesctx.display,
              eglvivsink_config_attribs,
              &eglvivsink->eglglesctx.config, 1, &egl_configs)) == EGL_FALSE) {
    got_egl_error ("eglChooseConfig");
    GST_ERROR_OBJECT (eglvivsink, "eglChooseConfig failed");
    goto HANDLE_EGL_ERROR;
  }

  if (egl_configs < 1) {
    GST_ERROR_OBJECT (eglvivsink,
        "Could not find matching framebuffer config");
    goto HANDLE_ERROR;
  }

  eglvivsink->eglglesctx.eglcontext =
      eglCreateContext (eglvivsink->eglglesctx.display,
      eglvivsink->eglglesctx.config, EGL_NO_CONTEXT, con_attribs);

  if (eglvivsink->eglglesctx.eglcontext == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (eglvivsink, "Error getting context, eglCreateContext");
    goto HANDLE_EGL_ERROR;
  }

  GST_DEBUG_OBJECT (eglvivsink, "EGL Context: %p",
      eglvivsink->eglglesctx.eglcontext);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Couldn't choose an usable config");
  return FALSE;
}

static void
gst_eglvivsink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  GstEglVivSink *eglvivsink = GST_EGLVIVSINK (overlay);

  g_return_if_fail (GST_IS_EGLVIVSINK (eglvivsink));
  GST_DEBUG_OBJECT (eglvivsink, "We got a window handle: %p", (gpointer) id);

  /* OK, we have a new window */
  GST_OBJECT_LOCK (eglvivsink);
  eglvivsink->eglglesctx.window = (EGLNativeWindowType) id;
  eglvivsink->have_window = ((gpointer) id != NULL);
  GST_OBJECT_UNLOCK (eglvivsink);

  return;
}

static void
gst_eglvivsink_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GstEglVivSink *eglvivsink = GST_EGLVIVSINK (overlay);

  g_return_if_fail (GST_IS_EGLVIVSINK (eglvivsink));

  GST_OBJECT_LOCK (eglvivsink);
  eglvivsink->render_region.x = x;
  eglvivsink->render_region.y = y;
  eglvivsink->render_region.w = width;
  eglvivsink->render_region.h = height;
  eglvivsink->render_region_changed = TRUE;
  eglvivsink->render_region_user = (width != -1 && height != -1);
  GST_OBJECT_UNLOCK (eglvivsink);

  return;
}

static void
queue_item_destroy (EGLGstDataQueueItem * item)
{
  gst_mini_object_replace (&item->object, NULL);
  g_slice_free (EGLGstDataQueueItem, item);
}

static GstFlowReturn
gst_eglvivsink_queue_object (GstEglVivSink * eglvivsink, GstMiniObject * obj)
{
  EGLGstDataQueueItem *item;
  GstFlowReturn last_flow;

  g_mutex_lock (&eglvivsink->render_lock);
  last_flow = eglvivsink->last_flow;
  g_mutex_unlock (&eglvivsink->render_lock);

  if (last_flow != GST_FLOW_OK)
    return last_flow;

  item = g_slice_new0 (EGLGstDataQueueItem);

  item->object = obj ? gst_mini_object_ref (obj) : NULL;
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) queue_item_destroy;

  GST_DEBUG_OBJECT (eglvivsink, "Queueing object %" GST_PTR_FORMAT, obj);

  g_mutex_lock (&eglvivsink->render_lock);

  if (!egl_gst_data_queue_push (eglvivsink->queue, item)) {
    item->destroy (item);
    g_mutex_unlock (&eglvivsink->render_lock);
    GST_DEBUG_OBJECT (eglvivsink, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  GST_DEBUG_OBJECT (eglvivsink, "Waiting for obj to be handled");
  g_cond_wait (&eglvivsink->render_cond, &eglvivsink->render_lock);
  GST_DEBUG_OBJECT (eglvivsink, "Buffer rendered: %s",
      gst_flow_get_name (eglvivsink->last_flow));
  last_flow = eglvivsink->last_flow;
  g_mutex_unlock (&eglvivsink->render_lock);

  return (obj ? last_flow : GST_FLOW_OK);
}

static gboolean
gst_eglvivsink_crop_changed (GstEglVivSink * eglvivsink,
    GstVideoCropMeta * crop)
{
  if (crop) {
    return ((gint)(crop->x) != eglvivsink->crop.x ||
        (gint)(crop->y) != eglvivsink->crop.y ||
        (gint)(crop->width) != eglvivsink->crop.w ||
        (gint)(crop->height) != eglvivsink->crop.h);
  }

  return (eglvivsink->crop.x != 0 || eglvivsink->crop.y != 0 ||
      eglvivsink->crop.w != eglvivsink->configured_info.width ||
      eglvivsink->crop.h != eglvivsink->configured_info.height);
}

static gboolean
gst_eglvivsink_map_viv_texture (GstEglVivSink * eglvivsink, GstVideoFormat fmt, GLvoid *virt_addr, GLuint phys_addr, GLuint stride, GLuint num_extra_lines)
{
  GLenum gl_format = gst_eglvivsink_get_viv_format (fmt);
  GLuint w = eglvivsink->configured_info.width;
  GLuint h = eglvivsink->configured_info.height;

  /* stride is in bytes, we need pixels */
  GLuint total_w = stride / gst_eglvivsink_video_bpp (fmt);
  GLuint total_h = h + num_extra_lines;

  /* The glTexDirectVIVMap call has no explicit stride and padding arguments.
   * The trick is to pass width and height values that include stride and padding.
   * These are stored in total_w & total_h.
   * The ratio of length to length+stride eventually gets sent to the fragment shader
   * as a uniform. In other words, the full frame (with extra stride and padding pixels) is
   * stored in the texture, and using texture coordinate scaling, these extra pixels are
   * clipped.
   * The ratios are stored only for the first plane (-> index #0), since the direct texture
   * reads the entirety of the frame buffer (that is: all planes) automatically, so the shader
   * does not need to care about multiple planes. */

  eglvivsink->stride[0] = (gdouble) total_w / (gdouble) w;
  eglvivsink->stride[1] = 1.0;
  eglvivsink->stride[2] = 1.0;
  eglvivsink->y_stride[0] = (gdouble) total_h / (gdouble) h;
  eglvivsink->y_stride[1] = 1.0;
  eglvivsink->y_stride[2] = 1.0;

  GST_DEBUG_OBJECT (
    eglvivsink,
    "using Vivante direct texture for displaying frame:  %d x %d pixels  gst format %s  GL format 0x%x  virt addr %p  phys addr 0x%x  stride %u  extra padding lines %u  (rel strides: x %.03f y %.03f)",
    w, h,
    gst_video_format_to_string (fmt), gl_format,
    virt_addr, phys_addr,
    stride, num_extra_lines,
    eglvivsink->stride[0], eglvivsink->y_stride[0]
  );

  glActiveTexture (GL_TEXTURE0);
  if (got_gl_error ("glActiveTexture"))
    return FALSE;

  glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
  if (got_gl_error ("glBindTexture"))
    return FALSE;

  glTexDirectVIVMap (GL_TEXTURE_2D,
    total_w, total_h,
    gl_format,
    (GLvoid **)(&virt_addr), &phys_addr);
  if (got_gl_error ("glTexDirectVIVMap"))
    return FALSE;

  glTexDirectInvalidateVIV (GL_TEXTURE_2D);
  if (got_gl_error ("glTexDirectInvalidateVIV"))
    return FALSE;

  return TRUE;
}

/* Rendering and display */
static gboolean
gst_eglvivsink_fill_texture (GstEglVivSink * eglvivsink, GstBuffer * buf)
{
  GstVideoFrame vframe;
  gint w, h;

  memset (&vframe, 0, sizeof (vframe));

  if (!gst_video_frame_map (&vframe, &eglvivsink->configured_info, buf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't map frame");
    goto HANDLE_ERROR;
  }

  w = GST_VIDEO_FRAME_WIDTH (&vframe);
  h = GST_VIDEO_FRAME_HEIGHT (&vframe);

  GST_DEBUG_OBJECT (eglvivsink,
      "Got buffer %p: %dx%d size %d", buf, w, h, gst_buffer_get_size (buf));

  eglvivsink->y_stride[0] = 1;
  eglvivsink->y_stride[1] = 1;
  eglvivsink->y_stride[2] = 1;

  switch (eglvivsink->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w * 3 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width * 3 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_SHORT_5_6_5, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_AYUV:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 1));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 2);

      glActiveTexture (GL_TEXTURE2);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[2] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 2),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 2));
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride / 2;

        if (GST_ROUND_UP_8 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglvivsink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglvivsink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglvivsink->eglglesctx.texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1));
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glTexImage2D"))
    goto HANDLE_ERROR;

  gst_video_frame_unmap (&vframe);

  return TRUE;

HANDLE_ERROR:
  {
    if (vframe.buffer)
      gst_video_frame_unmap (&vframe);
    return FALSE;
  }
}

/* Rendering and display */
static GstFlowReturn
gst_eglvivsink_upload (GstEglVivSink * eglvivsink, GstBuffer * buf)
{
  GstVideoFormat fmt;
  GstVideoCropMeta *crop = NULL;
  GstFslPhysMemMeta *phys_mem_meta = NULL;

  if (!buf) {
    GST_DEBUG_OBJECT (eglvivsink, "Rendering previous buffer again");
  } else if (buf) {
    crop = gst_buffer_get_video_crop_meta (buf);

    if (gst_eglvivsink_crop_changed (eglvivsink, crop)) {
      if (crop) {
        eglvivsink->crop.x = crop->x;
        eglvivsink->crop.y = crop->y;
        eglvivsink->crop.w = crop->width;
        eglvivsink->crop.h = crop->height;
      } else {
        eglvivsink->crop.x = 0;
        eglvivsink->crop.y = 0;
        eglvivsink->crop.w = eglvivsink->configured_info.width;
        eglvivsink->crop.h = eglvivsink->configured_info.height;
      }
      eglvivsink->crop_changed = TRUE;
    }

    fmt = eglvivsink->configured_info.finfo->format;

  /* If the video frame is stored in a physically contiguous buffer and
   * uses a format that can be used with glTexDirectVIVMap, do so,
   * otherwise use gst_eglvivsink_fill_texture as a fallback */
    if (gst_eglvivsink_is_format_supported (fmt) && ((phys_mem_meta = GST_FSL_PHYS_MEM_META_GET (buf)) != 0)) {
      gboolean ret;
      GstMapInfo map_info;
      guint num_extra_lines;
      GstVideoMeta *video_meta;
      guint stride;

      /* Get the stride and number of extra lines */
      video_meta = gst_buffer_get_video_meta (buf);
      if (video_meta != NULL)
        stride = video_meta->stride[0];
      else
        stride = GST_VIDEO_INFO_PLANE_STRIDE(&(eglvivsink->configured_info), 0);

      num_extra_lines = phys_mem_meta->padding / stride;

      /* Map the buffer to get a virtual address
       * glTexDirectVIVMap() only needs this to find a corresponding physical
       * address (even when one is specified) */
      gst_buffer_map(buf, &map_info, GST_MAP_READ);
      ret = gst_eglvivsink_map_viv_texture (eglvivsink, fmt, map_info.data, (GLuint)(phys_mem_meta->phys_addr), stride, num_extra_lines);
      gst_buffer_unmap (buf, &map_info);

      if (!ret)
        goto HANDLE_ERROR;
    } else {
      if (!gst_eglvivsink_fill_texture (eglvivsink, buf))
        goto HANDLE_ERROR;
    }
  }

  return GST_FLOW_OK;

HANDLE_ERROR:
  {
    GST_ERROR_OBJECT (eglvivsink, "Failed to upload texture");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_eglvivsink_render (GstEglVivSink * eglvivsink)
{
  guint dar_n, dar_d;
  gint i;

  /* If no one has set a display rectangle on us initialize
   * a sane default. According to the docs on the xOverlay
   * interface we are supposed to fill the overlay 100%. We
   * do this trying to take PAR/DAR into account unless the
   * calling party explicitly ask us not to by setting
   * force_aspect_ratio to FALSE.
   */
  if (gst_eglvivsink_update_surface_dimensions (eglvivsink) ||
      eglvivsink->render_region_changed ||
      !eglvivsink->display_region.w || !eglvivsink->display_region.h ||
      eglvivsink->crop_changed) {
    GST_OBJECT_LOCK (eglvivsink);

    if (!eglvivsink->render_region_user) {
      eglvivsink->render_region.x = 0;
      eglvivsink->render_region.y = 0;
      eglvivsink->render_region.w = eglvivsink->eglglesctx.surface_width;
      eglvivsink->render_region.h = eglvivsink->eglglesctx.surface_height;
    }
    eglvivsink->render_region_changed = FALSE;
    eglvivsink->crop_changed = FALSE;

    if (!eglvivsink->force_aspect_ratio) {
      eglvivsink->display_region.x = 0;
      eglvivsink->display_region.y = 0;
      eglvivsink->display_region.w = eglvivsink->render_region.w;
      eglvivsink->display_region.h = eglvivsink->render_region.h;
    } else {
      GstVideoRectangle frame;

      frame.x = 0;
      frame.y = 0;

      if (!gst_video_calculate_display_ratio (&dar_n, &dar_d,
              eglvivsink->crop.w, eglvivsink->crop.h,
              eglvivsink->configured_info.par_n,
              eglvivsink->configured_info.par_d,
              eglvivsink->eglglesctx.pixel_aspect_ratio,
              EGL_DISPLAY_SCALING)) {
        GST_WARNING_OBJECT (eglvivsink, "Could not compute resulting DAR");
        frame.w = eglvivsink->crop.w;
        frame.h = eglvivsink->crop.h;
      } else {
        /* Find suitable matching new size acording to dar & par
         * rationale for prefering leaving the height untouched
         * comes from interlacing considerations.
         * XXX: Move this to gstutils?
         */
        if (eglvivsink->crop.h % dar_d == 0) {
          frame.w =
              gst_util_uint64_scale_int (eglvivsink->crop.h, dar_n, dar_d);
          frame.h = eglvivsink->crop.h;
        } else if (eglvivsink->crop.w % dar_n == 0) {
          frame.h =
              gst_util_uint64_scale_int (eglvivsink->crop.w, dar_d, dar_n);
          frame.w = eglvivsink->crop.w;
        } else {
          /* Neither width nor height can be precisely scaled.
           * Prefer to leave height untouched. See comment above.
           */
          frame.w =
              gst_util_uint64_scale_int (eglvivsink->crop.h, dar_n, dar_d);
          frame.h = eglvivsink->crop.h;
        }
      }

      gst_video_sink_center_rect (frame, eglvivsink->render_region,
          &eglvivsink->display_region, TRUE);
    }

    glViewport (eglvivsink->render_region.x,
        eglvivsink->eglglesctx.surface_height -
        eglvivsink->render_region.y -
        eglvivsink->render_region.h,
        eglvivsink->render_region.w, eglvivsink->render_region.h);

    /* Clear the surface once if its content is preserved */
    if (eglvivsink->eglglesctx.buffer_preserved) {
      glClearColor (0.0, 0.0, 0.0, 1.0);
      glClear (GL_COLOR_BUFFER_BIT);
    }

    if (!gst_eglvivsink_setup_vbo (eglvivsink, FALSE)) {
      GST_OBJECT_UNLOCK (eglvivsink);
      GST_ERROR_OBJECT (eglvivsink, "VBO setup failed");
      goto HANDLE_ERROR;
    }
    GST_OBJECT_UNLOCK (eglvivsink);
  }

  if (!eglvivsink->eglglesctx.buffer_preserved) {
    /* Draw black borders */
    GST_DEBUG_OBJECT (eglvivsink, "Drawing black border 1");
    glUseProgram (eglvivsink->eglglesctx.glslprogram[1]);

    glVertexAttribPointer (eglvivsink->eglglesctx.position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (4 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;

    GST_DEBUG_OBJECT (eglvivsink, "Drawing black border 2");

    glVertexAttribPointer (eglvivsink->eglglesctx.position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (8 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;
  }

  /* Draw video frame */
  GST_DEBUG_OBJECT (eglvivsink, "Drawing video frame");
  glUseProgram (eglvivsink->eglglesctx.glslprogram[0]);

  glUniform2f (eglvivsink->eglglesctx.tex_scale_loc[0][0],
      eglvivsink->stride[0], eglvivsink->y_stride[0]);
  glUniform2f (eglvivsink->eglglesctx.tex_scale_loc[0][1],
      eglvivsink->stride[1], eglvivsink->y_stride[1]);
  glUniform2f (eglvivsink->eglglesctx.tex_scale_loc[0][2],
      eglvivsink->stride[2], eglvivsink->y_stride[2]);

  for (i = 0; i < eglvivsink->eglglesctx.n_textures; i++) {
    glUniform1i (eglvivsink->eglglesctx.tex_loc[0][i], i);
    if (got_gl_error ("glUniform1i"))
      goto HANDLE_ERROR;
  }

  glVertexAttribPointer (eglvivsink->eglglesctx.position_loc[0], 3,
      GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (0 * sizeof (coord5)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glVertexAttribPointer (eglvivsink->eglglesctx.texpos_loc[0], 2,
      GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (3 * sizeof (gfloat)));
  if (got_gl_error ("glVertexAttribPointer"))
    goto HANDLE_ERROR;

  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (got_gl_error ("glDrawElements"))
    goto HANDLE_ERROR;

  if ((eglSwapBuffers (eglvivsink->eglglesctx.display,
              eglvivsink->eglglesctx.surface))
      == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
    goto HANDLE_ERROR;
  }


  GST_DEBUG_OBJECT (eglvivsink, "Succesfully rendered 1 frame");
  return GST_FLOW_OK;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Rendering disabled for this frame");

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_eglvivsink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglVivSink *eglvivsink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglvivsink = GST_EGLVIVSINK (vsink);
  GST_DEBUG_OBJECT (eglvivsink, "Got buffer: %p", buf);

  return gst_eglvivsink_queue_object (eglvivsink, GST_MINI_OBJECT_CAST (buf));
}

static GstCaps *
gst_eglvivsink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstEglVivSink *eglvivsink;
  GstCaps *ret = NULL;

  eglvivsink = GST_EGLVIVSINK (bsink);

  GST_OBJECT_LOCK (eglvivsink);
  if (eglvivsink->sinkcaps) {
    ret = gst_caps_ref (eglvivsink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }
  GST_OBJECT_UNLOCK (eglvivsink);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_eglvivsink_propose_allocation (G_GNUC_UNUSED GstBaseSink * bsink, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_eglvivsink_configure_caps (GstEglVivSink * eglvivsink, GstCaps * caps)
{
  gboolean ret = TRUE;
  GstVideoInfo info;

  gst_video_info_init (&info);
  if (!(ret = gst_video_info_from_caps (&info, caps))) {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't parse caps");
    goto HANDLE_ERROR;
  }

  eglvivsink->configured_info = info;
  GST_VIDEO_SINK_WIDTH (eglvivsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (eglvivsink) = info.height;

  if (eglvivsink->configured_caps) {
    GST_DEBUG_OBJECT (eglvivsink, "Caps were already set");
    if (gst_caps_can_intersect (caps, eglvivsink->configured_caps)) {
      GST_DEBUG_OBJECT (eglvivsink, "Caps are compatible anyway");
      goto SUCCEED;
    }

    GST_DEBUG_OBJECT (eglvivsink, "Caps are not compatible, reconfiguring");

    /* EGL/GLES cleanup */
    gst_eglvivsink_wipe_eglglesctx (eglvivsink);

    gst_caps_unref (eglvivsink->configured_caps);
    eglvivsink->configured_caps = NULL;
  }

  if (!gst_eglvivsink_choose_config (eglvivsink)) {
    GST_ERROR_OBJECT (eglvivsink, "Couldn't choose EGL config");
    goto HANDLE_ERROR;
  }

  gst_caps_replace (&eglvivsink->configured_caps, caps);

  /* By now the application should have set a window
   * if it meant to do so
   */
  GST_OBJECT_LOCK (eglvivsink);
  if (!eglvivsink->have_window) {
    EGLNativeWindowType window;

    GST_INFO_OBJECT (eglvivsink,
        "No window. Will attempt internal window creation");
    if (!(window =
            gst_eglvivsink_create_window (eglvivsink, info.width,
                info.height))) {
      GST_ERROR_OBJECT (eglvivsink, "Internal window creation failed!");
      GST_OBJECT_UNLOCK (eglvivsink);
      goto HANDLE_ERROR;
    }
    eglvivsink->using_own_window = TRUE;
    eglvivsink->eglglesctx.window = window;
    eglvivsink->have_window = TRUE;
  }
  GST_DEBUG_OBJECT (eglvivsink, "Using window handle %p",
      eglvivsink->eglglesctx.window);
  eglvivsink->eglglesctx.used_window = eglvivsink->eglglesctx.window;
  GST_OBJECT_UNLOCK (eglvivsink);
  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (eglvivsink),
      (guintptr) eglvivsink->eglglesctx.used_window);

  if (!eglvivsink->have_surface) {
    if (!gst_eglvivsink_init_egl_surface (eglvivsink)) {
      GST_ERROR_OBJECT (eglvivsink, "Couldn't init EGL surface from window");
      goto HANDLE_ERROR;
    }
  }

SUCCEED:
  GST_INFO_OBJECT (eglvivsink, "Configured caps successfully");
  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglvivsink, "Configuring caps failed");
  return FALSE;
}

static gboolean
gst_eglvivsink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstEglVivSink *eglvivsink;

  eglvivsink = GST_EGLVIVSINK (bsink);

  GST_DEBUG_OBJECT (eglvivsink,
      "Current caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglvivsink->current_caps, caps);

  if (gst_eglvivsink_queue_object (eglvivsink,
          GST_MINI_OBJECT_CAST (caps)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (eglvivsink, "Failed to configure caps");
    return FALSE;
  }

  gst_caps_replace (&eglvivsink->current_caps, caps);

  return TRUE;
}

static gboolean
gst_eglvivsink_open (GstEglVivSink * eglvivsink)
{
  if (!egl_init (eglvivsink)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_eglvivsink_close (GstEglVivSink * eglvivsink)
{
  if (eglvivsink->eglglesctx.display) {
    eglTerminate (eglvivsink->eglglesctx.display);
    eglvivsink->eglglesctx.display = NULL;
  }

  gst_caps_unref (eglvivsink->sinkcaps);
  eglvivsink->sinkcaps = NULL;
  eglvivsink->egl_started = FALSE;

  return TRUE;
}

static GstStateChangeReturn
gst_eglvivsink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglVivSink *eglvivsink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglvivsink = GST_EGLVIVSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_eglvivsink_open (eglvivsink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_eglvivsink_start (eglvivsink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_eglvivsink_close (eglvivsink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_eglvivsink_stop (eglvivsink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglvivsink_finalize (GObject * object)
{
  GstEglVivSink *eglvivsink;

  g_return_if_fail (GST_IS_EGLVIVSINK (object));

  eglvivsink = GST_EGLVIVSINK (object);

  if (eglvivsink->queue)
    g_object_unref (eglvivsink->queue);
  eglvivsink->queue = NULL;

  g_cond_clear (&eglvivsink->render_cond);
  g_mutex_clear (&eglvivsink->render_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglvivsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglVivSink *eglvivsink;

  g_return_if_fail (GST_IS_EGLVIVSINK (object));

  eglvivsink = GST_EGLVIVSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      eglvivsink->create_window = g_value_get_boolean (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglvivsink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglvivsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglVivSink *eglvivsink;

  g_return_if_fail (GST_IS_EGLVIVSINK (object));

  eglvivsink = GST_EGLVIVSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglvivsink->create_window);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglvivsink->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the eglvivsink's class */
static void
gst_eglvivsink_class_init (GstEglVivSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglvivsink_set_property;
  gobject_class->get_property = gst_eglvivsink_get_property;
  gobject_class->finalize = gst_eglvivsink_finalize;

  gstelement_class->change_state = gst_eglvivsink_change_state;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglvivsink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglvivsink_getcaps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_eglvivsink_propose_allocation);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_eglvivsink_show_frame);

  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "If set to true, the sink will attempt to create it's own window to "
          "render to if none is provided. This is currently only supported "
          "when the sink is used under X11",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Respect aspect ratio when scaling",
          "If set to true, the sink will attempt to preserve the incoming "
          "frame's geometry while scaling, taking both the storage's and "
          "display's pixel aspect ratio into account",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the VideoOverlay interface, using Vivante direct textures",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>, "
      "Carlos Rafael Giani <dv@pseudoterminal.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_eglvivsink_sink_template_factory));
}

static gboolean
queue_check_full_func (G_GNUC_UNUSED EGLGstDataQueue * queue, guint visible, G_GNUC_UNUSED guint bytes,
    G_GNUC_UNUSED guint64 time, G_GNUC_UNUSED gpointer checkdata)
{
  return visible != 0;
}

static void
gst_eglvivsink_init (GstEglVivSink * eglvivsink)
{
  /* Init defaults */

  /** Flags */
  eglvivsink->have_window = FALSE;
  eglvivsink->have_surface = FALSE;
  eglvivsink->have_vbo = FALSE;
  eglvivsink->have_texture = FALSE;
  eglvivsink->egl_started = FALSE;
  eglvivsink->using_own_window = FALSE;

  /** Props */
  eglvivsink->create_window = TRUE;
  eglvivsink->force_aspect_ratio = TRUE;

  g_mutex_init (&eglvivsink->render_lock);
  g_cond_init (&eglvivsink->render_cond);
  eglvivsink->queue =
      egl_gst_data_queue_new (queue_check_full_func, NULL, NULL, NULL);
  eglvivsink->last_flow = GST_FLOW_FLUSHING;

  eglvivsink->render_region.x = 0;
  eglvivsink->render_region.y = 0;
  eglvivsink->render_region.w = -1;
  eglvivsink->render_region.h = -1;
  eglvivsink->render_region_changed = TRUE;
  eglvivsink->render_region_user = FALSE;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
eglvivsink_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglvivsink_debug, "eglvivsink",
      0, "Simple EGL/GLES Sink");

#ifdef USE_EGL_RPI
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

  return gst_element_register (plugin, "eglvivsink", GST_RANK_PRIMARY + 5,
      GST_TYPE_EGLVIVSINK);
}

/* gstreamer looks for this structure to register eglvivsinks */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eglvivsink,
    "EGL/GLES sink using Vivante direct textures",
    eglvivsink_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
