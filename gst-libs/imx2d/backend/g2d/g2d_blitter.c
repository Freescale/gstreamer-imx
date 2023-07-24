#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <g2d.h>
#include <g2dExt.h>

#include <config.h>

#include "imx2d/imx2d_priv.h"
#include "g2d_blitter.h"


/* Disabled YVYU in G2D 1.x, since there is a bug in those older
 * versions - G2D_YUYV and G2D_YVYU actually refer to the same
 * pixel format (G2D_YUYV).
 *
 * Disabled NV16 in G2D 1.x as well, since this format
 * is broken in those older versions.
 */
static Imx2dPixelFormat const supported_source_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_BGR565,
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_XRGB8888,
	IMX_2D_PIXEL_FORMAT_ARGB8888,
	IMX_2D_PIXEL_FORMAT_XBGR8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,

	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
#if G2D_VERSION_MAJOR < 1
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU,
#endif
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY,

	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21,
#if G2D_VERSION_MAJOR < 1
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16,
#endif
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61,

	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420,

#ifdef IMX2D_G2D_IMPLEMENTATION_BASED_ON_DPU
#ifdef IMX2D_G2D_AMPHION_TILE_LAYOUT_SUPPORTED
	IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128,
	IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128,
#endif
#ifdef IMX2D_G2D_AMPHION_10BIT_TILE_LAYOUT_SUPPORTED
	IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128_10BIT,
	IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128_10BIT,
#endif
#endif
};


/* G2D only supports RGB formats as destination.
 * Exception: When G2D is actually emulated on top of the DPU,
 * some YUV formats are also usable as destination formats.
 * The DPU only exists in some i.MX8 variants, however. */
static Imx2dPixelFormat const supported_dest_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_XRGB8888,
	IMX_2D_PIXEL_FORMAT_ARGB8888,
	IMX_2D_PIXEL_FORMAT_XBGR8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_BGR565,

#ifdef IMX2D_G2D_IMPLEMENTATION_BASED_ON_DPU
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
#endif
};


static BOOL g2d_format_has_alpha(enum g2d_format format)
{
	switch (format)
	{
		case G2D_RGBA8888:
		case G2D_BGRA8888:
		case G2D_ARGB8888:
		case G2D_ABGR8888:
			return TRUE;
		default:
			return FALSE;
	}
}


static BOOL get_g2d_format(Imx2dPixelFormat imx_2d_format, enum g2d_format *fmt)
{
	BOOL ret = TRUE;

	assert(fmt != NULL);

	switch (imx_2d_format)
	{
		case IMX_2D_PIXEL_FORMAT_RGB565: *fmt = G2D_RGB565; break;
		case IMX_2D_PIXEL_FORMAT_BGR565: *fmt = G2D_BGR565; break;
		case IMX_2D_PIXEL_FORMAT_RGBX8888: *fmt = G2D_RGBX8888; break;
		case IMX_2D_PIXEL_FORMAT_RGBA8888: *fmt = G2D_RGBA8888; break;
		case IMX_2D_PIXEL_FORMAT_BGRX8888: *fmt = G2D_BGRX8888; break;
		case IMX_2D_PIXEL_FORMAT_BGRA8888: *fmt = G2D_BGRA8888; break;
		case IMX_2D_PIXEL_FORMAT_XRGB8888: *fmt = G2D_XRGB8888; break;
		case IMX_2D_PIXEL_FORMAT_ARGB8888: *fmt = G2D_ARGB8888; break;
		case IMX_2D_PIXEL_FORMAT_XBGR8888: *fmt = G2D_XBGR8888; break;
		case IMX_2D_PIXEL_FORMAT_ABGR8888: *fmt = G2D_ABGR8888; break;

		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: *fmt = G2D_UYVY; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: *fmt = G2D_YUYV; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU: *fmt = G2D_YVYU; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY: *fmt = G2D_VYUY; break;

		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: *fmt = G2D_NV12; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21: *fmt = G2D_NV21; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16: *fmt = G2D_NV16; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61: *fmt = G2D_NV61; break;

		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: *fmt = G2D_YV12; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: *fmt = G2D_I420; break;

		case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128: *fmt = G2D_NV12; break;
		case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128: *fmt = G2D_NV21; break;

		case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128_10BIT: *fmt = G2D_NV12; break;
		case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128_10BIT: *fmt = G2D_NV21; break;

		default: ret = FALSE;
	}

	return ret;
}


