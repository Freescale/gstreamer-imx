/* G2D-based i.MX blitter class
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


#include <string.h>
#include <g2d.h>
#include "blitter.h"
#include "allocator.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_g2d_blitter_debug);
#define GST_CAT_DEFAULT imx_g2d_blitter_debug


G_DEFINE_TYPE(GstImxG2DBlitter, gst_imx_g2d_blitter, GST_TYPE_IMX_BASE_BLITTER)


typedef struct
{
	enum g2d_format format;
	guint bits_per_pixel;
}
GstImxG2DFormatDetails;


static void gst_imx_g2d_blitter_finalize(GObject *object);

static gboolean gst_imx_g2d_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info);
static gboolean gst_imx_g2d_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame);
static gboolean gst_imx_g2d_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame);
static gboolean gst_imx_g2d_blitter_set_output_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);
static GstAllocator* gst_imx_g2d_blitter_get_phys_mem_allocator(GstImxBaseBlitter *base_blitter);
static gboolean gst_imx_g2d_blitter_blit_frame(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *input_region);

static gboolean gst_imx_g2d_blitter_set_surface_params(GstImxG2DBlitter *g2d_blitter, GstBuffer *input_frame, struct g2d_surface *surface);
static GstImxG2DFormatDetails const * gst_imx_g2d_blitter_get_format_details(GstVideoFormat gst_format);
static void gst_imx_g2d_blitter_region_to_surface(struct g2d_surface *surf, GstImxBaseBlitterRegion const *region);




GType gst_imx_g2d_blitter_rotation_mode_get_type(void)
{
	static GType gst_imx_g2d_blitter_rotation_mode_type = 0;

	if (!gst_imx_g2d_blitter_rotation_mode_type)
	{
		static GEnumValue rotation_mode_values[] =
		{
			{ GST_IMX_G2D_BLITTER_ROTATION_NONE, "No rotation", "none" },
			{ GST_IMX_G2D_BLITTER_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip" },
			{ GST_IMX_G2D_BLITTER_ROTATION_VFLIP, "Flip vertically", "vertical-flip" },
			{ GST_IMX_G2D_BLITTER_ROTATION_90, "Rotate clockwise 90 degrees", "rotate-90" },
			{ GST_IMX_G2D_BLITTER_ROTATION_180, "Rotate 180 degrees", "rotate-180" },
			{ GST_IMX_G2D_BLITTER_ROTATION_270, "Rotate clockwise 270 degrees", "rotate-270" },
			{ 0, NULL, NULL },
		};

		gst_imx_g2d_blitter_rotation_mode_type = g_enum_register_static(
			"ImxG2DBlitterRotationMode",
			rotation_mode_values
		);
	}

	return gst_imx_g2d_blitter_rotation_mode_type;
}




void gst_imx_g2d_blitter_class_init(GstImxG2DBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBaseBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BASE_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_input_video_info);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_output_frame);
	base_class->set_output_regions     = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_set_output_regions);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_get_phys_mem_allocator);
	base_class->blit_frame             = GST_DEBUG_FUNCPTR(gst_imx_g2d_blitter_blit_frame);

	GST_DEBUG_CATEGORY_INIT(imx_g2d_blitter_debug, "imxg2dblitter", 0, "Freescale i.MX G2D blitter class");
}


void gst_imx_g2d_blitter_init(GstImxG2DBlitter *g2d_blitter)
{
	g2d_blitter->handle = NULL;
	memset(&(g2d_blitter->source_surface), 0, sizeof(struct g2d_surface));
	memset(&(g2d_blitter->dest_surface), 0, sizeof(struct g2d_surface));
	g2d_blitter->num_empty_dest_surfaces = 0;
	g2d_blitter->output_region_uptodate = FALSE;

	gst_imx_g2d_blitter_set_output_rotation(g2d_blitter, GST_IMX_G2D_BLITTER_OUTPUT_ROTATION_DEFAULT);
}


GstImxG2DBlitter* gst_imx_g2d_blitter_new(void)
{
	GstImxG2DBlitter* g2d_blitter = (GstImxG2DBlitter *)g_object_new(gst_imx_g2d_blitter_get_type(), NULL);

	return g2d_blitter;
}


GstImxG2DBlitterRotationMode gst_imx_g2d_blitter_get_output_rotation(GstImxG2DBlitter *g2d_blitter)
{
	switch (g2d_blitter->source_surface.rot)
	{
		case G2D_FLIP_H: return GST_IMX_G2D_BLITTER_ROTATION_HFLIP;
		case G2D_FLIP_V: return GST_IMX_G2D_BLITTER_ROTATION_VFLIP;
		default: break;
	}

	switch (g2d_blitter->dest_surface.rot)
	{
		case G2D_ROTATION_0: return GST_IMX_G2D_BLITTER_ROTATION_NONE;
		case G2D_ROTATION_90: return GST_IMX_G2D_BLITTER_ROTATION_90;
		case G2D_ROTATION_180: return GST_IMX_G2D_BLITTER_ROTATION_180;
		case G2D_ROTATION_270: return GST_IMX_G2D_BLITTER_ROTATION_270;
		default: break;
	}

	return GST_IMX_G2D_BLITTER_ROTATION_NONE;
}


void gst_imx_g2d_blitter_set_output_rotation(GstImxG2DBlitter *g2d_blitter, GstImxG2DBlitterRotationMode rotation)
{
	switch (rotation)
	{
		case GST_IMX_G2D_BLITTER_ROTATION_NONE:
			g2d_blitter->source_surface.rot = G2D_ROTATION_0;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_0;
			break;

		case GST_IMX_G2D_BLITTER_ROTATION_90:
			g2d_blitter->source_surface.rot = G2D_ROTATION_0;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_90;
			break;

		case GST_IMX_G2D_BLITTER_ROTATION_180:
			g2d_blitter->source_surface.rot = G2D_ROTATION_0;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_180;
			break;

		case GST_IMX_G2D_BLITTER_ROTATION_270:
			g2d_blitter->source_surface.rot = G2D_ROTATION_0;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_270;
			break;

		case GST_IMX_G2D_BLITTER_ROTATION_HFLIP:
			g2d_blitter->source_surface.rot = G2D_FLIP_H;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_0;
			break;

		case GST_IMX_G2D_BLITTER_ROTATION_VFLIP:
			g2d_blitter->source_surface.rot = G2D_FLIP_V;
			g2d_blitter->dest_surface.rot = G2D_ROTATION_0;
			break;
	}
}


static void gst_imx_g2d_blitter_finalize(GObject *object)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(object);

	g_assert(g2d_blitter != NULL);

	G_OBJECT_CLASS(gst_imx_g2d_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_g2d_blitter_set_input_video_info(G_GNUC_UNUSED GstImxBaseBlitter *base_blitter, G_GNUC_UNUSED GstVideoInfo *input_video_info)
{
	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(base_blitter);

	g_assert(g2d_blitter != NULL);

	gst_imx_g2d_blitter_set_surface_params(g2d_blitter, input_frame, &(g2d_blitter->source_surface));
	g2d_blitter->source_surface.blendfunc = G2D_ONE;
	g2d_blitter->source_surface.global_alpha = 255;
	g2d_blitter->source_surface.clrcolor = 0x00000000;
	/* g2d_blitter->source_surface.rot is set in gst_imx_g2d_blitter_set_output_rotation */

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame)
{
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(base_blitter);

	g_assert(g2d_blitter != NULL);

	gst_imx_g2d_blitter_set_surface_params(g2d_blitter, output_frame, &(g2d_blitter->dest_surface));
	g2d_blitter->dest_surface.blendfunc = G2D_ZERO;
	g2d_blitter->dest_surface.global_alpha = 255;
	g2d_blitter->dest_surface.clrcolor = 0xFF000000;
	/* g2d_blitter->dest_surface.rot is set in gst_imx_g2d_blitter_set_output_rotation */

	g2d_blitter->num_empty_dest_surfaces = 0;
	g2d_blitter->output_region_uptodate = FALSE;

	return TRUE;
}


