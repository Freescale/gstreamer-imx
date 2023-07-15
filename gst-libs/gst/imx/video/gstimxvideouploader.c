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

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gst/imx/video/gstimxvideoutils.h"
#include "gstimxvideouploader.h"


GST_DEBUG_CATEGORY_STATIC(imx_video_uploader_debug);
#define GST_CAT_DEFAULT imx_video_uploader_debug


struct _GstImxVideoUploader
{
	GstObject parent;

	guint stride_alignment;
	guint plane_row_alignment;

	GstVideoInfo original_input_video_info;
	GstVideoInfo aligned_input_video_info;
	gboolean original_input_video_info_aligned;

	GstBufferPool *aligned_frames_buffer_pool;

	GstImxDmaBufferUploader *dma_buffer_uploader;
};


struct _GstImxVideoUploaderClass
{
	GstObjectClass parent_class;
};


G_DEFINE_TYPE(GstImxVideoUploader, gst_imx_video_uploader, GST_TYPE_OBJECT)


static void gst_imx_video_uploader_dispose(GObject *object);


static void gst_imx_video_uploader_class_init(GstImxVideoUploaderClass *klass)
{
	GObjectClass *object_class;

	GST_DEBUG_CATEGORY_INIT(imx_video_uploader_debug, "imxvideoupload", 0, "NXP i.MX video frame upload");

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_video_uploader_dispose);
}


static void gst_imx_video_uploader_init(GstImxVideoUploader *self)
{
	self->aligned_frames_buffer_pool = NULL;
	self->dma_buffer_uploader = NULL;
}


static void gst_imx_video_uploader_dispose(GObject *object)
{
	GstImxVideoUploader *self = GST_IMX_VIDEO_UPLOADER(object);

	if (self->aligned_frames_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->aligned_frames_buffer_pool));
		self->aligned_frames_buffer_pool = NULL;
	}

	if (self->dma_buffer_uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(self->dma_buffer_uploader));
		self->dma_buffer_uploader = NULL;
	}

	G_OBJECT_CLASS(gst_imx_video_uploader_parent_class)->dispose(object);
}


GstImxVideoUploader* gst_imx_video_uploader_new(GstAllocator *imx_dma_buffer_allocator, guint stride_alignment, guint plane_row_alignment)
{
	GstImxVideoUploader *video_uploader;

	g_assert(imx_dma_buffer_allocator != NULL);
	g_assert(GST_IS_IMX_DMA_BUFFER_ALLOCATOR(imx_dma_buffer_allocator));

	video_uploader = g_object_new(gst_imx_video_uploader_get_type(), NULL);

	video_uploader->stride_alignment = (stride_alignment == 0) ? 1 : stride_alignment;
	video_uploader->plane_row_alignment = (plane_row_alignment == 0) ? 1 : plane_row_alignment;

	/* NOTE: gst_imx_dma_buffer_uploader_new() refs the allocator. */
	video_uploader->dma_buffer_uploader = gst_imx_dma_buffer_uploader_new(imx_dma_buffer_allocator);
	if (video_uploader->dma_buffer_uploader == NULL)
	{
		GST_ERROR_OBJECT(video_uploader, "could not create DMA buffer uploader");
		goto error;
	}

	GST_DEBUG_OBJECT(
		video_uploader,
		"created new video uploader with internal DMA buffer uploader %" GST_PTR_FORMAT " allocator %" GST_PTR_FORMAT " stride alignment %u plane alignment %u",
		(gpointer)(video_uploader->dma_buffer_uploader),
		(gpointer)imx_dma_buffer_allocator,
		stride_alignment,
		plane_row_alignment
	);

finish:
	return video_uploader;

error:
	gst_object_unref(GST_OBJECT(video_uploader));
	video_uploader = NULL;
	goto finish;
}


GstAllocator* gst_imx_video_uploader_get_allocator(GstImxVideoUploader *uploader)
{
	g_assert(uploader != NULL);
	return gst_imx_dma_buffer_uploader_get_allocator(uploader->dma_buffer_uploader);
}


