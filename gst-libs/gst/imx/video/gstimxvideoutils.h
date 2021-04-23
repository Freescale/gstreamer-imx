/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
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

#ifndef GST_IMX_VIDEO_UTILS_H
#define GST_IMX_VIDEO_UTILS_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


gint gst_imx_video_utils_calculate_total_num_frame_rows(GstBuffer *video_frame_buffer, GstVideoInfo const *video_info);


G_END_DECLS


#endif /* GST_IMX_VIDEO_UTILS_H */
