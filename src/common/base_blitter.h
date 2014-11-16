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


#ifndef GST_IMX_COMMON_BASE_BLITTER_H
#define GST_IMX_COMMON_BASE_BLITTER_H

#include <gst/gst.h>
#include <gst/video/video.h>

#include "phys_mem_allocator.h"


G_BEGIN_DECLS


typedef struct _GstImxBaseBlitter GstImxBaseBlitter;
typedef struct _GstImxBaseBlitterRegion GstImxBaseBlitterRegion;
typedef struct _GstImxBaseBlitterClass GstImxBaseBlitterClass;
typedef struct _GstImxBaseBlitterPrivate GstImxBaseBlitterPrivate;


#define GST_TYPE_IMX_BASE_BLITTER             (gst_imx_base_blitter_get_type())
#define GST_IMX_BASE_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BASE_BLITTER, GstImxBaseBlitter))
#define GST_IMX_BASE_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BASE_BLITTER, GstImxBaseBlitterClass))
#define GST_IMX_BASE_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_BASE_BLITTER, GstImxBaseBlitterClass))
#define GST_IS_IMX_BASE_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BASE_BLITTER))
#define GST_IS_IMX_BASE_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BASE_BLITTER))

#define GST_IMX_BASE_BLITTER_VIDEO_VISIBILITY_TYPE(obj)  (((GstImxBaseBlitter *)obj)->video_visibility_type)
#define GST_IMX_BASE_BLITTER_OUTPUT_VISIBILITY_TYPE(obj) (((GstImxBaseBlitter *)obj)->output_visibility_type)


#define GST_IMX_BASE_BLITTER_CROP_DEFAULT  FALSE


/**
 * The blitter base class implements operations common for various blitters
 * in the i.MX SoC. It handles fallbacks for input buffers that are not
 * physically contiguous, provides functions for creating buffer pools for
 * physically contiguous memory, and offers a high-level interface for
 * blit operations. Derived classes are informed about what the input and
 * output buffers are (both guaranteed to be physically contiguous), what
 * the input video info and the output region is, and then told to blit.
 */


/**
 * GstImxBaseBlitterRegion:
 *
 * Rectangular region. (x1,y1) describes its top left, (x2,y2) its bottom
 * right coordinates. (x2,y2) are right outside of the rectangle pixels,
 * meaning that for example a rectangle with top left coordinates (10,20)
 * and width 400 and height 300 has bottom right coordinates (410,320).
 */
struct _GstImxBaseBlitterRegion
{
	gint x1, y1, x2, y2;
};


/**
 * GstImxBaseBlitterVisibilityType:
 *
 * Indicates the visibility of a region.
 */
typedef enum
{
	GST_IMX_BASE_BLITTER_VISIBILITY_NONE,
	GST_IMX_BASE_BLITTER_VISIBILITY_PARTIAL,
	GST_IMX_BASE_BLITTER_VISIBILITY_FULL
}
GstImxBaseBlitterVisibilityType;


/**
 * GstImxBaseBlitter:
 *
 * The opaque #GstImxBaseBlitter data structure.
 */
struct _GstImxBaseBlitter
{
	GstObject parent;

	/*< protected >*/

	/* Internal buffer pool and video frame for temporary storage
	 * of frames in a DMA buffer. This is needed if upstream delivers
	 * frames in buffers which are not physically contiguous. Since
	 * derived blitters expect physically contiguous memory, the frame
	 * is copied to the internal_input_frame, which is then passed on
	 * to the derived blitter. If upstream delivers DMA buffers, then
	 * these are passed on directly, since they by definition are
	 * physically contiguous.
	 * This buffer pool is created internally by using
	 * @gst_imx_base_blitter_create_bufferpool . */
	GstBufferPool *internal_bufferpool;
	GstBuffer *internal_input_frame;

	/* Internal copy of the latest video info for the incoming video data. */
	GstVideoInfo input_video_info;

	/* TRUE if GstVideoCropMeta data in the input frame shall be applied */
	gboolean apply_crop_metadata;

