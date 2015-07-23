#ifndef GST_IMX_COMMON_REGION_H
#define GST_IMX_COMMON_REGION_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


typedef struct _GstImxRegion GstImxRegion;


/**
 * GstImxRegionContains:
 *
 * To what degree one region contains another.
 */
typedef enum
{
	GST_IMX_REGION_CONTAINS_NONE = 0,
	GST_IMX_REGION_CONTAINS_PARTIAL,
	GST_IMX_REGION_CONTAINS_FULL
}
GstImxRegionContains;


/**
 * GstImxRegion:
 *
 * Rectangular region. (x1,y1) describes its top left, (x2,y2) its bottom
 * right coordinates. (x2,y2) are right outside of the rectangle pixels,
 * meaning that for example a rectangle with top left coordinates (10,20)
 * and width 400 and height 300 has bottom right coordinates (410,320).
 */
struct _GstImxRegion
{
	gint x1, y1, x2, y2;
};


/* Helper macros for using printf-style format strings to output region coordinates */
#define GST_IMX_REGION_FORMAT "d,%d-%d,%d"
#define GST_IMX_REGION_ARGS(region) (region)->x1,(region)->y1,(region)->x2,(region)->y2



/* Checks if (and to what degree) second_region contains first_region.
 *
 * Regions can be contains fully, partially, or not at all.
 * "first_region" and "second_region" must be non-NULL.
 *
 * @param first_region region that may be contained in second_region
 * @param second_region that may contain first_region
 * @return Degree of containment
 */
GstImxRegionContains gst_imx_region_contains(GstImxRegion const *first_region, GstImxRegion const *second_region);

/* Checks if two regions are equal.
 *
 * "first_region" and "second_region" must be non-NULL.
 *
 * @param first_region region to compare
 * @param second_region region to compare
 * @return TRUE if both regions are equal (= have the same coordinates)
 */
gboolean gst_imx_region_equal(GstImxRegion const *first_region, GstImxRegion const *second_region);

/* Calculates the intersection of two regions. The result is a region
 * that encompasses the subset of the two regions that is contained
 * in both.
 * "first_region", "second_region", and "intersection" must be non-NULL.
 *
 * If one region fully contains the other, then the resulting region
 * equals the fully contained region. If the regions do not intersect
 * at all, the result is undefined.
 *
 * @param intersection pointer to region structure that will be filled
 *                     with the parameters for the intersection
 * @param first_region first region to intersect
 * @param second_region second region to intersect
 */
void gst_imx_region_intersect(GstImxRegion *intersection, GstImxRegion const *first_region, GstImxRegion const *second_region);

/* Calculates the merge of two regions. The result is a region
 * that encompasses both regions.
 * "first_region", "second_region", and "intersection" must be non-NULL.
 *
 * If one region fully contains the other, then the resulting region
 * equals the fully contained region.
 *
 * @param merged_region pointer to region structure that will be filled
 *                     with the parameters for the merge
 * @param first_region first region to merge
 * @param second_region second region to merge
 */
void gst_imx_region_merge(GstImxRegion *merged_region, GstImxRegion const *first_region, GstImxRegion const *second_region);

/* Given an outer region and information about the video and the aspect ratio, calculate a suitable inner region.
 *
 * The inner region is always either equal to or a subset of the outer region;
 * in other words, it never exceeds the boundaries of the outer region.
 * If "keep_aspect_ratio" is FALSE, inner_region will always equal outer_region.
 * Otherwise, inner_region may be a subset if the aspect ratio information
 * provided by "info" requires constraining the inner region. For example, if
 * the outer region is a 1000x400 rectangle, the video uses 600x600 frames,
 * and the aspect ratio is 1:1, then the inner region will be 400x400, and will
 * be centered in the outer region.
 *
 * If the video output will be transposed (that is, 90 or 270 degree rotated),
 * then transposed should be set to TRUE, otherwise to FALSE.
 *
 * All pointer arguments must be non-NULL.
 *
 * @param inner_region pointer to structure that will be filled with
 *                     the parameters for the inner region
 * @param outer_region outer region which encompasses all the other
 *                     regions (including the inner one)
 * @param info video information
 * @param transposed whether or not the output is 90- or 270-degree rotated
 * @param keep_aspect_ratio whether or not to keep the video aspect ratio
 *                          defined by "info"
 */
void gst_imx_region_calculate_inner_region(GstImxRegion *inner_region, GstImxRegion const *outer_region, GstVideoInfo const *info, gboolean transposed, gboolean keep_aspect_ratio);


G_END_DECLS


#endif
