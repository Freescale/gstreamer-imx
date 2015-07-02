/* G2D-based i.MX blitter class
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


#ifndef GST_IMX_G2D_BLITTER_H
#define GST_IMX_G2D_BLITTER_H

#include <g2d.h>
#include "../blitter/blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxG2DBlitter GstImxG2DBlitter;
typedef struct _GstImxG2DBlitterClass GstImxG2DBlitterClass;


#define GST_TYPE_IMX_G2D_BLITTER             (gst_imx_g2d_blitter_get_type())
#define GST_IMX_G2D_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_G2D_BLITTER, GstImxG2DBlitter))
#define GST_IMX_G2D_BLITTER_CAST(obj)        ((GstImxG2DBlitter *)(obj))
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
		"width = (int) [ 4, MAX ], " \
		"height = (int) [ 4, MAX ], " \
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
		"width = (int) [ 4, MAX ], " \
		"height = (int) [ 4, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


/* The G2D blitter uses several frames and surfaces.
 * Surfaces are g2d_surface instances which correspond to the frames with the same name.
 * For example, output_surface contains the physical address and video format of the output_frame.
 *
 * The various frames are:
 * - input_frame: The source input frame that will be blitted on the output frame
 * - output_frame: Where the input frame will be blitted on
 * - fill_frame: Very small auxiliary frame that gets filled with a solid color; used when filling
 *               empty canvas regions that are alpha blended
 *
 * Additional surfaces that have no direct corresponding frame:
 * - background_surface: Very similar to the output surface. Its coordinates will be set to what
 *                       is specified in the fill_region call. The output_surface is not used for
 *                       this purpose, since it has its own coordinates, which would get overwritten.
 * - empty_surface: Very similar to the output surface. Its coordinates will be set to those of
 *                  empty canvas regions during the blit() call.
 */


struct _GstImxG2DBlitter
{
	GstImxBlitter parent;

	GstVideoInfo input_video_info, output_video_info;
	GstAllocator *allocator;
	GstBuffer *input_frame, *output_frame, *fill_frame;
	gboolean use_entire_input_frame;

	void *handle;
	struct g2d_surface input_surface, output_surface, empty_surface, background_surface, fill_surface;
	guint8 visibility_mask;
	guint32 fill_color;
	GstImxRegion empty_regions[4];
	guint num_empty_regions;
};


struct _GstImxG2DBlitterClass
{
	GstImxBlitterClass parent_class;
};


GType gst_imx_g2d_blitter_get_type(void);

GstImxG2DBlitter* gst_imx_g2d_blitter_new(void);


G_END_DECLS


#endif
