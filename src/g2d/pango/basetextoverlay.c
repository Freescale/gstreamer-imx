/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2006> Julien Moutte <julien@moutte.net>
 * Copyright (C) <2006> Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2006-2008> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2009> Young-Ho Cha <ganadist@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "basetextoverlay.h"
#include "textoverlay.h"
#include "timeoverlay.h"
#include "clockoverlay.h"
#include "textrender.h"
#include "../common/phys_mem_meta.h"
#include "../common/phys_mem_allocator.h"
#include <string.h>
#include <math.h>

typedef struct
{
  enum g2d_format format;
  guint bits_per_pixel;
}
GstImxG2DFormatDetails;

enum g2d_format
gst_imx_get_g2d_format(GstVideoFormat gst_format)
{
  switch (gst_format)
  {
    case GST_VIDEO_FORMAT_RGB16: return G2D_RGB565;
    case GST_VIDEO_FORMAT_BGR16: return G2D_BGR565;

    case GST_VIDEO_FORMAT_RGB:   return G2D_RGBX8888;
    case GST_VIDEO_FORMAT_RGBA:  return G2D_RGBA8888;
    case GST_VIDEO_FORMAT_RGBx:  return G2D_RGBX8888;
    case GST_VIDEO_FORMAT_ARGB:  return G2D_ARGB8888;
    case GST_VIDEO_FORMAT_xRGB:  return G2D_XRGB8888;

    case GST_VIDEO_FORMAT_BGR:   return G2D_BGRX8888;
    case GST_VIDEO_FORMAT_BGRA:  return G2D_BGRA8888;
    case GST_VIDEO_FORMAT_BGRx:  return G2D_BGRX8888;
    case GST_VIDEO_FORMAT_ABGR:  return G2D_ABGR8888;
    case GST_VIDEO_FORMAT_xBGR:  return G2D_XBGR8888;

    default:
      g_assert_not_reached();
  }
}

static guint
gst_imx_get_bits_per_pixel(GstVideoFormat gst_format)
{
  switch (gst_format)
  {
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return 16;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      return 32;
    default:
      g_assert_not_reached();
  }
}

/* FIXME:
 *  - use proper strides and offset for I420
 *  - if text is wider than the video picture, it does not get
 *    clipped properly during blitting (if wrapping is disabled)
 */

GST_DEBUG_CATEGORY_EXTERN (pango_debug);
#define GST_CAT_DEFAULT pango_debug

#define DEFAULT_PROP_TEXT 	""
#define DEFAULT_PROP_SHADING	FALSE
#define DEFAULT_PROP_SHADING_COLOR 0xff000000
#define DEFAULT_PROP_SHADING_XPAD 6
#define DEFAULT_PROP_SHADING_YPAD 6
#define DEFAULT_PROP_VALIGNMENT	GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BASELINE
#define DEFAULT_PROP_HALIGNMENT	GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_CENTER
#define DEFAULT_PROP_XPAD	25
#define DEFAULT_PROP_YPAD	25
#define DEFAULT_PROP_DELTAX	0
#define DEFAULT_PROP_DELTAY	0
#define DEFAULT_PROP_XPOS       0.5
#define DEFAULT_PROP_YPOS       0.5
#define DEFAULT_PROP_WRAP_MODE  GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR
#define DEFAULT_PROP_FONT_DESC	""
#define DEFAULT_PROP_SILENT	FALSE
#define DEFAULT_PROP_LINE_ALIGNMENT GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER
#define DEFAULT_PROP_WAIT_TEXT	TRUE
#define DEFAULT_PROP_AUTO_ADJUST_SIZE TRUE
#define DEFAULT_PROP_VERTICAL_RENDER  FALSE
#define DEFAULT_PROP_DRAW_SHADOW TRUE
#define DEFAULT_PROP_DRAW_OUTLINE TRUE
#define DEFAULT_PROP_COLOR      0xffffffff
#define DEFAULT_PROP_OUTLINE_COLOR 0xff000000
#define DEFAULT_PROP_SHADING_VALUE    80
#define DEFAULT_PROP_TEXT_X 0
#define DEFAULT_PROP_TEXT_Y 0
#define DEFAULT_PROP_TEXT_WIDTH 1
#define DEFAULT_PROP_TEXT_HEIGHT 1

#define MINIMUM_OUTLINE_OFFSET 1.0
#define DEFAULT_SCALE_BASIS    640


enum
{
  PROP_0,
  PROP_TEXT,
  PROP_SHADING,
  PROP_SHADING_COLOR,
  PROP_SHADING_VALUE,
  PROP_SHADING_XPAD,
  PROP_SHADING_YPAD,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_XPAD,
  PROP_YPAD,
  PROP_DELTAX,
  PROP_DELTAY,
  PROP_XPOS,
  PROP_YPOS,
  PROP_X_ABSOLUTE,
  PROP_Y_ABSOLUTE,
  PROP_WRAP_MODE,
  PROP_FONT_DESC,
  PROP_SILENT,
  PROP_LINE_ALIGNMENT,
  PROP_WAIT_TEXT,
  PROP_AUTO_ADJUST_SIZE,
  PROP_VERTICAL_RENDER,
  PROP_COLOR,
  PROP_DRAW_SHADOW,
  PROP_DRAW_OUTLINE,
  PROP_OUTLINE_COLOR,
  PROP_TEXT_X,
  PROP_TEXT_Y,
  PROP_TEXT_WIDTH,
  PROP_TEXT_HEIGHT,
  PROP_LAST
};

#define BASE_TEXT_OVERLAY_CAPS \
  "video/x-raw," \
  "format = (string) { RGBx, xRGB, RGBA, ARGB, RGB } "

static GstStaticCaps sw_template_caps =
GST_STATIC_CAPS (BASE_TEXT_OVERLAY_CAPS);

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (BASE_TEXT_OVERLAY_CAPS)
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (BASE_TEXT_OVERLAY_CAPS)
    );

#define GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN (gst_imx_g2d_base_text_overlay_valign_get_type())
static GType
gst_imx_g2d_base_text_overlay_valign_get_type (void)
{
  static GType base_text_overlay_valign_type = 0;
  static const GEnumValue base_text_overlay_valign[] = {
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BASELINE, "baseline", "baseline"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BOTTOM, "bottom", "bottom"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP, "top", "top"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_POS, "position",
        "Absolute position clamped to canvas"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_CENTER, "center", "center"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_ABSOLUTE, "absolute", "Absolute position"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_valign_type) {
    base_text_overlay_valign_type =
        g_enum_register_static ("GstImxG2DBaseTextOverlayVAlign",
        base_text_overlay_valign);
  }
  return base_text_overlay_valign_type;
}

#define GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN (gst_imx_g2d_base_text_overlay_halign_get_type())
static GType
gst_imx_g2d_base_text_overlay_halign_get_type (void)
{
  static GType base_text_overlay_halign_type = 0;
  static const GEnumValue base_text_overlay_halign[] = {
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT, "left", "left"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_CENTER, "center", "center"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT, "right", "right"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_POS, "position",
        "Absolute position clamped to canvas"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_ABSOLUTE, "absolute", "Absolute position"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_halign_type) {
    base_text_overlay_halign_type =
        g_enum_register_static ("GstImxG2DBaseTextOverlayHAlign",
        base_text_overlay_halign);
  }
  return base_text_overlay_halign_type;
}


#define GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE (gst_imx_g2d_base_text_overlay_wrap_mode_get_type())
static GType
gst_imx_g2d_base_text_overlay_wrap_mode_get_type (void)
{
  static GType base_text_overlay_wrap_mode_type = 0;
  static const GEnumValue base_text_overlay_wrap_mode[] = {
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_NONE, "none", "none"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD, "word", "word"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR, "char", "char"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR, "wordchar", "wordchar"},
    {0, NULL, NULL},
  };

  if (!base_text_overlay_wrap_mode_type) {
    base_text_overlay_wrap_mode_type =
        g_enum_register_static ("GstImxG2DBaseTextOverlayWrapMode",
        base_text_overlay_wrap_mode);
  }
  return base_text_overlay_wrap_mode_type;
}

#define GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN (gst_imx_g2d_base_text_overlay_line_align_get_type())
static GType
gst_imx_g2d_base_text_overlay_line_align_get_type (void)
{
  static GType base_text_overlay_line_align_type = 0;
  static const GEnumValue base_text_overlay_line_align[] = {
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT, "left", "left"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER, "center", "center"},
    {GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT, "right", "right"},
    {0, NULL, NULL}
  };

  if (!base_text_overlay_line_align_type) {
    base_text_overlay_line_align_type =
        g_enum_register_static ("GstImxG2DBaseTextOverlayLineAlign",
        base_text_overlay_line_align);
  }
  return base_text_overlay_line_align_type;
}

#define GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_LOCK(ov) (&GST_IMX_G2D_BASE_TEXT_OVERLAY (ov)->lock)
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_COND(ov) (&GST_IMX_G2D_BASE_TEXT_OVERLAY (ov)->cond)
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK(ov)     (g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK(ov)   (g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_WAIT(ov)     (g_cond_wait (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_COND (ov), GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_LOCK (ov)))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_SIGNAL(ov)   (g_cond_signal (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_COND (ov)))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST(ov)(g_cond_broadcast (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_COND (ov)))

static GstElementClass *parent_class = NULL;
static void gst_imx_g2d_base_text_overlay_base_init (gpointer g_class);
static void gst_imx_g2d_base_text_overlay_class_init (GstImxG2DBaseTextOverlayClass * klass);
static void gst_imx_g2d_base_text_overlay_init (GstImxG2DBaseTextOverlay * overlay,
    GstImxG2DBaseTextOverlayClass * klass);

static GstStateChangeReturn gst_imx_g2d_base_text_overlay_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_imx_g2d_base_text_overlay_get_videosink_caps (GstPad * pad,
    GstImxG2DBaseTextOverlay * overlay, GstCaps * filter);
static GstCaps *gst_imx_g2d_base_text_overlay_get_src_caps (GstPad * pad,
    GstImxG2DBaseTextOverlay * overlay, GstCaps * filter);
static gboolean gst_imx_g2d_base_text_overlay_setcaps (GstImxG2DBaseTextOverlay * overlay,
    GstCaps * caps);
static gboolean gst_imx_g2d_base_text_overlay_setcaps_txt (GstImxG2DBaseTextOverlay * overlay,
    GstCaps * caps);
static gboolean gst_imx_g2d_base_text_overlay_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_imx_g2d_base_text_overlay_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_imx_g2d_base_text_overlay_video_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_imx_g2d_base_text_overlay_video_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_imx_g2d_base_text_overlay_video_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_imx_g2d_base_text_overlay_text_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_imx_g2d_base_text_overlay_text_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstPadLinkReturn gst_imx_g2d_base_text_overlay_text_pad_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_imx_g2d_base_text_overlay_text_pad_unlink (GstPad * pad,
    GstObject * parent);
static void gst_imx_g2d_base_text_overlay_pop_text (GstImxG2DBaseTextOverlay * overlay);

static void gst_imx_g2d_base_text_overlay_finalize (GObject * object);
static void gst_imx_g2d_base_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_imx_g2d_base_text_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_imx_g2d_base_text_overlay_adjust_values_with_fontdesc (GstImxG2DBaseTextOverlay * overlay,
    PangoFontDescription * desc);
static gboolean gst_imx_g2d_base_text_overlay_can_handle_caps (GstCaps * incaps);

static void
gst_imx_g2d_base_text_overlay_update_render_size (GstImxG2DBaseTextOverlay * overlay);


GType
gst_imx_g2d_base_text_overlay_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter ((gsize *) & type)) {
    static const GTypeInfo info = {
      sizeof (GstImxG2DBaseTextOverlayClass),
      (GBaseInitFunc) gst_imx_g2d_base_text_overlay_base_init,
      NULL,
      (GClassInitFunc) gst_imx_g2d_base_text_overlay_class_init,
      NULL,
      NULL,
      sizeof (GstImxG2DBaseTextOverlay),
      0,
      (GInstanceInitFunc) gst_imx_g2d_base_text_overlay_init,
      NULL,
    };

    g_once_init_leave ((gsize *) & type,
        g_type_register_static (GST_TYPE_ELEMENT, "GstImxG2DBaseTextOverlay", &info,
            0));
  }

  return type;
}

