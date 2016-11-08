/* G2D-based i.MX blitter class
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


#include <string.h>
#include <g2d.h>
#include "blitter.h"
#include "allocator.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_g2d_blitter_debug);
#define GST_CAT_DEFAULT imx_g2d_blitter_debug


G_DEFINE_TYPE(GstImxG2DBlitter, gst_imx_g2d_blitter, GST_TYPE_IMX_BLITTER)


typedef struct
{
	enum g2d_format format;
	guint bits_per_pixel;
}
GstImxG2DFormatDetails;


static void gst_imx_g2d_blitter_finalize(GObject *object);

static gboolean gst_imx_g2d_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info);
static gboolean gst_imx_g2d_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info);

static gboolean gst_imx_g2d_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region);
static gboolean gst_imx_g2d_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas);

static gboolean gst_imx_g2d_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame);
static gboolean gst_imx_g2d_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame);

static GstAllocator* gst_imx_g2d_blitter_get_phys_mem_allocator(GstImxBlitter *blitter);

static gboolean gst_imx_g2d_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color);
static gboolean gst_imx_g2d_blitter_blit(GstImxBlitter *blitter, guint8 alpha);

static gboolean gst_imx_g2d_blitter_allocate_internal_fill_frame(GstImxG2DBlitter *g2d_blitter);
static gboolean gst_imx_g2d_blitter_set_surface_params(GstImxG2DBlitter *g2d_blitter, GstBuffer *video_frame, struct g2d_surface *surface, GstVideoInfo const *info);
static void gst_imx_g2d_blitter_set_output_rotation(GstImxG2DBlitter *g2d_blitter, GstImxCanvasInnerRotation rotation);
static GstImxG2DFormatDetails const * gst_imx_g2d_blitter_get_format_details(GstVideoFormat gst_format);
static gboolean gst_imx_g2d_blitter_g2d_format_has_alpha_channel(enum g2d_format format);




static void gst_imx_g2d_blitter_class_init(GstImxG2DBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_input_video_info);
	base_class->set_output_video_info  = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_output_video_info);
	base_class->set_input_region       = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_input_region);
	base_class->set_output_canvas      = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_output_canvas);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_output_frame);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_get_phys_mem_allocator);
	base_class->fill_region            = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_fill_region);
	base_class->blit                   = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_blit);

	GST_DEBUG_CATEGORY_INIT(imx_g2d_blitter_debug, "imxg2dblitter", 0, "Freescale i.MX G2D blitter class");
}


static void gst_imx_g2d_blitter_init(GstImxG2DBlitter *g2d_blitter)
{
	gst_video_info_init(&(g2d_blitter->input_video_info));
	gst_video_info_init(&(g2d_blitter->output_video_info));
	g2d_blitter->allocator = NULL;
	g2d_blitter->input_frame = NULL;
	g2d_blitter->output_frame = NULL;
	g2d_blitter->use_entire_input_frame = TRUE;

	g2d_blitter->handle = NULL;
	memset(&(g2d_blitter->input_surface), 0, sizeof(struct g2d_surface));
	memset(&(g2d_blitter->output_surface), 0, sizeof(struct g2d_surface));
	g2d_blitter->visibility_mask = 0;
	g2d_blitter->fill_color = 0xFF000000;
	g2d_blitter->num_empty_regions = 0;
}


GstImxG2DBlitter* gst_imx_g2d_blitter_new(void)
{
	GstAllocator *allocator;
	GstImxG2DBlitter *g2d_blitter;

	allocator = gst_imx_g2d_allocator_new();
	if (allocator == NULL)
		return NULL;

	g2d_blitter = (GstImxG2DBlitter *)g_object_new(gst_imx_g2d_blitter_get_type(), NULL);
	g2d_blitter->allocator = gst_object_ref_sink(allocator);

	if (!gst_imx_g2d_blitter_allocate_internal_fill_frame(g2d_blitter))
	{
		gst_object_unref(GST_OBJECT(g2d_blitter));
		return NULL;
	}

	return g2d_blitter;
}


static void gst_imx_g2d_blitter_finalize(GObject *object)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(object);

	if (g2d_blitter->input_frame != NULL)
		gst_buffer_unref(g2d_blitter->input_frame);
	if (g2d_blitter->output_frame != NULL)
		gst_buffer_unref(g2d_blitter->output_frame);
	if (g2d_blitter->fill_frame != NULL)
		gst_buffer_unref(g2d_blitter->fill_frame);

	if (g2d_blitter->allocator != NULL)
		gst_object_unref(g2d_blitter->allocator);

	G_OBJECT_CLASS(gst_imx_g2d_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_g2d_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);
	g2d_blitter->input_video_info = *input_video_info;
	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);
	g2d_blitter->output_video_info = *output_video_info;
	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);

	if (input_region != NULL)
	{
		g2d_blitter->input_surface.left   = input_region->x1;
		g2d_blitter->input_surface.top    = input_region->y1;
		g2d_blitter->input_surface.right  = input_region->x2;
		g2d_blitter->input_surface.bottom = input_region->y2;

		g2d_blitter->use_entire_input_frame = FALSE;
	}
	else
		g2d_blitter->use_entire_input_frame = TRUE;

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);
	guint i;

	g2d_blitter->output_surface.left   = output_canvas->clipped_inner_region.x1;
	g2d_blitter->output_surface.top    = output_canvas->clipped_inner_region.y1;
	g2d_blitter->output_surface.right  = output_canvas->clipped_inner_region.x2;
	g2d_blitter->output_surface.bottom = output_canvas->clipped_inner_region.y2;

	g2d_blitter->visibility_mask = output_canvas->visibility_mask;
	g2d_blitter->fill_color = output_canvas->fill_color;

	g2d_blitter->num_empty_regions = 0;
	for (i = 0; i < 4; ++i)
	{
		if ((g2d_blitter->visibility_mask & (1 << i)) == 0)
			continue;

		g2d_blitter->empty_regions[g2d_blitter->num_empty_regions] = output_canvas->empty_regions[i];
		g2d_blitter->num_empty_regions++;
	}

	gst_imx_g2d_blitter_set_output_rotation(g2d_blitter, output_canvas->inner_rotation);

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);
	gst_buffer_replace(&(g2d_blitter->input_frame), input_frame);

	if (g2d_blitter->input_frame != NULL)
	{
		if (!gst_imx_g2d_blitter_set_surface_params(g2d_blitter, input_frame, &(g2d_blitter->input_surface), &(g2d_blitter->input_video_info)))
			return FALSE;

		if (g2d_blitter->use_entire_input_frame)
		{
			g2d_blitter->input_surface.left   = 0;
			g2d_blitter->input_surface.top    = 0;
			g2d_blitter->input_surface.right  = g2d_blitter->input_surface.width;
			g2d_blitter->input_surface.bottom = g2d_blitter->input_surface.height;
		}
	}

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);
	gst_buffer_replace(&(g2d_blitter->output_frame), output_frame);

	if (g2d_blitter->output_frame != NULL)
	{
		if (!gst_imx_g2d_blitter_set_surface_params(g2d_blitter, output_frame, &(g2d_blitter->output_surface), &(g2d_blitter->output_video_info)))
			return FALSE;

		g2d_blitter->empty_surface = g2d_blitter->output_surface;
		g2d_blitter->background_surface = g2d_blitter->output_surface;
	}

	return TRUE;
}


static GstAllocator* gst_imx_g2d_blitter_get_phys_mem_allocator(GstImxBlitter *blitter)
{
	return gst_object_ref(GST_IMX_G2D_BLITTER_CAST(blitter)->allocator);
}


static gboolean gst_imx_g2d_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color)
{
	gboolean ret = TRUE;
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);

	if (g2d_open(&(g2d_blitter->handle)) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "opening g2d device failed");
		return FALSE;
	}

	if (g2d_make_current(g2d_blitter->handle, G2D_HARDWARE_2D) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "g2d_make_current() failed");
		if (g2d_close(g2d_blitter->handle) != 0)
			GST_ERROR_OBJECT(g2d_blitter, "closing g2d device failed");
		return FALSE;
	}

	g2d_blitter->background_surface.clrcolor = color | 0xFF000000;
	g2d_blitter->background_surface.left   = region->x1;
	g2d_blitter->background_surface.top    = region->y1;
	g2d_blitter->background_surface.right  = region->x2;
	g2d_blitter->background_surface.bottom = region->y2;

	if (g2d_clear(g2d_blitter->handle, &(g2d_blitter->background_surface)) != 0)
	{
		GST_ERROR_OBJECT(
			g2d_blitter,
			"clearing background failed"
		);
		ret = FALSE;
	}

	if (g2d_finish(g2d_blitter->handle) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "finishing g2d device operations failed");
		ret = FALSE;
	}

	if (g2d_close(g2d_blitter->handle) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "closing g2d device failed");
		ret = FALSE;
	}

	return ret;
}


static gboolean gst_imx_g2d_blitter_blit(GstImxBlitter *blitter, guint8 alpha)
{
	guint i;
	gboolean ret = TRUE;
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(blitter);

	/* Don't do anything if the canvas is not visible or
	 * if alpha is 0 (which means 100% transparent) */
	if ((g2d_blitter->visibility_mask == 0) || (alpha == 0))
		return TRUE;

	if (g2d_open(&(g2d_blitter->handle)) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "opening g2d device failed");
		return FALSE;
	}

	if (g2d_make_current(g2d_blitter->handle, G2D_HARDWARE_2D) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "g2d_make_current() failed");
		if (g2d_close(g2d_blitter->handle) != 0)
			GST_ERROR_OBJECT(g2d_blitter, "closing g2d device failed");
		return FALSE;
	}

	/* Enable blending if either the global alpha is <255 or if the
	 * input frames have an alpha channel, since G2D can also perform
	 * per-pixel blending (the total alpha is the multiplicative combination
	 * of both global and per-pixel alpha).
	 */
	if ((alpha != 255) || gst_imx_g2d_blitter_g2d_format_has_alpha_channel(g2d_blitter->input_surface.format))
	{
		g2d_enable(g2d_blitter->handle, G2D_BLEND);
		/* If blending shall be used because the input has an alpha
		 * channel & the global alpah is 255, then it is unnecessary
		 * to enable global alpha */
		if (alpha == 255)
			g2d_disable(g2d_blitter->handle, G2D_GLOBAL_ALPHA);
		else
			g2d_enable(g2d_blitter->handle, G2D_GLOBAL_ALPHA);
	}
	else
	{
		g2d_disable(g2d_blitter->handle, G2D_BLEND);
		g2d_disable(g2d_blitter->handle, G2D_GLOBAL_ALPHA);
	}

	for (i = 0; i < g2d_blitter->num_empty_regions; ++i)
	{
		guint8 empty_alpha;
		struct g2d_surface *empty_surf = &(g2d_blitter->empty_surface);

		empty_surf->left   = g2d_blitter->empty_regions[i].x1;
		empty_surf->top    = g2d_blitter->empty_regions[i].y1;
		empty_surf->right  = g2d_blitter->empty_regions[i].x2;
		empty_surf->bottom = g2d_blitter->empty_regions[i].y2;

		empty_alpha = ((g2d_blitter->fill_color >> 24) * (guint)alpha) / 255;

		/* g2d_clear() ignores alpha blending, so if empty_alpha is not 255,
		 * use a trick. Take the fill_surface, which is a very small surface,
		 * fill it with the fill color, and blit it with blending. */
		if (empty_alpha == 255)
		{
			g2d_blitter->empty_surface.clrcolor = (g2d_blitter->fill_color & 0x00FFFFFF) | 0xFF000000;

			if (g2d_clear(g2d_blitter->handle, empty_surf) != 0)
			{
				GST_ERROR_OBJECT(
					g2d_blitter,
					"clearing region (%d,%d - %d,%d) failed",
					empty_surf->left, empty_surf->top, empty_surf->right, empty_surf->bottom
				);
				ret = FALSE;
				break;
			}
		}
		else
		{
			g2d_blitter->fill_surface.blendfunc = G2D_SRC_ALPHA;
			g2d_blitter->fill_surface.global_alpha = empty_alpha;
			g2d_blitter->empty_surface.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
			g2d_blitter->empty_surface.global_alpha = empty_alpha;

			g2d_blitter->fill_surface.clrcolor = (g2d_blitter->fill_color & 0x00FFFFFF) | 0xFF000000;
			if (g2d_clear(g2d_blitter->handle, &(g2d_blitter->fill_surface)) != 0)
			{
				GST_ERROR_OBJECT(g2d_blitter, "clearing fill surface failed");
				ret = FALSE;
				break;
			}

			if (g2d_blit(g2d_blitter->handle, &(g2d_blitter->fill_surface), &(g2d_blitter->empty_surface)) != 0)
			{
				GST_ERROR_OBJECT(g2d_blitter, "blitting failed");
				ret = FALSE;
				break;
			}
		}
	}

	if (ret && (g2d_blitter->visibility_mask & GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER))
	{
		g2d_blitter->input_surface.blendfunc = G2D_SRC_ALPHA;
		g2d_blitter->input_surface.global_alpha = alpha;
		g2d_blitter->output_surface.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
		g2d_blitter->output_surface.global_alpha = alpha;

		GST_DEBUG_OBJECT(g2d_blitter, "input_surface: %d %d %d %d", g2d_blitter->input_surface.left, g2d_blitter->input_surface.top, g2d_blitter->input_surface.right, g2d_blitter->input_surface.bottom);
		GST_DEBUG_OBJECT(g2d_blitter, "output_surface: %d %d %d %d", g2d_blitter->output_surface.left, g2d_blitter->output_surface.top, g2d_blitter->output_surface.right, g2d_blitter->output_surface.bottom);

		if (g2d_blit(g2d_blitter->handle, &(g2d_blitter->input_surface), &(g2d_blitter->output_surface)) != 0)
		{
			GST_ERROR_OBJECT(g2d_blitter, "blitting failed");
			ret = FALSE;
		}
	}

	if (g2d_finish(g2d_blitter->handle) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "finishing g2d device operations failed");
		ret = FALSE;
	}

	if (g2d_close(g2d_blitter->handle) != 0)
	{
		GST_ERROR_OBJECT(g2d_blitter, "closing g2d device failed");
		ret = FALSE;
	}

	return ret;
}


