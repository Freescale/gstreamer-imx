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

#include <gst/gst.h>
#include "gstimxvpudeccontext.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_dec_context_debug);
#define GST_CAT_DEFAULT imx_vpu_dec_context_debug


G_DEFINE_TYPE(GstImxVpuDecContext, gst_imx_vpu_dec_context, GST_TYPE_OBJECT)


static void gst_imx_vpu_dec_context_finalize(GObject *object);


void gst_imx_vpu_dec_context_class_init(GstImxVpuDecContextClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_context_finalize);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_dec_context_debug, "imxvpudecodercontext", 0, "NXP i.MX VPU decoder context");
}


void gst_imx_vpu_dec_context_init(GstImxVpuDecContext *imx_vpu_dec_context)
{
	imx_vpu_dec_context->decoder = NULL;

	g_mutex_init(&(imx_vpu_dec_context->mutex));
}


static void gst_imx_vpu_dec_context_finalize(GObject *object)
{
	GstImxVpuDecContext *imx_vpu_dec_context = GST_IMX_VPU_DEC_CONTEXT(object);

	gst_imx_vpu_dec_context_close_decoder(imx_vpu_dec_context);

	g_mutex_clear(&(imx_vpu_dec_context->mutex));

	G_OBJECT_CLASS(gst_imx_vpu_dec_context_parent_class)->finalize(object);
}


GstImxVpuDecContext* gst_imx_vpu_dec_context_new(ImxVpuApiDecoder *decoder)
{
	GstImxVpuDecContext *imx_vpu_dec_context = (GstImxVpuDecContext *)g_object_new(gst_imx_vpu_dec_context_get_type(), NULL);
	imx_vpu_dec_context->decoder = decoder;

	GST_DEBUG_OBJECT(imx_vpu_dec_context, "created new context with decoder instance %p", (gpointer)(imx_vpu_dec_context->decoder));

	return imx_vpu_dec_context;
}


void gst_imx_vpu_dec_context_close_decoder(GstImxVpuDecContext *imx_vpu_dec_context)
{
	GST_IMX_VPU_DEC_CONTEXT_LOCK(imx_vpu_dec_context);

	if (G_LIKELY(imx_vpu_dec_context->decoder != NULL))
	{
		imx_vpu_api_dec_close(imx_vpu_dec_context->decoder);
		GST_DEBUG_OBJECT(imx_vpu_dec_context, "closed decoder instance %p", (gpointer)(imx_vpu_dec_context->decoder));
		imx_vpu_dec_context->decoder = NULL;
	}

	GST_IMX_VPU_DEC_CONTEXT_UNLOCK(imx_vpu_dec_context);
}


void gst_imx_vpu_dec_context_return_framebuffer_to_decoder(GstImxVpuDecContext *imx_vpu_dec_context, ImxDmaBuffer *framebuffer)
{
	GST_IMX_VPU_DEC_CONTEXT_LOCK(imx_vpu_dec_context);

	/* If this has been set to NULL, then the decoder is gone.
	 * Just do nothing in that case. */
	if (imx_vpu_dec_context->decoder != NULL)
	{
		imx_vpu_api_dec_return_framebuffer_to_decoder(imx_vpu_dec_context->decoder, framebuffer);
		GST_LOG_OBJECT(imx_vpu_dec_context, "returned framebuffer DMA buffer %p to decoder instance %p", (gpointer)framebuffer, (gpointer)(imx_vpu_dec_context->decoder));
	}
	else
		GST_LOG_OBJECT(imx_vpu_dec_context, "not returning framebuffer DMA buffer %p since decoder instance is already gone", (gpointer)framebuffer);

	GST_IMX_VPU_DEC_CONTEXT_UNLOCK(imx_vpu_dec_context);
}