static gchar *
gst_imx_g2d_base_text_overlay_get_text (GstImxG2DBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  return g_strdup (overlay->default_text);
}

/* IMX patch */
static void
gst_imx_g2d_base_text_overlay_g2d_mem_free (GstImxG2DBaseTextOverlay * overlay)
{
  overlay->need_video_frame_surface_update = TRUE;
  overlay->need_shading_surface_clear = TRUE;

  if (overlay->g2d_text_buf) {
    if (g2d_free(overlay->g2d_text_buf) != 0)
      GST_ERROR_OBJECT(overlay, "free g2d text buffer failed");
    overlay->g2d_text_buf = NULL;
  }

  if (overlay->g2d_shading_buf) {
    if (g2d_free(overlay->g2d_shading_buf) != 0)
      GST_ERROR_OBJECT(overlay, "free g2d shading buffer failed");
    overlay->g2d_shading_buf = NULL;
  }
}

static gboolean
gst_imx_g2d_base_text_overlay_g2d_mem_allocate (GstImxG2DBaseTextOverlay * overlay,
  struct g2d_buf** buf, const gsize buf_size, const gboolean cacheable)
{
  if (buf == NULL) {
    GST_ERROR_OBJECT(overlay, "pointer to g2d buffer invalid");
    return FALSE;
  }

  *buf = g2d_alloc(buf_size, cacheable);

