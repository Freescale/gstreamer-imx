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

#ifndef GST_IMX_G2D_BASE_TEXT_OVERLAY_H
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <pango/pangocairo.h>
#include <g2d.h>


G_BEGIN_DECLS

#define GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY            (gst_imx_g2d_base_text_overlay_get_type())
#define GST_IMX_G2D_BASE_TEXT_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY, GstImxG2DBaseTextOverlay))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY,GstImxG2DBaseTextOverlayClass))
#define GST_IMX_G2D_BASE_TEXT_OVERLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY, GstImxG2DBaseTextOverlayClass))
#define GST_IS_IMX_G2D_BASE_TEXT_OVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY))
#define GST_IS_IMX_G2D_BASE_TEXT_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY))

typedef struct _GstImxG2DBaseTextOverlay      GstImxG2DBaseTextOverlay;
typedef struct _GstImxG2DBaseTextOverlayClass GstImxG2DBaseTextOverlayClass;

/**
 * GstImxG2DBaseTextOverlayVAlign:
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BASELINE: draw text on the baseline
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BOTTOM: draw text on the bottom
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP: draw text on top
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_POS: draw text according to the #GstImxG2DBaseTextOverlay:ypos property
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_CENTER: draw text vertically centered
 *
 * Vertical alignment of the text.
 */
typedef enum {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_POS,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_CENTER,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_ABSOLUTE
} GstImxG2DBaseTextOverlayVAlign;

/**
 * GstImxG2DBaseTextOverlayHAlign:
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT: align text left
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_CENTER: align text center
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT: align text right
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_POS: position text according to the #GstImxG2DBaseTextOverlay:xpos property
 *
 * Horizontal alignment of the text.
 */
/* FIXME 0.11: remove GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_UNUSED */
typedef enum {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_CENTER,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_RIGHT,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_UNUSED,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_POS,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_ABSOLUTE
} GstImxG2DBaseTextOverlayHAlign;

/**
 * GstImxG2DBaseTextOverlayWrapMode:
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_NONE: no wrapping
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD: do word wrapping
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR: do char wrapping
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR: do word and char wrapping
 *
 * Whether to wrap the text and if so how.
 */
typedef enum {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_NONE = -1,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD = PANGO_WRAP_WORD,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_CHAR = PANGO_WRAP_CHAR,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_WRAP_MODE_WORD_CHAR = PANGO_WRAP_WORD_CHAR
} GstImxG2DBaseTextOverlayWrapMode;

/**
 * GstImxG2DBaseTextOverlayLineAlign:
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT: lines are left-aligned
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER: lines are center-aligned
 * @GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT: lines are right-aligned
 *
 * Alignment of text lines relative to each other
 */
typedef enum {
    GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_LEFT = PANGO_ALIGN_LEFT,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_CENTER = PANGO_ALIGN_CENTER,
    GST_IMX_G2D_BASE_TEXT_OVERLAY_LINE_ALIGN_RIGHT = PANGO_ALIGN_RIGHT
} GstImxG2DBaseTextOverlayLineAlign;

/**
 * GstImxG2DBaseTextOverlay:
 *
 * Opaque textoverlay object structure
 */
struct _GstImxG2DBaseTextOverlay {
    GstElement               element;

    GstPad                  *video_sinkpad;
    GstPad                  *text_sinkpad;
    GstPad                  *srcpad;

    GstSegment               segment;
    GstSegment               text_segment;
    GstBuffer               *text_buffer;
    gboolean                 text_linked;
    gboolean                 video_flushing;
    gboolean                 video_eos;
    gboolean                 text_flushing;
    gboolean                 text_eos;

    GMutex                   lock;
    GCond                    cond;  /* to signal removal of a queued text
                                     * buffer, arrival of a text buffer,
                                     * a text segment update, or a change
                                     * in status (e.g. shutdown, flushing) */

    /* stream metrics */
    GstVideoInfo             info;
    GstVideoFormat           format;
    gint                     width;
    gint                     height;

    /* properties */
    gint                     xpad;
    gint                     ypad;
    gint                     deltax;
    gint                     deltay;
    gdouble                  xpos;
    gdouble                  ypos;
    gchar                   *default_text;
    gboolean                 want_shading;
    gboolean                 silent;
    gboolean                 wait_text;
    guint                    color, outline_color;
    PangoLayout             *layout;
    gboolean                 auto_adjust_size;
    gboolean                 draw_shadow;
    gboolean                 draw_outline;
    guint                    shading_color;
    gint                     shading_value;  /* for timeoverlay subclass */
    gint                     shading_xpad;
    gint                     shading_ypad;
    gboolean                 use_vertical_render;
    GstImxG2DBaseTextOverlayVAlign     valign;
    GstImxG2DBaseTextOverlayHAlign     halign;
    GstImxG2DBaseTextOverlayWrapMode   wrap_mode;
    GstImxG2DBaseTextOverlayLineAlign  line_align;

    /* text pad format */
    gboolean                 have_pango_markup;

    /* rendering state */
    gboolean                 need_render;
    GstBuffer               *text_image;

    /* G2D buffers */
    struct g2d_buf          *g2d_text_buf;
    struct g2d_buf          *g2d_shading_buf;

    /* G2D surfaces */
    struct g2d_surface       g2d_video_frame_surface;
    struct g2d_surface       g2d_text_surface;
    struct g2d_surface       g2d_shading_surface;

    /* enable on negotiate */
    gboolean                 need_video_frame_surface_update;
    gboolean                 need_shading_surface_clear;

    /* dimension relative to witch the render is done, this is the stream size
     * or a portion of the window_size (adapted to aspect ratio) */
    gint                     render_width;
    gint                     render_height;
    /* This is (render_width / width) uses to convert to stream scale */
    gdouble                  render_scale;

    /* dimension of text_image, the physical dimension */
    guint                    text_width;
    guint                    text_height;

    /* dimension of text_shadow_background, the physical dimension */
    guint                    shading_width;
    guint                    shading_height;

    /* position of rendering in image coordinates */
    gint                     text_left;
    gint                     text_top;
    gint                     text_right;
    gint                     text_bottom;

    /* position of shadow background rendering in image coordinates */
    gint                     shading_left;
    gint                     shading_top;
    gint                     shading_right;
    gint                     shading_bottom;

    /* window dimension, reported in the composition meta params. This is set
     * to stream width, height if missing */
    gint                     window_width;
    gint                     window_height;

    gdouble                  shadow_offset;
    gdouble                  outline_offset;

    PangoRectangle           ink_rect;
    PangoRectangle           logical_rect;
};

struct _GstImxG2DBaseTextOverlayClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
    GMutex       *pango_lock;

    gchar *     (*get_text) (GstImxG2DBaseTextOverlay *overlay, GstBuffer *video_frame);
};

GType gst_imx_g2d_base_text_overlay_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_IMX_G2D_BASE_TEXT_OVERLAY_H */
