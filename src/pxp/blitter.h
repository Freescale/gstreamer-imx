/* PxP-based i.MX blitter class
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


#ifndef GST_IMX_PXP_BLITTER_H
#define GST_IMX_PXP_BLITTER_H

#include "../common/base_blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxPxPBlitter GstImxPxPBlitter;
typedef struct _GstImxPxPBlitterClass GstImxPxPBlitterClass;
typedef struct _GstImxPxPBlitterPrivate GstImxPxPBlitterPrivate;


#define GST_TYPE_IMX_PXP_BLITTER             (gst_imx_pxp_blitter_get_type())
#define GST_IMX_PXP_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitter))
#define GST_IMX_PXP_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitterClass))
#define GST_IMX_PXP_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_PXP_BLITTER, GstImxPxPBlitterClass))
#define GST_IS_IMX_PXP_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_PXP_BLITTER))
#define GST_IS_IMX_PXP_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_PXP_BLITTER))


/* XXX: RGB15, GRAY8, NV21, NV16 are also supported,
 * but show incorrect output. Disabled for now. */
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
		"width = (int) [ 1, MAX ], " \
		"height = (int) [ 1, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)

/* XXX: RGBA, BGR, RGB15, NV12, NV21, NV16, UYVY, YVYU are also
 * supported, but show incorrect output. Disabled for now. */
#define GST_IMX_PXP_SRC_VIDEO_FORMATS \
	" { " \
	"   BGRx " \
	" , RGB16 " \
	" , GRAY8 " \
	" } "

#define GST_IMX_PXP_BLITTER_SRC_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_PXP_SRC_VIDEO_FORMATS ", " \
		"width = (int) [ 1, MAX ], " \
		"height = (int) [ 1, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


typedef enum
{
	GST_IMX_PXP_BLITTER_ROTATION_NONE,
	GST_IMX_PXP_BLITTER_ROTATION_HFLIP,
	GST_IMX_PXP_BLITTER_ROTATION_VFLIP,
	GST_IMX_PXP_BLITTER_ROTATION_90,
	GST_IMX_PXP_BLITTER_ROTATION_180,
	GST_IMX_PXP_BLITTER_ROTATION_270
}
GstImxPxPBlitterRotationMode;


#define GST_IMX_PXP_BLITTER_OUTPUT_ROTATION_DEFAULT  GST_IMX_PXP_BLITTER_ROTATION_NONE
#define GST_IMX_PXP_BLITTER_CROP_DEFAULT  FALSE


struct _GstImxPxPBlitter
{
	GstImxBaseBlitter parent;
	GstImxPxPBlitterPrivate *priv;

	gboolean apply_crop_metadata;
	GstImxPxPBlitterRotationMode rotation_mode;

	GstImxBaseBlitterRegion output_buffer_region;
};


struct _GstImxPxPBlitterClass
{
	GstImxBaseBlitterClass parent_class;
};


GType gst_imx_pxp_blitter_rotation_mode_get_type(void);

GType gst_imx_pxp_blitter_get_type(void);

GstImxPxPBlitter* gst_imx_pxp_blitter_new(void);

void gst_imx_pxp_blitter_enable_crop(GstImxPxPBlitter *pxp_blitter, gboolean crop);
gboolean gst_imx_pxp_blitter_is_crop_enabled(GstImxPxPBlitter *pxp_blitter);

GstImxPxPBlitterRotationMode gst_imx_pxp_blitter_get_output_rotation(GstImxPxPBlitter *pxp_blitter);
void gst_imx_pxp_blitter_set_output_rotation(GstImxPxPBlitter *pxp_blitter, GstImxPxPBlitterRotationMode rotation);


G_END_DECLS


#endif