  if (*buf == NULL) {
    GST_ERROR_OBJECT(overlay, "g2d buffer allocation failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_imx_g2d_base_text_overlay_g2d_surface_prepare (GstImxG2DBaseTextOverlay * overlay,
  struct g2d_buf *g2d_buf, struct g2d_surface *g2d_surface)
{
  if (g2d_buf == NULL) {
    GST_ERROR_OBJECT(overlay, "g2d buffer invalid");
    return FALSE;
  }

  memset(g2d_surface, 0, sizeof(struct g2d_surface));
  /* use BGRA format as default cairo surface colorspace */
  g2d_surface->format = gst_imx_get_g2d_format(GST_VIDEO_FORMAT_BGRA);
  g2d_surface->planes[0] = (gst_imx_phys_addr_t)(g2d_buf->buf_paddr);
  g2d_surface->width = overlay->window_width;
  g2d_surface->height = overlay->window_height;
  g2d_surface->stride = g2d_surface->width;

  g2d_surface->left = 0;
  g2d_surface->top = 0;
  g2d_surface->right = g2d_surface->width;
  g2d_surface->bottom = g2d_surface->height;

  g2d_surface->blendfunc = G2D_SRC_ALPHA;
  g2d_surface->global_alpha = 0xFF;

  return TRUE;
}

static gboolean
gst_imx_g2d_base_text_overlay_g2d_mem_surface_prepare(
  GstImxG2DBaseTextOverlay * overlay,
  struct g2d_buf **g2d_buf,
    const gsize buf_size,
    const gboolean cacheable,
  struct g2d_surface *g2d_surface)
{
  if (!gst_imx_g2d_base_text_overlay_g2d_mem_allocate(overlay,
        g2d_buf, buf_size, cacheable))
  {
    GST_ERROR_OBJECT(overlay, "g2d buffer allocation failed");
    return FALSE;
  }

  if (!gst_imx_g2d_base_text_overlay_g2d_surface_prepare(overlay,
        *g2d_buf, g2d_surface))
  {
    GST_ERROR_OBJECT(overlay, "g2d surface prepare failed");
    return FALSE;
  }

  if (g2d_cache_op(*g2d_buf, G2D_CACHE_CLEAN) != 0) {
    GST_ERROR_OBJECT(overlay, "g2d surface cache clean failed");
    return FALSE;
  }

  if (g2d_cache_op(*g2d_buf, G2D_CACHE_INVALIDATE) != 0) {
    GST_ERROR_OBJECT(overlay, "g2d surface cache invalidate failed");
    return FALSE;
  }

  return TRUE;
}

static void
gst_imx_g2d_base_text_overlay_g2d_surface_reset_position(GstImxG2DBaseTextOverlay * overlay)
{
  if (overlay->want_shading
    && overlay->shading_width > 0
    && overlay->shading_height > 0)
  {
    /* reset text surface */
    overlay->g2d_shading_surface.width  = overlay->shading_width;
    overlay->g2d_shading_surface.height = overlay->shading_height;
    overlay->g2d_shading_surface.stride = overlay->g2d_shading_surface.width;

    overlay->g2d_shading_surface.left   = 0;
    overlay->g2d_shading_surface.top    = 0;
    overlay->g2d_shading_surface.right  = overlay->g2d_shading_surface.width;
    overlay->g2d_shading_surface.bottom = overlay->g2d_shading_surface.height;
  }

  /* reset text surface */
  overlay->g2d_text_surface.width  = overlay->text_width;
  overlay->g2d_text_surface.height = overlay->text_height;
  overlay->g2d_text_surface.stride = overlay->g2d_text_surface.width;

  overlay->g2d_text_surface.left   = 0;
  overlay->g2d_text_surface.top    = 0;
  overlay->g2d_text_surface.right  = overlay->g2d_text_surface.width;
  overlay->g2d_text_surface.bottom = overlay->g2d_text_surface.height;
}
/* IMX patch */

static void
gst_imx_g2d_base_text_overlay_base_init (gpointer g_class)
{
  GstImxG2DBaseTextOverlayClass *klass = GST_IMX_G2D_BASE_TEXT_OVERLAY_CLASS (g_class);
  PangoFontMap *fontmap;

  /* Only lock for the subclasses here, the base class
   * doesn't have this mutex yet and it's not necessary
   * here */
  if (klass->pango_lock)
    g_mutex_lock (klass->pango_lock);
  fontmap = pango_cairo_font_map_get_default ();
  klass->pango_context =
      pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
  pango_context_set_base_gravity (klass->pango_context, PANGO_GRAVITY_SOUTH);
  if (klass->pango_lock)
    g_mutex_unlock (klass->pango_lock);
}

static void
gst_imx_g2d_base_text_overlay_class_init (GstImxG2DBaseTextOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_imx_g2d_base_text_overlay_finalize;
  gobject_class->set_property = gst_imx_g2d_base_text_overlay_set_property;
  gobject_class->get_property = gst_imx_g2d_base_text_overlay_get_property;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_template_factory));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_change_state);

  klass->pango_lock = g_slice_new (GMutex);
  g_mutex_init (klass->pango_lock);

  klass->get_text = gst_imx_g2d_base_text_overlay_get_text;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT,
      g_param_spec_string ("text", "text",
          "Text to be display.", DEFAULT_PROP_TEXT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING,
      g_param_spec_boolean ("shaded-background", "shaded background",
          "Whether to shade the background under the text area",
          DEFAULT_PROP_SHADING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING_VALUE,
      g_param_spec_uint ("shading-value", "background shading value",
          "Shading value to apply if shaded-background is true", 1, 255,
          DEFAULT_PROP_SHADING_VALUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING_COLOR,
      g_param_spec_uint ("shading-color", "background shading color",
          "Shading color to apply if shaded-background is true (big-endian ABGR).", 0, G_MAXUINT32,
          DEFAULT_PROP_SHADING_COLOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING_XPAD,
      g_param_spec_int ("shaded-background-xpad", "horizontal padding of shaded-background",
          "Horizontal padding of shaded-background when using left/right alignment", 0, G_MAXINT,
          DEFAULT_PROP_SHADING_XPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHADING_YPAD,
      g_param_spec_int ("shaded-background-ypad", "vertical padding of shaded-background",
          "Vertical padding of shaded-background when using top/bottom alignment", 0, G_MAXINT,
          DEFAULT_PROP_SHADING_YPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN,
          DEFAULT_PROP_VALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text", GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN,
          DEFAULT_PROP_HALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_XPAD,
      g_param_spec_int ("xpad", "horizontal padding",
          "Horizontal padding when using left/right alignment", 0, G_MAXINT,
          DEFAULT_PROP_XPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_YPAD,
      g_param_spec_int ("ypad", "vertical padding",
          "Vertical padding when using top/bottom alignment", 0, G_MAXINT,
          DEFAULT_PROP_YPAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELTAX,
      g_param_spec_int ("deltax", "X position modifier",
          "Shift X position to the left or to the right. Unit is pixels.",
          G_MININT, G_MAXINT, DEFAULT_PROP_DELTAX,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DELTAY,
      g_param_spec_int ("deltay", "Y position modifier",
          "Shift Y position up or down. Unit is pixels.",
          G_MININT, G_MAXINT, DEFAULT_PROP_DELTAY,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstImxG2DBaseTextOverlay:text-x:
   *
   * Resulting X position of font rendering.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT_X,
      g_param_spec_int ("text-x", "horizontal position.",
          "Resulting X position of font rendering.", -G_MAXINT,
          G_MAXINT, DEFAULT_PROP_TEXT_X, G_PARAM_READABLE));

  /**
   * GstImxG2DBaseTextOverlay:text-y:
   *
   * Resulting Y position of font rendering.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT_Y,
      g_param_spec_int ("text-y", "vertical position",
          "Resulting X position of font rendering.", -G_MAXINT,
          G_MAXINT, DEFAULT_PROP_TEXT_Y, G_PARAM_READABLE));

  /**
   * GstImxG2DBaseTextOverlay:text-width:
   *
   * Resulting width of font rendering.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT_WIDTH,
      g_param_spec_uint ("text-width", "width",
          "Resulting width of font rendering",
          0, G_MAXINT, DEFAULT_PROP_TEXT_WIDTH, G_PARAM_READABLE));

  /**
   * GstImxG2DBaseTextOverlay:text-height:
   *
   * Resulting height of font rendering.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TEXT_HEIGHT,
      g_param_spec_uint ("text-height", "height",
          "Resulting height of font rendering", 0,
          G_MAXINT, DEFAULT_PROP_TEXT_HEIGHT, G_PARAM_READABLE));

  /**
   * GstImxG2DBaseTextOverlay:xpos:
   *
   * Horizontal position of the rendered text when using positioned alignment.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_XPOS,
      g_param_spec_double ("xpos", "horizontal position",
          "Horizontal position when using clamped position alignment", 0, 1.0,
          DEFAULT_PROP_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:ypos:
   *
   * Vertical position of the rendered text when using positioned alignment.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_YPOS,
      g_param_spec_double ("ypos", "vertical position",
          "Vertical position when using clamped position alignment", 0, 1.0,
          DEFAULT_PROP_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstImxG2DBaseTextOverlay:x-absolute:
   *
   * Horizontal position of the rendered text when using absolute alignment.
   *
   * Maps the text area to be exactly inside of video canvas for [0, 0] - [1, 1]:
   *
   * [0, 0]: Top-Lefts of video and text are aligned
   * [0.5, 0.5]: Centers are aligned
   * [1, 1]: Bottom-Rights are aligned
   *
   * Values beyond [0, 0] - [1, 1] place the text outside of the video canvas.
   *
   * Since: 1.8
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_X_ABSOLUTE,
      g_param_spec_double ("x-absolute", "horizontal position",
          "Horizontal position when using absolute alignment", -G_MAXDOUBLE,
          G_MAXDOUBLE, DEFAULT_PROP_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:y-absolute:
   *
   * See x-absolute.
   *
   * Vertical position of the rendered text when using absolute alignment.
   *
   * Since: 1.8
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_Y_ABSOLUTE,
      g_param_spec_double ("y-absolute", "vertical position",
          "Vertical position when using absolute alignment", -G_MAXDOUBLE,
          G_MAXDOUBLE, DEFAULT_PROP_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WRAP_MODE,
      g_param_spec_enum ("wrap-mode", "wrap mode",
          "Whether to wrap the text and if so how.",
          GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE, DEFAULT_PROP_WRAP_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font to be used for rendering. "
          "See documentation of pango_font_description_from_string "
          "for syntax.", DEFAULT_PROP_FONT_DESC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:color:
   *
   * Color of the rendered text.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COLOR,
      g_param_spec_uint ("color", "Color",
          "Color to use for text (big-endian ARGB).", 0, G_MAXUINT32,
          DEFAULT_PROP_COLOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DTextOverlay:outline-color:
   *
   * Color of the outline of the rendered text.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUTLINE_COLOR,
      g_param_spec_uint ("outline-color", "Text Outline Color",
          "Color to use for outline the text (big-endian ARGB).", 0,
          G_MAXUINT32, DEFAULT_PROP_OUTLINE_COLOR,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstImxG2DBaseTextOverlay:line-alignment:
   *
   * Alignment of text lines relative to each other (for multi-line text)
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LINE_ALIGNMENT,
      g_param_spec_enum ("line-alignment", "line alignment",
          "Alignment of text lines relative to each other.",
          GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN, DEFAULT_PROP_LINE_ALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:silent:
   *
   * If set, no text is rendered. Useful to switch off text rendering
   * temporarily without removing the textoverlay element from the pipeline.
   */
  /* FIXME 0.11: rename to "visible" or "text-visible" or "render-text" */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Whether to render the text string",
          DEFAULT_PROP_SILENT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:draw-shadow:
   *
   * If set, a text shadow is drawn.
   *
   * Since: 1.6
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRAW_SHADOW,
      g_param_spec_boolean ("draw-shadow", "draw-shadow",
          "Whether to draw shadow",
          DEFAULT_PROP_DRAW_SHADOW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:draw-outline:
   *
   * If set, an outline is drawn.
   *
   * Since: 1.6
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRAW_OUTLINE,
      g_param_spec_boolean ("draw-outline", "draw-outline",
          "Whether to draw outline",
          DEFAULT_PROP_DRAW_OUTLINE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstImxG2DBaseTextOverlay:wait-text:
   *
   * If set, the video will block until a subtitle is received on the text pad.
   * If video and subtitles are sent in sync, like from the same demuxer, this
   * property should be set.
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WAIT_TEXT,
      g_param_spec_boolean ("wait-text", "Wait Text",
          "Whether to wait for subtitles",
          DEFAULT_PROP_WAIT_TEXT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_AUTO_ADJUST_SIZE, g_param_spec_boolean ("auto-resize", "auto resize",
          "Automatically adjust font size to screen-size.",
          DEFAULT_PROP_AUTO_ADJUST_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VERTICAL_RENDER,
      g_param_spec_boolean ("vertical-render", "vertical render",
          "Vertical Render.", DEFAULT_PROP_VERTICAL_RENDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_imx_g2d_base_text_overlay_finalize (GObject * object)
{
  GstImxG2DBaseTextOverlay *overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (object);

  g_free (overlay->default_text);

  if (overlay->text_image) {
    gst_buffer_unref (overlay->text_image);
    overlay->text_image = NULL;
  }

  /* IMX patch */
  gst_imx_g2d_base_text_overlay_g2d_mem_free(overlay);
  /* IMX patch */

  if (overlay->layout) {
    g_object_unref (overlay->layout);
    overlay->layout = NULL;
  }

  if (overlay->text_buffer) {
    gst_buffer_unref (overlay->text_buffer);
    overlay->text_buffer = NULL;
  }

  g_mutex_clear (&overlay->lock);
  g_cond_clear (&overlay->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_imx_g2d_base_text_overlay_init (GstImxG2DBaseTextOverlay * overlay,
    GstImxG2DBaseTextOverlayClass * klass)
{
  GstPadTemplate *template;
  PangoFontDescription *desc;

  /* video sink */
  template = gst_static_pad_template_get (&video_sink_template_factory);
  overlay->video_sinkpad = gst_pad_new_from_template (template, "video_sink");
  gst_object_unref (template);
  gst_pad_set_event_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_video_event));
  gst_pad_set_chain_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_video_chain));
  gst_pad_set_query_function (overlay->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_video_query));
  GST_PAD_SET_PROXY_ALLOCATION (overlay->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->video_sinkpad);

  template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
      "text_sink");
  if (template) {
    /* text sink */
    overlay->text_sinkpad = gst_pad_new_from_template (template, "text_sink");

    gst_pad_set_event_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_text_event));
    gst_pad_set_chain_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_text_chain));
    gst_pad_set_link_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_text_pad_link));
    gst_pad_set_unlink_function (overlay->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_text_pad_unlink));
    gst_element_add_pad (GST_ELEMENT (overlay), overlay->text_sinkpad);
  }

  /* (video) source */
  template = gst_static_pad_template_get (&src_template_factory);
  overlay->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_event_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_src_event));
  gst_pad_set_query_function (overlay->srcpad,
      GST_DEBUG_FUNCPTR (gst_imx_g2d_base_text_overlay_src_query));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
  overlay->layout =
      pango_layout_new (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS
      (overlay)->pango_context);
  desc =
      pango_context_get_font_description (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS
      (overlay)->pango_context);
  gst_imx_g2d_base_text_overlay_adjust_values_with_fontdesc (overlay, desc);

  overlay->color = DEFAULT_PROP_COLOR;
  overlay->outline_color = DEFAULT_PROP_OUTLINE_COLOR;
  overlay->halign = DEFAULT_PROP_HALIGNMENT;
  overlay->valign = DEFAULT_PROP_VALIGNMENT;
  overlay->xpad = DEFAULT_PROP_XPAD;
  overlay->ypad = DEFAULT_PROP_YPAD;
  overlay->deltax = DEFAULT_PROP_DELTAX;
  overlay->deltay = DEFAULT_PROP_DELTAY;
  overlay->xpos = DEFAULT_PROP_XPOS;
  overlay->ypos = DEFAULT_PROP_YPOS;

  overlay->wrap_mode = DEFAULT_PROP_WRAP_MODE;

  overlay->want_shading = DEFAULT_PROP_SHADING;
  overlay->shading_color = DEFAULT_PROP_SHADING_COLOR;
  overlay->shading_value = DEFAULT_PROP_SHADING_VALUE;
  overlay->shading_xpad = DEFAULT_PROP_SHADING_XPAD;
  overlay->shading_ypad = DEFAULT_PROP_SHADING_YPAD;
  overlay->silent = DEFAULT_PROP_SILENT;
  overlay->draw_shadow = DEFAULT_PROP_DRAW_SHADOW;
  overlay->draw_outline = DEFAULT_PROP_DRAW_OUTLINE;
  overlay->wait_text = DEFAULT_PROP_WAIT_TEXT;
  overlay->auto_adjust_size = DEFAULT_PROP_AUTO_ADJUST_SIZE;

  overlay->default_text = g_strdup (DEFAULT_PROP_TEXT);
  overlay->need_render = TRUE;
  overlay->text_image = NULL;
  /* IMX patch */
  overlay->g2d_text_buf = NULL;
  overlay->g2d_shading_buf = NULL;
  overlay->need_video_frame_surface_update = TRUE;
  overlay->need_shading_surface_clear = TRUE;
  /* IMX patch */
  overlay->use_vertical_render = DEFAULT_PROP_VERTICAL_RENDER;

  overlay->line_align = DEFAULT_PROP_LINE_ALIGNMENT;
  pango_layout_set_alignment (overlay->layout,
      (PangoAlignment) overlay->line_align);

  overlay->text_buffer = NULL;
  overlay->text_linked = FALSE;

  overlay->width = 1;
  overlay->height = 1;

  overlay->window_width = 1;
  overlay->window_height = 1;

  overlay->text_width = DEFAULT_PROP_TEXT_WIDTH;
  overlay->text_height = DEFAULT_PROP_TEXT_HEIGHT;

  overlay->text_left = DEFAULT_PROP_TEXT_X;
  overlay->text_top = DEFAULT_PROP_TEXT_Y;

  overlay->render_width = 1;
  overlay->render_height = 1;
  overlay->render_scale = 1.0l;

  g_mutex_init (&overlay->lock);
  g_cond_init (&overlay->cond);
  gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
  g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
}

static void
gst_imx_g2d_base_text_overlay_set_wrap_mode (GstImxG2DBaseTextOverlay * overlay, gint width)
{
  if (overlay->wrap_mode == GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_NONE) {
    GST_DEBUG_OBJECT (overlay, "Set wrap mode NONE");
    pango_layout_set_width (overlay->layout, -1);
  } else {
    width = width * PANGO_SCALE;

    GST_DEBUG_OBJECT (overlay, "Set layout width %d", width);
    GST_DEBUG_OBJECT (overlay, "Set wrap mode    %d", overlay->wrap_mode);
    pango_layout_set_width (overlay->layout, width);
  }

  pango_layout_set_wrap (overlay->layout, (PangoWrapMode) overlay->wrap_mode);
}

static gboolean
gst_imx_g2d_base_text_overlay_setcaps_txt (GstImxG2DBaseTextOverlay * overlay, GstCaps * caps)
{
  GstStructure *structure;
  const gchar *format;

  structure = gst_caps_get_structure (caps, 0);
  format = gst_structure_get_string (structure, "format");
  overlay->have_pango_markup = (strcmp (format, "pango-markup") == 0);

  return TRUE;
}

/* only negotiate/query video overlay composition support for now */
static gboolean
gst_imx_g2d_base_text_overlay_negotiate (GstImxG2DBaseTextOverlay * overlay, GstCaps * caps)
{
  gboolean upstream_has_meta = FALSE;
  gboolean caps_has_meta = FALSE;
  gboolean alloc_has_meta = FALSE;
  gboolean attach = FALSE;
  gboolean ret = TRUE;
  guint width, height;
  GstCapsFeatures *f;
  GstCaps *overlay_caps;
  GstQuery *query;
  guint alloc_index;
  gsize buffer_size;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  /* Clear any pending reconfigure to avoid negotiating twice */
  gst_pad_check_reconfigure (overlay->srcpad);

  if (!caps)
    caps = gst_pad_get_current_caps (overlay->video_sinkpad);
  else
    gst_caps_ref (caps);

  if (!caps || gst_caps_is_empty (caps))
    goto no_format;

  /* Check if upstream caps have meta */
  if ((f = gst_caps_get_features (caps, 0))) {
    upstream_has_meta = gst_caps_features_contains (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  }

  /* Initialize dimensions */
  width = overlay->width;
  height = overlay->height;

  if (upstream_has_meta) {
    overlay_caps = gst_caps_ref (caps);
  } else {
    GstCaps *peercaps;

    /* BaseTransform requires caps for the allocation query to work */
    overlay_caps = gst_caps_copy (caps);
    f = gst_caps_get_features (overlay_caps, 0);
    gst_caps_features_add (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    /* Then check if downstream accept overlay composition in caps */
    /* FIXME: We should probably check if downstream *prefers* the
     * overlay meta, and only enforce usage of it if we can't handle
     * the format ourselves and thus would have to drop the overlays.
     * Otherwise we should prefer what downstream wants here.
     */
    peercaps = gst_pad_peer_query_caps (overlay->srcpad, NULL);
    caps_has_meta = gst_caps_can_intersect (peercaps, overlay_caps);
    gst_caps_unref (peercaps);

    GST_DEBUG ("caps have overlay meta %d", caps_has_meta);
  }

  if (upstream_has_meta || caps_has_meta) {
    /* Send caps immediatly, it's needed by GstBaseTransform to get a reply
     * from allocation query */
    ret = gst_pad_set_caps (overlay->srcpad, overlay_caps);

    /* First check if the allocation meta has compositon */
    query = gst_query_new_allocation (overlay_caps, FALSE);

    if (!gst_pad_peer_query (overlay->srcpad, query)) {
      /* no problem, we use the query defaults */
      GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");

      /* In case we were flushing, mark reconfigure and fail this method,
       * will make it retry */
      if (overlay->video_flushing)
        ret = FALSE;
    }

    alloc_has_meta = gst_query_find_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &alloc_index);

    GST_DEBUG ("sink alloc has overlay meta %d", alloc_has_meta);

    if (alloc_has_meta) {
      const GstStructure *params;

      gst_query_parse_nth_allocation_meta (query, alloc_index, &params);
      if (params) {
        if (gst_structure_get (params, "width", G_TYPE_UINT, &width,
                "height", G_TYPE_UINT, &height, NULL)) {
          GST_DEBUG ("received window size: %dx%d", width, height);
          g_assert (width != 0 && height != 0);
        }
      }
    }

    gst_query_unref (query);
  }

  /* Update render size if needed */
  overlay->window_width = width;
  overlay->window_height = height;
  gst_imx_g2d_base_text_overlay_update_render_size (overlay);

  /* For backward compatbility, we will prefer bliting if downstream
   * allocation does not support the meta. In other case we will prefer
   * attaching, and will fail the negotiation in the unlikely case we are
   * force to blit, but format isn't supported. */

  if (upstream_has_meta) {
    attach = TRUE;
  } else if (caps_has_meta) {
    if (alloc_has_meta) {
      attach = TRUE;
    } else {
      /* Don't attach unless we cannot handle the format */
      attach = !gst_imx_g2d_base_text_overlay_can_handle_caps (caps);
    }
  } else {
    ret = gst_imx_g2d_base_text_overlay_can_handle_caps (caps);
  }

  /* If we attach, then pick the overlay caps */
  if (attach) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, overlay_caps);
    /* Caps where already sent */
  } else if (ret) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, caps);
    ret = gst_pad_set_caps (overlay->srcpad, caps);
  }

  if (!ret) {
    GST_DEBUG_OBJECT (overlay, "negotiation failed, schedule reconfigure");
    gst_pad_mark_reconfigure (overlay->srcpad);
  }

  gst_caps_unref (overlay_caps);
  gst_caps_unref (caps);

  /* IMX patch */
  gst_imx_g2d_base_text_overlay_g2d_mem_free(overlay);

  buffer_size = 4 * overlay->window_width * overlay->window_height;

  /* initialize g2d text buffer and surface */
  if (!gst_imx_g2d_base_text_overlay_g2d_mem_surface_prepare(overlay,
        &(overlay->g2d_text_buf), buffer_size, FALSE,
        &(overlay->g2d_text_surface)))
  {
      GST_ERROR_OBJECT(overlay, "g2d text buffer and surface failed");
      goto g2d_fail;
  }

  /* initialize g2d shading buffer and surface */
  if (!gst_imx_g2d_base_text_overlay_g2d_mem_surface_prepare(overlay,
        &(overlay->g2d_shading_buf), buffer_size, TRUE,
        &(overlay->g2d_shading_surface)))
  {
      GST_ERROR_OBJECT(overlay, "g2d shading buffer and surface failed");
      goto g2d_fail;
  }
  /* IMX patch */

  return ret;

