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

#ifndef GST_IMX_VPU_DEC_CONTEXT_H
#define GST_IMX_VPU_DEC_CONTEXT_H

#include <gst/gst.h>
#include <imxvpuapi2/imxvpuapi2.h>


G_BEGIN_DECLS


/* The GstImxVpuDecContext is an internal object used by VPU decoder
 * elements. Its primary function is to maintain the lifespan of an
 * ImxVpuApiDecoder instances until _all_ users of said instance are
 * done with it. These users include GstImxVpuDec instances (where
 * GstImxVpuDecContext instances are created), but also GstBuffers
 * created by a GstImxVpuDecBufferPool. As soon as the refcount of
 * one such GstBuffer reaches zero, the release vfunc of that pool
 * is called to release the GstBuffer back to that pool. Inside
 * that release function, the GstBuffer may be returned to the pool
 * just like in a regular pool, or it may be returned to the decoder
 * by calling gst_imx_vpu_dec_context_return_framebuffer_to_decoder().
 *
 * The latter is done if said decoder places decoded frames into
 * framebuffer DMA buffers that are owned by the *decoder's* internal
 * pool. Some hardware decoders have their own buffer pool, and
 * cannot be used unless said pool is set up. This of course makes
 * things more complicated, because that internal pool logic and
 * GStreamer's GstBufferPool logic collide with each other. To bring
 * these two together, the GstImxVpuDecBufferPool was written, and
 * as part of that, that pool' release function calls the function
 * gst_imx_vpu_dec_context_return_framebuffer_to_decoder().
 *
 * Now, libimxvpuapi's ImxVpuApiDecoder has no reference counting
 * mechanism, so if for example imx_vpu_api_close() were called
 * while other parts were still using that instance, there would
 * be a crash. However, GstImxVpuDecContext is based on GstObject,
 * so it *does* have a reference counting mechanism. So, the way
 * to avoid such problems is to make GstImxVpuDecBufferPool and
 * GstImxVpuDec hold references to an GstImxVpuDecContext instance.
 * That way, the context is not discarded until _all_ of its users
 * are done with it.
 *
 * There are additional benefits. The decoder instance can be closed
 * through the context, instead of directly. That way, if multiple
 * entities try to close the decoder at the same time, the context
 * can act as a mediator, and prevent duplicate close attempts
 * (which would lead to a segfault). Also, once the decoder instance
 * was closed, gst_imx_vpu_dec_context_return_framebuffer_to_decoder()
 * calls will respect this and effectively do nothing (since there
 * is no decoder to return the framebuffer to anymore).
 *
 * Also see the GstImxVpuDecBufferPool documentation for additional
 * explanations, since that object is used with the context together.
 */


#define GST_TYPE_IMX_VPU_DEC_CONTEXT             (gst_imx_vpu_dec_context_get_type())
#define GST_IMX_VPU_DEC_CONTEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DEC_CONTEXT, GstImxVpuDecContext))
#define GST_IMX_VPU_DEC_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DEC_CONTEXT, GstImxVpuDecContextClass))
#define GST_IMX_VPU_DEC_CONTEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_DEC_CONTEXT, GstImxVpuDecContextClass))
#define GST_IS_IMX_VPU_DEC_CONTEXT(obj)          (G_TYPE_CHECK_CONTEXT_TYPE((obj), GST_TYPE_IMX_VPU_DEC_CONTEXT))
#define GST_IS_IMX_VPU_DEC_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DEC_CONTEXT))


/* Helper macros for using the context' mutex. */
#define GST_IMX_VPU_DEC_CONTEXT_LOCK(obj)        (g_mutex_lock(&(((GstImxVpuDecContext*)(obj))->mutex)))
#define GST_IMX_VPU_DEC_CONTEXT_UNLOCK(obj)      (g_mutex_unlock(&(((GstImxVpuDecContext*)(obj))->mutex)))


typedef struct _GstImxVpuDecContext GstImxVpuDecContext;
typedef struct _GstImxVpuDecContextClass GstImxVpuDecContextClass;


struct _GstImxVpuDecContext
{
	GstObject parent;

	/*< private >*/

	ImxVpuApiDecoder *decoder;

	/* This mutex is used for thread-synchronized access to the decoder instance. */
	GMutex mutex;
};


struct _GstImxVpuDecContextClass
{
	GstObjectClass parent_class;
};


GstImxVpuDecContext* gst_imx_vpu_dec_context_new(ImxVpuApiDecoder *decoder);
void gst_imx_vpu_dec_context_close_decoder(GstImxVpuDecContext *imx_vpu_dec_context);
void gst_imx_vpu_dec_context_return_framebuffer_to_decoder(GstImxVpuDecContext *imx_vpu_dec_context, ImxDmaBuffer *framebuffer);


G_END_DECLS


#endif /* GST_IMX_VPU_DEC_CONTEXT_H */
