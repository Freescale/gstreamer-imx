/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

#ifndef GST_IMX_VPU_ENC_VP8_H
#define GST_IMX_VPU_ENC_VP8_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_VPU_ENC_VP8             (gst_imx_vpu_enc_vp8_get_type())
#define GST_IMX_VPU_ENC_VP8(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_ENC_VP8,GstImxVpuEncVP8))
#define GST_IMX_VPU_ENC_VP8_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_ENC_VP8,GstImxVpuEncVP8Class))
#define GST_IMX_VPU_ENC_VP8_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_ENC_VP8, GstImxVpuEncVP8Class))
#define GST_IMX_VPU_ENC_VP8_CAST(obj)        ((GstImxVpuEncVP8 *)(obj))
#define GST_IS_IMX_VPU_ENC_VP8(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_ENC_VP8))
#define GST_IS_IMX_VPU_ENC_VP8_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_ENC_VP8))


typedef struct _GstImxVpuEncVP8 GstImxVpuEncVP8;
typedef struct _GstImxVpuEncVP8Class GstImxVpuEncVP8Class;


GType gst_imx_vpu_enc_vp8_get_type(void);


G_END_DECLS


#endif /* GST_IMX_VPU_ENC_VP8_H */
