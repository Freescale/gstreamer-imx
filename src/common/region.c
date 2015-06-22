#include "region.h"


inline static int sgn(int const val)
{
	return (0 < val) - (val < 0);
}


GstImxRegionContains gst_imx_region_contains(GstImxRegion const *first_region, GstImxRegion const *second_region)
{
	g_assert(first_region != NULL);
	g_assert(second_region != NULL);

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
		     ? GST_IMX_REGION_CONTAINS_FULL
		     : GST_IMX_REGION_CONTAINS_PARTIAL;
	}
	else
		return GST_IMX_REGION_CONTAINS_NONE;
}


gboolean gst_imx_region_equal(GstImxRegion const *first_region, GstImxRegion const *second_region)
{
	g_assert(first_region != NULL);
	g_assert(second_region != NULL);

	return (first_region->x1 == second_region->x1) &&
	       (first_region->y1 == second_region->y1) &&
	       (first_region->x2 == second_region->x2) &&
	       (first_region->y2 == second_region->y2);
}


void gst_imx_region_intersect(GstImxRegion *intersection, GstImxRegion const *first_region, GstImxRegion const *second_region)
{
	g_assert(intersection != NULL);
	g_assert(first_region != NULL);
	g_assert(second_region != NULL);

	intersection->x1 = MAX(first_region->x1, second_region->x1);
	intersection->y1 = MAX(first_region->y1, second_region->y1);
	intersection->x2 = MIN(first_region->x2, second_region->x2);
	intersection->y2 = MIN(first_region->y2, second_region->y2);
}


void gst_imx_region_merge(GstImxRegion *merged_region, GstImxRegion const *first_region, GstImxRegion const *second_region)
{
	g_assert(merged_region != NULL);
	g_assert(first_region != NULL);
	g_assert(second_region != NULL);

	merged_region->x1 = MIN(first_region->x1, second_region->x1);
	merged_region->y1 = MIN(first_region->y1, second_region->y1);
	merged_region->x2 = MAX(first_region->x2, second_region->x2);
	merged_region->y2 = MAX(first_region->y2, second_region->y2);
}


void gst_imx_region_calculate_inner_region(GstImxRegion *inner_region, GstImxRegion const *outer_region, GstVideoInfo const *info, gboolean transposed, gboolean keep_aspect_ratio)
{
	guint display_ratio_n, display_ratio_d;
	guint video_width, video_height;

	g_assert(inner_region != NULL);
	g_assert(outer_region != NULL);
	g_assert(info != NULL);

	/* Calculate aspect ratio factors if required */
	if (keep_aspect_ratio)
	{
		guint video_par_n, video_par_d, window_par_n, window_par_d;

		video_width = GST_VIDEO_INFO_WIDTH(info);
		video_height = GST_VIDEO_INFO_HEIGHT(info);
		video_par_n = GST_VIDEO_INFO_PAR_N(info);
		video_par_d = GST_VIDEO_INFO_PAR_D(info);
		window_par_n = 1;
		window_par_d = 1;

		if ((video_width == 0) || (video_height == 0))
		{
			/* Turn off aspect ratio calculation if either width
			 * or height is 0 */
			keep_aspect_ratio = FALSE;
		}
		else
		{
			if (!gst_video_calculate_display_ratio(
				&display_ratio_n, &display_ratio_d,
				video_width, video_height,
				video_par_n, video_par_d,
				window_par_n, window_par_d
			))
			{
				keep_aspect_ratio = FALSE;
			}
			else if (transposed)
			{
				guint d = display_ratio_d;
				display_ratio_d = display_ratio_n;
				display_ratio_n = d;
			}
		}
	}

	/* Calculate the inner region, either with or without
	 * aspect ratio correction (in the latter case, the inner
	 * and outer regions are identical) */
	if (keep_aspect_ratio)
	{
		guint ratio_factor;
		guint outw, outh, innerw, innerh;

		/* Fit the inner region in the outer one, keeping display ratio
		 * This means that either its width or its height will be set to the
		 * outer region's width/height, and the other length will be shorter,
		 * scaled accordingly to retain the display ratio
		 *
		 * Setting dn = display_ratio_n , dd = display_ratio_d ,
		 * outw = outer_region_width , outh = outer_region_height ,
		 * we can identify cases:
		 *
		 * (1) Inner region fits in outer one with its width maximized
		 *     In this case, this holds: outw/outh < dn/dd
		 * (1) Inner region fits in outer one with its height maximized
		 *     In this case, this holds: outw/outh > dn/dd
		 *
		 * To simplify comparison, the inequality outw/outh > dn/dd is
		 * transformed to: outw*dd/outh > dn
		 * outw*dd/outh is the ratio_factor
		 */

		outw = outer_region->x2 - outer_region->x1;
		outh = outer_region->y2 - outer_region->y1;

		ratio_factor = (guint)gst_util_uint64_scale_int(outw, display_ratio_d, outh);

		if (ratio_factor >= display_ratio_n)
		{
			innerw = (guint)gst_util_uint64_scale_int(outh, display_ratio_n, display_ratio_d);
			innerh = outh;
		}
		else
		{
			innerw = outw;
			innerh = (guint)gst_util_uint64_scale_int(outw, display_ratio_d, display_ratio_n);
		}

		/* Safeguard to ensure width/height aren't out of bounds
		 * (should not happen, but better safe than sorry) */
		innerw = MIN(innerw, outw);
		innerh = MIN(innerh, outh);

		inner_region->x1 = outer_region->x1 + (outw - innerw) / 2;
		inner_region->y1 = outer_region->y1 + (outh - innerh) / 2;
		inner_region->x2 = inner_region->x1 + innerw;
		inner_region->y2 = inner_region->y1 + innerh;
	}
	else
	{
		*inner_region = *outer_region;
	}
}
