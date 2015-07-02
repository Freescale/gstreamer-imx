/* G2D-based i.MX compositor class
 * Copyright (C) 2015  Carlos Rafael Giani
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef GST_IMX_G2D_COMPOSITOR_H
#define GST_IMX_G2D_COMPOSITOR_H

#include <gst/gst.h>
#include "../blitter/compositor.h"
#include "blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxG2DCompositor GstImxG2DCompositor;
typedef struct _GstImxG2DCompositorClass GstImxG2DCompositorClass;


#define GST_TYPE_IMX_G2D_COMPOSITOR             (gst_imx_g2d_compositor_get_type())
#define GST_IMX_G2D_COMPOSITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_G2D_COMPOSITOR, GstImxG2DCompositor))
#define GST_IMX_G2D_COMPOSITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_G2D_COMPOSITOR, GstImxG2DCompositorClass))
#define GST_IS_IMX_G2D_COMPOSITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_G2D_COMPOSITOR))
#define GST_IS_IMX_G2D_COMPOSITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_G2D_COMPOSITOR))


struct _GstImxG2DCompositor
{
	GstImxBlitterCompositor parent;
	GstImxG2DBlitter *blitter;
};


struct _GstImxG2DCompositorClass
{
	GstImxBlitterCompositorClass parent_class;
};


GType gst_imx_g2d_compositor_get_type(void);


G_END_DECLS


#endif
