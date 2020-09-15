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

#ifndef GST_IMX_DMA_BUFFER_UPLOADER_H
#define GST_IMX_DMA_BUFFER_UPLOADER_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_DMA_BUFFER_UPLOADER             (gst_imx_dma_buffer_uploader_get_type())
#define GST_IMX_DMA_BUFFER_UPLOADER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_DMA_BUFFER_UPLOADER,GstImxDmaBufferUploader))
#define GST_IMX_DMA_BUFFER_UPLOADER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_DMA_BUFFER_UPLOADER,GstImxDmaBufferUploaderClass))
#define GST_IMX_DMA_BUFFER_UPLOADER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_DMA_BUFFER_UPLOADER, GstImxDmaBufferUploaderClass))
#define GST_IMX_DMA_BUFFER_UPLOADER_CAST(obj)        ((GstImxDmaBufferUploader *)(obj))
#define GST_IS_IMX_DMA_BUFFER_UPLOADER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_DMA_BUFFER_UPLOADER))
#define GST_IS_IMX_DMA_BUFFER_UPLOADER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_DMA_BUFFER_UPLOADER))


typedef struct _GstImxDmaBufferUploader GstImxDmaBufferUploader;
typedef struct _GstImxDmaBufferUploaderClass GstImxDmaBufferUploaderClass;


typedef struct _GstImxDmaBufferUploadMethodContext GstImxDmaBufferUploadMethodContext;
typedef struct _GstImxDmaBufferUploadMethodType GstImxDmaBufferUploadMethodType;


/**
 * GstImxDmaBufferUploader:
 *
 * Uploads data into @GstMemory instances that use an ImxDmaBuffer as the underlying memory.
 *
 * gstreamer-imx elements use libimxdmabuffer's ImxDmaBuffer structure as the basic memory
 * unit. ImxDmaBuffer represents a block of physically contiguous memory (also called
 * "DMA buffers", not to be confused with Linux DMA-BUF), suitable for DMA transfers.
 *
 * An allocator that implements the @GstImxDmaBufferAllocatorInterface can allocate
 * @GstMemory blocks that contain ImxDmaBuffer instances. In gstreamer-imx, such allocators
 * also implement @GstPhysMemoryAllocatorInterface. Additionally, the @GstImxIonAllocator
 * allows for allocating DMA-BUF memory through the system wide ION allocator.
 *
 * This means that a @GstMemory that is backed by ImxDmaBuffer is the most "generic" type
 * of memory in gstreamer-imx. It can be accessed like system memory, its physical address
 * can be obtained via @gst_phys_memory_get_phys_addr, its ImxDmaBuffer can be retrieved
 * by using @gst_imx_get_dma_buffer_from_memory or @gst_imx_get_dma_buffer_from_buffer,
 * and, on an ION-enabled i.MX platform, the @gst_dmabuf_memory_get_fd function can be used
 * for retrieving the DMA-BUF FD. Therefore, it makes sense to unify these three ways of
 * access into one.
 *
 * For input, this "uploader" takes care of getting incoming data into ImxDmaBuffer-backed
 * @GstMemory. Internally, the uploader has "upload methods". The uploader asks each method
 * to try to perform the upload. As soon as one succeeds, the uploader considers the upload
 * to be done. There are upload methods for DMA-BUF buffers, for raw uploads (meaning that
 * the bytes of input buffers are copied into an ImxDmaBuffer-based GstBuffer), etc.
 * The upload is done by calling @gst_imx_dma_buffer_uploader_perform.
 *
 * For output, things are much simpler, since, as described above, ImxDmaBuffer can be used
 * in 3 ways without chaging a single thing. The same @GstMemory that was allocated by an
 * allocator that implements the aforementioned interfaces and extends @GstDmaBufAllocator
 * will be accessible via regular system memory functions (@gst_memory_map etc.), via
 * @gst_dmabuf_memory_get_fd, via @gst_imx_get_dma_buffer_from_memory etc. without any
 * changes. So, there is no need for an actual "downloader".
 *
 * In short: Upload = transfer incoming data into ImxDmaBuffer blocks, or wrap DMA-BUF FDs
 * into custom ImxDmaBuffer blocks, or simply pass through GstMemory if it already contains
 * an ImxDmaBuffer. Download = Just push ImxDmaBuffer-backed GstMemory downstream.
 */
struct _GstImxDmaBufferUploader
{
	GstObject parent;

	/*< private >*/

	GstImxDmaBufferUploadMethodContext **upload_method_contexts;

	GstAllocator *imx_dma_buffer_allocator;
	GstCaps *output_caps;
};


struct _GstImxDmaBufferUploaderClass
{
	GstObjectClass parent_class;
};



GType gst_imx_dma_buffer_uploader_get_type(void);


/**
 * gst_imx_dma_buffer_uploader_new:
 * @imx_dma_buffer_allocator: ImxDmaBuffer allocator to use in this
 *     object. If ION support is enabled, this must be an ION allocator.
 *
 * Creates a new upload object.
 *
 * The specified ImxDmaBuffer allocator will be used in
 * @gst_imx_dma_buffer_uploader_propose_allocation calls.
 *
 * The allocator is ref'd in this function and unref'd when
 * the uploader is destroyed.
 *
 * Returns: (transfer floating) A new upload object.
 */
GstImxDmaBufferUploader* gst_imx_dma_buffer_uploader_new(GstAllocator *imx_dma_buffer_allocator);

/**
 * gst_imx_dma_buffer_uploader_perform:
 * @uploader: Uploader instance to use for uploading data.
 * @input_buffer: (transfer-none) Buffer that shall be uploaded.
 * @output_buffer: (out) (transfer-full) Uploaded version of @input_buffer.
 *
 * The central function of the uploader. This is the function that does the actual uploading.
 * A version of @buffer is produced that uses an ImxDmaBuffer backed @GstMemory as its memory.
 *
 * If @input_buffer has no memory blocks, @output_buffer is set to @input_buffer, and
 * @input_buffer is ref'd.
 *
 * In case of an error, @output_buffer will remain unmodified, and the refcount of @input_buffer
 * will not be changed.
 *
 * Returns: @GST_FLOW_OK if the upload succeeded.
 */
GstFlowReturn gst_imx_dma_buffer_uploader_perform(GstImxDmaBufferUploader *uploader, GstBuffer *input_buffer, GstBuffer **output_buffer);


G_END_DECLS


#endif /* GST_IMX_DMA_BUFFER_UPLOADER_H */
