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

static gboolean gst_imx_base_blitter_do_regions_intersect(GstImxBaseBlitterRegion const *first_region, GstImxBaseBlitterRegion const *second_region);
static void gst_imx_base_blitter_calc_region_intersection(GstImxBaseBlitterRegion const *first_region, GstImxBaseBlitterRegion const *second_region, GstImxBaseBlitterRegion *intersection);
static gboolean gst_imx_base_blitter_is_region_contained(GstImxBaseBlitterRegion const *outer_region, GstImxBaseBlitterRegion const *inner_region);
static void gst_imx_base_blitter_computer_visible_input_region(GstImxBaseBlitter *base_blitter);
static GstImxBaseBlitterRegion const * gst_imx_base_blitter_calc_region_visibility(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *region, GstImxBaseBlitterVisibilityType *visibility_type, GstImxBaseBlitterRegion *sub_out_region);





void gst_imx_base_blitter_class_init(GstImxBaseBlitterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_base_blitter_finalize);

	klass->set_input_video_info   = NULL;
	klass->set_input_frame        = NULL;
	klass->set_output_frame       = NULL;
	klass->set_output_regions     = NULL;
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
	base_blitter->visible_input_region_uptodate = FALSE;

	base_blitter->video_visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;
	base_blitter->output_visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;

	gst_video_info_init(&(base_blitter->input_video_info));

	base_blitter->apply_crop_metadata = GST_IMX_BASE_BLITTER_CROP_DEFAULT;
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
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(input_buffer != NULL);
	g_assert(klass->set_input_frame != NULL);

	/* Clean up any previously used internal input frame
	 * Do this here in case deinterlacing was disabled, so the frame
	 * is cleaned up in either case */
	if (base_blitter->internal_input_frame != NULL)
	{
		gst_buffer_unref(base_blitter->internal_input_frame);
		base_blitter->internal_input_frame = NULL;
	}

	video_meta = gst_buffer_get_video_meta(input_buffer);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(input_buffer);

	{
		GstVideoCropMeta *video_crop_meta;
		guint width = (video_meta != NULL) ? video_meta->width : (guint)(GST_VIDEO_INFO_WIDTH(&(base_blitter->input_video_info)));
		guint height = (video_meta != NULL) ? video_meta->height : (guint)(GST_VIDEO_INFO_HEIGHT(&(base_blitter->input_video_info)));

		if (base_blitter->apply_crop_metadata && ((video_crop_meta = gst_buffer_get_video_crop_meta(input_buffer)) != NULL))
		{
			base_blitter->full_input_region.x1 = video_crop_meta->x;
			base_blitter->full_input_region.y1 = video_crop_meta->y;
			base_blitter->full_input_region.x2 = MIN(video_crop_meta->x + video_crop_meta->width, width);
			base_blitter->full_input_region.y2 = MIN(video_crop_meta->y + video_crop_meta->height, height);
		}
		else
		{
			base_blitter->full_input_region.x1 = 0;
			base_blitter->full_input_region.y1 = 0;
			base_blitter->full_input_region.x2 = width;
			base_blitter->full_input_region.y2 = height;
		}
	}

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
				GST_TRACE_OBJECT(base_blitter, "need to create internal bufferpool");

				/* Internal bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input buffer */

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

		/* Create new temporary internal input frame */
		GST_TRACE_OBJECT(base_blitter, "acquiring buffer for temporary internal input frame");
		flow_ret = gst_buffer_pool_acquire_buffer(base_blitter->internal_bufferpool, &(base_blitter->internal_input_frame), NULL);
		if (flow_ret != GST_FLOW_OK)
		{
			GST_ERROR_OBJECT(base_blitter, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
			return FALSE;
		}

		/* Copy the input buffer's pixels to the temp input frame */
		{
			GstVideoFrame input_frame, temp_input_frame;

			gst_video_frame_map(&input_frame, &(base_blitter->input_video_info), input_buffer, GST_MAP_READ);
			gst_video_frame_map(&temp_input_frame, &(base_blitter->input_video_info), base_blitter->internal_input_frame, GST_MAP_WRITE);

			/* gst_video_frame_copy() makes sure stride and plane offset values from both frames are respected */
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
	GstVideoMeta *video_meta;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));
	video_meta = gst_buffer_get_video_meta(output_buffer);

	g_assert(klass->set_output_frame != NULL);
	g_assert(output_buffer != NULL);
	g_assert(GST_IMX_PHYS_MEM_META_GET(output_buffer) != NULL);
	g_assert(video_meta != NULL);

	base_blitter->output_buffer_region.x1 = 0;
	base_blitter->output_buffer_region.y1 = 0;
	base_blitter->output_buffer_region.x2 = video_meta->width;
	base_blitter->output_buffer_region.y2 = video_meta->height;

	return klass->set_output_frame(base_blitter, output_buffer);
}