#ifdef IMX2D_G2D_COLORIMETRY_SUPPORTED
static BOOL colorimetry_to_g2d_cap_mode(Imx2dColorimetry colorimetry, enum g2d_cap_mode *cap_mode)
{
	switch (colorimetry)
	{
		case IMX2D_COLORIMETRY_BT_601: *cap_mode = G2D_YUV_BT_601; return TRUE;
		case IMX2D_COLORIMETRY_BT_709: *cap_mode = G2D_YUV_BT_709; return TRUE;
#ifdef IMX2D_G2D_FULL_RANGE_COLORIMETRY_SUPPORTED
		case IMX2D_COLORIMETRY_BT_601_FULL_RANGE: *cap_mode = G2D_YUV_BT_601FR; return TRUE;
		case IMX2D_COLORIMETRY_BT_709_FULL_RANGE: *cap_mode = G2D_YUV_BT_709FR; return TRUE;
#endif
		default: return FALSE;
	}
}
#endif


static void copy_region_to_g2d_surface(struct g2d_surface *surface, Imx2dSurface *imx2d_surface, Imx2dRegion const *region)
{
	if (region == NULL)
	{
		Imx2dSurfaceDesc const *imx2d_surface_desc = imx_2d_surface_get_desc(imx2d_surface);

		surface->left   = 0;
		surface->top    = 0;
		surface->right  = imx2d_surface_desc->width;
		surface->bottom = imx2d_surface_desc->height;
	}
	else
	{
		surface->left   = region->x1;
		surface->top    = region->y1;
		surface->right  = region->x2;
		surface->bottom = region->y2;
	}
}


static char const * g2d_tile_layout_to_string(enum g2d_tiling tiling)
{
	switch (tiling)
	{
		case G2D_LINEAR: return "linear (none)";
#ifdef IMX2D_G2D_AMPHION_TILE_LAYOUT_SUPPORTED
		case G2D_AMPHION_TILED: return "Amphion 8x128";
#endif
#ifdef IMX2D_G2D_AMPHION_10BIT_TILE_LAYOUT_SUPPORTED
		case G2D_AMPHION_TILED_10BIT: return "Amphion 8x128 10-bit";
#endif
#ifdef IMX2D_G2D_AMPHION_INTERLACED_TILE_LAYOUT_SUPPORTED
		case G2D_AMPHION_INTERLACED: return "Amphion 8x128 interlaced";
#endif
		default: return "<unknown>";
	}
}


static BOOL fill_g2d_surface_info(struct g2d_surface *g2d_surface, Imx2dSurface *imx_2d_surface)
{
	int i;
	imx_physical_address_t physical_address;
	ImxDmaBuffer *dma_buffer;
	Imx2dPixelFormatInfo const *fmt_info;
	Imx2dSurfaceDesc const *desc = imx_2d_surface_get_desc(imx_2d_surface);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);
	if (fmt_info == NULL)
	{
		IMX_2D_LOG(ERROR, "could not get information about pixel format");
		return FALSE;
	}
	assert(fmt_info->num_planes <= 3);

	if (!get_g2d_format(desc->format, &(g2d_surface->format)))
	{
		IMX_2D_LOG(ERROR, "pixel format not supported by G2D");
		return FALSE;
	}

	/* G2D expects the stride in pixels, not bytes. Perform a bytes->pixels conversion. */
	g2d_surface->stride = desc->plane_strides[0] / fmt_info->pixel_stride;
	g2d_surface->width = g2d_surface->stride;
	g2d_surface->height = desc->height + desc->num_padding_rows;

	for (i = 0; i < fmt_info->num_planes; ++i)
	{
		dma_buffer = imx_2d_surface_get_dma_buffer(imx_2d_surface, i);
		assert(dma_buffer != NULL);

		physical_address = imx_dma_buffer_get_physical_address(dma_buffer);
		if (physical_address == 0)
		{
			IMX_2D_LOG(ERROR, "could not get physical address from DMA buffer");
			return FALSE;
		}

		g2d_surface->planes[i] = physical_address + imx_2d_surface_get_dma_buffer_offset(imx_2d_surface, i);
	}

	for (i = fmt_info->num_planes; i < 3; ++i)
		g2d_surface->planes[i] = 0;

