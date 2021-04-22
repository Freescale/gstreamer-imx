/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#ifndef GST_IMX_2D_VIDEO_MISC_H
#define GST_IMX_2D_VIDEO_MISC_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "imx2d/imx2d.h"


G_BEGIN_DECLS


typedef enum
{
	GST_IMX_2D_TILE_LAYOUT_NONE,
	GST_IMX_2D_TILE_LAYOUT_AMPHION_8x128
}
GstImx2dTileLayout;


void gst_imx_2d_setup_logging(void);

GstCaps* gst_imx_remove_tile_layout_from_caps(GstCaps *caps, GstImx2dTileLayout *tile_layout);
gboolean gst_imx_video_info_from_caps(GstVideoInfo *info, GstCaps const *caps, GstImx2dTileLayout *tile_layout, GstCaps **modified_caps);

Imx2dPixelFormat gst_imx_2d_convert_from_gst_video_format(GstVideoFormat gst_video_format, GstImx2dTileLayout const *tile_layout);
GstVideoFormat gst_imx_2d_convert_to_gst_video_format(Imx2dPixelFormat imx2d_format);

GstCaps* gst_imx_2d_get_caps_from_imx2d_capabilities(Imx2dHardwareCapabilities const *capabilities, GstPadDirection direction);
GstCaps* gst_imx_2d_get_caps_from_imx2d_capabilities_full(Imx2dHardwareCapabilities const *capabilities, GstPadDirection direction, gboolean add_composition_meta);

void gst_imx_2d_canvas_calculate_letterbox_margin(
	Imx2dBlitMargin *margin,
	Imx2dRegion *inner_region,
	Imx2dRegion const *outer_region,
	gboolean video_transposed,
	guint video_width, guint video_height,
	guint video_par_n, guint video_par_d
);

gboolean gst_imx_2d_check_input_buffer_structure(GstBuffer *input_buffer, guint num_planes);

/* Assigns DMA buffers of uploaded_input_buffer to the surface,
 * and fills the surface description plane_stride values.
 * input_video_info is used as a fallback in case original_input_buffer
 * has no video meta. If it is 100% guaranteed that original_input_buffer
 * _does_ have that meta, it is OK to set input_video_info to NULL. */
void gst_imx_2d_assign_input_buffer_to_surface(
	GstBuffer *original_input_buffer, GstBuffer *uploaded_input_buffer,
	Imx2dSurface *surface,
	Imx2dSurfaceDesc *surface_desc,
	GstVideoInfo const *input_video_info
);
void gst_imx_2d_assign_output_buffer_to_surface(Imx2dSurface *surface, GstBuffer *output_buffer, GstVideoInfo const *output_video_info);

void gst_imx_2d_align_output_video_info(GstVideoInfo *output_video_info, gint *num_padding_rows, Imx2dHardwareCapabilities const *hardware_capabilities);

Imx2dRotation gst_imx_2d_convert_from_video_orientation_method(GstVideoOrientationMethod method);

gboolean gst_imx_2d_orientation_from_image_direction_tag(GstTagList const *taglist, GstVideoOrientationMethod *orientation);


G_END_DECLS


#endif /* GST_IMX_2D_VIDEO_MISC_H */