static gboolean gst_imx_g2d_blitter_set_output_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	guint i;
	GstImxBaseBlitterRegion empty_regions[4];
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(base_blitter);

	g2d_blitter->output_region_uptodate = FALSE;

	gst_imx_g2d_blitter_region_to_surface(&(g2d_blitter->dest_surface), video_region);

	gst_imx_base_blitter_calculate_empty_regions(base_blitter, empty_regions, &(g2d_blitter->num_empty_dest_surfaces), video_region, output_region);

	for (i = 0; i < g2d_blitter->num_empty_dest_surfaces; ++i)
	{
		g2d_blitter->empty_dest_surfaces[i] = g2d_blitter->dest_surface;
		g2d_blitter->empty_dest_surfaces[i].clrcolor = 0xFF000000;
		gst_imx_g2d_blitter_region_to_surface(&(g2d_blitter->empty_dest_surfaces[i]), &(empty_regions[i]));
	}

	return TRUE;
}


static GstAllocator* gst_imx_g2d_blitter_get_phys_mem_allocator(G_GNUC_UNUSED GstImxBaseBlitter *base_blitter)
{
	return gst_imx_g2d_allocator_new();
}


static gboolean gst_imx_g2d_blitter_blit_frame(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *input_region)
{
	guint i;
	gboolean ret;
	GstImxG2DBlitter *g2d_blitter = GST_IMX_G2D_BLITTER(base_blitter);

	g_assert(g2d_blitter != NULL);

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

	ret = TRUE;

	if (!(g2d_blitter->output_region_uptodate))
	{
		g_assert(g2d_blitter->num_empty_dest_surfaces <= GST_IMX_G2D_BLITTER_MAX_NUM_EMPTY_SURFACES);

		GST_LOG_OBJECT(g2d_blitter, "need to clear empty regions");

		for (i = 0; ret && (i < g2d_blitter->num_empty_dest_surfaces); ++i)
		{
			struct g2d_surface *empty_surf = &(g2d_blitter->empty_dest_surfaces[i]);
			if (g2d_clear(g2d_blitter->handle, empty_surf) != 0)
			{
				GST_ERROR_OBJECT(
					g2d_blitter,
					"clearing region (%d,%d - %d,%d) failed",
					empty_surf->left, empty_surf->top, empty_surf->right, empty_surf->bottom
				);
				ret = FALSE;
			}
		}

		g2d_blitter->output_region_uptodate = TRUE;
	}

	if (GST_IMX_BASE_BLITTER_VIDEO_VISIBILITY_TYPE(g2d_blitter) != GST_IMX_BASE_BLITTER_VISIBILITY_NONE)
	{
		gst_imx_g2d_blitter_region_to_surface(&(g2d_blitter->source_surface), input_region);

		if (ret && (g2d_blit(g2d_blitter->handle, &(g2d_blitter->source_surface), &(g2d_blitter->dest_surface)) != 0))
		{
			GST_ERROR_OBJECT(g2d_blitter, "blitting failed");
			ret = FALSE;
		}
	}

	if (g2d_finish(g2d_blitter->handle) != 0)
		GST_ERROR_OBJECT(g2d_blitter, "finishing g2d device operations failed");

	if (g2d_close(g2d_blitter->handle) != 0)
		GST_ERROR_OBJECT(g2d_blitter, "closing g2d device failed");

	return ret;
}