GstImxDmaBufferUploader* gst_imx_video_uploader_get_dma_buffer_uploader(GstImxVideoUploader *uploader)
{
	g_assert(uploader != NULL);
	gst_object_ref(GST_OBJECT(uploader->dma_buffer_uploader));
	return uploader->dma_buffer_uploader;
}


GstFlowReturn gst_imx_video_uploader_perform(GstImxVideoUploader *uploader, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	guint i;
	GstVideoMeta *video_meta;
	gboolean needs_frame_copy;
	GstVideoFrame input_buffer_frame;
	gboolean input_buffer_frame_mapped;
	GstVideoFrame uploaded_buffer_frame;
	gboolean uploaded_buffer_frame_mapped;

	g_assert(uploader != NULL);
	g_assert(input_buffer != NULL);
	g_assert(output_buffer != NULL);

	video_meta = gst_buffer_get_video_meta(input_buffer);
	needs_frame_copy = FALSE;

	GST_LOG_OBJECT(
		uploader,
		"processing input buffer (buffer has video meta: %d); buffer details: %" GST_PTR_FORMAT,
		(video_meta != NULL),
		(gpointer)input_buffer
	);

	*output_buffer = NULL;

	input_buffer_frame_mapped = FALSE;
	uploaded_buffer_frame_mapped = FALSE;

	if (video_meta != NULL)
	{
		/* We need to check if the stride and plane offset values in the videometa
		 * are already aligned. If not, we have to perform a frame copy. */

		gint stride_remainder, plane_row_remainder;
		gboolean stride_aligned, plane_offsets_aligned;
		gint num_plane_rows;

		stride_remainder = (video_meta->stride[0] % uploader->stride_alignment);
		stride_aligned = (stride_remainder == 0);

		num_plane_rows = gst_imx_video_utils_calculate_total_num_frame_rows(input_buffer, NULL);

		plane_row_remainder = (num_plane_rows % uploader->plane_row_alignment);
		plane_offsets_aligned = (plane_row_remainder == 0);

		needs_frame_copy = !stride_aligned || !plane_offsets_aligned;

		GST_LOG_OBJECT(uploader, "stride in video meta is aligned: %d  (stride: %d  stride remainder: %d)", stride_aligned, video_meta->stride[0], stride_remainder);
		GST_LOG_OBJECT(uploader, "plane offsets in video meta is aligned: %d  (num_plane_rows: %d  plane row remainder: %d)", plane_offsets_aligned, num_plane_rows, plane_row_remainder);
	}
	else
	{
		GST_LOG_OBJECT(uploader, "original input video info is aligned: %d", uploader->original_input_video_info_aligned);
		needs_frame_copy = !(uploader->original_input_video_info_aligned);
	}

	GST_LOG_OBJECT(uploader, "-> GstVideoFrame based frame copy is needed: %d", needs_frame_copy);

	if (needs_frame_copy)
	{
		flow_ret = gst_buffer_pool_acquire_buffer(uploader->aligned_frames_buffer_pool, output_buffer, NULL);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		{
			GST_ERROR_OBJECT(uploader, "could not acquire buffer from aligned buffer pool: %s", gst_flow_get_name(flow_ret));
			goto error;
		}

		if (!gst_video_frame_map(
			&input_buffer_frame,
			&(uploader->original_input_video_info),
			input_buffer,
			GST_MAP_READ
		))
		{
			GST_ERROR_OBJECT(uploader, "could not map input video frame");
			goto error;
		}
		input_buffer_frame_mapped = TRUE;

		if (!gst_video_frame_map(
			&uploaded_buffer_frame,
			&(uploader->aligned_input_video_info),
			*output_buffer,
			GST_MAP_WRITE
		))
		{
			GST_ERROR_OBJECT(uploader, "could not map input video frame");
			goto error;
		}
		uploaded_buffer_frame_mapped = TRUE;

		if (!gst_video_frame_copy(&uploaded_buffer_frame, &input_buffer_frame))
		{
			GST_ERROR_OBJECT(uploader, "could not copy pixels from input buffer into output buffer");
			goto error;
		}

		GST_LOG_OBJECT(
			uploader,
			"copied pixels from input buffer into output buffer"
		);

		gst_video_frame_unmap(&uploaded_buffer_frame);
		uploaded_buffer_frame_mapped = FALSE;

		/* Copy everything from the input buffer that's not the main buffer data
		 * (since these are pixels, and we already took care of those above).
		 * This includes GstMeta values such as the videometa. */
		if (!gst_buffer_copy_into(
			*output_buffer,
			input_buffer,
			GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META,
			0,
			-1
		))
		{
			GST_ERROR_OBJECT(uploader, "could not copy extra buffer data (metadata, gstmetas, timestamps ..)");
			goto error;
		}

		/* The output buffer's videometa needs to be adjusted, since it does not
		 * yet have the plane stride / offset values from our aligned video info. */
		{
			GstVideoInfo *aligned_video_info = &(uploader->aligned_input_video_info);
			GstVideoMeta *output_video_meta;

			output_video_meta = gst_buffer_get_video_meta(*output_buffer);
			g_assert(output_video_meta != NULL);

			for (i = 0; i < GST_VIDEO_INFO_N_PLANES(aligned_video_info); ++i)
			{
				output_video_meta->stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(aligned_video_info, i);
				output_video_meta->offset[i] = GST_VIDEO_INFO_PLANE_OFFSET(aligned_video_info, i);
			}
		}
	}
	else
	{
		/* Input buffer video data is already aligned, so we do not have to perform
		 * a frame copy. Just use the internal DMA buffer uploader instead. */

		flow_ret = gst_imx_dma_buffer_uploader_perform(uploader->dma_buffer_uploader, input_buffer, output_buffer);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			goto error;

		/* gst_buffer_copy_into() is used by the DMA buffer uploader, but it does not
		 * copy memory metas like videometa by default. Do this manually here. */
		if ((video_meta != NULL) && (gst_buffer_get_video_meta(*output_buffer) == NULL))
		{
			GST_TRACE_OBJECT(uploader, "copying videometa from input to output buffer");
			gst_buffer_add_video_meta_full(
				*output_buffer,
				video_meta->flags,
				video_meta->format,
				video_meta->width,
				video_meta->height,
				video_meta->n_planes,
				video_meta->offset,
				video_meta->stride
			);
		}
	}

finish:
	if (input_buffer_frame_mapped)
		gst_video_frame_unmap(&input_buffer_frame);
	if (uploaded_buffer_frame_mapped)
		gst_video_frame_unmap(&uploaded_buffer_frame);

	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;

	gst_buffer_replace(output_buffer, NULL);

	goto finish;
}