g2d_fail:
  gst_imx_g2d_base_text_overlay_g2d_mem_free(overlay);
  return FALSE;

no_format:
  {
    if (caps)
      gst_caps_unref (caps);
    return FALSE;
  }
}

static gboolean
gst_imx_g2d_base_text_overlay_can_handle_caps (GstCaps * incaps)
{
  gboolean ret;
  GstCaps *caps;
  static GstStaticCaps static_caps = GST_STATIC_CAPS (BASE_TEXT_OVERLAY_CAPS);

  caps = gst_static_caps_get (&static_caps);
  ret = gst_caps_is_subset (incaps, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_imx_g2d_base_text_overlay_setcaps (GstImxG2DBaseTextOverlay * overlay, GstCaps * caps)
{
  GstVideoInfo info;
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  /* Render again if size have changed */
  if (GST_VIDEO_INFO_WIDTH (&info) != GST_VIDEO_INFO_WIDTH (&overlay->info) ||
      GST_VIDEO_INFO_HEIGHT (&info) != GST_VIDEO_INFO_HEIGHT (&overlay->info))
    overlay->need_render = TRUE;

  overlay->info = info;
  overlay->format = GST_VIDEO_INFO_FORMAT (&info);
  overlay->width = GST_VIDEO_INFO_WIDTH (&info);
  overlay->height = GST_VIDEO_INFO_HEIGHT (&info);

  ret = gst_imx_g2d_base_text_overlay_negotiate (overlay, caps);

  GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);

  if (!gst_imx_g2d_base_text_overlay_can_handle_caps (caps)) {
    GST_DEBUG_OBJECT (overlay, "unsupported caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }
  GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (overlay, "could not parse caps");
    return FALSE;
  }
}

static void
gst_imx_g2d_base_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImxG2DBaseTextOverlay *overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (object);

  GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
  switch (prop_id) {
    case PROP_TEXT:
      g_free (overlay->default_text);
      overlay->default_text = g_value_dup_string (value);
      break;
    case PROP_SHADING:
      overlay->need_shading_surface_clear = (overlay->want_shading != value) && value;
      overlay->want_shading = g_value_get_boolean (value);
      break;
    case PROP_XPAD:
      overlay->xpad = g_value_get_int (value);
      break;
    case PROP_YPAD:
      overlay->ypad = g_value_get_int (value);
      break;
    case PROP_DELTAX:
      overlay->deltax = g_value_get_int (value);
      break;
    case PROP_DELTAY:
      overlay->deltay = g_value_get_int (value);
      break;
    case PROP_XPOS:
      overlay->xpos = g_value_get_double (value);
      break;
    case PROP_YPOS:
      overlay->ypos = g_value_get_double (value);
      break;
    case PROP_X_ABSOLUTE:
      overlay->xpos = g_value_get_double (value);
      break;
    case PROP_Y_ABSOLUTE:
      overlay->ypos = g_value_get_double (value);
      break;
    case PROP_VALIGNMENT:
      overlay->valign = g_value_get_enum (value);
      break;
    case PROP_HALIGNMENT:
      overlay->halign = g_value_get_enum (value);
      break;
    case PROP_WRAP_MODE:
      overlay->wrap_mode = g_value_get_enum (value);
      break;
    case PROP_FONT_DESC:
    {
      PangoFontDescription *desc;
      const gchar *fontdesc_str;

      fontdesc_str = g_value_get_string (value);
      g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      desc = pango_font_description_from_string (fontdesc_str);
      if (desc) {
        GST_LOG_OBJECT (overlay, "font description set: %s", fontdesc_str);
        pango_layout_set_font_description (overlay->layout, desc);
        gst_imx_g2d_base_text_overlay_adjust_values_with_fontdesc (overlay, desc);
        pango_font_description_free (desc);
      } else {
        GST_WARNING_OBJECT (overlay, "font description parse failed: %s",
            fontdesc_str);
      }
      g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    }
    case PROP_COLOR:
      overlay->color = g_value_get_uint (value);
      break;
    case PROP_OUTLINE_COLOR:
      overlay->outline_color = g_value_get_uint (value);
      break;
    case PROP_SILENT:
      overlay->silent = g_value_get_boolean (value);
      break;
    case PROP_DRAW_SHADOW:
      overlay->draw_shadow = g_value_get_boolean (value);
      break;
    case PROP_DRAW_OUTLINE:
      overlay->draw_outline = g_value_get_boolean (value);
      break;
    case PROP_LINE_ALIGNMENT:
      overlay->line_align = g_value_get_enum (value);
      g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      pango_layout_set_alignment (overlay->layout,
          (PangoAlignment) overlay->line_align);
      g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    case PROP_WAIT_TEXT:
      overlay->wait_text = g_value_get_boolean (value);
      break;
    case PROP_AUTO_ADJUST_SIZE:
      overlay->auto_adjust_size = g_value_get_boolean (value);
      break;
    case PROP_VERTICAL_RENDER:
      overlay->use_vertical_render = g_value_get_boolean (value);
      if (overlay->use_vertical_render) {
        overlay->valign = GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP;
        overlay->halign = GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT;
        overlay->line_align = GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT;
        g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
        pango_layout_set_alignment (overlay->layout,
            (PangoAlignment) overlay->line_align);
        g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      }
      break;
    case PROP_SHADING_VALUE:
      overlay->shading_value = g_value_get_uint (value);
      overlay->need_shading_surface_clear = TRUE;
      break;
    case PROP_SHADING_COLOR:
      overlay->shading_color = g_value_get_uint (value);
      overlay->need_shading_surface_clear = TRUE;
      break;
    case PROP_SHADING_XPAD:
      overlay->shading_xpad = g_value_get_int (value);
      overlay->need_shading_surface_clear = TRUE;
      break;
    case PROP_SHADING_YPAD:
      overlay->shading_ypad = g_value_get_int (value);
      overlay->need_shading_surface_clear = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  overlay->need_render = TRUE;
  GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
}

static void
gst_imx_g2d_base_text_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImxG2DBaseTextOverlay *overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (object);

  GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
  switch (prop_id) {
    case PROP_TEXT:
      g_value_set_string (value, overlay->default_text);
      break;
    case PROP_SHADING:
      g_value_set_boolean (value, overlay->want_shading);
      break;
    case PROP_XPAD:
      g_value_set_int (value, overlay->xpad);
      break;
    case PROP_YPAD:
      g_value_set_int (value, overlay->ypad);
      break;
    case PROP_DELTAX:
      g_value_set_int (value, overlay->deltax);
      break;
    case PROP_DELTAY:
      g_value_set_int (value, overlay->deltay);
      break;
    case PROP_XPOS:
      g_value_set_double (value, overlay->xpos);
      break;
    case PROP_YPOS:
      g_value_set_double (value, overlay->ypos);
      break;
    case PROP_X_ABSOLUTE:
      g_value_set_double (value, overlay->xpos);
      break;
    case PROP_Y_ABSOLUTE:
      g_value_set_double (value, overlay->ypos);
      break;
    case PROP_VALIGNMENT:
      g_value_set_enum (value, overlay->valign);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, overlay->halign);
      break;
    case PROP_WRAP_MODE:
      g_value_set_enum (value, overlay->wrap_mode);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, overlay->silent);
      break;
    case PROP_DRAW_SHADOW:
      g_value_set_boolean (value, overlay->draw_shadow);
      break;
    case PROP_DRAW_OUTLINE:
      g_value_set_boolean (value, overlay->draw_outline);
      break;
    case PROP_LINE_ALIGNMENT:
      g_value_set_enum (value, overlay->line_align);
      break;
    case PROP_WAIT_TEXT:
      g_value_set_boolean (value, overlay->wait_text);
      break;
    case PROP_AUTO_ADJUST_SIZE:
      g_value_set_boolean (value, overlay->auto_adjust_size);
      break;
    case PROP_VERTICAL_RENDER:
      g_value_set_boolean (value, overlay->use_vertical_render);
      break;
    case PROP_COLOR:
      g_value_set_uint (value, overlay->color);
      break;
    case PROP_OUTLINE_COLOR:
      g_value_set_uint (value, overlay->outline_color);
      break;
    case PROP_SHADING_VALUE:
      g_value_set_uint (value, overlay->shading_value);
      break;
    case PROP_SHADING_COLOR:
      g_value_set_uint (value, overlay->shading_color);
      break;
    case PROP_SHADING_XPAD:
      g_value_set_int (value, overlay->shading_xpad);
      break;
    case PROP_SHADING_YPAD:
      g_value_set_int (value, overlay->shading_ypad);
      break;
    case PROP_FONT_DESC:
    {
      const PangoFontDescription *desc;

      g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      desc = pango_layout_get_font_description (overlay->layout);
      if (!desc)
        g_value_set_string (value, "");
      else {
        g_value_take_string (value, pango_font_description_to_string (desc));
      }
      g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
      break;
    }
    case PROP_TEXT_X:
      g_value_set_int (value, overlay->text_left);
      break;
    case PROP_TEXT_Y:
      g_value_set_int (value, overlay->text_top);
      break;
    case PROP_TEXT_WIDTH:
      g_value_set_uint (value, overlay->text_width);
      break;
    case PROP_TEXT_HEIGHT:
      g_value_set_uint (value, overlay->text_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  overlay->need_render = TRUE;
  GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
}

static gboolean
gst_imx_g2d_base_text_overlay_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstImxG2DBaseTextOverlay *overlay;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_imx_g2d_base_text_overlay_get_src_caps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static void
gst_imx_g2d_base_text_overlay_update_render_size (GstImxG2DBaseTextOverlay * overlay)
{
  gdouble video_aspect = (gdouble) overlay->width / (gdouble) overlay->height;
  gdouble window_aspect = (gdouble) overlay->window_width /
      (gdouble) overlay->window_height;

  guint text_buffer_width = 0;
  guint text_buffer_height = 0;

  if (video_aspect >= window_aspect) {
    text_buffer_width = overlay->window_width;
    text_buffer_height = window_aspect * overlay->window_height / video_aspect;
  } else if (video_aspect < window_aspect) {
    text_buffer_width = video_aspect * overlay->window_width / window_aspect;
    text_buffer_height = overlay->window_height;
  }

  if ((overlay->render_width == text_buffer_width) &&
      (overlay->render_height == text_buffer_height))
    return;

  overlay->need_render = TRUE;
  overlay->render_width = text_buffer_width;
  overlay->render_height = text_buffer_height;
  overlay->render_scale = (gdouble) overlay->render_width /
      (gdouble) overlay->width;

  GST_DEBUG ("updating render dimensions %dx%d from stream %dx%d, window %dx%d "
      "and render scale %f", overlay->render_width, overlay->render_height,
      overlay->width, overlay->height, overlay->window_width,
      overlay->window_height, overlay->render_scale);
}

static gboolean
gst_imx_g2d_base_text_overlay_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstImxG2DBaseTextOverlay *overlay;
  gboolean ret;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  if (overlay->text_linked) {
    gst_event_ref (event);
    ret = gst_pad_push_event (overlay->video_sinkpad, event);
    gst_pad_push_event (overlay->text_sinkpad, event);
  } else {
    ret = gst_pad_push_event (overlay->video_sinkpad, event);
  }

  return ret;
}

