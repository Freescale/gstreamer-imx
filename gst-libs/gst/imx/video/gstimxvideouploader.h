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

#ifndef GST_IMX_VIDEO_UPLOADER_H
#define GST_IMX_VIDEO_UPLOADER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferuploader.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_VIDEO_UPLOADER             (gst_imx_video_uploader_get_type())
#define GST_IMX_VIDEO_UPLOADER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_UPLOADER, GstImxVideoUploader))
#define GST_IMX_VIDEO_UPLOADER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VIDEO_UPLOADER, GstImxVideoUploaderClass))
#define GST_IMX_VIDEO_UPLOADER_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VIDEO_UPLOADER, GstImxVideoUploaderClass))
#define GST_IMX_VIDEO_UPLOADER_CAST(obj)        ((GstImxVideoUploader *)(obj))
#define GST_IS_IMX_VIDEO_UPLOADER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VIDEO_UPLOADER))
#define GST_IS_IMX_VIDEO_UPLOADER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VIDEO_UPLOADER))


/**
 * GstImxVideoUploader:
 *
 * Uploads video frame data into DMA memory, using @GstVideoFrame based copies if necessary due to alignment issues.
 *
 * Internally, this uses a @GstImxDmaBufferUploader if the input frames
 * are already aligned according to the alignment requirements specified
 * by the @gst_imx_video_uploader_new arguments. If a frame is not aligned,
 * the internal uploader is not used. Instead, a custom frame copy is
 * made using @GstVideoFrame and @gst_video_frame_copy to create a copy
 * of the frame that is properly aligned. GstBuffer instances created for
 * this custom frame copy use the same allocator that the internal uploader
 * uses (that is, the allocator pased to @gst_imx_video_uploader_new).
 * For these custom copies, there is also an intenal buffer pool to be able
 * to reuse these buffers.
 *
 * The API is mostly designed as a drop-in replacement for @GstImxDmaBufferUploader.
 * If an element has been using that one, this video uploader can easily be
 * used instead. The only two differences are the extra alignment information
 * passed to @gst_imx_video_uploader_new and the extra input video info function
 * @gst_imx_video_uploader_set_input_video_info that needs to be called before
 * @gst_imx_video_uploader_perform can be used (the former is typically called
 * in set_caps style functions).
 */
typedef struct _GstImxVideoUploader GstImxVideoUploader;
typedef struct _GstImxVideoUploaderClass GstImxVideoUploaderClass;


GType gst_imx_video_uploader_get_type(void);

/**
 * gst_imx_video_uploader_new:
 * @imx_dma_buffer_allocator: ImxDmaBuffer allocator to use in this
 *     object. If ION support is enabled, this allows for DMA-BUF based uploads.
 * @stride_alignment: Required stride alignment, in bytes.
 * @plane_row_alignment: Required plane alignment, in number of scanline rows.
 *
 * Creates a new video frame uploader.
 *
 * The alignments are specified in terms of what integer multiple the
 * respective values must be of. For example, if @stride_alignment is 16,
 * then the stride values must be an integer multiple of 16. If the input
 * frames do not satisfy these alignment requirements, then a @GstVideoFrame
 * based internal copy is made.
 *
 * An alignment value of 0 is treated just like the value 1 - that is, both
 * essentially mean "no special alignment required".
 *
 * The allocator is ref'd in this function and unref'd when
 * the uploader is destroyed.
 *
 * Returns: (transfer floating) A new upload object.
 */
GstImxVideoUploader* gst_imx_video_uploader_new(GstAllocator *imx_dma_buffer_allocator, guint stride_alignment, guint plane_row_alignment);

/**
 * gst_imx_video_uploader_get_allocator:
 * @uploader: Video uploader instance to get the allocator from.
 *
 * Returns: (transfer full) The ImxDmaBuffer allocator that this video uploader uses.
 * Unref with gst_object_unref() after use.
 */
GstAllocator* gst_imx_video_uploader_get_allocator(GstImxVideoUploader *uploader);

/**
 * gst_imx_video_uploader_get_dma_buffer_uploader:
 * @uploader: Video uploader instance to get the internal DMA buffer uploader from.
 *
 * Returns: (transfer full) The internal @GstImxDmaBufferUploader.
 * Unref with gst_object_unref() after use.
 */
GstImxDmaBufferUploader* gst_imx_video_uploader_get_dma_buffer_uploader(GstImxVideoUploader *uploader);

/**
 * gst_imx_video_uploader_perform:
 * @uploader: Video uploader instance to use for uploading video frame data.
 * @input_buffer: (transfer-none) Buffer that shall be uploaded.
 * @output_buffer: (out) (transfer-full) Uploaded version of @input_buffer.
 *
 * The main uploading function. As mentioned in the @GstImxVideoUploader, this
 * uploads by performing a CPU- and @gst_video_frame_copy based upload into DMA
 * memory, creating a copy of @input_buffer that fulfills the alignment requirements
 * that were specified in the @gst_imx_video_uploader_new call. If the video frame
 * in @input_buffer already fulfills the alignment requirements, then the internal
 * uploader's @gst_imx_dma_buffer_uploader_perform is used instead.
 *
 * @gst_video_frame_copy based copies always also contain a @GstVideoMeta.
 *
 * This must not be called before @gst_imx_video_uploader_set_input_video_info,
 * since that function is necessary for setting up the internal buffer pool that
 * allocates output buffers for the frame copies.
 *
 * If @input_buffer has no memory blocks, @output_buffer is set to @input_buffer, and
 * @input_buffer is ref'd.
 *
 * In case of an error, @output_buffer will remain unmodified, and the refcount of @input_buffer
 * will not be changed.
 *
 * Returns: @GST_FLOW_OK if the upload succeeded.
 */
GstFlowReturn gst_imx_video_uploader_perform(GstImxVideoUploader *uploader, GstBuffer *input_buffer, GstBuffer **output_buffer);

/**
 * gst_imx_video_uploader_set_input_video_info:
 * @uploader: Video uploader instance that shall have its input video info set.
 *
 * This must be called before any uploads can be done using @gst_imx_video_uploader_perform.
 * It should be called again later if the input video info changes.
 *
 * Returns: TRUE if the call succeeded, FALSE in case of a non-recoverable error.
 */
gboolean gst_imx_video_uploader_set_input_video_info(GstImxVideoUploader *uploader, GstVideoInfo const *input_video_info);

/**
 * gst_imx_video_uploader_set_alignments:
 * @uploader: Video uploader instance that shall have new alignment values set.
 *
 * Sets new stride and plane row alignment values. These have the same semantics as the values
 * that are passed to @gst_imx_video_uploader_new. This is useful if later, after creating the
 * video uploader, updated alignment information becomes available. The new alignments will be
 * used starting with the next @gst_imx_video_uploader_perform call.
 */
void gst_imx_video_uploader_set_alignments(GstImxVideoUploader *uploader, guint stride_alignment, guint plane_row_alignment);


G_END_DECLS


#endif /* GST_IMX_VIDEO_UPLOADER_H */
