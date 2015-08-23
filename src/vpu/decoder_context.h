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


#ifndef GST_IMX_VPU_DECODER_CONTEXT_H
#define GST_IMX_VPU_DECODER_CONTEXT_H


#include <gst/gst.h>
#include "imxvpuapi/imxvpuapi.h"
#include "framebuffer_array.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuDecoderContext GstImxVpuDecoderContext;
typedef struct _GstImxVpuDecoderContextClass GstImxVpuDecoderContextClass;


#define GST_TYPE_IMX_VPU_DECODER_CONTEXT             (gst_imx_vpu_decoder_context_get_type())
#define GST_IMX_VPU_DECODER_CONTEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DECODER_CONTEXT, GstImxVpuDecoderContext))
#define GST_IMX_VPU_DECODER_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DECODER_CONTEXT, GstImxVpuDecoderContextClass))
#define GST_IMX_VPU_DECODER_CONTEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_DECODER_CONTEXT, GstImxVpuDecoderContextClass))
#define GST_IS_IMX_VPU_DECODER_CONTEXT(obj)          (G_TYPE_CHECK_CONTEXT_TYPE((obj), GST_TYPE_IMX_VPU_DECODER_CONTEXT))
#define GST_IS_IMX_VPU_DECODER_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DECODER_CONTEXT))

#define GST_IMX_VPU_DECODER_CONTEXT_LOCK(obj)        (g_mutex_lock(&(((GstImxVpuDecoderContext*)(obj))->mutex)))
#define GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(obj)      (g_mutex_unlock(&(((GstImxVpuDecoderContext*)(obj))->mutex)))


/**
 * GstImxVpuDecoderContext:
 *
 * A decoder context refers to an entity combining a decoder
 * with a framebuffer array and some states. The framebuffer
 * array is registered with the decoder, which then uses it
 * as its memory pool for decoded buffers. Once created,
 * the context framebuffers cannot be reallocated. If this
 * is necessary (for example, because the video format changed),
 * then the current decoder context is unref'd, and a new
 * context is created.
 *
 * The mutex and condition variables are necessary for the
 * @gst_imx_vpu_decoder_context_wait_until_decoding_possible
 * function.
 */
struct _GstImxVpuDecoderContext
{
	GstObject parent;

	ImxVpuDecoder *decoder;

	GstImxVpuFramebufferArray *framebuffer_array;

	gboolean uses_interlacing;

	GMutex mutex;
	GCond cond;
	gboolean no_wait;
};


struct _GstImxVpuDecoderContextClass
{
	GstObjectClass parent_class;
};


/* Creates a new decoder context.
 *
 * Internally, this creates a new framebuffer array out of the given
 * initial_info values, using the given allocator to allocate the
 * framebuffer DMA memory blocks. The framebuffer array is then
 * registered automatically with the decoder.
 *
 * The return value is a floating GLib reference. See
 * gst_object_ref_sink() for details.
 */
GstImxVpuDecoderContext* gst_imx_vpu_decoder_context_new(ImxVpuDecoder *decoder, ImxVpuDecInitialInfo *initial_info, GstImxPhysMemAllocator *allocator);

/* Puts the decoder context in the no_wait mode, disabling any waiting.
 *
 * If no_wait is TRUE, then the context will be set to the no_wait mode.
 * In this mode, gst_imx_vpu_decoder_context_wait_until_decoding_possible()
 * calls will exit immediately. This is useful during shutdown and when
 * the state changes from PAUSED to READY. If no_wait is FALSE, the mode
 * is disabled.
 */
void gst_imx_vpu_decoder_context_set_no_wait(GstImxVpuDecoderContext *decoder_context, gboolean no_wait);
/* Waits until either decoding is possible again or until this function is interrupted.
 *
 * This function is necessary during decoding, since the VPU framebuffer pool is of
 * a fixed size. It is allocated and registered once (in @gst_imx_vpu_decoder_context_new),
 * and cannot be expanded later during decoding. Therefore, it can happen that all
 * framebuffers are currently in use, and no free framebuffer for decoding is available.
 * If this is the case, then this function blocks until some other code (for example,
 * the decoder_framebuffer_pool's @release function) determines tha a framebuffer is free and
 * calls @gst_imx_vpu_decoder_context_mark_as_displayed, which unblocks this function.
 *
 * The function can be interrupted by enabling the no_wait mode via
 * @gst_imx_vpu_decoder_context_set_no_wait.
 *
 * Internally, the blocking is done with a GCond condition variable.
 */
void gst_imx_vpu_decoder_context_wait_until_decoding_possible(GstImxVpuDecoderContext *decoder_context);

/* Marks the decoder in the context as gone.
 *
 * When the decoder itself is stopped, this is called. The reason is that the context might
 * also be ref'd by other entities, such as the decoder_framebuffer_pool. This means that
 * the context might "survive" the decoder. If it then tries to access the decoder (for example,
 * because the decoder_framebuffer_pool tries to call @gst_imx_vpu_decoder_context_set_decoder_as_gone),
 * it leads to a segmentation fault. Therefore, mark the decoder as gone, preventing any such
 * activities internally.
 */
void gst_imx_vpu_decoder_context_set_decoder_as_gone(GstImxVpuDecoderContext *decoder_context);

/* Marks a framebuffer as displayed, thus returning it to the VPU framebuffer pool.
 *
 * It is critically important to call this function when a decoded frame is no longer needed.
 * If it is not called, the VPU will eventually run out of framebuffers to decode into, which
 * will cause @gst_imx_vpu_decoder_context_wait_until_decoding_possible to deadlock.
 * As long as a framebuffer with a decoded frame inside is not passed on to this function,
 * the framebuffer can be accessed freely. But once this is called, the framebuffer must not be
 * accessed until the decoder outputs it again.
 *
 * Returns TRUE if the operation succeeded, FALSE otherwise. If this returns FALSE, the
 * framebuffer is not considered marked.
 */
gboolean gst_imx_vpu_decoder_context_mark_as_displayed(GstImxVpuDecoderContext *decoder_context, ImxVpuFramebuffer *framebuffer);


G_END_DECLS


#endif