#if ((G2D_VERSION_MAJOR < 1) || ((G2D_VERSION_MAJOR == 1) && (G2D_VERSION_MINOR <= 2)))
	/* XXX: Older G2D versions seem  to use YV12 with incorrect plane
	 * order. In other words, for G2D, YV12 seems to be the same as
	 * I420. Consequently, we have to swap U/V plane addresses. */
	if (desc->format == IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12)
	{
		int paddr = g2d_surface->planes[1];
		g2d_surface->planes[1] = g2d_surface->planes[2];
		g2d_surface->planes[2] = paddr;
	}
#endif

	return TRUE;
}


static BOOL fill_g2d_surfaceEx_info(struct g2d_surfaceEx *g2d_surfaceEx, Imx2dSurface *imx_2d_surface)
{
	Imx2dSurfaceDesc const *desc = imx_2d_surface_get_desc(imx_2d_surface);

	if (!fill_g2d_surface_info(&(g2d_surfaceEx->base), imx_2d_surface))
		return FALSE;

	switch (desc->format)
	{
#ifdef IMX2D_G2D_AMPHION_TILE_LAYOUT_SUPPORTED
		case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128:
		case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128:
			g2d_surfaceEx->tiling = G2D_AMPHION_TILED;
			break;
#endif

#ifdef IMX2D_G2D_AMPHION_10BIT_TILE_LAYOUT_SUPPORTED
		case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128_10BIT:
		case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128_10BIT:
			g2d_surfaceEx->tiling = G2D_AMPHION_TILED_10BIT;
			break;
#endif

		default:
			g2d_surfaceEx->tiling = G2D_LINEAR;
			break;
	}

	return TRUE;
}


#define DUMP_G2D_SURFACE_TO_LOG(DESC, SURFACE) \
	do { \
		IMX_2D_LOG(TRACE, \
			"%s:  planes %" IMX_PHYSICAL_ADDRESS_FORMAT " %" IMX_PHYSICAL_ADDRESS_FORMAT " %" IMX_PHYSICAL_ADDRESS_FORMAT "  left/top/right/bottom %d/%d/%d/%d  stride %d  width/height %d/%d  global_alpha %d  clrcolor %08x", \
			(DESC), \
			(SURFACE)->base.planes[0], (SURFACE)->base.planes[1], (SURFACE)->base.planes[2], \
			(SURFACE)->base.left, (SURFACE)->base.top, (SURFACE)->base.right, (SURFACE)->base.bottom, \
			(SURFACE)->base.stride, \
			(SURFACE)->base.width, (SURFACE)->base.height, \
			(SURFACE)->base.global_alpha, \
			(SURFACE)->base.clrcolor \
		); \
	} while (0)




typedef struct _Imx2dG2DBlitter Imx2dG2DBlitter;


struct _Imx2dG2DBlitter
{
	Imx2dBlitter parent;

	void *g2d_handle;

	struct g2d_surface fill_g2d_surface;
	ImxDmaBuffer *fill_g2d_surface_dmabuffer;

	ImxDmaBufferAllocator *internal_dmabuffer_allocator;

	Imx2dColorimetry current_colorimetry, pending_colorimetry;
};


static void imx_2d_backend_g2d_blitter_destroy(Imx2dBlitter *blitter);

static int imx_2d_backend_g2d_blitter_start(Imx2dBlitter *blitter);
static int imx_2d_backend_g2d_blitter_finish(Imx2dBlitter *blitter);

static int imx_2d_backend_g2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params);
static int imx_2d_backend_g2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params);

static Imx2dHardwareCapabilities const * imx_2d_backend_g2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);

static void imx_2d_backend_g2d_blitter_set_colorimetry(Imx2dG2DBlitter *g2d_blitter);


static Imx2dBlitterClass imx_2d_backend_g2d_blitter_class =
{
	imx_2d_backend_g2d_blitter_destroy,

	imx_2d_backend_g2d_blitter_start,
	imx_2d_backend_g2d_blitter_finish,

	imx_2d_backend_g2d_blitter_do_blit,
	imx_2d_backend_g2d_blitter_fill_region,

	imx_2d_backend_g2d_blitter_get_hardware_capabilities
};


