/* G2D-based i.MX blitter class
 * Copyright (C) 2014  Carlos Rafael Giani
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


#ifndef GST_IMX_G2D_BLITTER_H
#define GST_IMX_G2D_BLITTER_H

#include <g2d.h>
#include "../common/base_blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxG2DBlitter GstImxG2DBlitter;
typedef struct _GstImxG2DBlitterClass GstImxG2DBlitterClass;
typedef struct _GstImxG2DBlitterPrivate GstImxG2DBlitterPrivate;


#define GST_TYPE_IMX_G2D_BLITTER             (gst_imx_g2d_blitter_get_type())
#define GST_IMX_G2D_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_G2D_BLITTER, GstImxG2DBlitter))
#define GST_IMX_G2D_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_G2D_BLITTER, GstImxG2DBlitterClass))
#define GST_IMX_G2D_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_G2D_BLITTER, GstImxG2DBlitterClass))
#define GST_IS_IMX_G2D_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_G2D_BLITTER))
#define GST_IS_IMX_G2D_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_G2D_BLITTER))


#define GST_IMX_G2D_SINK_VIDEO_FORMATS \
	" { " \
	"   RGBx " \
	" , BGRx " \
	" , RGBA " \
	" , BGRA " \
	" , RGB16 " \
	" , NV12 " \
	" , NV21 " \
	" , I420 " \
	" , YV12 " \
	" , YUY2 " \
	" , UYVY " \
	" } "

#define GST_IMX_G2D_BLITTER_SINK_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_G2D_SINK_VIDEO_FORMATS ", " \
		"width = (int) [ 1, MAX ], " \
		"height = (int) [ 1, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


#define GST_IMX_G2D_SRC_VIDEO_FORMATS \
	" { " \
	"   RGBx " \
	" , BGRx " \
	" , RGBA " \
	" , BGRA " \
	" , RGB16 " \
	" } "

#define GST_IMX_G2D_BLITTER_SRC_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_G2D_SRC_VIDEO_FORMATS ", " \
		"width = (int) [ 1, MAX ], " \
		"height = (int) [ 1, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


typedef enum
{
	GST_IMX_G2D_BLITTER_ROTATION_NONE,
	GST_IMX_G2D_BLITTER_ROTATION_HFLIP,
	GST_IMX_G2D_BLITTER_ROTATION_VFLIP,
	GST_IMX_G2D_BLITTER_ROTATION_90,
	GST_IMX_G2D_BLITTER_ROTATION_180,
	GST_IMX_G2D_BLITTER_ROTATION_270
}
GstImxG2DBlitterRotationMode;


#define GST_IMX_G2D_BLITTER_OUTPUT_ROTATION_DEFAULT  GST_IMX_G2D_BLITTER_ROTATION_NONE


#define GST_IMX_G2D_BLITTER_MAX_NUM_EMPTY_SURFACES 4


struct _GstImxG2DBlitter
{
	GstImxBaseBlitter parent;

	void* handle;
	struct g2d_surface source_surface, dest_surface;
	struct g2d_surface empty_dest_surfaces[GST_IMX_G2D_BLITTER_MAX_NUM_EMPTY_SURFACES];
	guint num_empty_dest_surfaces;
	gboolean output_region_uptodate;
};


struct _GstImxG2DBlitterClass
{
	GstImxBaseBlitterClass parent_class;
};


GType gst_imx_g2d_blitter_rotation_mode_get_type(void);

GType gst_imx_g2d_blitter_get_type(void);

GstImxG2DBlitter* gst_imx_g2d_blitter_new(void);

GstImxG2DBlitterRotationMode gst_imx_g2d_blitter_get_output_rotation(GstImxG2DBlitter *g2d_blitter);
void gst_imx_g2d_blitter_set_output_rotation(GstImxG2DBlitter *g2d_blitter, GstImxG2DBlitterRotationMode rotation);


G_END_DECLS


#endif
