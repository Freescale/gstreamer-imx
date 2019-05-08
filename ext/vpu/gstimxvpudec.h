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

#ifndef GST_IMX_VPU_DEC_H
#define GST_IMX_VPU_DEC_H

#include <gst/gst.h>
#include <imxvpuapi2/imxvpuapi2.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_VPU_DEC             (gst_imx_vpu_dec_get_type())
#define GST_IMX_VPU_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DEC,GstImxVpuDec))
#define GST_IMX_VPU_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DEC,GstImxVpuDecClass))
#define GST_IMX_VPU_DEC_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_DEC, GstImxVpuDecClass))
#define GST_IMX_VPU_DEC_CAST(obj)        ((GstImxVpuDec *)(obj))
#define GST_IS_IMX_VPU_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_DEC))
#define GST_IS_IMX_VPU_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DEC))


typedef struct _GstImxVpuDec GstImxVpuDec;
typedef struct _GstImxVpuDecClass GstImxVpuDecClass;


GTypeInfo gst_imx_vpu_dec_get_derived_type_info(void);

GType gst_imx_vpu_dec_get_type(void);

gboolean gst_imx_vpu_dec_register_decoder_type(GstPlugin *plugin, ImxVpuApiCompressionFormat compression_format);


G_END_DECLS


#endif /* GST_IMX_VPU_DEC_H */
