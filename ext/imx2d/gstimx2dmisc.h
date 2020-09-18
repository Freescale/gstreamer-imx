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


void gst_imx_2d_setup_logging(void);

Imx2dPixelFormat gst_imx_2d_convert_from_gst_video_format(GstVideoFormat gst_video_format);
GstVideoFormat gst_imx_2d_convert_to_gst_video_format(Imx2dPixelFormat imx2d_format);

GstCaps* gst_imx_2d_get_caps_from_imx2d_capabilities(Imx2dHardwareCapabilities const *capabilities, GstPadDirection direction);

GType gst_imx_2d_flip_mode_get_type(void);

GType gst_imx_2d_rotation_get_type(void);


G_END_DECLS


#endif /* GST_IMX_2D_VIDEO_MISC_H */