/* NOTE: Some parts are enclosed in IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD
 * #ifdef blocks. This is because older G2D versions required all G2D calls
 * to be made from one and the same thread. Newer G2D versions do not require
 * this anymore, and in fact work less efficiently when these workarounds for
 * the older G2D implementations are applied (because g2d_open() is called
 * for every frame then, and in newer versions, g2d_open() is a rather slow
 * function, since it sets up OpenCL resources). DPU-based G2D implementations
 * (present on the i.MX8qm and the i.MX8qxp) also do not have this limitation
 * and also behave worse when these workarounds are used.
 *
 * This means that for DPU-based G2D versions and for G2D versions >= 2.0,
 * IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD is _not_ defined. For all other
 * versions, it is.
 *
 * This enhances https://github.com/Freescale/gstreamer-imx/pull/282 . */




static void imx_2d_backend_g2d_blitter_destroy(Imx2dBlitter *blitter)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;

	assert(blitter != NULL);

#ifndef IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD
	if (g2d_blitter->g2d_handle != NULL)
	{
		if (g2d_close(g2d_blitter->g2d_handle) != 0)
			IMX_2D_LOG(ERROR, "closing g2d device failed");
		g2d_blitter->g2d_handle = NULL;
	}
#endif

	if (g2d_blitter->fill_g2d_surface_dmabuffer != NULL)
	{
		IMX_2D_LOG(DEBUG, "destroying G2D fill surface DMA buffer %p", (void *)(g2d_blitter->fill_g2d_surface_dmabuffer));
		imx_dma_buffer_deallocate(g2d_blitter->fill_g2d_surface_dmabuffer);
	}

	if (g2d_blitter->internal_dmabuffer_allocator != NULL)
	{
		IMX_2D_LOG(DEBUG, "destroying i.MX DMA buffer allocator %p", (void *)(g2d_blitter->internal_dmabuffer_allocator));
		imx_dma_buffer_allocator_destroy(g2d_blitter->internal_dmabuffer_allocator);
	}

	free(blitter);
}


static int imx_2d_backend_g2d_blitter_start(Imx2dBlitter *blitter)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;

#ifndef IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD
	if (g2d_blitter->g2d_handle == NULL)
#endif
	{
		if (g2d_open(&(g2d_blitter->g2d_handle)) != 0)
		{
			IMX_2D_LOG(ERROR, "opening g2d device failed");
			return FALSE;
		}
	}

	if (g2d_make_current(g2d_blitter->g2d_handle, G2D_HARDWARE_2D) != 0)
	{
		IMX_2D_LOG(ERROR, "g2d_make_current() failed");
		if (g2d_close(g2d_blitter->g2d_handle) != 0)
			IMX_2D_LOG(ERROR, "closing g2d device failed");
		g2d_blitter->g2d_handle = NULL;
		return FALSE;
	}

	return TRUE;
}


static int imx_2d_backend_g2d_blitter_finish(Imx2dBlitter *blitter)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	int ret;

	assert(blitter != NULL);

	ret = (g2d_finish(g2d_blitter->g2d_handle) == 0);

#ifdef IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD
	if (g2d_close(g2d_blitter->g2d_handle) != 0)
		IMX_2D_LOG(ERROR, "closing g2d device failed");
	/* Set this back to UNKNOWN to force the set_colorimetry
	 * call in _start() to set the colorimetry cap modes. */
	g2d_blitter->current_colorimetry = IMX2D_COLORIMETRY_UNKNOWN;
#endif

	return ret;
}


