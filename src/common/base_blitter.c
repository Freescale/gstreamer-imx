/* IMX base class for i.MX blitters
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include "base_blitter.h"

#include "../common/phys_mem_meta.h"
#include "../common/phys_mem_buffer_pool.h"



GST_DEBUG_CATEGORY_STATIC(imx_base_blitter_debug);
#define GST_CAT_DEFAULT imx_base_blitter_debug


G_DEFINE_ABSTRACT_TYPE(GstImxBaseBlitter, gst_imx_base_blitter, GST_TYPE_OBJECT)


static void gst_imx_base_blitter_finalize(GObject *object);





void gst_imx_base_blitter_class_init(GstImxBaseBlitterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_base_blitter_finalize);

	klass->set_input_video_info   = NULL;
	klass->set_input_frame        = NULL;
	klass->set_output_frame       = NULL;
	klass->set_regions            = NULL;
	klass->get_phys_mem_allocator = NULL;
	klass->blit_frame             = NULL;
	klass->flush                  = NULL;

	GST_DEBUG_CATEGORY_INIT(imx_base_blitter_debug, "imxbaseblitter", 0, "Freescale i.MX base blitter class");
}


void gst_imx_base_blitter_init(GstImxBaseBlitter *base_blitter)
{
	GST_TRACE_OBJECT(base_blitter, "initializing base blitter");

	base_blitter->internal_bufferpool = NULL;
	base_blitter->internal_input_frame = NULL;

	gst_video_info_init(&(base_blitter->input_video_info));
}




static void gst_imx_base_blitter_finalize(GObject *object)
{
	GstImxBaseBlitter *base_blitter = GST_IMX_BASE_BLITTER(object);

	g_assert(base_blitter != NULL);

	GST_TRACE_OBJECT(base_blitter, "finalizing base blitter");

	if (base_blitter->internal_input_frame != NULL)
		gst_buffer_unref(base_blitter->internal_input_frame);
	if (base_blitter->internal_bufferpool != NULL)
		gst_object_unref(base_blitter->internal_bufferpool);

	G_OBJECT_CLASS(gst_imx_base_blitter_parent_class)->finalize(object);
}




gboolean gst_imx_base_blitter_set_input_buffer(GstImxBaseBlitter *base_blitter, GstBuffer *input_buffer)
{
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(input_buffer != NULL);
	g_assert(klass->set_input_frame != NULL);

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(input_buffer);

	/* Test if the input buffer uses DMA memory */
	if ((phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0))
	{
		/* DMA memory present - the input buffer can be used as an actual input buffer */
		klass->set_input_frame(base_blitter, input_buffer);

		GST_TRACE_OBJECT(base_blitter, "input buffer uses DMA memory - setting it as actual input buffer");
	}
	else
	{
		/* No DMA memory present; the input buffer needs to be copied to an internal
		 * temporary input buffer */

		GstFlowReturn flow_ret;

		GST_TRACE_OBJECT(base_blitter, "input buffer does not use DMA memory - need to copy it to an internal input DMA buffer");

		{
			/* The internal input buffer is the temp input frame's DMA memory.
			 * If it does not exist yet, it needs to be created here. The temp input
			 * frame is then mapped. */

			if (base_blitter->internal_bufferpool == NULL)
			{
				/* Internal bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input buffer */

				/* But first, clean up any existing input frame from a
				 * previous pool */
				if (base_blitter->internal_input_frame != NULL)
				{
					gst_buffer_unref(base_blitter->internal_input_frame);
					base_blitter->internal_input_frame = NULL;
				}

				/* Now create new pool */

				GstCaps *caps = gst_video_info_to_caps(&(base_blitter->input_video_info));

				base_blitter->internal_bufferpool = gst_imx_base_blitter_create_bufferpool(
					base_blitter,
					caps,
					base_blitter->input_video_info.size,
					0, 0,
					NULL,
					NULL
				);

				gst_caps_unref(caps);

				if (base_blitter->internal_bufferpool == NULL)
				{
					GST_ERROR_OBJECT(base_blitter, "failed to create internal bufferpool");
					return FALSE;
				}
			}

			/* Future versions of this code may propose the internal bufferpool upstream;
			 * hence the is_active check */
			if (!gst_buffer_pool_is_active(base_blitter->internal_bufferpool))
				gst_buffer_pool_set_active(base_blitter->internal_bufferpool, TRUE);
		}

		if (base_blitter->internal_input_frame == NULL)
		{
			flow_ret = gst_buffer_pool_acquire_buffer(base_blitter->internal_bufferpool, &(base_blitter->internal_input_frame), NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(base_blitter, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
				return FALSE;
			}
		}

		{
			GstVideoFrame input_frame, temp_input_frame;

			gst_video_frame_map(&input_frame, &(base_blitter->input_video_info), input_buffer, GST_MAP_READ);
			gst_video_frame_map(&temp_input_frame, &(base_blitter->input_video_info), base_blitter->internal_input_frame, GST_MAP_WRITE);

			/* Copy the input buffer's pixels to the temp input frame
			 * The gst_video_frame_copy() makes sure stride and plane offset values from both
			 * frames are respected */
			gst_video_frame_copy(&temp_input_frame, &input_frame);

			GST_BUFFER_FLAGS(base_blitter->internal_input_frame) |= (GST_BUFFER_FLAGS(input_buffer) & (GST_VIDEO_BUFFER_FLAG_INTERLACED | GST_VIDEO_BUFFER_FLAG_TFF | GST_VIDEO_BUFFER_FLAG_RFF | GST_VIDEO_BUFFER_FLAG_ONEFIELD));

			gst_video_frame_unmap(&temp_input_frame);
			gst_video_frame_unmap(&input_frame);
		}

		klass->set_input_frame(base_blitter, base_blitter->internal_input_frame);
	}

	return TRUE;
}