gboolean gst_imx_video_uploader_set_input_video_info(GstImxVideoUploader *uploader, GstVideoInfo const *input_video_info)
{
	gint stride_remainder, plane_row_remainder;
	gint num_plane_rows, stride;
	guint plane_index, num_planes;
	GstVideoAlignment video_alignment;
	GstStructure *pool_config;
	GstCaps *input_caps;
	guint buffer_size;
	GstAllocationParams allocation_params;
	GstAllocator *dma_buffer_allocator;

	g_assert(uploader != NULL);
	g_assert(input_video_info != NULL);

	memcpy(&(uploader->original_input_video_info), input_video_info, sizeof(GstVideoInfo));
	memcpy(&(uploader->aligned_input_video_info), input_video_info, sizeof(GstVideoInfo));

	num_planes = GST_VIDEO_INFO_N_PLANES(input_video_info);
	stride = GST_VIDEO_INFO_PLANE_STRIDE(input_video_info, 0);


	/* Analyze the input video info to see if stride and plane row count are already aligned.
	 * The remainders are computed by calculating the aligned version of the quantity and
	 * then subtracting that from the unaligned one. If the remainder is 0, this means
	 * that the quantity is already aligned. */

	stride_remainder = ((stride + (uploader->stride_alignment - 1)) / uploader->stride_alignment) * uploader->stride_alignment - stride;

	num_plane_rows = gst_imx_video_utils_calculate_total_num_frame_rows(NULL, input_video_info);

	plane_row_remainder = ((num_plane_rows + (uploader->plane_row_alignment - 1)) / uploader->plane_row_alignment) * uploader->plane_row_alignment - num_plane_rows;

	GST_DEBUG_OBJECT(uploader, "stride remainder: %d  plane row remainder: %d", stride_remainder, plane_row_remainder);

	uploader->original_input_video_info_aligned = (stride_remainder == 0) && (plane_row_remainder == 0);


	/* Align the stride and number of plane rows. */

	gst_video_alignment_reset(&video_alignment);
	for (plane_index = 0; plane_index < num_planes; ++plane_index)
	{
		gint w_sub = GST_VIDEO_FORMAT_INFO_W_SUB(input_video_info->finfo, plane_index);
		video_alignment.stride_align[plane_index] = GST_VIDEO_SUB_SCALE(w_sub, uploader->stride_alignment) - 1;
	}

	video_alignment.padding_bottom = plane_row_remainder;

	if (!gst_video_info_align(&(uploader->aligned_input_video_info), &video_alignment))
		return FALSE;

	/* There is no way to instruct gst_video_info_align() to just align the plane
	 * offsets. Setting the GstVideoAlignment padding_bottom field adjusts those,
	 * but also modifies the height value. Since we don't want that, we reset
	 * the height back to its original value. */
	GST_VIDEO_INFO_HEIGHT(&(uploader->aligned_input_video_info)) = GST_VIDEO_INFO_HEIGHT(input_video_info);


	/* Create new buffer pool to be used for aligned frame copies. */

	if (uploader->aligned_frames_buffer_pool != NULL)
		gst_object_unref(GST_OBJECT(uploader->aligned_frames_buffer_pool));

	for (plane_index = 0; plane_index < num_planes; ++plane_index)
	{
		GST_DEBUG_OBJECT(
			uploader,
			"plane %u  plane stride: original: %d aligned: %d  plane offset: original: %" G_GSIZE_FORMAT " aligned: %" G_GSIZE_FORMAT,
			plane_index,
			GST_VIDEO_INFO_PLANE_STRIDE(&(uploader->original_input_video_info), plane_index),
			GST_VIDEO_INFO_PLANE_STRIDE(&(uploader->aligned_input_video_info), plane_index),
			GST_VIDEO_INFO_PLANE_OFFSET(&(uploader->original_input_video_info), plane_index),
			GST_VIDEO_INFO_PLANE_OFFSET(&(uploader->aligned_input_video_info), plane_index)
		);
	}

	uploader->aligned_frames_buffer_pool = gst_video_buffer_pool_new();

	input_caps = gst_video_info_to_caps(&(uploader->original_input_video_info));

	buffer_size = GST_VIDEO_INFO_SIZE(&(uploader->aligned_input_video_info));

	gst_allocation_params_init(&allocation_params);
	dma_buffer_allocator = gst_imx_dma_buffer_uploader_get_allocator(uploader->dma_buffer_uploader);

	pool_config = gst_buffer_pool_get_config(uploader->aligned_frames_buffer_pool);
	gst_buffer_pool_config_set_params(pool_config, input_caps, buffer_size, 0, 0);
	gst_buffer_pool_config_set_allocator(pool_config, dma_buffer_allocator, &allocation_params);
	gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(uploader->aligned_frames_buffer_pool, pool_config);

	gst_buffer_pool_set_active(uploader->aligned_frames_buffer_pool, TRUE);


	/* Cleanup. */

	gst_object_unref(GST_OBJECT(dma_buffer_allocator));
	gst_caps_unref(input_caps);

	return TRUE;
}