	/* Regions and flags to determine the actually visible portion of the
	 * input and output frames (which might be a subset if the frame is
	 * placed partially outside of the output buffer). */
	GstImxBaseBlitterRegion full_input_region, visible_input_region, output_buffer_region, full_video_region, visible_video_region;
	gboolean visible_input_region_uptodate;
	GstImxBaseBlitterVisibilityType video_visibility_type, output_visibility_type;
};


/**
 * GstImxBaseBlitterClass
 * @parent_class:           The parent class structure
 * @set_input_video_info:   Optional.
 *                          Called when @gst_imx_base_blitter_set_input_video_info is called.
 *                          This gives derived blitters the chance to update any internal state
 *                          related to the video info.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_input_frame:        Required.
 *                          Sets the blitter's input frame. This may or may not be the frame set by
 *                          @gst_imx_base_blitter_set_input_buffer . It depends on whether or not
 *                          the input buffer passed to this function is physically contiguous or not.
 *                          If it isn't, an internal copy is made to a DMA buffer, and that buffer
 *                          is passed on to this function, as the "input_frame" parameter.
 *                          If the blitter needs this input frame for later (for example, to apply
 *                          deinterlacing), it should ref the frame buffer, since there is no
 *                          guarantee that the input frame will remain valid until the next
 *                          @gst_imx_base_blitter_blit call. This is especially true if the internal
 *                          copy mentioned above is used. In short: the blitter should always assume
 *                          that the frames are invalid after @blit_frame was called.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_output_frame:       Required.
 *                          Sets the blitter's output frame. The output frame must be a physically
 *                          contiguous buffer.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_output_regions:     Optional.
 *                          Sets the blitter's output and video regions in the output framebuffer.
 *                          Called by @gst_imx_base_blitter_set_output_regions. See this
 *                          function's documentation for details. Unlike that function though,
 *                          this vfunc's video_region and output_region parameters are never NULL.
 * @get_phys_mem_allocator: Required.
 *                          Returns a GstAllocator which allocates physically contiguous memory.
 *                          Which allocator to use is up to the derived blitter.
 *                          The base blitter class unrefs the returned allocator when it is no
 *                          longer needed.
 *                          If something went wrong, it returns NULL.
 * @blit_frame:             Required.
 *                          Performs the actual blit operation.
 *                          Note that after this is executed, the input & output frames might not
 *                          be set to valid values anymore. See @set_input_frame for more details.
 *                          If the video region is set set to a subset of the output region, this
 *                          function must ensure the pixels outside of the video- but inside of
 *                          the output region are set to black.
 *                          The "input_region" parameter contains what region inside the input frame
 *                          to actually blit. This might be a subset of the frame if cropping is
 *                          applied and/or if the frame is blit partially outside of the visible area.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @flush:                  Optional.
 *                          Flushes any internal cached or temporary states, buffers etc.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 */
struct _GstImxBaseBlitterClass
{
	GstObjectClass parent_class;

	gboolean (*set_input_video_info)(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info);
	gboolean (*set_input_frame)(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame);
	gboolean (*set_output_frame)(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame);
	gboolean (*set_output_regions)(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);
	GstAllocator* (*get_phys_mem_allocator)(GstImxBaseBlitter *base_blitter);
	gboolean (*blit_frame)(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *input_region);
	gboolean (*flush)(GstImxBaseBlitter *base_blitter);
};


GType gst_imx_base_blitter_get_type(void);


/* Sets the input buffer.
 *
 * Internally, this performs a copy of the frame in the input_buffer if
 * said buffer isn't physically contiguous before calling @set_input_frame.
 * See the @set_input_framefor for further details.
 * Returns TRUE if copying the frame succeeded (if a copy is necessary), and
 * if @set_input_frame succeeded, FALSE otherwise.
 */
gboolean gst_imx_base_blitter_set_input_buffer(GstImxBaseBlitter *base_blitter, GstBuffer *input_buffer);

/* Sets the output buffer.
 *
 * Internally, this checks whether or not the output buffer is physically contiguous,
 * and calls @set_output_frame, returning its return value.
 */
gboolean gst_imx_base_blitter_set_output_buffer(GstImxBaseBlitter *base_blitter, GstBuffer *output_buffer);

