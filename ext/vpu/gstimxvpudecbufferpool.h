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

#ifndef GST_IMX_VPU_DEC_BUFFER_POOL_H
#define GST_IMX_VPU_DEC_BUFFER_POOL_H

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <imxvpuapi2/imxvpuapi2.h>
#include "gstimxvpudeccontext.h"


G_BEGIN_DECLS


/* The GstImxVpuDecBufferPool is a special buffer pool for internal use with
 * i.MX decoder elements.
 *
 * A special buffer pool is necessary because of a peculiarity of at least
 * some of the i.MX VPU decoders: They use their own internal buffer pool
 * logic. That is, one has to add/register framebuffers to the decoders, and
 * the decoder itself picks one of the added framebuffers to decode frames
 * into. However, a GstBufferPool does the same thing. Having two buffer pool
 * logics does not work out of the box - they'll get in the way of each other.
 *
 * The solution is this special GstBufferPool subclass. It knows two types of
 * buffers: The regular ones, which are handled by the functionaity of the
 * GstBufferPool class, and "reserved" ones, which are handled by the subclass.
 * "Reserved" means that while they _are_ allocated, they are _not_ actually
 * placed into the internal GstBuffer collection that is inside GstBufferPool.
 * Instead, these "reserved" GstBuffers are kept in a list in the _subclass_.
 *
 * Reserved buffers differ from regular ones in two ways:
 *
 * First, they are "selected".  Once a reserved buffer is selected, the next
 * gst_buffer_pool_acquire_buffer() call will return this reserved buffer. This
 * is accomplished by caling. gst_imx_vpu_dec_buffer_pool_select_reserved_buffer().
 * In other words, the reserved buffer to acquire is not chosen automatically.
 *
 * Second, when an acquired reserved buffer's refcount reaches zero, it is
 * released back to the pool as usual. However, when this happens, it is also
 * returned to the VPU by a gst_imx_vpu_dec_context_return_framebuffer_to_decoder()
 * call. (The GstImxVpuDecBufferPool can detect whether or not a GstBuffer is
 * a reserved one by checking for the ACQUIRE_FLAG_SELECTED flag.)
 *
 * This makes an integration possible. When the VPU requests framebuffers to be
 * added to its pool, one GstBuffer with ImxDmaBuffer backing is allocated for
 * each requested framebuffer. gst_imx_vpu_dec_buffer_pool_reserve_buffer() is
 * used for this purpose. These GstBuffers are all reserved. If the VPU decodes
 * frames into buffers from its own pool (that is, it does not decode into a
 * separate output buffer), then gst_imx_vpu_dec_buffer_pool_select_reserved_buffer()
 * is called to select this buffer, making sure that the next acquire call picks
 * the buffer that holds the newly decoded frame. And, once that buffer is no
 * longer needed, it is properly returned to the VPU's pool by the behavior in the
 * release() function.
 */


#define GST_TYPE_IMX_VPU_DEC_BUFFER_POOL             (gst_imx_vpu_dec_buffer_pool_get_type())
#define GST_IMX_VPU_DEC_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DEC_BUFFER_POOL,GstImxVpuDecBufferPool))
#define GST_IMX_VPU_DEC_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DEC_BUFFER_POOL,GstImxVpuDecBufferPoolClass))
#define GST_IMX_VPU_DEC_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_DEC_BUFFER_POOL, GstImxVpuDecBufferPoolClass))
#define GST_IMX_VPU_DEC_BUFFER_POOL_CAST(obj)        ((GstImxVpuDecBufferPool *)(obj))
#define GST_IS_IMX_VPU_DEC_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_DEC_BUFFER_POOL))
#define GST_IS_IMX_VPU_DEC_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DEC_BUFFER_POOL))


#define GST_BUFFER_POOL_OPTION_IMX_VPU_DEC_BUFFER_POOL "GstBufferPoolOptionImxVpuDecBufferPool"


typedef struct _GstImxVpuDecBufferPool GstImxVpuDecBufferPool;
typedef struct _GstImxVpuDecBufferPoolClass GstImxVpuDecBufferPoolClass;


typedef enum
{
	GST_IMX_VPU_DEC_BUFFER_POOL_ACQUIRE_FLAG_SELECTED = GST_BUFFER_POOL_ACQUIRE_FLAG_LAST
}
GstImxVpuDecBufferPoolAcquireFlags;


GType gst_imx_vpu_dec_buffer_pool_get_type(void);

GstImxVpuDecBufferPool* gst_imx_vpu_dec_buffer_pool_new(ImxVpuApiDecStreamInfo *stream_info, GstImxVpuDecContext *decoder_context);

GstVideoInfo const * gst_imx_vpu_dec_buffer_pool_get_video_info(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool);

GstBuffer* gst_imx_vpu_dec_buffer_pool_reserve_buffer(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool);
void gst_imx_vpu_dec_buffer_pool_select_reserved_buffer(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool, GstBuffer *buffer);


G_END_DECLS


#endif /* GST_IMX_VPU_DEC_BUFFER_POOL_H */
