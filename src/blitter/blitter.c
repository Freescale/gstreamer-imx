/* base class for i.MX blitters
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


#include "blitter.h"

#include "../common/phys_mem_meta.h"
#include "../common/phys_mem_buffer_pool.h"



GST_DEBUG_CATEGORY_STATIC(imx_blitter_debug);
#define GST_CAT_DEFAULT imx_blitter_debug


G_DEFINE_ABSTRACT_TYPE(GstImxBlitter, gst_imx_blitter, GST_TYPE_OBJECT)


static void gst_imx_blitter_dispose(GObject *object);





void gst_imx_blitter_class_init(GstImxBlitterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_blitter_dispose);

	klass->set_input_video_info   = NULL;
	klass->set_output_video_info  = NULL;
	klass->set_input_region       = NULL;
	klass->set_output_canvas      = NULL;
	klass->set_num_output_pages   = NULL;
	klass->set_input_frame        = NULL;
	klass->set_output_frame       = NULL;
	klass->get_phys_mem_allocator = NULL;
	klass->fill_region            = NULL;
	klass->blit                   = NULL;
	klass->flush                  = NULL;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_debug, "imxblitter", 0, "Freescale i.MX blitter base");
}


void gst_imx_blitter_init(GstImxBlitter *blitter)
{
	GST_TRACE_OBJECT(blitter, "initializing blitter base");
	blitter->dma_bufferpool = NULL;
	gst_video_info_init(&(blitter->input_video_info));
}


static void gst_imx_blitter_dispose(GObject *object)
{
	GstImxBlitter *blitter = GST_IMX_BLITTER(object);

	gst_imx_blitter_flush(blitter);

	if (blitter->dma_bufferpool != NULL)
	{
		gst_object_unref(GST_OBJECT(blitter->dma_bufferpool));
		blitter->dma_bufferpool = NULL;
	}

	G_OBJECT_CLASS(gst_imx_blitter_parent_class)->dispose(object);
}


gboolean gst_imx_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	g_assert(input_video_info != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	/* Don't actually do anything unless the video info changed */
	if (gst_video_info_is_equal(&(blitter->input_video_info), input_video_info))
		return TRUE;

	if (klass->set_input_video_info != NULL)
	{
		if (!klass->set_input_video_info(blitter, input_video_info))
			return FALSE;
	}

	blitter->input_video_info = *input_video_info;

	/* Destroy the existing buffer pool, since it is no longer usable
	 * (the new video info has a different size)
	 * the next time the buffer pool is needed, it will be recreated
	 * in blit() with the new input video info */
	if (blitter->dma_bufferpool != NULL)
	{
		gst_object_unref(GST_OBJECT(blitter->dma_bufferpool));
		blitter->dma_bufferpool = NULL;
	}

	return TRUE;
}


gboolean gst_imx_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	g_assert(output_video_info != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	if (klass->set_output_video_info != NULL)
		return klass->set_output_video_info(blitter, output_video_info);
	else
		return TRUE;
}


gboolean gst_imx_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	if (klass->set_input_region != NULL)
		return klass->set_input_region(blitter, input_region);
	else
		return TRUE;
}


gboolean gst_imx_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	if (klass->set_output_canvas != NULL)
		return klass->set_output_canvas(blitter, output_canvas);
	else
		return TRUE;
}


gboolean gst_imx_blitter_set_num_output_pages(GstImxBlitter *blitter, guint num_output_pages)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	g_assert(num_output_pages >= 1);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	if (klass->set_num_output_pages != NULL)
		return klass->set_num_output_pages(blitter, num_output_pages);
	else
		return TRUE;
}