static int imx_2d_backend_g2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params)
{
	BOOL do_alpha;
	int g2d_ret;
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	struct g2d_surfaceEx g2d_source_surf, g2d_dest_surf;

	assert(blitter != NULL);
	assert(blitter->dest != NULL);
	assert(internal_blit_params != NULL);
	assert(internal_blit_params->source != NULL);

	assert(g2d_blitter->g2d_handle != NULL);


	if (!fill_g2d_surfaceEx_info(&g2d_source_surf, internal_blit_params->source) || !fill_g2d_surfaceEx_info(&g2d_dest_surf, blitter->dest))
		return FALSE;

	copy_region_to_g2d_surface(&(g2d_source_surf.base), internal_blit_params->source, internal_blit_params->source_region);
	copy_region_to_g2d_surface(&(g2d_dest_surf.base), blitter->dest, internal_blit_params->dest_region);

	g2d_source_surf.base.clrcolor = g2d_dest_surf.base.clrcolor = 0xFF000000;

	do_alpha = (internal_blit_params->dest_surface_alpha != 255) || g2d_format_has_alpha(g2d_source_surf.base.format);

	g2d_source_surf.base.rot = g2d_dest_surf.base.rot = G2D_ROTATION_0;
	switch (internal_blit_params->rotation)
	{
		case IMX_2D_ROTATION_90:  g2d_dest_surf.base.rot = G2D_ROTATION_90; break;
		case IMX_2D_ROTATION_180: g2d_dest_surf.base.rot = G2D_ROTATION_180; break;
		case IMX_2D_ROTATION_270: g2d_dest_surf.base.rot = G2D_ROTATION_270; break;
		case IMX_2D_ROTATION_FLIP_HORIZONTAL: g2d_source_surf.base.rot = G2D_FLIP_H; break;
		case IMX_2D_ROTATION_FLIP_VERTICAL: g2d_source_surf.base.rot = G2D_FLIP_V; break;
		case IMX_2D_ROTATION_UL_LR:
			g2d_source_surf.base.rot = G2D_FLIP_V;
			g2d_dest_surf.base.rot = G2D_ROTATION_90;
			break;
		case IMX_2D_ROTATION_UR_LL:
			g2d_source_surf.base.rot = G2D_FLIP_H;
			g2d_dest_surf.base.rot = G2D_ROTATION_90;
			break;
		default: break;
	}

	DUMP_G2D_SURFACE_TO_LOG("blit source", &g2d_source_surf);
	DUMP_G2D_SURFACE_TO_LOG("blit dest", &g2d_dest_surf);

	IMX_2D_LOG(TRACE, "source tile layout: %s", g2d_tile_layout_to_string(g2d_source_surf.tiling));

	g2d_blitter->pending_colorimetry = internal_blit_params->colorimetry;
	imx_2d_backend_g2d_blitter_set_colorimetry(g2d_blitter);

	/* If there is an expanded_dest_region, it means that
	 * there is a margin that must be drawn. */
	if (internal_blit_params->expanded_dest_region != NULL)
	{
		int i;
		struct g2d_surface margin_g2d_surf;
		int margin_alpha = (internal_blit_params->margin_fill_color >> 24) & 0xFF;

		memcpy(&margin_g2d_surf, &g2d_dest_surf, sizeof(struct g2d_surface));

		/* G2D clear color is 0x00BBGGRR, margin_fill_color
		 * is 0x00RRGGBB, so we need to convert.  Also, the
		 * clear operation exhibited problems when the MSB
		 * wasn't 0xFF, so set it. */
		margin_g2d_surf.clrcolor = ((internal_blit_params->margin_fill_color & 0x0000FF) << 16)
		                         | ((internal_blit_params->margin_fill_color & 0x00FF00))
		                         | ((internal_blit_params->margin_fill_color & 0xFF0000) >> 16)
		                         | 0xFF000000;

		IMX_2D_LOG(TRACE, "margin fill color: %#08x alpha: %d", internal_blit_params->margin_fill_color & 0x00FFFFFF, margin_alpha);

		if (margin_alpha != 255)
		{
			/* g2d_clear() ignores alpha blending, so if margin_alpha is not 255,
			 * use a trick. Take the fill_surface, which is a very small surface,
			 * fill it with the fill color, and blit it with blending. */

			g2d_blitter->fill_g2d_surface.clrcolor = margin_g2d_surf.clrcolor;
			if (g2d_clear(g2d_blitter->g2d_handle, &(g2d_blitter->fill_g2d_surface)) != 0)
			{
				IMX_2D_LOG(ERROR, "could not fill margin");
				return FALSE;
			}

			g2d_blitter->fill_g2d_surface.blendfunc = G2D_SRC_ALPHA;
			g2d_blitter->fill_g2d_surface.global_alpha = margin_alpha;
			margin_g2d_surf.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
			margin_g2d_surf.global_alpha = margin_alpha;

			g2d_enable(g2d_blitter->g2d_handle, G2D_BLEND);
			g2d_enable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
		}

		for (i = 0; i < 4; ++i)
		{
			BOOL skip_margin_region = FALSE;

			/* There are four rectangular margin regions: left, top, right, bottom.
			 * Figure out their coordinates here and write them directly into the
			 * margin_g2d_surf structure so we can call g2d_clear() right away.
			 * skip_margin_region is used in case the computed coordinates yield
			 * a region that has no pixels because one of its sidelengths is zero. */
			switch (i)
			{
				case 0:
					margin_g2d_surf.left = internal_blit_params->expanded_dest_region->x1;
					margin_g2d_surf.top = internal_blit_params->dest_region->y1;
					margin_g2d_surf.right = internal_blit_params->dest_region->x1;
					margin_g2d_surf.bottom = internal_blit_params->dest_region->y2;
					skip_margin_region = (margin_g2d_surf.left == margin_g2d_surf.right);
					break;

				case 1:
					margin_g2d_surf.left = internal_blit_params->expanded_dest_region->x1;
					margin_g2d_surf.top = internal_blit_params->expanded_dest_region->y1;
					margin_g2d_surf.right = internal_blit_params->expanded_dest_region->x2;
					margin_g2d_surf.bottom = internal_blit_params->dest_region->y1;
					skip_margin_region = (margin_g2d_surf.top == margin_g2d_surf.bottom);
					break;

				case 2:
					margin_g2d_surf.left = internal_blit_params->dest_region->x2;
					margin_g2d_surf.top = internal_blit_params->dest_region->y1;
					margin_g2d_surf.right = internal_blit_params->expanded_dest_region->x2;
					margin_g2d_surf.bottom = internal_blit_params->dest_region->y2;
					skip_margin_region = (margin_g2d_surf.left == margin_g2d_surf.right);
					break;

				case 3:
					margin_g2d_surf.left = internal_blit_params->expanded_dest_region->x1;
					margin_g2d_surf.top = internal_blit_params->dest_region->y2;
					margin_g2d_surf.right = internal_blit_params->expanded_dest_region->x2;
					margin_g2d_surf.bottom = internal_blit_params->expanded_dest_region->y2;
					skip_margin_region = (margin_g2d_surf.top == margin_g2d_surf.bottom);
					break;

				default:
					assert(FALSE);
			}

			IMX_2D_LOG(TRACE, "margin #%d G2D surface: %d/%d/%d/%d", i, margin_g2d_surf.left, margin_g2d_surf.top, margin_g2d_surf.right, margin_g2d_surf.bottom);

			if (skip_margin_region)
			{
				IMX_2D_LOG(TRACE, "skipping margin");
				continue;
			}

			if (margin_alpha == 255)
			{
				IMX_2D_LOG(TRACE, "filling margin with g2d_clear()");
				if (g2d_clear(g2d_blitter->g2d_handle, &margin_g2d_surf) != 0)
				{
					IMX_2D_LOG(ERROR, "could not fill margin");
					return FALSE;
				}
			}
			else
			{
				IMX_2D_LOG(TRACE, "filling margin with g2d_blit() and the fill surface; alpha = %d", margin_alpha);

				if (g2d_blit(g2d_blitter->g2d_handle, &(g2d_blitter->fill_g2d_surface), &margin_g2d_surf) != 0)
				{
					IMX_2D_LOG(ERROR, "could not blit fill surface - drawing margin failed");
					return FALSE;
				}
			}
		}

		if (margin_alpha != 255)
		{
			g2d_disable(g2d_blitter->g2d_handle, G2D_BLEND);
			g2d_disable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
		}
	}

	if (do_alpha)
	{
		g2d_source_surf.base.blendfunc = G2D_SRC_ALPHA;
		g2d_dest_surf.base.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
		g2d_enable(g2d_blitter->g2d_handle, G2D_BLEND);

		if (internal_blit_params->dest_surface_alpha != 255)
		{
			g2d_enable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
			g2d_source_surf.base.global_alpha = internal_blit_params->dest_surface_alpha;
			g2d_dest_surf.base.global_alpha = 255 - internal_blit_params->dest_surface_alpha;
		}
		else
			g2d_disable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
	}
	else
	{
		g2d_source_surf.base.blendfunc = G2D_ONE;
		g2d_dest_surf.base.blendfunc = G2D_ZERO;
		g2d_source_surf.base.global_alpha = 0;
		g2d_dest_surf.base.global_alpha = 0;
		g2d_disable(g2d_blitter->g2d_handle, G2D_BLEND);
		g2d_disable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
	}

	g2d_ret = g2d_blitEx(g2d_blitter->g2d_handle, &g2d_source_surf, &g2d_dest_surf);

	if (do_alpha)
		g2d_disable(g2d_blitter->g2d_handle, G2D_BLEND);

	if (g2d_ret != 0)
	{
		IMX_2D_LOG(ERROR, "could not blit surface");
		return FALSE;
	}
	else
		return TRUE;
}