/* Sets the video and output region for the blit operation.
 *
 * The output region is a rectangular subset of the output buffer which will be written to
 * by the blitter. The video region is a rectangular subset of the output region, and
 * contains the actual video frame. Pixels inside the output- but outside the video region
 * are painted black. Pixels outside the output region are left untouched.
 *
 * If output_region is NULL, the entire output buffer is assumed to be the output region.
 * If video_region is NULL, then the video region is equal to the output region.
 *
 * This must be called after @gst_imx_base_blitter_set_output_buffer and before
 * @gst_imx_base_blitter_blit. If @gst_imx_base_blitter_set_output_buffer is called,
 * any regions previously defined by this function are no longer valid. Note that derived
 * blitters are free ignore this call, and define the regions on their own.
 *
 * If @set_output_regions is NULL, this function does nothing.
 *
 * Return TRUE is setting the regions completed successfully (or if @set_output_regions is NULL),
 * FALSE otherwise.
 */
gboolean gst_imx_base_blitter_set_output_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);

/* Calculates empty regions.
 *
 * Empty regions are those with pixels that lie inside the output but outside the video region.
 * Blitters must fill these regions with black pixels. This utility function takes care of
 * calculating these empty regions. empty_regions must point to an array of four
 * GstImxBaseBlitterRegion instances. num_defined_regions gets filled with the number of actually
 * computed regions (this number is always <= 4). A derived blitter can then fill the
 * first N regions described the empty_region array with black pixels (N = num_defined_regions).
 *
 * video_region can be NULL. In that case, the video region is assumed to encompass the
 * entire output region, which means there are no empty regions. num_defined_regions is set to 0
 * in that case. The other parameters must not be NULL.
 */
void gst_imx_base_blitter_calculate_empty_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion *empty_regions, guint *num_defined_regions, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);

/* Sets the input video info.
 *
 * A copy of this video info is placed in the base_blitter's input_video_info member.
 * @set_input_video_info is called if this vfunc is defined.
 *
 * Return TRUE is @set_input_video_info completed successfully (or if @set_input_video_info is NULL),
 * FALSE otherwise.
 */
gboolean gst_imx_base_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info);

/* Performs the actual blit operation.
 *
 * Internally, this calls @blit_frame, returning its return value.
 */
gboolean gst_imx_base_blitter_blit(GstImxBaseBlitter *base_blitter);

/* Flush any temporary and/or cached data in the blitter.
 *
 * Return TRUE is @flush completed successfully (or if @flush is NULL), FALSE otherwise.
 */
gboolean gst_imx_base_blitter_flush(GstImxBaseBlitter *base_blitter);

/* Creates a buffer pool for physically contiguous buffers.
 *
 * This function is intended both for internal use inside GstImxBaseBlitter,
 * and for code that uses blitters and needs a buffer pool (usually for allocating
 * output buffers.
 * caps, size, min_buffers, max_buffers are passed to @gst_buffer_pool_config_set_params.
 * If allocator is NULL, an allocator is created, by using the class' @get_phys_mem_allocator
 * function.
 * Returns the newly created buffer pool. As with other buffer pools, use @gst_object_unref
 * to unref the buffer when it is no longer needed. At refcount 0, all of its memory is freed.
 * If creating the buffer pool failed, it returns NULL.
 */
GstBufferPool* gst_imx_base_blitter_create_bufferpool(GstImxBaseBlitter *base_blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params);

/* Gets a new physical memory allocator from the blitter.
 *
 * Return pointer to newly created allocator, or NULL if an error occurred.
 */
GstAllocator* gst_imx_base_blitter_get_phys_mem_allocator(GstImxBaseBlitter *base_blitter);

/* Enable input frame cropping based on GstVideoCropMeta information.
 *
 * Internally, this adjusts the input region to the crop region, thus making sure the blitter uses
 * the crop region (or a visible subset of the crop region in case the output region is partially
 * obscured) when blitting.
 */
void gst_imx_base_blitter_enable_crop(GstImxBaseBlitter *base_blitter, gboolean crop);
/* Return the current cropping state.
 *
 * Return TRUE if input cropping is enabled, FALSE otherwise.
 */
gboolean gst_imx_base_blitter_is_crop_enabled(GstImxBaseBlitter *base_blitter);


G_END_DECLS


#endif
