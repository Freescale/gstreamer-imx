#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "imx2d.h"
#include "imx2d_priv.h"


/***********************/
/******* LOGGING *******/
/***********************/


static void default_logging_fn(Imx2dLogLevel level, char const *file, int const line, char const *function_name, const char *format, ...)
{
	IMX_2D_UNUSED_PARAM(level);
	IMX_2D_UNUSED_PARAM(file);
	IMX_2D_UNUSED_PARAM(line);
	IMX_2D_UNUSED_PARAM(function_name);
	IMX_2D_UNUSED_PARAM(format);
}

Imx2dLogLevel imx_2d_cur_log_level_threshold = IMX_2D_LOG_LEVEL_ERROR;
Imx2dLoggingFunc imx_2d_cur_logging_fn = default_logging_fn;

void imx_2d_set_logging_threshold(Imx2dLogLevel threshold)
{
	imx_2d_cur_log_level_threshold = threshold;
}

void imx_2d_set_logging_function(Imx2dLoggingFunc logging_function)
{
	imx_2d_cur_logging_fn = (logging_function != NULL) ? logging_function : default_logging_fn;
}




char const * imx_2d_pixel_format_to_string(Imx2dPixelFormat format)
{
	Imx2dPixelFormatInfo const *info = imx_2d_get_pixel_format_info(format);
	return (info != NULL) ? info->description : "<unknown>";
}


char const * imx_2d_rotation_string(Imx2dRotation rotation)
{
	switch (rotation)
	{
		case IMX_2D_ROTATION_NONE: return "none";
		case IMX_2D_ROTATION_90: return "90-degree rotation";
		case IMX_2D_ROTATION_180: return "180-degree rotation";
		case IMX_2D_ROTATION_270: return "270-degree rotation";
		case IMX_2D_ROTATION_FLIP_HORIZONTAL: return "horizontal flip";
		case IMX_2D_ROTATION_FLIP_VERTICAL: return "vertical flip";
		default: return "<unknown>";
	}
}