static int imx_2d_backend_g2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	struct g2d_surface g2d_dest_surf;

	assert(blitter != NULL);
	assert(blitter->dest != NULL);
	assert(internal_fill_region_params != NULL);
	assert(internal_fill_region_params->dest_region != NULL);

	assert(g2d_blitter->g2d_handle != NULL);

	if (!fill_g2d_surface_info(&g2d_dest_surf, blitter->dest))
		return FALSE;

	copy_region_to_g2d_surface(&g2d_dest_surf, blitter->dest, internal_fill_region_params->dest_region);

	/* G2D clear color is 0x00BBGGRR, margin_fill_color
	 * is 0x00RRGGBB, so we need to convert.  Also, the
	 * clear operation exhibited problems when the MSB
	 * wasn't 0xFF, so set it. */
	g2d_dest_surf.clrcolor = ((internal_fill_region_params->fill_color & 0x0000FF) << 16)
	                       | ((internal_fill_region_params->fill_color & 0x00FF00))
	                       | ((internal_fill_region_params->fill_color & 0xFF0000) >> 16)
	                       | 0xFF000000;

	if (g2d_clear(g2d_blitter->g2d_handle, &g2d_dest_surf) != 0)
	{
		IMX_2D_LOG(ERROR, "could not clear area");
		return FALSE;
	}
	else
		return TRUE;
}


