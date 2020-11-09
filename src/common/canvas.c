#include "canvas.h"


GType gst_imx_canvas_inner_rotation_get_type(void)
{
	static volatile GType gst_imx_canvas_inner_rotation_type = 0;

	if (g_once_init_enter(&gst_imx_canvas_inner_rotation_type))
	{
		GType _type;

		static GEnumValue rotation_values[] =
		{
			{ GST_IMX_CANVAS_INNER_ROTATION_NONE, "No rotation", "none" },
			{ GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES, "Rotate clockwise 90 degrees", "rotate-90" },
			{ GST_IMX_CANVAS_INNER_ROTATION_180_DEGREES, "Rotate 180 degrees", "rotate-180" },
			{ GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES, "Rotate clockwise 270 degrees", "rotate-270" },
			{ GST_IMX_CANVAS_INNER_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip" },
			{ GST_IMX_CANVAS_INNER_ROTATION_VFLIP, "Flip vertically", "vertical-flip" },
			{ GST_IMX_CANVAS_INNER_ROTATION_UL_LR, "Flip across upper left/lower right diagonal", "upper-left-diagonal"},
			{ GST_IMX_CANVAS_INNER_ROTATION_UR_LL, "Flip across upper right/lower left diagonal", "upper-right-diagonal"},
			{ 0, NULL, NULL },
		};

		_type = g_enum_register_static(
			"ImxCanvasInnerRotation",
			rotation_values
		);

		g_once_init_leave(&gst_imx_canvas_inner_rotation_type, _type);
	}

	return gst_imx_canvas_inner_rotation_type;
}


gboolean gst_imx_canvas_does_rotation_transpose(GstImxCanvasInnerRotation rotation)
{
	switch (rotation)
	{
		case GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES:
		case GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES:
		case GST_IMX_CANVAS_INNER_ROTATION_UL_LR:
		case GST_IMX_CANVAS_INNER_ROTATION_UR_LL:
			return TRUE;
		default:
			return FALSE;
	}
}


void gst_imx_canvas_calculate_inner_region(GstImxCanvas *canvas, GstVideoInfo const *info)
{
	GstImxRegion outer_region;

	g_assert(canvas != NULL);
	g_assert(info != NULL);

	/* Apply margin first */
	outer_region = canvas->outer_region;
	outer_region.x1 += canvas->margin_left;
	outer_region.y1 += canvas->margin_top;
	outer_region.x2 -= canvas->margin_right;
	outer_region.y2 -= canvas->margin_bottom;

	/* Then, calculate inner region */
	gst_imx_region_calculate_inner_region(
		&(canvas->inner_region),
		&outer_region,
		info,
		gst_imx_canvas_does_rotation_transpose(canvas->inner_rotation),
		canvas->keep_aspect_ratio
	);
}


void gst_imx_canvas_clip(GstImxCanvas *canvas, GstImxRegion const *screen_region, GstVideoInfo const *info, GstImxRegion const *source_region, GstImxRegion *source_subset)
{
	GstImxRegion *clipped_outer_region;
	GstImxRegion *clipped_inner_region;
	GstImxRegion actual_source_region;
	GstImxRegionContains contains;

	g_assert(canvas != NULL);
	g_assert(screen_region != NULL);
	g_assert(info != NULL);
	g_assert(source_subset != NULL);

	canvas->visibility_mask = 0;

	/* Do an early check to see if the outer region is at least partially
	 * inside the overall region. (The overall region is for example the
	 * whole screen in a video sink.) If it isn't, then there is no point
	 * in computing anything. visibility_mask is 0 at this point, indicating
	 * that the canvas is not visible at all. */
	if ((contains = gst_imx_region_contains(&(canvas->outer_region), screen_region)) == GST_IMX_REGION_CONTAINS_NONE)
		return;

	clipped_outer_region = &(canvas->clipped_outer_region);
	clipped_inner_region = &(canvas->clipped_inner_region);

	/* Clip the outer region if necessary */
	if (contains == GST_IMX_REGION_CONTAINS_PARTIAL)
		gst_imx_region_intersect(clipped_outer_region, &(canvas->outer_region), screen_region);
	else
		*clipped_outer_region = canvas->outer_region;

	/* Check the visibility of the inner region. Clip it if necessary.
	 * Also calculate the visible subset of the source region. */
	if (source_region == NULL)
	{
		actual_source_region.x1 = 0;
		actual_source_region.y1 = 0;
		actual_source_region.x2 = GST_VIDEO_INFO_WIDTH(info);
		actual_source_region.y2 = GST_VIDEO_INFO_HEIGHT(info);
	}
	else
	{
		actual_source_region = *source_region;
		g_assert(actual_source_region.x1 <= actual_source_region.x2);
		g_assert(actual_source_region.y1 <= actual_source_region.y2);
		g_assert(actual_source_region.x2 <= GST_VIDEO_INFO_WIDTH(info));
		g_assert(actual_source_region.y2 <= GST_VIDEO_INFO_HEIGHT(info));
	}

	switch (gst_imx_region_contains(&(canvas->inner_region), screen_region))
	{
		case GST_IMX_REGION_CONTAINS_FULL:
		{
			/* Inner region is fully visible. The entire source region is
			 * used for the blit operation. */
			*source_subset = actual_source_region;
			*clipped_inner_region = canvas->inner_region;
			canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER;
			break;
		}

		case GST_IMX_REGION_CONTAINS_PARTIAL:
		{
			gint src_w, src_h, inner_w, inner_h;

			/* Inner region is partially visible. Based on the intersection
			 * between the overall and inner region, compute the subset of
			 * the source region that shall be blitted. */

			GstImxRegion *full_inner_region = &(canvas->inner_region);
			gst_imx_region_intersect(clipped_inner_region, full_inner_region, screen_region);

			src_w = actual_source_region.x2 - actual_source_region.x1;
			src_h = actual_source_region.y2 - actual_source_region.y1;
			inner_w = full_inner_region->x2 - full_inner_region->x1;
			inner_h = full_inner_region->y2 - full_inner_region->y1;

			/* The source subset uses the same coordinate space as the source region,
			 * so the intersection region's offsets must be scaled appropriately,
			 * and the resulting coordinates must retain the original x/y offset. */
			source_subset->x1 = (clipped_inner_region->x1 - full_inner_region->x1) * src_w / inner_w + actual_source_region.x1;
			source_subset->y1 = (clipped_inner_region->y1 - full_inner_region->y1) * src_h / inner_h + actual_source_region.y1;
			source_subset->x2 = (clipped_inner_region->x2 - full_inner_region->x1) * src_w / inner_w + actual_source_region.x1;
			source_subset->y2 = (clipped_inner_region->y2 - full_inner_region->y1) * src_h / inner_h + actual_source_region.y1;

			canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER;
			break;
		}

		case GST_IMX_REGION_CONTAINS_NONE:
		{
			/* Inner region is not visible. Set its values to ensure
			 * the empty space computations below still work correctly.
			 * Derived classes are not supposed to do anything with
			 * the inner region's values anyway, since its visibility
			 * flag isn't set. */

			if (clipped_inner_region->x1 > screen_region->x2)
			{
				clipped_inner_region->x1 = screen_region->x2;
				clipped_inner_region->x2 = screen_region->x2;
			}
			else if (clipped_inner_region->x2 < screen_region->x1)
			{
				clipped_inner_region->x1 = screen_region->x1;
				clipped_inner_region->x2 = screen_region->x1;
			}

			if (clipped_inner_region->y1 > screen_region->y2)
			{
				clipped_inner_region->y1 = screen_region->y2;
				clipped_inner_region->y2 = screen_region->y2;
			}
			else if (clipped_inner_region->y2 < screen_region->y1)
			{
				clipped_inner_region->y1 = screen_region->y1;
				clipped_inner_region->y2 = screen_region->y1;
			}

			break;
		}
	}

	/* Next, compute the empty regions. Both outer and clipped_inner regions
	 * are guaranteed to be clipped at this point. */

	/* Compute the left empty region, and check if it is visible */
	if (clipped_inner_region->x1 > clipped_outer_region->x1)
	{
		GstImxRegion *empty_region = &(canvas->empty_regions[GST_IMX_CANVAS_EMPTY_REGION_INDEX_LEFT]);
		empty_region->x1 = clipped_outer_region->x1;
		empty_region->x2 = clipped_inner_region->x1;
		empty_region->y1 = clipped_inner_region->y1;
		empty_region->y2 = clipped_inner_region->y2;

		canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_LEFT;
	}

	/* Compute the right empty region, and check if it is visible */
	if (clipped_inner_region->x2 < clipped_outer_region->x2)
	{
		GstImxRegion *empty_region = &(canvas->empty_regions[GST_IMX_CANVAS_EMPTY_REGION_INDEX_RIGHT]);
		empty_region->x1 = clipped_inner_region->x2;
		empty_region->x2 = clipped_outer_region->x2;
		empty_region->y1 = clipped_inner_region->y1;
		empty_region->y2 = clipped_inner_region->y2;

		canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_RIGHT;
	}

	/* Compute the top empty region, and check if it is visible */
	if (clipped_inner_region->y1 > clipped_outer_region->y1)
	{
		GstImxRegion *empty_region = &(canvas->empty_regions[GST_IMX_CANVAS_EMPTY_REGION_INDEX_TOP]);
		empty_region->x1 = clipped_outer_region->x1;
		empty_region->x2 = clipped_outer_region->x2;
		empty_region->y1 = clipped_outer_region->y1;
		empty_region->y2 = clipped_inner_region->y1;

		canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_TOP;
	}

	/* Compute the bottom empty region, and check if it is visible */
	if (clipped_inner_region->y2 < clipped_outer_region->y2)
	{
		GstImxRegion *empty_region = &(canvas->empty_regions[GST_IMX_CANVAS_EMPTY_REGION_INDEX_BOTTOM]);
		empty_region->x1 = clipped_outer_region->x1;
		empty_region->x2 = clipped_outer_region->x2;
		empty_region->y1 = clipped_inner_region->y2;
		empty_region->y2 = clipped_outer_region->y2;

		canvas->visibility_mask |= GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_BOTTOM;
	}

	
}


