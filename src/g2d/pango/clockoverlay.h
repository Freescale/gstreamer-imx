/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Tim-Philipp Müller <tim@centricular.net>
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


#ifndef GST_IMX_G2D_CLOCK_OVERLAY_H
#define GST_IMX_G2D_CLOCK_OVERLAY_H

#include "basetextoverlay.h"

G_BEGIN_DECLS

#define GST_TYPE_IMX_G2D_CLOCK_OVERLAY \
  (gst_imx_g2d_clock_overlay_get_type())
#define GST_IMX_G2D_CLOCK_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMX_G2D_CLOCK_OVERLAY,GstImxG2DClockOverlay))
#define GST_IMX_G2D_CLOCK_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IMX_G2D_CLOCK_OVERLAY,GstImxG2DClockOverlayClass))
#define GST_IS_IMX_G2D_CLOCK_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMX_G2D_CLOCK_OVERLAY))
#define GST_IS_IMX_G2D_CLOCK_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IMX_G2D_CLOCK_OVERLAY))

typedef struct _GstImxG2DClockOverlay GstImxG2DClockOverlay;
typedef struct _GstImxG2DClockOverlayClass GstImxG2DClockOverlayClass;

typedef enum {
  GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_LEFT,
  GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_RIGHT
} GstImxG2DClockOverlayTimeAlignment;

/**
 * GstImxG2DClockOverlay:
 *
 * Opaque clockoverlay data structure.
 */
struct _GstImxG2DClockOverlay {
  GstImxG2DBaseTextOverlay textoverlay;
  gchar         *format; /* as in strftime () */
  gchar         *text;

  /*< private >*/
  GstImxG2DClockOverlayTimeAlignment time_alignment;
};

struct _GstImxG2DClockOverlayClass {
  GstImxG2DBaseTextOverlayClass parent_class;
};

GType gst_imx_g2d_clock_overlay_get_type (void);


/* This is a hack hat allows us to use nonliterals for strftime without
 * triggering a warning from -Wformat-nonliteral. We need to allow this
 * because we export the format string as a property of the element.
 * For the inspiration of this and a discussion of why this is necessary,
 * see http://gcc.gnu.org/bugzilla/show_bug.cgi?id=39438
 */
#ifdef __GNUC__
#pragma GCC system_header
static size_t my_strftime(char *s, size_t max, const char *format,
                          const struct tm *tm)
{
  return strftime (s, max, format, tm);
}
#define strftime my_strftime
#endif


G_END_DECLS

#endif /* GST_IMX_G2D_CLOCK_OVERLAY_H */