gboolean gst_imx_base_blitter_set_output_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	GstImxBaseBlitterClass *klass;
	GstImxBaseBlitterRegion sub_out_region, sub_video_region;
	GstImxBaseBlitterRegion const *orig_video_region;
	GstImxBaseBlitterVisibilityType out_vis_type, video_vis_type;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	base_blitter->visible_input_region_uptodate = FALSE;

	if (klass->set_output_regions == NULL)
	{
		GST_TRACE_OBJECT(base_blitter, "set_output_regions function is NULL -> setting visibility to full");
		base_blitter->video_visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;
		base_blitter->output_visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;
		return TRUE;
	}

	if (output_region == NULL)
		output_region = &(base_blitter->output_buffer_region);

	output_region = gst_imx_base_blitter_calc_region_visibility(base_blitter, output_region, &out_vis_type, &sub_out_region);

	if (video_region == NULL)
		video_region = output_region;

	orig_video_region = video_region;

	switch (out_vis_type)
	{
		case GST_IMX_BASE_BLITTER_VISIBILITY_FULL:
		{
			GST_TRACE_OBJECT(base_blitter, "output region is fully contained in the output buffer region -> video region fully visible");
			video_vis_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;
			break;
		}

		case GST_IMX_BASE_BLITTER_VISIBILITY_NONE:
		{
			GST_TRACE_OBJECT(base_blitter, "output region is fully outside of the output buffer region -> video region not visible");
			video_vis_type = GST_IMX_BASE_BLITTER_VISIBILITY_NONE;
			break;
		}

		case GST_IMX_BASE_BLITTER_VISIBILITY_PARTIAL:
		{
			GST_TRACE_OBJECT(base_blitter, "output region is not fully contained in the output buffer region -> need to check video region visibility");
			orig_video_region = video_region;
			video_region = gst_imx_base_blitter_calc_region_visibility(base_blitter, video_region, &video_vis_type, &sub_video_region);
			break;
		}
	}

	base_blitter->video_visibility_type = video_vis_type;
	base_blitter->output_visibility_type = out_vis_type;

	base_blitter->full_video_region = *orig_video_region;
	base_blitter->visible_video_region = *video_region;

	if ((out_vis_type == GST_IMX_BASE_BLITTER_VISIBILITY_NONE) || (video_vis_type == GST_IMX_BASE_BLITTER_VISIBILITY_NONE))
		return TRUE;
	else
		return klass->set_output_regions(base_blitter, video_region, output_region);
}


void gst_imx_base_blitter_calculate_empty_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion *empty_regions, guint *num_defined_regions, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	guint n;
	gint vleft, vtop, vright, vbottom;
	gint oleft, otop, oright, obottom;

	g_assert(base_blitter != NULL);
	g_assert(empty_regions != NULL);
	g_assert(num_defined_regions != NULL);
	g_assert(output_region != NULL);

	if (video_region == NULL)
	{
		*num_defined_regions = 0;
		GST_DEBUG_OBJECT(base_blitter, "no video region specified, implying output_region == video_region  ->  no empty regions to define");
		return;
	}

	if (GST_IMX_BASE_BLITTER_VIDEO_VISIBILITY_TYPE(base_blitter) == GST_IMX_BASE_BLITTER_VISIBILITY_NONE)
	{
		GST_DEBUG_OBJECT(base_blitter, "video region is not visible -> output region equals the single visible empty region");
		empty_regions[0] = *output_region;
		*num_defined_regions = 1;
		return;
	}

	vleft   = video_region->x1;
	vtop    = video_region->y1;
	vright  = video_region->x2;
	vbottom = video_region->y2;

	oleft   = output_region->x1;
	otop    = output_region->y1;
	oright  = output_region->x2;
	obottom = output_region->y2;

	n = 0;

	GST_DEBUG_OBJECT(base_blitter, "defined video region (%d,%d - %d,%d)", vleft, vtop, vright, vbottom);
	GST_DEBUG_OBJECT(base_blitter, "defined output region (%d,%d - %d,%d)", oleft, otop, oright, obottom);

	if (vleft > oleft)
	{
		GstImxBaseBlitterRegion *empty_region = &(empty_regions[n]);
		empty_region->x1 = oleft;
		empty_region->y1 = otop;
		empty_region->x2 = vleft;
		empty_region->y2 = obottom;
		++n;

		GST_DEBUG_OBJECT(base_blitter, "added left empty region (%d,%d - %d,%d)", empty_region->x1, empty_region->y1, empty_region->x2, empty_region->y2);
	}
	if (vright < oright)
	{
		GstImxBaseBlitterRegion *empty_region = &(empty_regions[n]);
		empty_region->x1 = vright;
		empty_region->y1 = otop;
		empty_region->x2 = oright;
		empty_region->y2 = obottom;
		++n;

		GST_DEBUG_OBJECT(base_blitter, "added right empty region (%d,%d - %d,%d)", empty_region->x1, empty_region->y1, empty_region->x2, empty_region->y2);
	}
	if (vtop > otop)
	{
		GstImxBaseBlitterRegion *empty_region = &(empty_regions[n]);
		empty_region->x1 = vleft;
		empty_region->y1 = otop;
		empty_region->x2 = vright;
		empty_region->y2 = vtop;
		++n;

		GST_DEBUG_OBJECT(base_blitter, "added top empty region (%d,%d - %d,%d)", empty_region->x1, empty_region->y1, empty_region->x2, empty_region->y2);
	}
	if (vbottom < obottom)
	{
		GstImxBaseBlitterRegion *empty_region = &(empty_regions[n]);
		empty_region->x1 = vleft;
		empty_region->y1 = vbottom;
		empty_region->x2 = vright;
		empty_region->y2 = obottom;
		++n;

		GST_DEBUG_OBJECT(base_blitter, "added bottom empty region (%d,%d - %d,%d)", empty_region->x1, empty_region->y1, empty_region->x2, empty_region->y2);
	}

	*num_defined_regions = n;
}