/**
 * gst_imx_g2d_base_text_overlay_add_feature_and_intersect:
 *
 * Creates a new #GstCaps containing the (given caps +
 * given caps feature) + (given caps intersected by the
 * given filter).
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_imx_g2d_base_text_overlay_add_feature_and_intersect (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_copy (caps);

  caps_size = gst_caps_get_size (new_caps);
  for (i = 0; i < caps_size; i++) {
    GstCapsFeatures *features = gst_caps_get_features (new_caps, i);

    if (!gst_caps_features_is_any (features)) {
      gst_caps_features_add (features, feature);
    }
  }

  gst_caps_append (new_caps, gst_caps_intersect_full (caps,
          filter, GST_CAPS_INTERSECT_FIRST));

  return new_caps;
}

/**
 * gst_imx_g2d_base_text_overlay_intersect_by_feature:
 *
 * Creates a new #GstCaps based on the following filtering rule.
 *
 * For each individual caps contained in given caps, if the
 * caps uses the given caps feature, keep a version of the caps
 * with the feature and an another one without. Otherwise, intersect
 * the caps with the given filter.
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_imx_g2d_base_text_overlay_intersect_by_feature (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_new_empty ();

  caps_size = gst_caps_get_size (caps);
  for (i = 0; i < caps_size; i++) {
    GstStructure *caps_structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *caps_features =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *filtered_caps;
    GstCaps *simple_caps =
        gst_caps_new_full (gst_structure_copy (caps_structure), NULL);
    gst_caps_set_features (simple_caps, 0, caps_features);

    if (gst_caps_features_contains (caps_features, feature)) {
      gst_caps_append (new_caps, gst_caps_copy (simple_caps));

      gst_caps_features_remove (caps_features, feature);
      filtered_caps = gst_caps_ref (simple_caps);
    } else {
      filtered_caps = gst_caps_intersect_full (simple_caps, filter,
          GST_CAPS_INTERSECT_FIRST);
    }

    gst_caps_unref (simple_caps);
    gst_caps_append (new_caps, filtered_caps);
  }

  return new_caps;
}

static GstCaps *
gst_imx_g2d_base_text_overlay_get_videosink_caps (GstPad * pad,
    GstImxG2DBaseTextOverlay * overlay, GstCaps * filter)
{
  GstPad *srcpad = overlay->srcpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (G_UNLIKELY (!overlay))
    return gst_pad_get_pad_template_caps (pad);

  if (filter) {
    /* filter caps + composition feature + filter caps
     * filtered by the software caps. */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter = gst_imx_g2d_base_text_overlay_add_feature_and_intersect (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);

    GST_DEBUG_OBJECT (overlay, "overlay filter %" GST_PTR_FORMAT,
        overlay_filter);
  }

  peer_caps = gst_pad_peer_query_caps (srcpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {
      /* if peer returns ANY caps, return filtered src pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (srcpad));
    } else {

      /* duplicate caps which contains the composition into one version with
       * the meta and one without. Filter the other caps by the software caps */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_imx_g2d_base_text_overlay_intersect_by_feature (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full (filter, caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstCaps *
gst_imx_g2d_base_text_overlay_get_src_caps (GstPad * pad, GstImxG2DBaseTextOverlay * overlay,
    GstCaps * filter)
{
  GstPad *sinkpad = overlay->video_sinkpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (G_UNLIKELY (!overlay))
    return gst_pad_get_pad_template_caps (pad);

  if (filter) {
    /* duplicate filter caps which contains the composition into one version
     * with the meta and one without. Filter the other caps by the software
     * caps */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter =
        gst_imx_g2d_base_text_overlay_intersect_by_feature (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);
  }

  peer_caps = gst_pad_peer_query_caps (sinkpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {

      /* if peer returns ANY caps, return filtered sink pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (sinkpad));

    } else {

      /* return upstream caps + composition feature + upstream caps
       * filtered by the software caps. */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_imx_g2d_base_text_overlay_add_feature_and_intersect (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }
  GST_DEBUG_OBJECT (overlay, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static void
gst_imx_g2d_base_text_overlay_adjust_values_with_fontdesc (GstImxG2DBaseTextOverlay * overlay,
    PangoFontDescription * desc)
{
  gint font_size = pango_font_description_get_size (desc) / PANGO_SCALE;
  overlay->shadow_offset = (double) (font_size) / 13.0;
  overlay->outline_offset = (double) (font_size) / 15.0;
  if (overlay->outline_offset < MINIMUM_OUTLINE_OFFSET)
    overlay->outline_offset = MINIMUM_OUTLINE_OFFSET;
}

static void
gst_imx_g2d_base_text_overlay_update_pos (GstImxG2DBaseTextOverlay * overlay)
{
  gint xpos, ypos, width, height;

  width = overlay->logical_rect.width;
  height = overlay->logical_rect.height;

  xpos = overlay->ink_rect.x - overlay->logical_rect.x;
  switch (overlay->halign) {
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT:
      xpos += overlay->xpad;
      xpos = MAX (0, xpos);
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_CENTER:
      xpos += (overlay->width - width) / 2;
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT:
      xpos += overlay->width - width - overlay->xpad;
      xpos = MIN (overlay->width - overlay->ink_rect.width, xpos);
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_POS:
      xpos += (gint) (overlay->width * overlay->xpos) - width / 2;
      xpos = CLAMP (xpos, 0, overlay->width - overlay->ink_rect.width);
      if (xpos < 0)
        xpos = 0;
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_ABSOLUTE:
      xpos = (overlay->width - overlay->text_width) * overlay->xpos;
      break;
    default:
      xpos = 0;
  }
  xpos += overlay->deltax;

  ypos = overlay->ink_rect.y - overlay->logical_rect.y;
  switch (overlay->valign) {
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BOTTOM:
      /* This will be the same as baseline, if there is enough padding,
       * otherwise it will avoid clipping the text */
      ypos += overlay->height - height - overlay->ypad;
      ypos = MIN (overlay->height - overlay->ink_rect.height, ypos);
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BASELINE:
      ypos += overlay->height - height - overlay->ypad;
      /* Don't clip, this would not respect the base line */
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP:
      ypos += overlay->ypad;
      ypos = MAX (0.0, ypos);
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_POS:
      ypos = (gint) (overlay->height * overlay->ypos) - height / 2;
      ypos = CLAMP (ypos, 0, overlay->height - overlay->ink_rect.height);
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_ABSOLUTE:
      ypos = (overlay->height - overlay->text_height) * overlay->ypos;
      break;
    case GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_CENTER:
      ypos = (overlay->height - height) / 2;
      break;
    default:
      ypos = overlay->ypad;
      break;
  }
  ypos += overlay->deltay;

  overlay->text_left = CLAMP (xpos, 0, overlay->window_width);
  overlay->text_top = CLAMP (ypos, 0, overlay->window_height);
  overlay->text_right = CLAMP (overlay->text_left + overlay->text_width, 0, overlay->window_width);
  overlay->text_bottom = CLAMP(overlay->text_top + overlay->text_height, 0, overlay->window_height);

  GST_DEBUG_OBJECT (overlay, "Placing overlay at (%d, %d)", xpos, ypos);

  overlay->shading_left = CLAMP(overlay->text_left - overlay->shading_xpad, 0, overlay->window_width);
  overlay->shading_top = CLAMP(overlay->text_top - overlay->shading_ypad, 0, overlay->window_height);
  overlay->shading_right = CLAMP(overlay->text_right + overlay->shading_xpad, 0, overlay->window_width);
  overlay->shading_bottom = CLAMP(overlay->text_bottom + overlay->shading_ypad, 0, overlay->window_height);
  overlay->shading_width = MAX(overlay->shading_right - overlay->shading_left, 0);
  overlay->shading_height = MAX(overlay->shading_bottom - overlay->shading_top, 0);
}

static gboolean
gst_imx_g2d_text_overlay_filter_foreground_attr (PangoAttribute * attr, gpointer data)
{
  if (attr->klass->type == PANGO_ATTR_FOREGROUND) {
    return FALSE;
  } else {
    return TRUE;
  }
}

static void
gst_imx_g2d_base_text_overlay_render_pangocairo (GstImxG2DBaseTextOverlay * overlay,
    const gchar * string, gint textlen)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  PangoRectangle ink_rect, logical_rect;
  cairo_matrix_t cairo_matrix;
  gint unscaled_width, unscaled_height;
  gint width, height;
  gboolean full_width = FALSE;
  double scalef = 1.0, scalefx, scalefy;
  double a, r, g, b;
  gdouble shadow_offset = 0.0;
  gdouble outline_offset = 0.0;
  gint xpad = 0, ypad = 0;
  GstBuffer *buffer;

  g_mutex_lock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);

  if (overlay->auto_adjust_size) {
    /* 640 pixel is default */
    scalef = (double) (overlay->width) / DEFAULT_SCALE_BASIS;
  }

  if (overlay->draw_shadow)
    shadow_offset = ceil (overlay->shadow_offset);

  /* This value is uses as cairo line width, which is the diameter of a pen
   * that is circular. That's why only half of it is used to offset */
  if (overlay->draw_outline)
    outline_offset = ceil (overlay->outline_offset);

  if (overlay->halign == GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT ||
      overlay->halign == GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT)
    xpad = overlay->xpad;

  if (overlay->valign == GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP ||
      overlay->valign == GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BOTTOM)
    ypad = overlay->ypad;

  pango_layout_set_width (overlay->layout, -1);
  /* set text on pango layout */
  pango_layout_set_markup (overlay->layout, string, textlen);

  /* get subtitle image size */
  pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);

  unscaled_width = ink_rect.width + shadow_offset + outline_offset;
  width = ceil (unscaled_width * scalef);

  /*
   * subtitle image width can be larger then overlay width
   * so rearrange overlay wrap mode.
   */
  if (overlay->use_vertical_render) {
    if (width + ypad > overlay->height) {
      width = overlay->height - ypad;
      full_width = TRUE;
    }
  } else if (width + xpad > overlay->width) {
    width = overlay->width - xpad;
    full_width = TRUE;
  }

  if (full_width) {
    unscaled_width = width / scalef;
    gst_imx_g2d_base_text_overlay_set_wrap_mode (overlay,
        unscaled_width - shadow_offset - outline_offset);
    pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);

    unscaled_width = ink_rect.width + shadow_offset + outline_offset;
    width = ceil (unscaled_width * scalef);
  }

  unscaled_height = ink_rect.height + shadow_offset + outline_offset;
  height = ceil (unscaled_height * scalef);

  if (overlay->use_vertical_render) {
    if (height + xpad > overlay->width) {
      height = overlay->width - xpad;
      unscaled_height = width / scalef;
    }
  } else if (height + ypad > overlay->height) {
    height = overlay->height - ypad;
    unscaled_height = height / scalef;
  }

  GST_DEBUG_OBJECT (overlay, "Rendering with ink rect (%d, %d) %dx%d and "
      "logical rect (%d, %d) %dx%d", ink_rect.x, ink_rect.y, ink_rect.width,
      ink_rect.height, logical_rect.x, logical_rect.y, logical_rect.width,
      logical_rect.height);
  GST_DEBUG_OBJECT (overlay, "Rendering with width %d and height %d "
      "(shadow %f, outline %f)", unscaled_width, unscaled_height,
      shadow_offset, outline_offset);


  /* Save and scale the rectangles so get_pos() can place the text */
  overlay->ink_rect.x =
      ceil ((ink_rect.x - ceil (outline_offset / 2.0l)) * scalef);
  overlay->ink_rect.y =
      ceil ((ink_rect.y - ceil (outline_offset / 2.0l)) * scalef);
  overlay->ink_rect.width = width;
  overlay->ink_rect.height = height;

  overlay->logical_rect.x =
      ceil ((logical_rect.x - ceil (outline_offset / 2.0l)) * scalef);
  overlay->logical_rect.y =
      ceil ((logical_rect.y - ceil (outline_offset / 2.0l)) * scalef);
  overlay->logical_rect.width =
      ceil ((logical_rect.width + shadow_offset + outline_offset) * scalef);
  overlay->logical_rect.height =
      ceil ((logical_rect.height + shadow_offset + outline_offset) * scalef);

  /* flip the rectangle if doing vertical render */
  if (overlay->use_vertical_render) {
    PangoRectangle tmp = overlay->ink_rect;

    overlay->ink_rect.x = tmp.y;
    overlay->ink_rect.y = tmp.x;
    overlay->ink_rect.width = tmp.height;
    overlay->ink_rect.height = tmp.width;
    /* We want the top left corect, but we now have the top right */
    overlay->ink_rect.x += overlay->ink_rect.width;

    tmp = overlay->logical_rect;
    overlay->logical_rect.x = tmp.y;
    overlay->logical_rect.y = tmp.x;
    overlay->logical_rect.width = tmp.height;
    overlay->logical_rect.height = tmp.width;
    overlay->logical_rect.x += overlay->logical_rect.width;
  }

  /* scale to reported window size */
  width = ceil (width * overlay->render_scale);
  height = ceil (height * overlay->render_scale);
  scalef *= overlay->render_scale;

  /* i.MX special, will cause text a little small */
  scalefx = scalef * ((gdouble)GST_ROUND_DOWN_8 (width)) / width;
  scalefy = scalef * ((gdouble)GST_ROUND_DOWN_8 (height)) / height;
  width = GST_ROUND_DOWN_8 (width);
  height = GST_ROUND_DOWN_8 (height);
  GST_DEBUG_OBJECT (overlay, "Rendering with width %d and height %d "
      , width, height);

  if (width <= 0 || height <= 0) {
    g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
    GST_DEBUG_OBJECT (overlay,
        "Overlay is outside video frame. Skipping text rendering");
    return;
  }

  if (unscaled_height <= 0 || unscaled_width <= 0) {
    g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);
    GST_DEBUG_OBJECT (overlay,
        "Overlay is outside video frame. Skipping text rendering");
    return;
  }
  /* Prepare the transformation matrix. Note that the transformation happens
   * in reverse order. So for horizontal text, we will translate and then
   * scale. This is important to understand which scale shall be used. */
  cairo_matrix_init_scale (&cairo_matrix, scalefx, scalefy);

  if (overlay->use_vertical_render) {
    gint tmp;

    /* tranlate to the center of the image, rotate, and tranlate the rotated
     * image back to the right place */
    cairo_matrix_translate (&cairo_matrix, unscaled_height / 2.0l,
        unscaled_width / 2.0l);
    /* 90 degree clockwise rotation which is PI / 2 in radiants */
    cairo_matrix_rotate (&cairo_matrix, G_PI_2);
    cairo_matrix_translate (&cairo_matrix, -(unscaled_width / 2.0l),
        -(unscaled_height / 2.0l));

    /* Swap width and height */
    tmp = height;
    height = width;
    width = tmp;
  }

  cairo_matrix_translate (&cairo_matrix,
      ceil (outline_offset / 2.0l) - ink_rect.x,
      ceil (outline_offset / 2.0l) - ink_rect.y);

  /* reallocate overlay buffer */
  /* buffer = gst_buffer_new_and_alloc (4 * width * height); */
  buffer = gst_buffer_new ();
  gst_buffer_replace (&overlay->text_image, buffer);
  gst_buffer_unref (buffer);

  surface = cairo_image_surface_create_for_data (overlay->g2d_text_buf->buf_vaddr,
      CAIRO_FORMAT_ARGB32, width, height, width * 4);
  cr = cairo_create (surface);

  /* clear surface */
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

  /* apply transformations */
  cairo_set_matrix (cr, &cairo_matrix);

  /* FIXME: We use show_layout everywhere except for the surface
   * because it's really faster and internally does all kinds of
   * caching. Unfortunately we have to paint to a cairo path for
   * the outline and this is slow. Once Pango supports user fonts
   * we should use them, see
   * https://bugzilla.gnome.org/show_bug.cgi?id=598695
   *
   * Idea would the be, to create a cairo user font that
   * does shadow, outline, text painting in the
   * render_glyph function.
   */

  /* draw shadow text */
  if (overlay->draw_shadow) {
    PangoAttrList *origin_attr, *filtered_attr, *temp_attr;

    /* Store a ref on the original attributes for later restoration */
    origin_attr =
        pango_attr_list_ref (pango_layout_get_attributes (overlay->layout));
    /* Take a copy of the original attributes, because pango_attr_list_filter
     * modifies the passed list */
    temp_attr = pango_attr_list_copy (origin_attr);
    filtered_attr =
        pango_attr_list_filter (temp_attr,
        gst_imx_g2d_text_overlay_filter_foreground_attr, NULL);
    pango_attr_list_unref (temp_attr);

    cairo_save (cr);
    cairo_translate (cr, overlay->shadow_offset, overlay->shadow_offset);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
    pango_layout_set_attributes (overlay->layout, filtered_attr);
    pango_cairo_show_layout (cr, overlay->layout);
    pango_layout_set_attributes (overlay->layout, origin_attr);
    pango_attr_list_unref (filtered_attr);
    pango_attr_list_unref (origin_attr);
    cairo_restore (cr);
  }

  /* draw outline text */
  if (overlay->draw_outline) {
    a = (overlay->outline_color >> 24) & 0xff;
    r = (overlay->outline_color >> 16) & 0xff;
    g = (overlay->outline_color >> 8) & 0xff;
    b = (overlay->outline_color >> 0) & 0xff;

    cairo_save (cr);
    cairo_set_source_rgba (cr, r / 255.0, g / 255.0, b / 255.0, a / 255.0);
    cairo_set_line_width (cr, overlay->outline_offset);
    pango_cairo_layout_path (cr, overlay->layout);
    cairo_stroke (cr);
    cairo_restore (cr);
  }

  a = (overlay->color >> 24) & 0xff;
  r = (overlay->color >> 16) & 0xff;
  g = (overlay->color >> 8) & 0xff;
  b = (overlay->color >> 0) & 0xff;

  /* draw text */
  cairo_save (cr);
  cairo_set_source_rgba (cr, r / 255.0, g / 255.0, b / 255.0, a / 255.0);
  pango_cairo_show_layout (cr, overlay->layout);
  cairo_restore (cr);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  if (width != 0)
    overlay->text_width = width;
  if (height != 0)
    overlay->text_height = height;
  g_mutex_unlock (GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay)->pango_lock);

  if (overlay->text_width != 1) {
    gst_imx_g2d_base_text_overlay_update_pos (overlay);
    gst_imx_g2d_base_text_overlay_g2d_surface_reset_position(overlay);
  }
}

static void
gst_imx_g2d_base_text_overlay_render_text (GstImxG2DBaseTextOverlay * overlay,
    const gchar * text, gint textlen)
{
  gchar *string;

  if (!overlay->need_render) {
    GST_DEBUG ("Using previously rendered text.");
    return;
  }

  /* -1 is the whole string */
  if (text != NULL && textlen < 0) {
    textlen = strlen (text);
  }

  if (text != NULL) {
    string = g_strndup (text, textlen);
  } else {                      /* empty string */
    string = g_strdup (" ");
  }
  g_strdelimit (string, "\r\t", ' ');
  textlen = strlen (string);

  /* FIXME: should we check for UTF-8 here? */

  GST_DEBUG ("Rendering '%s'", string);
  gst_imx_g2d_base_text_overlay_render_pangocairo (overlay, string, textlen);

  g_free (string);

  overlay->need_render = FALSE;
}

/* IMX patch */
static gboolean
gst_imx_g2d_base_text_overlay_set_surface_params(GstImxG2DBaseTextOverlay *overlay,
  GstBuffer *buffer,
  struct g2d_surface *surface,
  GstVideoInfo const *info)
{
  GstVideoMeta *video_meta;
  GstImxPhysMemMeta *phys_mem_meta;
  GstVideoFormat format;
  guint width, height, stride, n_planes;

  g_assert_nonnull(buffer);

  video_meta = gst_buffer_get_video_meta(buffer);

  g_assert_nonnull(video_meta);

  phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer);

  g_assert_nonnull(phys_mem_meta);
  g_assert_nonnull(phys_mem_meta->phys_addr);

  if (overlay->need_video_frame_surface_update) {
    memset(surface, 0, sizeof(struct g2d_surface));

    format = video_meta->format;
    width = video_meta->width;
    height = video_meta->height;
    stride = video_meta->stride[0];
    n_planes = video_meta->n_planes;

    GST_LOG_OBJECT(overlay, "number of planes: %u", video_meta->n_planes);

    g_assert_cmpuint(n_planes, ==, 1);

    surface->format = gst_imx_get_g2d_format(format);
    surface->width = width + phys_mem_meta->x_padding;
    surface->height = height + phys_mem_meta->y_padding;
    surface->stride = stride * 8 / gst_imx_get_bits_per_pixel(format);

    GST_DEBUG_OBJECT(overlay, "surface stride: %d pixels  width: %d pixels height: %d pixels", surface->stride, surface->width, surface->height);

    surface->blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
    surface->global_alpha = 0xFF;

    overlay->need_video_frame_surface_update = FALSE;
  }

  /* RGB using only 1st plane */
  surface->planes[0] = (gst_imx_phys_addr_t)phys_mem_meta->phys_addr;

  return TRUE;
}

static gboolean
gst_imx_g2d_base_text_overlay_blend_g2d(GstImxG2DBaseTextOverlay * overlay,
  GstBuffer * video_frame)
{
  void *g2d_handle;
  gboolean ret = TRUE;

  g_assert_nonnull(video_frame);
  g_assert_nonnull(overlay->text_image);

  if (!gst_imx_is_phys_memory(gst_buffer_get_memory(video_frame, 0))) {
    GST_ERROR_OBJECT (overlay, "video frame data is not contiguous physical memory");
    return FALSE;
  }

  if (!gst_imx_g2d_base_text_overlay_set_surface_params(overlay,
        video_frame, &overlay->g2d_video_frame_surface, &(overlay->info) ))
  {
    GST_ERROR_OBJECT (overlay, "set video frame surface params failed");
    return FALSE;
  }

  if (g2d_open(&g2d_handle) != 0) {
    GST_ERROR_OBJECT(overlay, "opening g2d device failed");
    return FALSE;
  }

  if (g2d_make_current(g2d_handle, G2D_HARDWARE_2D) != 0) {
    GST_ERROR_OBJECT(overlay, "g2d_make_current() failed");
    ret = FALSE;
    goto end;
  }

  g2d_enable(g2d_handle, G2D_BLEND);

  /* blend text shadow background on video frame */
  if (overlay->want_shading
    && overlay->shading_width > 0
    && overlay->shading_height > 0)
  {

    if (overlay->need_shading_surface_clear) {
      overlay->g2d_shading_surface.clrcolor =
            (overlay->shading_color & 0x00FFFFFF)
            | ((overlay->shading_value << 24) & 0xFF000000);
      if (g2d_clear(g2d_handle, &(overlay->g2d_shading_surface)) != 0) {
        GST_ERROR_OBJECT(overlay, "clear shadow failed");
      }
      overlay->need_shading_surface_clear = FALSE;
    }

    /* blitting text with background */
    overlay->g2d_video_frame_surface.left = overlay->shading_left;
    overlay->g2d_video_frame_surface.top = overlay->shading_top;
    overlay->g2d_video_frame_surface.right = overlay->shading_right;
    overlay->g2d_video_frame_surface.bottom = overlay->shading_bottom;

    ret = g2d_blit(g2d_handle, &(overlay->g2d_shading_surface), &(overlay->g2d_video_frame_surface)) == 0;

    if (!ret) {
      GST_ERROR_OBJECT(overlay, "blitting shadow with video frame failed");
      goto finish;
    }
  }

  /* blitting text with background */
  overlay->g2d_video_frame_surface.left = overlay->text_left;
  overlay->g2d_video_frame_surface.top = overlay->text_top;
  overlay->g2d_video_frame_surface.right = overlay->text_right;
  overlay->g2d_video_frame_surface.bottom = overlay->text_bottom;

  ret = g2d_blit(g2d_handle, &(overlay->g2d_text_surface), &(overlay->g2d_video_frame_surface)) == 0;

  if (!ret) {
    GST_ERROR_OBJECT(overlay, "blitting text with video frame failed");
    goto finish;
  }

finish:
  if (g2d_finish(g2d_handle) != 0)
  {
    GST_ERROR_OBJECT(overlay, "finishing g2d device operations failed");
    ret = FALSE;
  }

  g2d_disable(g2d_handle, G2D_BLEND);

end:
  if (g2d_close(g2d_handle) != 0)
  {
    GST_ERROR_OBJECT(overlay, "closing g2d device failed");
    ret = FALSE;
  }

  return ret;
}
/* IMX patch */

static GstFlowReturn
gst_imx_g2d_base_text_overlay_push_frame (GstImxG2DBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  if (gst_pad_check_reconfigure (overlay->srcpad))
    gst_imx_g2d_base_text_overlay_negotiate (overlay, NULL);

  video_frame = gst_buffer_make_writable(video_frame);

  if (!gst_imx_g2d_base_text_overlay_blend_g2d(overlay, video_frame))
  {
    gst_buffer_unref (video_frame);
    GST_DEBUG_OBJECT (overlay, "received invalid buffer");
    return GST_FLOW_OK;
  }

  return gst_pad_push (overlay->srcpad, video_frame);
}

static GstPadLinkReturn
gst_imx_g2d_base_text_overlay_text_pad_link (GstPad * pad, GstObject * parent,
    GstPad * peer)
{
  GstImxG2DBaseTextOverlay *overlay;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);
  if (G_UNLIKELY (!overlay))
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG_OBJECT (overlay, "Text pad linked");

  overlay->text_linked = TRUE;

  return GST_PAD_LINK_OK;
}