gboolean gst_imx_base_blitter_set_output_buffer(GstImxBaseBlitter *base_blitter, GstBuffer *output_buffer)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(klass->set_output_frame != NULL);
	g_assert(output_buffer != NULL);
	g_assert(GST_IMX_PHYS_MEM_META_GET(output_buffer) != NULL);

	return klass->set_output_frame(base_blitter, output_buffer);
}


gboolean gst_imx_base_blitter_set_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	if (klass->set_regions != NULL)
		return klass->set_regions(base_blitter, video_region, output_region);
	else
		return TRUE;
}


gboolean gst_imx_base_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(input_video_info != NULL);

	if ((klass->set_input_video_info != NULL) && !(klass->set_input_video_info(base_blitter, input_video_info)))
		return FALSE;

	/* Unref the internal input frame, since the input video info
	 * changed, and the frame therefore no longer fits */
	if (base_blitter->internal_input_frame != NULL)
	{
		gst_buffer_unref(base_blitter->internal_input_frame);
		base_blitter->internal_input_frame = NULL;
	}

	/* New videoinfo means new frame sizes, new strides etc.
	 * making the existing internal bufferpool unusable
	 * -> shut it down; it will be recreated on-demand in the
	 * gst_imx_base_blitter_set_input_buffer() call
	 * (if there is any GstBuffer in the pipeline from this
	 * pool, it will keep the pool alive until it is unref'd) */
	if (base_blitter->internal_bufferpool != NULL)
	{
		gst_object_unref(base_blitter->internal_bufferpool);
		base_blitter->internal_bufferpool = NULL;
	}

	base_blitter->input_video_info = *input_video_info;

	return TRUE;
}


gboolean gst_imx_base_blitter_blit(GstImxBaseBlitter *base_blitter)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(klass->blit_frame != NULL);

	return klass->blit_frame(base_blitter);
}


gboolean gst_imx_base_blitter_flush(GstImxBaseBlitter *base_blitter)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	return (klass->flush != NULL) ? klass->flush(base_blitter) : TRUE;
}


GstBufferPool* gst_imx_base_blitter_create_bufferpool(GstImxBaseBlitter *base_blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params)
{
	GstBufferPool *pool;
	GstStructure *config;
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(klass->get_phys_mem_allocator != NULL);

	pool = gst_imx_phys_mem_buffer_pool_new(FALSE);

	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);

	/* If the allocator value is NULL, create an allocator */
	if (allocator == NULL)
		allocator = klass->get_phys_mem_allocator(base_blitter);
	if (allocator == NULL)
	{
		GST_ERROR_OBJECT(base_blitter, "could not create physical memory bufferpool allocator");
		return NULL;
	}

	gst_buffer_pool_config_set_allocator(config, allocator, alloc_params);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	return pool;
}


GstAllocator* gst_imx_base_blitter_get_phys_mem_allocator(GstImxBaseBlitter *base_blitter)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(klass->get_phys_mem_allocator != NULL);

	return klass->get_phys_mem_allocator(base_blitter);
}
