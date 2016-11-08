/* VPU decoder context structure
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


#include "decoder_context.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_decoder_context_debug);
#define GST_CAT_DEFAULT imx_vpu_decoder_context_debug


G_DEFINE_TYPE(GstImxVpuDecoderContext, gst_imx_vpu_decoder_context, GST_TYPE_OBJECT)


static void gst_imx_vpu_decoder_context_finalize(GObject *object);






void gst_imx_vpu_decoder_context_class_init(GstImxVpuDecoderContextClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_context_finalize);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_decoder_context_debug, "imxvpudecodercontext", 0, "Freescale i.MX VPU decoder context");
}


void gst_imx_vpu_decoder_context_init(GstImxVpuDecoderContext *decoder_context)
{
	decoder_context->decoder = NULL;

	decoder_context->framebuffer_array = NULL;

	g_mutex_init(&(decoder_context->mutex));
	g_cond_init(&(decoder_context->cond));
	decoder_context->no_wait = FALSE;
}


static void gst_imx_vpu_decoder_context_finalize(GObject *object)
{
	GstImxVpuDecoderContext *decoder_context = GST_IMX_VPU_DECODER_CONTEXT(object);

	g_mutex_clear(&(decoder_context->mutex));
	g_cond_clear(&(decoder_context->cond));

	if (decoder_context->framebuffer_array != NULL)
		gst_object_unref(GST_OBJECT(decoder_context->framebuffer_array));

	G_OBJECT_CLASS(gst_imx_vpu_decoder_context_parent_class)->finalize(object);
}


GstImxVpuDecoderContext* gst_imx_vpu_decoder_context_new(ImxVpuDecoder *decoder, ImxVpuDecInitialInfo *initial_info, gboolean chroma_interleave, GstImxPhysMemAllocator *allocator)
{
	ImxVpuDecReturnCodes ret;
	GstImxVpuDecoderContext *decoder_context;
	
	decoder_context = g_object_new(gst_imx_vpu_decoder_context_get_type(), NULL);
	decoder_context->decoder = decoder;

	GST_DEBUG_OBJECT(
		decoder_context,
		"initial info:  color format: %s  size: %ux%u pixel  rate: %u/%u  min num required framebuffers: %u  interlacing: %d  chroma_interleave: %d  framebuffer alignment: %u",
		imx_vpu_color_format_string(initial_info->color_format),
		initial_info->frame_width,
		initial_info->frame_height,
		initial_info->frame_rate_numerator,
		initial_info->frame_rate_denominator,
		initial_info->min_num_required_framebuffers,
		initial_info->interlacing,
		chroma_interleave,
		initial_info->framebuffer_alignment
	);

	decoder_context->framebuffer_array = gst_imx_vpu_framebuffer_array_new(
		initial_info->color_format,
		initial_info->frame_width,
		initial_info->frame_height,
		initial_info->framebuffer_alignment,
		initial_info->interlacing,
		chroma_interleave,
		initial_info->min_num_required_framebuffers + 1, /* add one extra framebuffer, since GStreamer video sinks typically keep a reference on the last displayed frame */
		allocator
	);

	if (decoder_context->framebuffer_array == NULL)
	{
		GST_ERROR_OBJECT(decoder_context, "could not create new framebuffer array");
		goto cleanup;
	}

	if ((ret = imx_vpu_dec_register_framebuffers(decoder_context->decoder, decoder_context->framebuffer_array->framebuffers, decoder_context->framebuffer_array->num_framebuffers)) != IMX_VPU_DEC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(decoder_context, "could not register framebuffers: %s", imx_vpu_dec_error_string(ret));
		goto cleanup;
	}

	return decoder_context;

cleanup:
	gst_object_unref(GST_OBJECT(decoder_context));
	return NULL;
}


void gst_imx_vpu_decoder_context_set_no_wait(GstImxVpuDecoderContext *decoder_context, gboolean no_wait)
{
	/* must be called with lock */

	GST_LOG_OBJECT(decoder_context, "setting no_wait value to %d", no_wait);
	decoder_context->no_wait = no_wait;
	if (no_wait)
		g_cond_signal(&(decoder_context->cond));
}


void gst_imx_vpu_decoder_context_wait_until_decoding_possible(GstImxVpuDecoderContext *decoder_context)
{
	/* must be called with lock */

	while (TRUE)
	{
		if (decoder_context->no_wait || imx_vpu_dec_check_if_can_decode(decoder_context->decoder))
			break;

		g_cond_wait(&(decoder_context->cond), &(decoder_context->mutex));
	}
}


void gst_imx_vpu_decoder_context_set_decoder_as_gone(GstImxVpuDecoderContext *decoder_context)
{
	/* must be called with lock */

	decoder_context->decoder = NULL;
}


gboolean gst_imx_vpu_decoder_context_mark_as_displayed(GstImxVpuDecoderContext *decoder_context, ImxVpuFramebuffer *framebuffer)
{
	/* must be called with lock */

	ImxVpuDecReturnCodes ret;

	/* If this has been set to NULL, then the decoder is gone. Just do nothing
	 * in that case. This is not an error case, so it returns TRUE. */
	if (decoder_context->decoder == NULL)
		return TRUE;

	if ((ret = imx_vpu_dec_mark_framebuffer_as_displayed(decoder_context->decoder, framebuffer)) != IMX_VPU_DEC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(decoder_context, "could not mark framebuffer as displayed: %s", imx_vpu_dec_error_string(ret));
		return FALSE;
	}

	return TRUE;
}
