#ifndef GST_IMX_COMMON_CANVAS_H
#define GST_IMX_COMMON_CANVAS_H

#include <gst/gst.h>
#include <gst/video/video.h>

#include "region.h"


G_BEGIN_DECLS


typedef struct _GstImxCanvas GstImxCanvas;


typedef enum
{
	GST_IMX_CANVAS_EMPTY_REGION_INDEX_TOP = 0,
	GST_IMX_CANVAS_EMPTY_REGION_INDEX_BOTTOM = 1,
	GST_IMX_CANVAS_EMPTY_REGION_INDEX_LEFT = 2,
	GST_IMX_CANVAS_EMPTY_REGION_INDEX_RIGHT = 3
}
GstImxCanvasEmptyRegionIndices;


/**
 * GstImxCanvasVisibilityFlags:
 *
 * Flags identifying a visible region within a canvas. Used for the visibility
 * bitmask in the canvas to check if a region is visible.
 *
 * The empty regions are guaranteed to start at bit 0. It is therefore valid
 * to go over all empty regions simply by using (1 << i) in a loop, where i starts
 * at 0, and ends at 3.
 */
typedef enum
{
	GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_TOP    = (1 << GST_IMX_CANVAS_EMPTY_REGION_INDEX_TOP),
	GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_BOTTOM = (1 << GST_IMX_CANVAS_EMPTY_REGION_INDEX_BOTTOM),
	GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_LEFT   = (1 << GST_IMX_CANVAS_EMPTY_REGION_INDEX_LEFT),
	GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_EMPTY_RIGHT  = (1 << GST_IMX_CANVAS_EMPTY_REGION_INDEX_RIGHT),
	GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER = (1 << 4)
}
GstImxCanvasVisibilityFlags;


/**
 * GstImxCanvasInnerRotation:
 *
 * Modes for rotating blitter output, in 90-degree steps, and for horizontal/vertical flipping.
 */
typedef enum
{
	GST_IMX_CANVAS_INNER_ROTATION_NONE,
	GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES,
	GST_IMX_CANVAS_INNER_ROTATION_180_DEGREES,
	GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES,
	GST_IMX_CANVAS_INNER_ROTATION_HFLIP,
	GST_IMX_CANVAS_INNER_ROTATION_VFLIP,
	GST_IMX_CANVAS_INNER_ROTATION_UL_LR,
	GST_IMX_CANVAS_INNER_ROTATION_UR_LL
}
GstImxCanvasInnerRotation;


/**
 * GstImxCanvas:
 *
 * Rectangular space containing multiple regions.
 * The outer region contains all the other ones fully. Any pixels that
 * lie in the outer but not the inner region is in one of the empty
 * regions. Blitters are supposed to paint the empty regions with the
 * fill_color, which is a 32-bit RGBA tuple, in format: 0xAABBGGRR.
 * The inner region contains the actual video frame.
 * The visibility mask describes what regions are visible.
 * The margin values determine margin sizes in pixels between inner and
 * outer region. The margin is applied prior to the computation of the
 * inner region.
 */
struct _GstImxCanvas
{
	GstImxRegion outer_region;
	guint32 fill_color;
	guint margin_left, margin_top, margin_right, margin_bottom;
	gboolean keep_aspect_ratio;
	GstImxCanvasInnerRotation inner_rotation;

	/* these are computed by calculate_inner_region() */
	GstImxRegion inner_region;

	/* these are computed by clip() */
	GstImxRegion clipped_outer_region;
	GstImxRegion clipped_inner_region;
	GstImxRegion empty_regions[4];
	guint8 visibility_mask;
};


GType gst_imx_canvas_inner_rotation_get_type(void);


/* Determines if the given rotation mode would transpose the frame.
 *
 * Here, transposing refers to swapping X and Y axes.
 *
 * @param rotation Rotation mode to check
 * @return true if the rotation mode is 90 or 270 degrees, false otherwise
 */
gboolean gst_imx_canvas_does_rotation_transpose(GstImxCanvasInnerRotation rotation);
/* Given a canvas, calculate its inner region.
 *
 * Internally, this makes a copy of the outer region, shrinks it by the
 * defined margin, and then calls gst_imx_region_calculate_inner_region().
 *
 * It does not fill the empty region fields.
 *
 * @param canvas canvas with filled outer_region, keep_aspect_ratio, margin,
 *               and inner_rotation fields
 * @param info input video information
 */
void gst_imx_canvas_calculate_inner_region(GstImxCanvas *canvas, GstVideoInfo const *info);
/* Given a canvas, calculate its clipped region, and empty region fields.
 *
 * This function clips both inner and outer region against screen_region, defines the
 * empty regions, and sets the visibility_mask. This is useful for determining which
 * parts of the canvas are actually visible.
 *
 * The canvas will be updated by writing to the visibility_mask, empty region,
 * inner region, and clipped region fields. The other fields are left unmodified.
 *
 * It requires the canvas' outer_region, inner_region, keep_aspect_ratio, margin, and
 * inner_rotation fields to be set. Using gst_imx_canvas_calculate_inner_region() to
 * compute the inner_region field is recommended.
 *
 * Also, it determines which parts of the source video are visible, and passes this
 * region to source_subset. This way, all a blitter has to do is to copy pixels
 * from the source_subset on the input video frames to the clipped-inner_region on
 * the output frame, and then fill the empty regions on the output frame with a solid
 * color. All of the required canvas/region calculations are done in this function.
 * source_subset is a subset of the "source region", which is either the entire
 * input frame is source_region is NULL, or exactly the region described by
 * source_region.
 *
 * @param canvas canvas to clip
 * @param screen_region region on the output frame representing the screen (or the
 *        subset of the screen where painting shall occur)
 * @param info input video information
 * @param source_region region on the input video frame which is what can maximally
 *        be blitted from that frame. If this is NULL, the source region contains
 *        the entire frame. Otherwise, this region must be fully contained within the
 *        input video frame. This is useful for cropping.
 * @param source_subset region value which will be filled with values describing a
 *        subset of the source region which will be visible on the output frame
 */
void gst_imx_canvas_clip(GstImxCanvas *canvas, GstImxRegion const *screen_region, GstVideoInfo const *info, GstImxRegion const *source_region, GstImxRegion *source_subset);


G_END_DECLS


#endif