static Imx2dHardwareCapabilities const * imx_2d_backend_g2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return imx_2d_backend_g2d_get_hardware_capabilities();
}


static void imx_2d_backend_g2d_blitter_set_colorimetry(Imx2dG2DBlitter *g2d_blitter)
{
#ifdef IMX2D_G2D_COLORIMETRY_SUPPORTED
	int i;

	if (g2d_blitter->current_colorimetry == g2d_blitter->pending_colorimetry)
		return;

	g2d_blitter->current_colorimetry = g2d_blitter->pending_colorimetry;

	for (i = 0; i < IMX2D_NUM_COLORIMETRY_ITEMS; ++i)
	{
		Imx2dColorimetry colorimetry = (Imx2dColorimetry)i;
		enum g2d_cap_mode cap_mode;
		BOOL do_enable;

		if (!colorimetry_to_g2d_cap_mode(colorimetry, &cap_mode))
			continue;

		do_enable = (g2d_blitter->current_colorimetry == colorimetry);

		IMX_2D_LOG(DEBUG, "%s G2D %s mode", do_enable ? "enabling" : "disabling", imx_2d_colorimetry_to_string(colorimetry));

		if (do_enable)
			g2d_enable(g2d_blitter->g2d_handle, cap_mode);
		else
			g2d_disable(g2d_blitter->g2d_handle, cap_mode);
	}
#endif
}