static gboolean gst_imx_blitter_set_input_frame_internal(GstImxBlitter *blitter, GstBuffer **frame, gboolean cache)
{
	gboolean ret;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxBlitterClass *klass;

	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));
	g_assert(klass->set_input_frame != NULL);

	if (*frame == NULL)
		return klass->set_input_frame(blitter, NULL);

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(*frame);

	if ((phys_mem_meta == NULL) || (phys_mem_meta->phys_addr == 0))
	{
		GstFlowReturn flow_ret;
		GstBuffer *internal_input_frame;

		/* No DMA memory present; the input frame needs to be copied to an internal input frame */

		GST_TRACE_OBJECT(blitter, "input frame does not use DMA memory - copying input frame to internal frame");

		{
			if (blitter->dma_bufferpool == NULL)
			{
				GST_TRACE_OBJECT(blitter, "need to create internal bufferpool");

				/* DMA bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input frame */

				GstCaps *caps = gst_video_info_to_caps(&(blitter->input_video_info));

				blitter->dma_bufferpool = gst_imx_blitter_create_bufferpool(
					blitter,
					caps,
					blitter->input_video_info.size,
					0, 0,
					NULL,
					NULL
				);

				gst_caps_unref(caps);

				if (blitter->dma_bufferpool == NULL)
				{
					GST_ERROR_OBJECT(blitter, "failed to create internal bufferpool");
					return FALSE;
				}
			}

			/* Future versions of this code may propose the internal bufferpool upstream;
			 * hence the is_active check */
			if (!gst_buffer_pool_is_active(blitter->dma_bufferpool))
				gst_buffer_pool_set_active(blitter->dma_bufferpool, TRUE);
		}

		/* Create new internal input frame */
		GST_TRACE_OBJECT(blitter, "acquiring buffer for internal input frame");
		internal_input_frame = NULL;
		flow_ret = gst_buffer_pool_acquire_buffer(blitter->dma_bufferpool, &internal_input_frame, NULL);
		if (flow_ret != GST_FLOW_OK)
		{
			if (internal_input_frame != NULL)
				gst_buffer_unref(internal_input_frame);

			GST_ERROR_OBJECT(blitter, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
			return FALSE;
		}

		/* Copy the input buffer's pixels to the internal input frame */
		{
			GstVideoFrame input_vidframe, internal_input_vidframe;

			gst_video_frame_map(&input_vidframe, &(blitter->input_video_info), *frame, GST_MAP_READ);
			gst_video_frame_map(&internal_input_vidframe, &(blitter->input_video_info), internal_input_frame, GST_MAP_WRITE);

			/* gst_video_frame_copy() makes sure stride and plane offset values from both frames are respected */
			gst_video_frame_copy(&internal_input_vidframe, &input_vidframe);

			/* copy interlace flags */
			GST_BUFFER_FLAGS(internal_input_frame) |= (GST_BUFFER_FLAGS(frame) & (GST_VIDEO_BUFFER_FLAG_INTERLACED | GST_VIDEO_BUFFER_FLAG_TFF | GST_VIDEO_BUFFER_FLAG_RFF | GST_VIDEO_BUFFER_FLAG_ONEFIELD));

			gst_video_frame_unmap(&internal_input_vidframe);
			gst_video_frame_unmap(&input_vidframe);
		}

                /* Replace the frame for future use. This is a trick to effectively implement caching.
		 * In some cases, one frame may be used multiple times, for example if stream A has a
		 * frame rate of 10 fps, stream B 30 fps, and both shall be composed together - the
		 * frames from stream A will be used 3 times each. If these frames are not placed in
		 * DMA memory, they would be copied by the code above ... every time. So, instead,
		 * update the input frame, replacing it with the temporary copy that was created above.
		 * This copy *is* in DMA memory, so if it is used again in a subsequent output frame
		 * by the composer, then the if check above will see that it is DMA memory
		 * (= there will be a physical address), and therefore the frame can be used directly,
		 * without the CPU having to copy its pixels. */
		if (cache)
			gst_buffer_replace(frame, internal_input_frame);
		ret = klass->set_input_frame(blitter, internal_input_frame);
		gst_buffer_unref(internal_input_frame);
	}
	else
	{
		GST_TRACE_OBJECT(blitter, "input frame uses DMA memory - setting it directly as input frame");
		ret = klass->set_input_frame(blitter, *frame);
	}

	return ret;
}

gboolean gst_imx_blitter_set_input_frame_and_cache(GstImxBlitter *blitter, GstBuffer **frame)
{
	return gst_imx_blitter_set_input_frame_internal(blitter, frame, TRUE);
}

gboolean gst_imx_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *frame)
{
	return gst_imx_blitter_set_input_frame_internal(blitter, &frame, FALSE);
}

gboolean gst_imx_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *frame)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	g_assert(klass->set_output_frame != NULL);
	return klass->set_output_frame(blitter, frame);
}


GstBufferPool* gst_imx_blitter_create_bufferpool(GstImxBlitter *blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params)
{
	GstBufferPool *pool;
	GstStructure *config;
	GstImxBlitterClass *klass;

	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	g_assert(klass->get_phys_mem_allocator != NULL);

	pool = gst_imx_phys_mem_buffer_pool_new(FALSE);

	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);

	/* If the allocator value is NULL, get an allocator
	 * it is unref'd by the buffer pool when it is unref'd */
	if (allocator == NULL)
		allocator = klass->get_phys_mem_allocator(blitter);
	if (allocator == NULL)
	{
		GST_ERROR_OBJECT(blitter, "could not create physical memory bufferpool allocator");
		return NULL;
	}

	gst_buffer_pool_config_set_allocator(config, allocator, alloc_params);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	gst_object_unref(allocator);

	return pool;
}


GstAllocator* gst_imx_blitter_get_phys_mem_allocator(GstImxBlitter *blitter)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	g_assert(klass->get_phys_mem_allocator != NULL);
	return klass->get_phys_mem_allocator(blitter);
}


gboolean gst_imx_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	g_assert(klass->fill_region != NULL);
	return klass->fill_region(blitter, region, color);
}


gboolean gst_imx_blitter_blit(GstImxBlitter *blitter, guint8 alpha)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	g_assert(klass->blit != NULL);
	return klass->blit(blitter, alpha);
}


void gst_imx_blitter_flush(GstImxBlitter *blitter)
{
	GstImxBlitterClass *klass;
	g_assert(blitter != NULL);
	klass = GST_IMX_BLITTER_CLASS(G_OBJECT_GET_CLASS(blitter));

	if (klass->flush != NULL)
		klass->flush(blitter);
}