Imx2dPixelFormatInfo const * imx_2d_get_pixel_format_info(Imx2dPixelFormat format)
{
#define PIXEL_FORMAT_DESC(DESC, FMT, NUM_PLANES, FIRST_PLANE_BPP, X_SS, Y_SS, IS_SEMI_PLANAR) \
	case (IMX_2D_PIXEL_FORMAT_##FMT): \
	{ \
		static Imx2dPixelFormatInfo const info = { \
			(DESC), (NUM_PLANES), (FIRST_PLANE_BPP), (X_SS), (Y_SS), (IS_SEMI_PLANAR) \
		}; \
		return &info; \
	}

	switch (format)
	{
		PIXEL_FORMAT_DESC("RGB 5:6:5", RGB565, 1, 16, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("BGR 5:6:5", BGR565, 1, 16, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("RGB 8:8:8", RGB888, 1, 24, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("BGR 8:8:8", BGR888, 1, 24, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("RGBX 8:8:8:8", RGBX8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("RGBA 8:8:8:8", RGBA8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("BGRX 8:8:8:8", BGRX8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("BGRA 8:8:8:8", BGRA8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("XRGB 8:8:8:8", XRGB8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("ARGB 8:8:8:8", ARGB8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("XBGR 8:8:8:8", XBGR8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("ABGR 8:8:8:8", ABGR8888, 1, 32, 1, 1, FALSE)
		PIXEL_FORMAT_DESC("grayscale 8", GRAY8, 1, 8, 1, 1, FALSE)

		PIXEL_FORMAT_DESC("YUV 4:2:2 packed UYVY", PACKED_YUV422_UYVY, 1, 16, 2, 1, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed YUYV", PACKED_YUV422_YUYV, 1, 16, 2, 1, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed YVYU", PACKED_YUV422_YVYU, 1, 16, 2, 1, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed VYUY", PACKED_YUV422_VYUY, 1, 16, 2, 1, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:4:4 packed", PACKED_YUV444, 1, 24, 1, 1, FALSE)

		PIXEL_FORMAT_DESC("YUV 4:2:0 semi planar NV12", SEMI_PLANAR_NV12, 2, 8, 2, 2, TRUE)
		PIXEL_FORMAT_DESC("YUV 4:2:0 semi planar NV21", SEMI_PLANAR_NV21, 2, 8, 2, 2, TRUE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 semi planar NV16", SEMI_PLANAR_NV16, 2, 8, 2, 1, TRUE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 semi planar NV61", SEMI_PLANAR_NV61, 2, 8, 2, 1, TRUE)

		PIXEL_FORMAT_DESC("YUV 4:2:0 fully planar YV12", FULLY_PLANAR_YV12, 3, 8, 2, 2, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:2:0 fully planar I420", FULLY_PLANAR_I420, 3, 8, 2, 2, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:2:2 fully planar Y42B", FULLY_PLANAR_Y42B, 3, 8, 2, 1, FALSE)
		PIXEL_FORMAT_DESC("YUV 4:4:4 fully planar Y444", FULLY_PLANAR_Y444, 3, 8, 1, 1, FALSE)

		default: return NULL;
	}

#undef PIXEL_FORMAT_DESC
}




inline static int sgn(int const val)
{
	return (0 < val) - (val < 0);
}


Imx2dRegionInclusion imx_2d_region_check_inclusion(Imx2dRegion const *first_region, Imx2dRegion const *second_region)
{
	assert(first_region != NULL);
	assert(second_region != NULL);

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

	if ((xt1 != xt2) && (yt1 != yt2))
	{
		/* In case there is an overlap, check if second_region (dx/dy)
		 * contains first_region (sx/sy) partially or fully */
		return ((sx1 >= dx1) && (sy1 >= dy1) && (sx2 <= dx2) && (sy2 <= dy2))
		     ? IMX_2D_REGION_INCLUSION_FULL
		     : IMX_2D_REGION_INCLUSION_PARTIAL;
	}
	else
		return IMX_2D_REGION_INCLUSION_NONE;
}


int imx_2d_region_check_if_equal(Imx2dRegion const *first_region, Imx2dRegion const *second_region)
{
	assert(first_region != NULL);
	assert(second_region != NULL);

	return (first_region->x1 == second_region->x1) &&
	       (first_region->y1 == second_region->y1) &&
	       (first_region->x2 == second_region->x2) &&
	       (first_region->y2 == second_region->y2);
}


void imx_2d_region_intersect(Imx2dRegion *intersection, Imx2dRegion const *first_region, Imx2dRegion const *second_region)
{
	assert(intersection != NULL);
	assert(first_region != NULL);
	assert(second_region != NULL);

	intersection->x1 = MAX(first_region->x1, second_region->x1);
	intersection->y1 = MAX(first_region->y1, second_region->y1);
	intersection->x2 = MIN(first_region->x2, second_region->x2);
	intersection->y2 = MIN(first_region->y2, second_region->y2);
}


void imx_2d_region_merge(Imx2dRegion *merged_region, Imx2dRegion const *first_region, Imx2dRegion const *second_region)
{
	assert(merged_region != NULL);
	assert(first_region != NULL);
	assert(second_region != NULL);

	merged_region->x1 = MIN(first_region->x1, second_region->x1);
	merged_region->y1 = MIN(first_region->y1, second_region->y1);
	merged_region->x2 = MAX(first_region->x2, second_region->x2);
	merged_region->y2 = MAX(first_region->y2, second_region->y2);
}




void imx_2d_surface_desc_calculate_strides_and_offsets(Imx2dSurfaceDesc *desc, Imx2dHardwareCapabilities const *capabilities)
{
	Imx2dPixelFormatInfo const *fmt_info;
	int offset;
	int plane_nr;

	assert(desc != NULL);
	assert(desc->width > 0);
	assert(desc->height > 0);
	assert(desc->format != IMX_2D_PIXEL_FORMAT_UNKNOWN);
	assert(capabilities != NULL);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);

	offset = 0;
	for (plane_nr = 0; plane_nr < fmt_info->num_planes; ++plane_nr)
	{
		int stride;

		int x_subsampling = (plane_nr == 0) ? 1 : fmt_info->x_subsampling;
		int y_subsampling = (plane_nr == 0) ? 1 : fmt_info->y_subsampling;

		stride = desc->width * fmt_info->num_first_plane_bpp / 8 / x_subsampling;

		desc->plane_stride[plane_nr] = stride;
		desc->plane_offset[plane_nr] = offset;

		offset += stride * desc->height / y_subsampling;
	}
}


int imx_2d_surface_desc_calculate_framesize(Imx2dSurfaceDesc const *desc)
{
	int y_subsampling;
	int last_plane_nr;
	Imx2dPixelFormatInfo const * fmt_info;

	assert(desc != NULL);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);
	if (fmt_info == NULL)
		return 0;

	assert(fmt_info->num_planes >= 1);

	last_plane_nr = fmt_info->num_planes - 1;
	y_subsampling = (last_plane_nr == 0) ? 1 : fmt_info->y_subsampling;

	/* Use the offset of the last plane when computing the frame size.
	 * This is because there may be padding bytes in between planes.
	 * By using the last plane's offset, we implicitely factor in these
	 * padding bytes into our calculations. */
	return desc->plane_offset[last_plane_nr] + desc->plane_stride[last_plane_nr] * desc->height / y_subsampling;
}


Imx2dSurface* imx_2d_surface_create(ImxDmaBuffer *dma_buffer, Imx2dSurfaceDesc const *desc)
{
	Imx2dSurface *surface = malloc(sizeof(Imx2dSurface));
	assert(surface != NULL);

	surface->dma_buffer = dma_buffer;
	memset(&(surface->region), 0, sizeof(surface->region));

	if (desc != NULL)
		imx_2d_surface_set_desc(surface, desc);

	return surface;
}


void imx_2d_surface_destroy(Imx2dSurface *surface)
{
	free(surface);
}


void imx_2d_surface_set_desc(Imx2dSurface *surface, Imx2dSurfaceDesc const *desc)
{
	assert(surface != NULL);
	assert(desc != NULL);
	memcpy(&(surface->desc), desc, sizeof(Imx2dSurfaceDesc));
	surface->region.x2 = desc->width;
	surface->region.y2 = desc->height;
}


Imx2dSurfaceDesc const * imx_2d_surface_get_desc(Imx2dSurface *surface)
{
	return &(surface->desc);
}


void imx_2d_surface_set_dma_buffer(Imx2dSurface *surface, ImxDmaBuffer *dma_buffer)
{
	assert(surface != NULL);
	assert(dma_buffer != NULL);
	surface->dma_buffer = dma_buffer;
}


ImxDmaBuffer* imx_2d_surface_get_dma_buffer(Imx2dSurface *surface)
{
	assert(surface != NULL);
	return surface->dma_buffer;
}


Imx2dRegion const * imx_2d_surface_get_region(Imx2dSurface *surface)
{
	assert(surface != NULL);
	return &(surface->region);
}




void imx_2d_blitter_destroy(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->destroy != NULL));
	blitter->blitter_class->destroy(blitter);
}


int imx_2d_blitter_start(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->start != NULL));
	return blitter->blitter_class->start(blitter);
}


int imx_2d_blitter_finish(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->start != NULL));
	return blitter->blitter_class->finish(blitter);
}


int imx_2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params)
{
	static Imx2dBlitParams const default_params =
	{
		.source_region = NULL,
		.dest_region = NULL,
		.rotation = IMX_2D_ROTATION_NONE,
		.alpha = 255
	};

	Imx2dBlitParams const *params_in_use = (params != NULL) ? params : &default_params;

	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->do_blit != NULL));

	if (params_in_use->alpha == 0)
	{
		/* If alpha is set to 0, then the blitting would effectively
		 * do nothing due to the pixels being 100% translucent. */
		IMX_2D_LOG(TRACE, "not blitting because params alpha value is 0");
		return TRUE;
	}
	else if (params_in_use->alpha < 0)
	{
		IMX_2D_LOG(ERROR, "attempting to blit with alpha value %u; minimum allowed value is 0", params_in_use->alpha);
		return FALSE;
	}
	else if (params_in_use->alpha > 255)
	{
		IMX_2D_LOG(ERROR, "attempting to blit with alpha value %u; maximum allowed value is 255", params_in_use->alpha);
		return FALSE;
	}

	if (params_in_use->dest_region != NULL)
	{
		/* dest_region is set, so we need to check if and to what
		 * degree dest_region is inside the dest surface. */

		Imx2dRegionInclusion dest_region_inclusion = IMX_2D_REGION_INCLUSION_FULL;
		Imx2dRegion const *expanded_dest_region_to_use = NULL;
		Imx2dRegion full_expanded_dest_region;
		Imx2dRegion clipped_expanded_dest_region;
		uint32_t margin_fill_color = 0x00000000;

		/* Get the margin and look at its alpha value. If it is 0,
		 * then the margin cannot be visible, so we disable it.
		 * Otherwise, modulate the alpha value with the alpha value
		 * from the params. If the result from that is 0, again,
		 * disable the margin. */
		Imx2dBlitMargin const *margin = params_in_use->margin;
		if (margin != NULL)
		{
			int margin_alpha = (margin->color >> 24) & 0xFF;
			if (margin_alpha != 0)
			{
				int combined_alpha = margin_alpha * params_in_use->alpha / 255;
				IMX_2D_LOG(
					TRACE,
					"global alpha: %d  margin alpha: %d  combined alpha: %d",
					params_in_use->alpha,
					margin_alpha,
					combined_alpha
				);
				if (combined_alpha != 0)
				{
					uint32_t orig_margin_color_without_alpha = margin->color & 0x00FFFFFF;
					margin_fill_color = orig_margin_color_without_alpha | (((uint32_t)combined_alpha) << 24);
					IMX_2D_LOG(
						TRACE,
						"merging margin fill color %#06" PRIx32 " and combined alpha %d to new margin fill color %#08" PRIx32,
						orig_margin_color_without_alpha,
						combined_alpha,
						margin_fill_color
					);
				}
				else
				{
					IMX_2D_LOG(TRACE, "combined alpha is 0; disabling margin");
					margin = NULL;
				}
			}
			else
			{
				IMX_2D_LOG(TRACE, "margin alpha is 0; disabling margin");
				margin = NULL;
			}
		}

		if (margin != NULL)
		{
			Imx2dRegionInclusion expanded_dest_region_inclusion;

			assert(margin->left_margin >= 0);
			assert(margin->top_margin >= 0);
			assert(margin->right_margin >= 0);
			assert(margin->bottom_margin >= 0);

			full_expanded_dest_region.x1 = params_in_use->dest_region->x1 - margin->left_margin;
			full_expanded_dest_region.y1 = params_in_use->dest_region->y1 - margin->top_margin;
			full_expanded_dest_region.x2 = params_in_use->dest_region->x2 + margin->right_margin;
			full_expanded_dest_region.y2 = params_in_use->dest_region->y2 + margin->bottom_margin;

			expanded_dest_region_inclusion = imx_2d_region_check_inclusion(
				&full_expanded_dest_region,
				&(dest->region)
			);

			IMX_2D_LOG(TRACE, "margin defined; expanded dest region: %" IMX_2D_REGION_FORMAT, IMX_2D_REGION_ARGS(&full_expanded_dest_region));

			switch (expanded_dest_region_inclusion)
			{
				case IMX_2D_REGION_INCLUSION_NONE:
				{
					/* if the expanded dest region is fully outside of the
					 * dest surface, then we can exit right away, since then,
					 * neither the margin nor the actual dest region can
					 * possibly be visible, so there is nothing to blit. */
					IMX_2D_LOG(TRACE, "expanded dest region is fully outside of the dest surface bounds; skipping blitter operation");
					return TRUE;
				}

				case IMX_2D_REGION_INCLUSION_FULL:
				{
					/* If the expanded dest region is fully inside the dest
					 * surface, this implies that the original dest region is too.
					 * Therefore, set dest_region_inclusion to "full" to let
					 * the rest of the code know that no more checks are needed. */
					IMX_2D_LOG(TRACE, "expanded dest region is fully inside of the dest surface bounds");
					dest_region_inclusion = IMX_2D_REGION_INCLUSION_FULL;
					expanded_dest_region_to_use = &full_expanded_dest_region;
					break;
				}

				case IMX_2D_REGION_INCLUSION_PARTIAL:
					/* Partial inclusion -> check the inclusion of the original
					 * dest region, and also clip the expanded dest region against
					 * the dest surface. */

					IMX_2D_LOG(TRACE, "expanded dest region is partially inside of the dest surface bounds");

					dest_region_inclusion = imx_2d_region_check_inclusion(
						params_in_use->dest_region,
						&(dest->region)
					);

					imx_2d_region_intersect(
						&clipped_expanded_dest_region,
						&full_expanded_dest_region,
						&(dest->region)
					);
					expanded_dest_region_to_use = &clipped_expanded_dest_region;

					break;

				default:
					assert(FALSE);
			}
		}
		else
		{
			IMX_2D_LOG(TRACE, "no margin defined");
			dest_region_inclusion = imx_2d_region_check_inclusion(
				params_in_use->dest_region,
				&(dest->region)
			);
		}

		/* If we reach this point, then either dest_region is
		 * at least partially visible, or there is a margin &
		 * it is partially visible, or both. In other words,
		 * it is not possible that we have a margin which is
		 * fully invisible if we made it to this point. */

		switch (dest_region_inclusion)
		{
			case IMX_2D_REGION_INCLUSION_NONE:
				if (margin != NULL)
				{
					IMX_2D_LOG(TRACE, "dest region is fully outside of the dest surface bounds, but margin is visible; skipping blitter operation, filling margin");
					return blitter->blitter_class->fill_region(
						blitter,
						dest,
						expanded_dest_region_to_use,
						margin_fill_color
					);
				}
				else
				{
					IMX_2D_LOG(TRACE, "dest region is fully outside of the dest surface bounds; skipping blitter operation");
					return TRUE;
				}
				break;

			case IMX_2D_REGION_INCLUSION_FULL:
				/* We can blit with zero adjustments, since the dest
				 * region is fully inside the dest surface. */
				IMX_2D_LOG(TRACE, "dest region is fully inside of the dest surface bounds");
				return blitter->blitter_class->do_blit(
					blitter,
					source, params_in_use->source_region,
					dest, params_in_use->dest_region,
					params_in_use->rotation,
					expanded_dest_region_to_use,
					params_in_use->alpha,
					margin_fill_color
				);

			case IMX_2D_REGION_INCLUSION_PARTIAL:
			{
				/* We must adjust both the dest and the source region,
				 * since the dest region is only partially inside the
				 * dest surface. We also have to adjust the source region,
				 * because we can only blit a subset of the source region. */

				Imx2dRegion const *source_region = (params_in_use->source_region != NULL) ? params_in_use->source_region : &(source->region);
				Imx2dRegion const *dest_region = params_in_use->dest_region;
				Imx2dRegion clipped_source_region;
				Imx2dRegion clipped_dest_region;

				int source_region_width = source_region->x2 - source_region->x1;
				int source_region_height = source_region->y2 - source_region->y1;
				int dest_region_width = dest_region->x2 - dest_region->x1;
				int dest_region_height = dest_region->y2 - dest_region->y1;

				imx_2d_region_intersect(
					&clipped_dest_region,
					dest_region,
					&(dest->region)
				);

				memcpy(&clipped_source_region, source_region, sizeof(Imx2dRegion));

				switch (params_in_use->rotation)
				{
					case IMX_2D_ROTATION_NONE:
						if (dest_region->x1 < 0)
							clipped_source_region.x1 += source_region_width * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.y1 += source_region_height * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.x2 -= source_region_width * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.y2 -= source_region_height * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					case IMX_2D_ROTATION_90:
						if (dest_region->x1 < 0)
							clipped_source_region.y2 -= source_region_height * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.x1 += source_region_width * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.y1 += source_region_height * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.x2 -= source_region_width * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					case IMX_2D_ROTATION_180:
						if (dest_region->x1 < 0)
							clipped_source_region.x2 -= source_region_width * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.y2 -= source_region_height * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.x1 += source_region_width * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.y1 += source_region_height * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					case IMX_2D_ROTATION_270:
						if (dest_region->x1 < 0)
							clipped_source_region.y1 += source_region_height * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.x2 -= source_region_width * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.y2 -= source_region_height * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.x1 += source_region_width * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					case IMX_2D_ROTATION_FLIP_HORIZONTAL:
						if (dest_region->x1 < 0)
							clipped_source_region.x2 -= source_region_width * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.y1 += source_region_height * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.x1 += source_region_width * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.y2 -= source_region_height * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					case IMX_2D_ROTATION_FLIP_VERTICAL:
						if (dest_region->x1 < 0)
							clipped_source_region.x1 += source_region_width * (-dest_region->x1) / dest_region_width;
						if (dest_region->y1 < 0)
							clipped_source_region.y2 -= source_region_height * (-dest_region->y1) / dest_region_height;
						if (dest_region->x2 > dest->region.x2)
							clipped_source_region.x2 -= source_region_width * (dest_region->x2 - dest->region.x2) / dest_region_width;
						if (dest_region->y2 > dest->region.y2)
							clipped_source_region.y1 += source_region_height * (dest_region->y2 - dest->region.y2) / dest_region_height;
						break;

					default:
						assert(FALSE);
				}

				IMX_2D_LOG(TRACE, "dest region is partially inside of the dest surface bounds");
				IMX_2D_LOG(
					TRACE,
					"clipped source region: %" IMX_2D_REGION_FORMAT " clipped dest region: %" IMX_2D_REGION_FORMAT,
					IMX_2D_REGION_ARGS(&clipped_source_region),
					IMX_2D_REGION_ARGS(&clipped_dest_region)
				);

				return blitter->blitter_class->do_blit(
					blitter,
					source, &clipped_source_region,
					dest, &clipped_dest_region,
					params_in_use->rotation,
					expanded_dest_region_to_use,
					params_in_use->alpha,
					margin_fill_color
				);
			}

			default:
				assert(FALSE);
		}

		/* Should not be reached. Just here to shut up superfluous compiler warnings. */
		return FALSE;
	}
	else
	{
		/* dest_region is not set. This implies that we can directly
		 * go ahead and do the blitter operation, since then, the
		 * entire dest surface is the dest region, so a full inclusion
		 * of the dest region is implied. No need to calculate
		 * inclusions, intersections etc. */

		return blitter->blitter_class->do_blit(
			blitter,
			source, params_in_use->source_region,
			dest, params_in_use->dest_region,
			params_in_use->rotation,
			NULL,
			params_in_use->alpha,
			/* Setting the margin fill color to 0 since the margin is anyway not present in this case */
			0x00000000
		);
	}
}


int imx_2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRegion const *dest_region, uint32_t fill_color)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->fill_region != NULL));
	return blitter->blitter_class->fill_region(blitter, dest, (dest_region != NULL) ? dest_region : &(dest->region), fill_color);
}


Imx2dHardwareCapabilities const * imx_2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->get_hardware_capabilities != NULL));
	return blitter->blitter_class->get_hardware_capabilities(blitter);
}