static void
gst_imx_g2d_base_text_overlay_text_pad_unlink (GstPad * pad, GstObject * parent)
{
  GstImxG2DBaseTextOverlay *overlay;

  /* don't use gst_pad_get_parent() here, will deadlock */
  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  GST_DEBUG_OBJECT (overlay, "Text pad unlinked");

  overlay->text_linked = FALSE;

  gst_segment_init (&overlay->text_segment, GST_FORMAT_UNDEFINED);
}

static gboolean
gst_imx_g2d_base_text_overlay_text_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstImxG2DBaseTextOverlay *overlay = NULL;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  GST_LOG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_imx_g2d_base_text_overlay_setcaps_txt (overlay, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      overlay->text_eos = FALSE;

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
        gst_segment_copy_into (segment, &overlay->text_segment);
        GST_DEBUG_OBJECT (overlay, "TEXT SEGMENT now: %" GST_SEGMENT_FORMAT,
            &overlay->text_segment);
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on text input"));
      }

      gst_event_unref (event);
      ret = TRUE;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime start, duration;

      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        start += duration;
      /* we do not expect another buffer until after gap,
       * so that is our position now */
      overlay->text_segment.position = start;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);

      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "text flush stop");
      overlay->text_flushing = FALSE;
      overlay->text_eos = FALSE;
      gst_imx_g2d_base_text_overlay_pop_text (overlay);
      gst_segment_init (&overlay->text_segment, GST_FORMAT_TIME);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "text flush start");
      overlay->text_flushing = TRUE;
      GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_eos = TRUE;
      GST_INFO_OBJECT (overlay, "text EOS");
      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_imx_g2d_base_text_overlay_video_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstImxG2DBaseTextOverlay *overlay = NULL;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  GST_DEBUG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_imx_g2d_base_text_overlay_setcaps (overlay, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      GST_DEBUG_OBJECT (overlay, "received new segment");

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (overlay, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            &overlay->segment);

        gst_segment_copy_into (segment, &overlay->segment);
      } else {
        GST_ELEMENT_WARNING (overlay, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video EOS");
      overlay->video_eos = TRUE;
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_START:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video flush start");
      overlay->video_flushing = TRUE;
      GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      GST_INFO_OBJECT (overlay, "video flush stop");
      overlay->video_flushing = FALSE;
      overlay->video_eos = FALSE;
      gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_imx_g2d_base_text_overlay_video_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstImxG2DBaseTextOverlay *overlay;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_imx_g2d_base_text_overlay_get_videosink_caps (pad, overlay, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

/* Called with lock held */
static void
gst_imx_g2d_base_text_overlay_pop_text (GstImxG2DBaseTextOverlay * overlay)
{
  g_return_if_fail (GST_IS_IMX_G2D_BASE_TEXT_OVERLAY (overlay));

  if (overlay->text_buffer) {
    GST_DEBUG_OBJECT (overlay, "releasing text buffer %p",
        overlay->text_buffer);
    gst_buffer_unref (overlay->text_buffer);
    overlay->text_buffer = NULL;
  }

  /* Let the text task know we used that buffer */
  GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
}

/* We receive text buffers here. If they are out of segment we just ignore them.
   If the buffer is in our segment we keep it internally except if another one
   is already waiting here, in that case we wait that it gets kicked out */
static GstFlowReturn
gst_imx_g2d_base_text_overlay_text_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstImxG2DBaseTextOverlay *overlay = NULL;
  gboolean in_seg = FALSE;
  guint64 clip_start = 0, clip_stop = 0;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);

  if (overlay->text_flushing) {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = GST_FLOW_FLUSHING;
    GST_LOG_OBJECT (overlay, "text flushing");
    goto beach;
  }

  if (overlay->text_eos) {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = GST_FLOW_EOS;
    GST_LOG_OBJECT (overlay, "text EOS");
    goto beach;
  }

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer)));

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buffer)))
      stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (&overlay->text_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    else if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;

    /* Wait for the previous buffer to go away */
    while (overlay->text_buffer != NULL) {
      GST_DEBUG ("Pad %s:%s has a buffer queued, waiting",
          GST_DEBUG_PAD_NAME (pad));
      GST_IMX_G2D_BASE_TEXT_OVERLAY_WAIT (overlay);
      GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
      if (overlay->text_flushing) {
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        ret = GST_FLOW_FLUSHING;
        goto beach;
      }
    }

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      overlay->text_segment.position = clip_start;

    overlay->text_buffer = buffer;      /* pass ownership of @buffer */
    buffer = NULL;

    /* That's a new text buffer we need to render */
    overlay->need_render = TRUE;

    /* in case the video chain is waiting for a text buffer, wake it up */
    GST_IMX_G2D_BASE_TEXT_OVERLAY_BROADCAST (overlay);
  }

  GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);