Imx2dBlitter* imx_2d_backend_g2d_blitter_create(void)
{
	int err;

	/* Set up the internal fill surface that will be used when drawing margins
	 * that aren't 100% opaque (see imx_2d_backend_g2d_blitter_do_blit() above).
	 * The internal fill surface does not have to be large. In fact, it is desirable
	 * to make it as small as possible to ensure the g2d_clear() calls in the
	 * blit() function uses as little bandwidth as possible. For this reason, the
	 * fill surface is allocated to use a size of 4x1 pixels, the smallest one
	 * allowed by the G2D API. */
	static enum g2d_format const fill_surface_format = G2D_RGBX8888;
	static int const fill_surface_bpp = 4;
	static int const fill_surface_width = 4;
	static int const fill_surface_height = 1;
	static int const fill_surface_stride = fill_surface_width;
	static size_t fill_surface_dmabuffer_size = fill_surface_stride * fill_surface_height * fill_surface_bpp;

	Imx2dG2DBlitter *g2d_blitter = malloc(sizeof(Imx2dG2DBlitter));
	assert(g2d_blitter != NULL);

	memset(g2d_blitter, 0, sizeof(Imx2dG2DBlitter));

	g2d_blitter->parent.blitter_class = &imx_2d_backend_g2d_blitter_class;

	g2d_blitter->fill_g2d_surface.format = fill_surface_format;
	g2d_blitter->fill_g2d_surface.width = fill_surface_width;
	g2d_blitter->fill_g2d_surface.height = fill_surface_height;
	g2d_blitter->fill_g2d_surface.right = fill_surface_width;
	g2d_blitter->fill_g2d_surface.bottom = fill_surface_height;
	g2d_blitter->fill_g2d_surface.stride = fill_surface_stride;

	g2d_blitter->internal_dmabuffer_allocator = imx_dma_buffer_allocator_new(&err);
	if (g2d_blitter->internal_dmabuffer_allocator == NULL)
	{
		IMX_2D_LOG(ERROR, "could not create internal G2D DMA buffer allocator: %s (%d)", strerror(err), err);
		goto error;
	}
	IMX_2D_LOG(DEBUG, "created new internal i.MX DMA buffer allocator %p", (void *)(g2d_blitter->internal_dmabuffer_allocator));

	g2d_blitter->fill_g2d_surface_dmabuffer = imx_dma_buffer_allocate(g2d_blitter->internal_dmabuffer_allocator, fill_surface_dmabuffer_size, 1, &err);
	if (g2d_blitter->fill_g2d_surface_dmabuffer == NULL)
	{
		IMX_2D_LOG(ERROR, "could not allocate fill surface DMA buffer");
		goto error;
	}
	IMX_2D_LOG(DEBUG, "created new G2D fill surface DMA buffer %p; buffer size: %zu byte(s)", (void *)(g2d_blitter->fill_g2d_surface_dmabuffer), fill_surface_dmabuffer_size);

	g2d_blitter->fill_g2d_surface.planes[0] = imx_dma_buffer_get_physical_address(g2d_blitter->fill_g2d_surface_dmabuffer);

	g2d_blitter->current_colorimetry = IMX2D_COLORIMETRY_UNKNOWN;
	g2d_blitter->pending_colorimetry = IMX2D_COLORIMETRY_BT_601;

finish:
	return (Imx2dBlitter *)g2d_blitter;

error:
	imx_2d_backend_g2d_blitter_destroy((Imx2dBlitter *)g2d_blitter);
	g2d_blitter = NULL;
	goto finish;
}


/* RGB formats require 16-byte alignments according to the G2D documentation,
 * but this is covered by the default stride_alignment value further below. */
static Imx2dFormatAlignment const special_format_alignments[] = {
	{ .format = IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY, .alignment = 8 },

	{ .format = IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16, .alignment = 8 },
	{ .format = IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61, .alignment = 8 },

	/* XXX: G2D does not work if I420 and YV12 frame strides aren't 32-byte
	 * aligned, even though other YUV formats only require an 8-byte alignment.
	 * This is not document, and it is unknown why this alignment is different. */
	{ .format = IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420, .alignment = 32 },
	{ .format = IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12, .alignment = 32 }
};


static Imx2dHardwareCapabilities const capabilities = {
	.supported_source_pixel_formats = supported_source_pixel_formats,
	.num_supported_source_pixel_formats = sizeof(supported_source_pixel_formats) / sizeof(Imx2dPixelFormat),

	.supported_dest_pixel_formats = supported_dest_pixel_formats,
	.num_supported_dest_pixel_formats = sizeof(supported_dest_pixel_formats) / sizeof(Imx2dPixelFormat),

	.min_width = 4, .max_width = INT_MAX, .width_step_size = 1,
	.min_height = 4, .max_height = INT_MAX, .height_step_size = 1,

	.stride_alignment = 16,
	.total_row_count_alignment = 2,

	.can_handle_multi_buffer_surfaces = 1,

	.special_format_stride_alignments = special_format_alignments,
	.num_special_format_stride_alignments = sizeof(special_format_alignments) / sizeof(Imx2dFormatAlignment)
};

Imx2dHardwareCapabilities const * imx_2d_backend_g2d_get_hardware_capabilities(void)
{
	return &capabilities;
}
