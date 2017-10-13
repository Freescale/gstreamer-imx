/* GStreamer
 * Copyright (C) 2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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


#ifndef GST_IMX_G2D_TEXT_OVERLAY_H
#define GST_IMX_G2D_TEXT_OVERLAY_H

#include "basetextoverlay.h"

G_BEGIN_DECLS

#define GST_TYPE_IMX_G2D_TEXT_OVERLAY \
  (gst_imx_g2d_text_overlay_get_type())
#define GST_IMX_G2D_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IMX_G2D_TEXT_OVERLAY,GstImxG2DTextOverlay))
#define GST_IMX_G2D_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IMX_G2D_TEXT_OVERLAY,GstImxG2DTextOverlayClass))
#define GST_IS_IMX_G2D_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IMX_G2D_TEXT_OVERLAY))
#define GST_IS_IMX_G2D_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IMX_G2D_TEXT_OVERLAY))

typedef struct _GstImxG2DTextOverlay GstImxG2DTextOverlay;
typedef struct _GstImxG2DTextOverlayClass GstImxG2DTextOverlayClass;

/**
 * GstImxG2DTextOverlay:
 *
 * Opaque textoverlay data structure.
 */
struct _GstImxG2DTextOverlay {
  GstImxG2DBaseTextOverlay parent;
};

struct _GstImxG2DTextOverlayClass {
  GstImxG2DBaseTextOverlayClass parent_class;
};

GType gst_imx_g2d_text_overlay_get_type (void);

G_END_DECLS

#endif /* GST_IMX_G2D_TEXT_OVERLAY_H */