static gboolean gst_imx_g2d_blitter_set_surface_params(GstImxG2DBlitter *g2d_blitter, GstBuffer *video_frame, struct g2d_surface *surface)
{
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxG2DFormatDetails const *fmt_details;

	g_assert(video_frame != NULL);

	video_meta = gst_buffer_get_video_meta(video_frame);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(video_frame);

	g_assert((video_meta != NULL) && (phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0));

	fmt_details = gst_imx_g2d_blitter_get_format_details(video_meta->format);
	if (fmt_details == NULL)
	{
		GST_ERROR_OBJECT(g2d_blitter, "unsupported format %s", gst_video_format_to_string(video_meta->format));
		return FALSE;
	}

	surface->format = fmt_details->format;
	surface->stride = video_meta->stride[0] * 8 / fmt_details->bits_per_pixel;
	surface->width = video_meta->width;
	surface->height = video_meta->height;

	GST_DEBUG_OBJECT(g2d_blitter, "surface stride: %d pixels  width: %d pixels", surface->stride, surface->width);

	{
		guint i;

		if (video_meta->n_planes > 3)
			GST_WARNING_OBJECT(g2d_blitter, "there are %u planes, exceeding the supported number; using the first 3 planes only", video_meta->n_planes);

		for (i = 0; i < MIN(video_meta->n_planes, 3); ++i)
			surface->planes[i] = (int)(phys_mem_meta->phys_addr + video_meta->offset[i]);

		GST_LOG_OBJECT(g2d_blitter, "number of planes: %u", video_meta->n_planes);

		/* XXX: G2D seems to use YV12 with incorrect plane order */
		if (video_meta->format == GST_VIDEO_FORMAT_YV12)
		{
			int paddr = surface->planes[1];
			surface->planes[1] = surface->planes[2];
			surface->planes[2] = paddr;
		}
	}

	return TRUE;
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


static void gst_imx_g2d_blitter_region_to_surface(struct g2d_surface *surf, GstImxBaseBlitterRegion const *region)
{
	surf->left   = region->x1;
	surf->top    = region->y1;
	surf->right  = region->x2;
	surf->bottom = region->y2;
}
