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


#ifndef GST_IMX_G2D_TIME_OVERLAY_H
#define GST_IMX_G2D_TIME_OVERLAY_H

#include "basetextoverlay.h"

G_BEGIN_DECLS

#define GST_TYPE_IMX_G2D_TIME_OVERLAY \
  (gst_imx_g2d_time_overlay_get_type())
#define GST_IMX_G2D_TIME_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMX_G2D_TIME_OVERLAY,GstImxG2DTimeOverlay))
#define GST_IMX_G2D_TIME_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IMX_G2D_TIME_OVERLAY,GstImxG2DTimeOverlayClass))
#define GST_IS_IMX_G2D_TIME_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMX_G2D_TIME_OVERLAY))
#define GST_IS_IMX_G2D_TIME_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IMX_G2D_TIME_OVERLAY))
#define GST_IMX_G2D_TIME_OVERLAY_CAST(obj) ((GstImxG2DTimeOverlay*)(obj))

typedef struct _GstImxG2DTimeOverlay GstImxG2DTimeOverlay;
typedef struct _GstImxG2DTimeOverlayClass GstImxG2DTimeOverlayClass;

typedef enum {
  GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_LEFT,
  GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_RIGHT
} GstImxG2DTimeOverlayTimeAlignment;

typedef enum {
  GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_BUFFER_TIME,
  GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_STREAM_TIME,
  GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_RUNNING_TIME
} GstImxG2DTimeOverlayTimeLine;

/**
 * GstImxG2DTimeOverlay:
 *
 * Opaque timeoverlay data structure.
 */
struct _GstImxG2DTimeOverlay {
  GstImxG2DBaseTextOverlay textoverlay;

  /*< private >*/
  GstImxG2DTimeOverlayTimeLine time_line;
  GstImxG2DTimeOverlayTimeAlignment time_alignment;
};

struct _GstImxG2DTimeOverlayClass {
  GstImxG2DBaseTextOverlayClass parent_class;
};

GType gst_imx_g2d_time_overlay_get_type (void);

G_END_DECLS

#endif /* GST_IMX_G2D_TIME_OVERLAY_H */