gboolean gst_imx_base_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info)
{
	GstImxBaseBlitterClass *klass;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(input_video_info != NULL);

	if ((klass->set_input_video_info != NULL) && !(klass->set_input_video_info(base_blitter, input_video_info)))
		return FALSE;

	GST_DEBUG_OBJECT(base_blitter, "setting new input video info ; need to clean up old internal input frame & bufferpool");

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
	GstImxBaseBlitterRegion *input_region;

	g_assert(base_blitter != NULL);
	klass = GST_IMX_BASE_BLITTER_CLASS(G_OBJECT_GET_CLASS(base_blitter));

	g_assert(klass->blit_frame != NULL);

	if (base_blitter->output_visibility_type == GST_IMX_BASE_BLITTER_VISIBILITY_NONE)
	{
		GST_TRACE_OBJECT(base_blitter, "output region outside of output buffer bounds -> no need to draw anything");
		return TRUE;
	}

	if (base_blitter->video_visibility_type == GST_IMX_BASE_BLITTER_VISIBILITY_NONE)
	{
		GST_TRACE_OBJECT(base_blitter, "video region outside of output buffer bounds -> no need to draw anything");
		return TRUE;
	}

	if (base_blitter->output_visibility_type == GST_IMX_BASE_BLITTER_VISIBILITY_FULL)
		input_region = &(base_blitter->full_input_region);
	else
	{
		input_region = &(base_blitter->visible_input_region);
		if (!(base_blitter->visible_input_region_uptodate))
			gst_imx_base_blitter_computer_visible_input_region(base_blitter);
	}

	return klass->blit_frame(base_blitter, input_region);
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

	/* If the allocator value is NULL, get an allocator
	 * it is unref'd by the buffer pool when it is unref'd */
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


void gst_imx_base_blitter_enable_crop(GstImxBaseBlitter *base_blitter, gboolean crop)
{
	GST_TRACE_OBJECT(base_blitter, "set crop to %d", crop);
	base_blitter->apply_crop_metadata = crop;
}


gboolean gst_imx_base_blitter_is_crop_enabled(GstImxBaseBlitter *base_blitter)
{
	return base_blitter->apply_crop_metadata;
}


inline static int sgn(int const val)
{
	return (0 < val) - (val < 0);
}


static gboolean gst_imx_base_blitter_do_regions_intersect(GstImxBaseBlitterRegion const *first_region, GstImxBaseBlitterRegion const *second_region)
{
	/* The -1 subtraction is necessary since the (x2,y2)
	 * coordinates are right outside of the region */

	int sx1 = first_region->x1;
	int sx2 = first_region->x2 - 1;
	int sy1 = first_region->y1;
	int sy2 = first_region->y2 - 1;
	int dx1 = second_region->x1;
	int dx2 = second_region->x2 - 1;
	int dy1 = second_region->y1;
	int dy2 = second_region->y2 - 1;

	int xt1 = sgn(dx2 - sx1);
	int xt2 = sgn(dx1 - sx2);
	int yt1 = sgn(dy2 - sy1);
	int yt2 = sgn(dy1 - sy2);

	return (xt1 != xt2) && (yt1 != yt2);
}


static void gst_imx_base_blitter_calc_region_intersection(GstImxBaseBlitterRegion const *first_region, GstImxBaseBlitterRegion const *second_region, GstImxBaseBlitterRegion *intersection)
{
	intersection->x1 = MAX(first_region->x1, second_region->x1);
	intersection->y1 = MAX(first_region->y1, second_region->y1);
	intersection->x2 = MIN(first_region->x2, second_region->x2);
	intersection->y2 = MIN(first_region->y2, second_region->y2);
}


static gboolean gst_imx_base_blitter_is_region_contained(GstImxBaseBlitterRegion const *outer_region, GstImxBaseBlitterRegion const *inner_region)
{
	return
		((inner_region->x1 >= outer_region->x1) && (inner_region->x2 <= outer_region->x2)) &&
		((inner_region->y1 >= outer_region->y1) && (inner_region->y2 <= outer_region->y2));
}


static void gst_imx_base_blitter_computer_visible_input_region(GstImxBaseBlitter *base_blitter)
{
	GstImxBaseBlitterRegion *full_input_region = &(base_blitter->full_input_region);
	GstImxBaseBlitterRegion *vis_input_region = &(base_blitter->visible_input_region);
	GstImxBaseBlitterRegion *full_vid_region = &(base_blitter->full_video_region);
	GstImxBaseBlitterRegion *vis_vid_region = &(base_blitter->visible_video_region);

	if (G_UNLIKELY(base_blitter->video_visibility_type != GST_IMX_BASE_BLITTER_VISIBILITY_PARTIAL))
		return;

	GST_TRACE_OBJECT(base_blitter, "full video region:  (%d, %d) - (%d, %d)", full_vid_region->x1, full_vid_region->y1, full_vid_region->x2, full_vid_region->y2);
	GST_TRACE_OBJECT(base_blitter, "visible video region:   (%d, %d) - (%d, %d)", vis_vid_region->x1, vis_vid_region->y1, vis_vid_region->x2, vis_vid_region->y2);
	GST_TRACE_OBJECT(base_blitter, "full input region: (%d, %d) - (%d, %d)", full_input_region->x1, full_input_region->y1, full_input_region->x2, full_input_region->y2);

	vis_input_region->x1 = full_input_region->x1 + (full_input_region->x2 - full_input_region->x1) * (vis_vid_region->x1 - full_vid_region->x1) / (full_vid_region->x2 - full_vid_region->x1);
	vis_input_region->y1 = full_input_region->y1 + (full_input_region->y2 - full_input_region->y1) * (vis_vid_region->y1 - full_vid_region->y1) / (full_vid_region->y2 - full_vid_region->y1);
	vis_input_region->x2 = full_input_region->x1 + (full_input_region->x2 - full_input_region->x1) * (vis_vid_region->x2 - full_vid_region->x1) / (full_vid_region->x2 - full_vid_region->x1);
	vis_input_region->y2 = full_input_region->y1 + (full_input_region->y2 - full_input_region->y1) * (vis_vid_region->y2 - full_vid_region->y1) / (full_vid_region->y2 - full_vid_region->y1);

	GST_TRACE_OBJECT(base_blitter, "visible input region:   (%d, %d) - (%d, %d)", vis_input_region->x1, vis_input_region->y1, vis_input_region->x2, vis_input_region->y2);

	base_blitter->visible_input_region_uptodate = TRUE;
}


static GstImxBaseBlitterRegion const * gst_imx_base_blitter_calc_region_visibility(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *region, GstImxBaseBlitterVisibilityType *visibility_type, GstImxBaseBlitterRegion *sub_out_region)
{
	if (gst_imx_base_blitter_is_region_contained(&(base_blitter->output_buffer_region), region))
	{
		GST_TRACE_OBJECT(base_blitter, "region is fully contained in the output buffer region");
		*visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_FULL;
	}
	else
	{
		if (gst_imx_base_blitter_do_regions_intersect(&(base_blitter->output_buffer_region), region))
		{
			GST_TRACE_OBJECT(base_blitter, "region is not fully contained in the output buffer region");
			gst_imx_base_blitter_calc_region_intersection(&(base_blitter->output_buffer_region), region, sub_out_region);
			region = sub_out_region;
			GST_TRACE_OBJECT(base_blitter, "clipped region: (%d, %d) - (%d, %d)", sub_out_region->x1, sub_out_region->y1, sub_out_region->x2, sub_out_region->y2);
			*visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_PARTIAL;
		}
		else
		{
			GST_TRACE_OBJECT(base_blitter, "region is fully outside of the output buffer region");
			*visibility_type = GST_IMX_BASE_BLITTER_VISIBILITY_NONE;
		}
	}

	return region;
}