static gboolean gst_imx_g2d_blitter_allocate_internal_fill_frame(GstImxG2DBlitter *g2d_blitter)
{
	/* The internal fill frame does not have to be large. In fact, it is desirable
	 * to make it as small as possible to ensure the g2d_clear() calls in the
	 * blit() function uses as little bandwidth as possible. For this reason, the
	 * fill frame is allocated to use a size of 4x1 pixels, the smallest one
	 * allowed by the G2D API. */

	GstImxPhysMemory *phys_mem;
	guint const fill_frame_width = 4;
	guint const fill_frame_height = 1;
	GstImxG2DFormatDetails const *fmt_details = gst_imx_g2d_blitter_get_format_details(GST_VIDEO_FORMAT_RGBx);

	/* Not using the dma bufferpool for this, since that bufferpool will
	 * be configured to input frame sizes, not 4x1 pixels. Plus, the pool
	 * wouldn't yield any benefits here. */
	g2d_blitter->fill_frame = gst_buffer_new_allocate(
		g2d_blitter->allocator,
		fill_frame_width * fill_frame_height * fmt_details->bits_per_pixel / 8,
		NULL
	);

	if (g2d_blitter->fill_frame == NULL)
	{
		GST_ERROR_OBJECT(g2d_blitter, "could not allocate internal fill frame");
		return FALSE;
	}

	phys_mem = (GstImxPhysMemory *)gst_buffer_peek_memory(g2d_blitter->fill_frame, 0);

	memset(&(g2d_blitter->fill_surface), 0, sizeof(struct g2d_surface));
	g2d_blitter->fill_surface.format = fmt_details->format;
	g2d_blitter->fill_surface.planes[0] = phys_mem->phys_addr;
	g2d_blitter->fill_surface.width = g2d_blitter->fill_surface.right = fill_frame_width;
	g2d_blitter->fill_surface.height = g2d_blitter->fill_surface.bottom = fill_frame_height;
	g2d_blitter->fill_surface.stride = fill_frame_width;

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_surface_params(GstImxG2DBlitter *g2d_blitter, GstBuffer *video_frame, struct g2d_surface *surface, GstVideoInfo const *info)
{
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxG2DFormatDetails const *fmt_details;
	GstVideoFormat format;
	guint width, height, stride, n_planes;
	guint i;

	g_assert(video_frame != NULL);

	video_meta = gst_buffer_get_video_meta(video_frame);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(video_frame);

	g_assert((phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0));

	format = (video_meta != NULL) ? video_meta->format : GST_VIDEO_INFO_FORMAT(info);
	width = (video_meta != NULL) ? video_meta->width : (guint)(GST_VIDEO_INFO_WIDTH(info));
	height = (video_meta != NULL) ? video_meta->height : (guint)(GST_VIDEO_INFO_HEIGHT(info));
	stride = (video_meta != NULL) ? video_meta->stride[0] : GST_VIDEO_INFO_PLANE_STRIDE(info, 0);
	n_planes = (video_meta != NULL) ? video_meta->n_planes : GST_VIDEO_INFO_N_PLANES(info);

	if (n_planes > 3)
	{
		GST_WARNING_OBJECT(g2d_blitter, "there are %u planes, exceeding the supported number; using the first 3 planes only", n_planes);
		n_planes = 3;
	}

	GST_LOG_OBJECT(g2d_blitter, "number of planes: %u", video_meta->n_planes);

	if (video_meta != NULL)
	{
		for (i = 0; i < n_planes; ++i)
			surface->planes[i] = (int)(phys_mem_meta->phys_addr + video_meta->offset[i]);
	}
	else
	{
		for (i = 0; i < n_planes; ++i)
			surface->planes[i] = (int)(phys_mem_meta->phys_addr + GST_VIDEO_INFO_PLANE_OFFSET(info, i));
	}

	fmt_details = gst_imx_g2d_blitter_get_format_details(format);
	if (fmt_details == NULL)
	{
		GST_ERROR_OBJECT(g2d_blitter, "unsupported format %s", gst_video_format_to_string(format));
		return FALSE;
	}

	/* XXX: G2D seems to use YV12 with incorrect plane order */
	if (format == GST_VIDEO_FORMAT_YV12)
	{
		int paddr = surface->planes[1];
		surface->planes[1] = surface->planes[2];
		surface->planes[2] = paddr;
	}

	surface->format = fmt_details->format;
	surface->width = width + phys_mem_meta->x_padding;
	surface->height = height + phys_mem_meta->y_padding;
	surface->stride = stride * 8 / fmt_details->bits_per_pixel;

	GST_DEBUG_OBJECT(g2d_blitter, "surface stride: %d pixels  width: %d pixels height: %d pixels", surface->stride, surface->width, surface->height);

	return TRUE;
}


static void gst_imx_g2d_blitter_set_output_rotation(GstImxG2DBlitter *g2d_blitter, GstImxCanvasInnerRotation rotation)
{
	switch (rotation)
	{
		case GST_IMX_CANVAS_INNER_ROTATION_NONE:
			g2d_blitter->input_surface.rot = G2D_ROTATION_0;
			g2d_blitter->output_surface.rot = G2D_ROTATION_0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES:
			g2d_blitter->input_surface.rot = G2D_ROTATION_0;
			g2d_blitter->output_surface.rot = G2D_ROTATION_90;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_180_DEGREES:
			g2d_blitter->input_surface.rot = G2D_ROTATION_0;
			g2d_blitter->output_surface.rot = G2D_ROTATION_180;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES:
			g2d_blitter->input_surface.rot = G2D_ROTATION_0;
			g2d_blitter->output_surface.rot = G2D_ROTATION_270;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_HFLIP:
			g2d_blitter->input_surface.rot = G2D_FLIP_H;
			g2d_blitter->output_surface.rot = G2D_ROTATION_0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_VFLIP:
			g2d_blitter->input_surface.rot = G2D_FLIP_V;
			g2d_blitter->output_surface.rot = G2D_ROTATION_0;
			break;
	}
}


static GstImxG2DFormatDetails const * gst_imx_g2d_blitter_get_format_details(GstVideoFormat gst_format)
{
#define FORMAT_DETAILS(G2DFMT, BPP) \
		do { static GstImxG2DFormatDetails const d = { G2DFMT, BPP }; return &d; } while (0)

	/* Disabled YVYU, since there is a bug in G2D - G2D_YUYV and G2D_YVYU
	 * actually refer to the same pixel format (G2D_YUYV)
	 *
	 * Disabled NV16, since the formats are corrupted, and it appears
	 * to be a problem with G2D itself
	 *
	 * G2D_VYUY and G2D_NV61 do not have an equivalent in GStreamer
	 */

	switch (gst_format)
	{
		case GST_VIDEO_FORMAT_RGB16: FORMAT_DETAILS(G2D_RGB565, 16);
		case GST_VIDEO_FORMAT_RGBA: FORMAT_DETAILS(G2D_RGBA8888, 32);
		case GST_VIDEO_FORMAT_RGBx: FORMAT_DETAILS(G2D_RGBX8888, 32);
		case GST_VIDEO_FORMAT_BGRA: FORMAT_DETAILS(G2D_BGRA8888, 32);
		case GST_VIDEO_FORMAT_BGRx: FORMAT_DETAILS(G2D_BGRX8888, 32);
		case GST_VIDEO_FORMAT_NV12: FORMAT_DETAILS(G2D_NV12, 8);
		case GST_VIDEO_FORMAT_I420: FORMAT_DETAILS(G2D_I420, 8);
		case GST_VIDEO_FORMAT_YV12: FORMAT_DETAILS(G2D_YV12, 8);
		case GST_VIDEO_FORMAT_NV21: FORMAT_DETAILS(G2D_NV21, 8);
		case GST_VIDEO_FORMAT_YUY2: FORMAT_DETAILS(G2D_YUYV, 16);
		//case GST_VIDEO_FORMAT_YVYU: FORMAT_DETAILS(G2D_YVYU, 16);
		case GST_VIDEO_FORMAT_UYVY: FORMAT_DETAILS(G2D_UYVY, 16);
		//case GST_VIDEO_FORMAT_NV16: FORMAT_DETAILS(G2D_NV16, 16);

		default: return NULL;
	}

#undef FORMAT_DETAILS
}


static gboolean gst_imx_g2d_blitter_g2d_format_has_alpha_channel(enum g2d_format format)
{
	switch (format)
	{
		case G2D_RGBA8888:
		case G2D_BGRA8888:
			return TRUE;
		default:
			return FALSE;
	}
}
