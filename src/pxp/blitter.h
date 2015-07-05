/* PxP-based i.MX blitter class
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


#ifndef GST_IMX_PXP_BLITTER_H
#define GST_IMX_PXP_BLITTER_H

#include "../blitter/blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxPxPBlitter GstImxPxPBlitter;
typedef struct _GstImxPxPBlitterClass GstImxPxPBlitterClass;
typedef struct _GstImxPxPBlitterPrivate GstImxPxPBlitterPrivate;


#define GST_TYPE_IMX_PXP_BLITTER             (gst_imx_pxp_blitter_get_type())
#define GST_IMX_PXP_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitter))
#define GST_IMX_PXP_BLITTER_CAST(obj)        ((GstImxPxPBlitter *)(obj))
#define GST_IMX_PXP_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitterClass))
#define GST_IMX_PXP_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitterClass))
#define GST_IS_IMX_PXP_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_PXP_BLITTER))
#define GST_IS_IMX_PXP_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_PXP_BLITTER))


/* The PxP headers define many formats, but only a subset of those is actually supported.
 * The notes below explain why certain source and destination formats were excluded. The
 * subsequent section outlines the formats that *do* work.
 * The format names are those of GstVideoFormat. A table describing the corresponging PxP
 * format can be found at the end of this comment block.
 *
 * Broken source formats:
 *   RGBx RGBA BGRA ABGR RGB BGR: black screen for RGB & grayscale formats, green screen
 *                                for YUV formats
 *   RGB15: the first scanline is repeated in all other scanlines
 *   GRAY8: produces greenish output with BGRx BGRA RGB RGB16 UYVY as destination formats
 *   v308: image corrupted
 *   IYU1: green screen with all destination formats (except for GRAY8, which display black)
 *   NV21: colors are corrupted
 *   NV16: left half of the screen is fine, right one is greenish
 *   YUV9 YVU9: green screen
 *
 * Broken destination formats:
 *   YUY2 YVYU v308 IYU1 I420 YV12 Y42B NV12 NV21 NV16 YUV9 YVU9: produce a green screen
 *   RGBx BGRA ABGR BGR RGB15: either show black, or only the first scanline
 *   RGB: red<->blue channels reversed
 *
 * Working source formats:
 *   BGRx RGB16 YUY2 UYVY YVYU I420 YV12 Y42B NV12
 *
 * Working destination formats:
 *   BGRx BGRA RGB16 GRAY8 UYVY
 *
 * "Working" means any of these source can be used with any of these destination formats.
 * Exception: BGRx, RGB16 => UYVY produces reversed colors (red<->blue channels reversed).
 *
 * GstVideoFormat -> PxP mapping table:
 * NOTE: for the RGBx/BGRx formats, PxP RGB == GStreamer BGR , and vice versa
 *       for v308, the PxP format is PXP_PIX_FMT_VUY444 in FSL kernel 3.14 and above
 *   RGBx -> PXP_PIX_FMT_BGR32
 *   BGRx -> PXP_PIX_FMT_RGB32
 *   RGBA -> PXP_PIX_FMT_RGBA32
 *   BGRA -> PXP_PIX_FMT_BGRA32
 *   ABGR -> PXP_PIX_FMT_ABGR32
 *   RGB -> PXP_PIX_FMT_RGB24
 *   BGR -> PXP_PIX_FMT_BGR24
 *   RGB16 -> PXP_PIX_FMT_RGB565
 *   RGB15 -> PXP_PIX_FMT_RGB555
 *   GRAY8 -> PXP_PIX_FMT_GREY
 *   YUY2 -> PXP_PIX_FMT_YUYV
 *   UYVY -> PXP_PIX_FMT_UYVY
 *   YVYU -> PXP_PIX_FMT_YVYU
 *   v308 -> PXP_PIX_FMT_YUV444
 *   IYU1 -> PXP_PIX_FMT_Y41P
 *   I420 -> PXP_PIX_FMT_YUV420P
 *   YV12 -> PXP_PIX_FMT_YVU420P
 *   Y42B -> PXP_PIX_FMT_YUV422P
 *   NV12 -> PXP_PIX_FMT_NV12
 *   NV21 -> PXP_PIX_FMT_NV21
 *   NV16 -> PXP_PIX_FMT_NV16
 *   YUV9 -> PXP_PIX_FMT_YUV410P
 *   YVU9 -> PXP_PIX_FMT_YVU410P
 */


#define GST_IMX_PXP_SINK_VIDEO_FORMATS \
	" { " \
	"   BGRx " \
	" , RGB16 " \
	" , I420 " \
	" , YV12 " \
	" , Y42B " \
	" , NV12 " \
	" , YUY2 " \
	" , UYVY " \
	" , YVYU " \
	" } "

#define GST_IMX_PXP_BLITTER_SINK_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_PXP_SINK_VIDEO_FORMATS ", " \
		"width = (int) [ 4, MAX ], " \
		"height = (int) [ 4, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


#define GST_IMX_PXP_SRC_VIDEO_FORMATS \
	" { " \
	"   BGRx " \
	" , BGRA " \
	" , RGB16 " \
	" , GRAY8 " \
	" } "

#define GST_IMX_PXP_BLITTER_SRC_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_PXP_SRC_VIDEO_FORMATS ", " \
		"width = (int) [ 4, MAX ], " \
		"height = (int) [ 4, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


struct _GstImxPxPBlitter
{
	GstImxBlitter parent;

	GstVideoInfo input_video_info, output_video_info;
	GstAllocator *allocator;
	GstBuffer *input_frame, *output_frame, *fill_frame;
	gboolean use_entire_input_frame;

	GstImxPxPBlitterPrivate *priv;
	guint8 visibility_mask;
	guint32 fill_color;
	GstImxRegion empty_regions[4];
	guint num_empty_regions;
};


struct _GstImxPxPBlitterClass
{
	GstImxBlitterClass parent_class;
};


GType gst_imx_pxp_blitter_get_type(void);

GstImxPxPBlitter* gst_imx_pxp_blitter_new(void);


G_END_DECLS


#endif