beach:
  if (buffer)
    gst_buffer_unref (buffer);

  return ret;
}

static GstFlowReturn
gst_imx_g2d_base_text_overlay_video_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstImxG2DBaseTextOverlayClass *klass;
  GstImxG2DBaseTextOverlay *overlay;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  guint64 start, stop, clip_start = 0, clip_stop = 0;
  gchar *text = NULL;

  overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (parent);

  klass = GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS (overlay);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  /* ignore buffers that are outside of the current segment */
  start = GST_BUFFER_TIMESTAMP (buffer);

  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  GST_LOG_OBJECT (overlay, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &overlay->segment,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  /* segment_clip() will adjust start unconditionally to segment_start if
   * no stop time is provided, so handle this ourselves */
  if (stop == GST_CLOCK_TIME_NONE && start < overlay->segment.start)
    goto out_of_segment;

  in_seg = gst_segment_clip (&overlay->segment, GST_FORMAT_TIME, start, stop,
      &clip_start, &clip_stop);

  if (!in_seg)
    goto out_of_segment;

  /* if the buffer is only partially in the segment, fix up stamps */
  if (clip_start != start || (stop != -1 && clip_stop != stop)) {
    GST_DEBUG_OBJECT (overlay, "clipping buffer timestamp/duration to segment");
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  /* now, after we've done the clipping, fix up end time if there's no
   * duration (we only use those estimated values internally though, we
   * don't want to set bogus values on the buffer itself) */
  if (stop == -1) {
    if (overlay->info.fps_n && overlay->info.fps_d) {
      GST_DEBUG_OBJECT (overlay, "estimating duration based on framerate");
      stop = start + gst_util_uint64_scale_int (GST_SECOND,
          overlay->info.fps_d, overlay->info.fps_n);
    } else {
      GST_LOG_OBJECT (overlay, "no duration, assuming minimal duration");
      stop = start + 1;         /* we need to assume some interval */
    }
  }

  gst_object_sync_values (GST_OBJECT (overlay), GST_BUFFER_TIMESTAMP (buffer));

wait_for_text_buf:

  GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);

  if (overlay->video_flushing)
    goto flushing;

  if (overlay->video_eos)
    goto have_eos;

  if (overlay->silent) {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    ret = gst_pad_push (overlay->srcpad, buffer);

    /* Update position */
    overlay->segment.position = clip_start;

    return ret;
  }

  /* Text pad not linked, rendering internal text */
  if (!overlay->text_linked) {
    if (klass->get_text) {
      text = klass->get_text (overlay, buffer);
    } else {
      text = g_strdup (overlay->default_text);
    }

    GST_LOG_OBJECT (overlay, "Text pad not linked, rendering default "
        "text: '%s'", GST_STR_NULL (text));

    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);

    if (text != NULL && *text != '\0') {
      /* Render and push */
      gst_imx_g2d_base_text_overlay_render_text (overlay, text, -1);
      ret = gst_imx_g2d_base_text_overlay_push_frame (overlay, buffer);
    } else {
      /* Invalid or empty string */
      ret = gst_pad_push (overlay->srcpad, buffer);
    }
  } else {
    /* Text pad linked, check if we have a text buffer queued */
    if (overlay->text_buffer) {
      gboolean pop_text = FALSE, valid_text_time = TRUE;
      GstClockTime text_start = GST_CLOCK_TIME_NONE;
      GstClockTime text_end = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time_end = GST_CLOCK_TIME_NONE;
      GstClockTime vid_running_time, vid_running_time_end;

      /* if the text buffer isn't stamped right, pop it off the
       * queue and display it for the current video frame only */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (overlay->text_buffer) ||
          !GST_BUFFER_DURATION_IS_VALID (overlay->text_buffer)) {
        GST_WARNING_OBJECT (overlay,
            "Got text buffer with invalid timestamp or duration");
        pop_text = TRUE;
        valid_text_time = FALSE;
      } else {
        text_start = GST_BUFFER_TIMESTAMP (overlay->text_buffer);
        text_end = text_start + GST_BUFFER_DURATION (overlay->text_buffer);
      }

      vid_running_time =
          gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
          start);
      vid_running_time_end =
          gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
          stop);

      /* If timestamp and duration are valid */
      if (valid_text_time) {
        text_running_time =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, text_start);
        text_running_time_end =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, text_end);
      }

      GST_LOG_OBJECT (overlay, "T: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (text_running_time),
          GST_TIME_ARGS (text_running_time_end));
      GST_LOG_OBJECT (overlay, "V: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (vid_running_time),
          GST_TIME_ARGS (vid_running_time_end));

      /* Text too old or in the future */
      if (valid_text_time && text_running_time_end <= vid_running_time) {
        /* text buffer too old, get rid of it and do nothing  */
        GST_LOG_OBJECT (overlay, "text buffer too old, popping");
        pop_text = FALSE;
        gst_imx_g2d_base_text_overlay_pop_text (overlay);
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        goto wait_for_text_buf;
      } else if (valid_text_time && vid_running_time_end <= text_running_time) {
        GST_LOG_OBJECT (overlay, "text in future, pushing video buf");
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        /* Push the video frame */
        ret = gst_pad_push (overlay->srcpad, buffer);
      } else {
        GstMapInfo map;
        gchar *in_text;
        gsize in_size;

        gst_buffer_map (overlay->text_buffer, &map, GST_MAP_READ);
        in_text = (gchar *) map.data;
        in_size = map.size;

        if (in_size > 0) {
          /* g_markup_escape_text() absolutely requires valid UTF8 input, it
           * might crash otherwise. We don't fall back on GST_SUBTITLE_ENCODING
           * here on purpose, this is something that needs fixing upstream */
          if (!g_utf8_validate (in_text, in_size, NULL)) {
            const gchar *end = NULL;

            GST_WARNING_OBJECT (overlay, "received invalid UTF-8");
            in_text = g_strndup (in_text, in_size);
            while (!g_utf8_validate (in_text, in_size, &end) && end)
              *((gchar *) end) = '*';
          }

          /* Get the string */
          if (overlay->have_pango_markup) {
            text = g_strndup (in_text, in_size);
          } else {
            text = g_markup_escape_text (in_text, in_size);
          }

          if (text != NULL && *text != '\0') {
            gint text_len = strlen (text);

            while (text_len > 0 && (text[text_len - 1] == '\n' ||
                    text[text_len - 1] == '\r')) {
              --text_len;
            }
            GST_DEBUG_OBJECT (overlay, "Rendering text '%*s'", text_len, text);
            gst_imx_g2d_base_text_overlay_render_text (overlay, text, text_len);
          } else {
            GST_DEBUG_OBJECT (overlay, "No text to render (empty buffer)");
            gst_imx_g2d_base_text_overlay_render_text (overlay, " ", 1);
          }
          if (in_text != (gchar *) map.data)
            g_free (in_text);
        } else {
          GST_DEBUG_OBJECT (overlay, "No text to render (empty buffer)");
          gst_imx_g2d_base_text_overlay_render_text (overlay, " ", 1);
        }

        gst_buffer_unmap (overlay->text_buffer, &map);

        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        ret = gst_imx_g2d_base_text_overlay_push_frame (overlay, buffer);

        if (valid_text_time && text_running_time_end <= vid_running_time_end) {
          GST_LOG_OBJECT (overlay, "text buffer not needed any longer");
          pop_text = TRUE;
        }
      }
      if (pop_text) {
        GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
        gst_imx_g2d_base_text_overlay_pop_text (overlay);
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      }
    } else {
      gboolean wait_for_text_buf = TRUE;

      if (overlay->text_eos)
        wait_for_text_buf = FALSE;

      if (!overlay->wait_text)
        wait_for_text_buf = FALSE;

      /* Text pad linked, but no text buffer available - what now? */
      if (overlay->text_segment.format == GST_FORMAT_TIME) {
        GstClockTime text_start_running_time, text_position_running_time;
        GstClockTime vid_running_time;

        vid_running_time =
            gst_segment_to_running_time (&overlay->segment, GST_FORMAT_TIME,
            GST_BUFFER_TIMESTAMP (buffer));
        text_start_running_time =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, overlay->text_segment.start);
        text_position_running_time =
            gst_segment_to_running_time (&overlay->text_segment,
            GST_FORMAT_TIME, overlay->text_segment.position);

        if ((GST_CLOCK_TIME_IS_VALID (text_start_running_time) &&
                vid_running_time < text_start_running_time) ||
            (GST_CLOCK_TIME_IS_VALID (text_position_running_time) &&
                vid_running_time < text_position_running_time)) {
          wait_for_text_buf = FALSE;
        }
      }

      if (wait_for_text_buf) {
        GST_DEBUG_OBJECT (overlay, "no text buffer, need to wait for one");
        GST_IMX_G2D_BASE_TEXT_OVERLAY_WAIT (overlay);
        GST_DEBUG_OBJECT (overlay, "resuming");
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        goto wait_for_text_buf;
      } else {
        GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
        GST_LOG_OBJECT (overlay, "no need to wait for a text buffer");
        ret = gst_pad_push (overlay->srcpad, buffer);
      }
    }
  }

  g_free (text);

  /* Update position */
  overlay->segment.position = clip_start;

  return ret;

missing_timestamp:
  {
    GST_WARNING_OBJECT (overlay, "buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

flushing:
  {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "flushing, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
have_eos:
  {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    GST_DEBUG_OBJECT (overlay, "eos, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
out_of_segment:
  {
    GST_DEBUG_OBJECT (overlay, "buffer out of segment, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_imx_g2d_base_text_overlay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstImxG2DBaseTextOverlay *overlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_flushing = TRUE;
      overlay->video_flushing = TRUE;
      /* pop_text will broadcast on the GCond and thus also make the video
       * chain exit if it's waiting for a text buffer */
      gst_imx_g2d_base_text_overlay_pop_text (overlay);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_IMX_G2D_BASE_TEXT_OVERLAY_LOCK (overlay);
      overlay->text_flushing = FALSE;
      overlay->video_flushing = FALSE;
      overlay->video_eos = FALSE;
      overlay->text_eos = FALSE;
      gst_segment_init (&overlay->segment, GST_FORMAT_TIME);
      gst_segment_init (&overlay->text_segment, GST_FORMAT_TIME);
      GST_IMX_G2D_BASE_TEXT_OVERLAY_UNLOCK (overlay);
      break;
    default:
      break;
  }

  return ret;
}
