/* GStreamer h.263 video encoder using the Freescale VPU hardware video engine
 * Copyright (C) 2013  Carlos Rafael Giani
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


#ifndef GST_IMX_VPU_ENCODER_H263_H
#define GST_IMX_VPU_ENCODER_H263_H

#include "base_enc.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuH263Enc GstImxVpuH263Enc;
typedef struct _GstImxVpuH263EncClass GstImxVpuH263EncClass;


#define GST_TYPE_IMX_VPU_H263_ENC             (gst_imx_vpu_h263_enc_get_type())
#define GST_IMX_VPU_H263_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_H263_ENC, GstImxVpuH263Enc))
#define GST_IMX_VPU_H263_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_H263_ENC, GstImxVpuH263EncClass))
#define GST_IMX_VPU_H263_ENC_CAST(obj)        ((GstImxVpuH263Enc)(obj))
#define GST_IS_IMX_VPU_H263_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_H263_ENC))
#define GST_IS_IMX_VPU_H263_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_H263_ENC))


struct _GstImxVpuH263Enc
{
	GstImxVpuBaseEnc parent;
	guint quant_param;
};


struct _GstImxVpuH263EncClass
{
	GstImxVpuBaseEncClass parent_class;
};


GType gst_imx_vpu_h263_enc_get_type(void);


G_END_DECLS


#endif
