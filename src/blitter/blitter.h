/* base class for i.MX blitters
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


#ifndef GST_IMX_BLITTER_H
#define GST_IMX_BLITTER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "../common/canvas.h"


G_BEGIN_DECLS


typedef struct _GstImxBlitter GstImxBlitter;
typedef struct _GstImxBlitterClass GstImxBlitterClass;


#define GST_TYPE_IMX_BLITTER             (gst_imx_blitter_get_type())
#define GST_IMX_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BLITTER, GstImxBlitter))
#define GST_IMX_BLITTER_CAST(obj)        ((GstImxBlitter *)(obj))
#define GST_IMX_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BLITTER, GstImxBlitterClass))
#define GST_IMX_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_BLITTER, GstImxBlitterClass))
#define GST_IS_IMX_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BLITTER))
#define GST_IS_IMX_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BLITTER))


struct _GstImxBlitter
{
	GstObject parent;

	/* buffer pool used for temporary internal input frames
	 * (in case upstream doesn't deliver DMA buffers already) */
	GstBufferPool *dma_bufferpool;

	GstVideoInfo input_video_info;
};


/**
 * GstImxBlitterClass:
 *
 * The blitter base class implements operations common for various blitters
 * in the i.MX SoC. It handles fallbacks for input buffers that are not
 * physically contiguous, provides functions for creating buffer pools for
 * physically contiguous memory, and offers a high-level interface for
 * blit operations. Derived classes are informed about what the input and
 * output buffers are (both guaranteed to be physically contiguous), what
 * the input video info and the output region is, and then told to blit.
 * 90-degree step rotation is also handled by the base class.
 *
 * The blitters get as input a buffer containing a frame in physically
 * contiguous memory, and a region describing what subset of that frame is
 * to be blitted. The output consists of a physically contiguous frame and
 * a canvas with precalculated empty regions, inner region, and visibility
 * mask. Rotation is automatically handled during those internal calculations,
 * so the derived class does not have to care about computing any of these
 * regions. It can focus on the blitting itself.
 *
 * The derived class is expected to ref the input frame at least until a new
 * one is set via @gst_imx_blitter_set_input_frame. Same applies to output frames.
 * Derived classes are free to keep frames ref'd for as long as they need.
 * In practice, derived classes unref the old frame as soon as a new frame is passed,
 * and at most hold on to the old frame for deinterlacing purposes.
 * @blit may be called multiple times before the next frame is set, so the derived
 * class must ensure that the frames stay valid until the next ones are set, as
 * described above. This is for example used when the frame needs to be redrawn
 * while the pipeline is in PAUSED state.
 *
 * Derived classes must unref all frames though when @flush is called, and when
 * it shuts down. Furthermore, it is not recommended to keep a hold on frames unless
 * strictly necessary, since this wastes resources, and can in extreme cases lead to
 * deadlocks (if the buffers come from a fixed bufferpool which allows no additional
 * allocations on the fly).
 *
 *
 * @parent_class:           The parent class structure
 * @set_input_video_info:   Optional.
 *                          Called when @gst_imx_blitter_set_input_video_info is called.
 *                          This gives derived blitters the chance to update any internal state
 *                          related to the video info.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_output_video_info:  Optional.
 *                          Called when @gst_imx_blitter_set_output_video_info is called.
 *                          This gives derived blitters the chance to update any internal state
 *                          related to the video info.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_input_region:       Optional.
 *                          Defines what subset of the input frame shall be blitted.
 *                          A NULL region means the entire input frame shall be blitted.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_output_canvas:      Optional.
 *                          Defines where on the output frame the input pixels shall be blitted to.
 *                          The canvas must have valid visibility mask and (clipped) regions.
 *                          Use @gst_imx_canvas_calculate_inner_region and @gst_imx_canvas_clip
 *                          for this purpose.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_num_output_pages:   Optional.
 *                          If a blitter clears the empty regions only once, this information is useful,
 *                          since a num_output_pages larger than 1 means the caller will instruct the
 *                          blitter to blit to multiple output pages as part of a page flipping process.
 *                          The blitter therefore has to clear the empty regions once for each page.
 *                          Blitters which update the empty regions every time can ignore this.
 *                          A page count of 0 is invalid.
 *                          If this is not called, or if this vfunc is not defined, then the blitter
 *                          behaves as if the vfunc was called with a page count of 1.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_input_frame:        Required.
 *                          Sets the blitter's input frame. This may or may not be the frame set by
 *                          @gst_imx_blitter_set_input_frame . It depends on whether or not the
 *                          input buffer passed to this function is physically contiguous or not.
 *                          If it isn't, an internal copy is made to a DMA buffer, and that buffer
 *                          is passed on to this function, as the "input_frame" parameter.
 *                          This function must ref the frame and store it internally until the
 *                          blitter is flushed, shut down, or a new frame is set. (In the latter
 *                          case, it can hold on to the old frame even if a new frame is supplied;
 *                          this is useful for deinterlacing, for example.)
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @set_output_frame:       Required.
 *                          Sets the blitter's output frame. This function works just like
 *                          @set_input_frame, except that the output frame *must* be a physically
 *                          contiguous buffer (it does not do any internal copies, unlike
 *                          @set_input_frame).
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @get_phys_mem_allocator: Required.
 *                          Returns a GstAllocator which allocates physically contiguous memory.
 *                          Which allocator to use is up to the derived blitter.
 *                          The blitter base class unrefs the returned allocator when it is no
 *                          longer needed.
 *                          If something went wrong, it returns NULL.
 * @fill_region:            Required.
 *                          The color is specified in this format: 0xBBGGRR (the MSB are not used).
 *                          The region must be fully within the output frame.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @blit:                   Required.
 *                          Performs the actual blit operation.
 *                          Derived classes should consider calling this an error if the input and
 *                          output frames weren't both set before.
 *                          This call can be repeated multiple times, each one resulting in the same
 *                          blit operation. The output is not guaranteed to look the same though,
 *                          unless alpha is 255.
 *                          alpha is an alpha blending factor. 0 means completely translucent, 255
 *                          completely opaque.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @flush:                  Optional.
 *                          Flushes any internal cached or temporary states, buffers, ref'd frames etc.
 *                          This may be called repeatedly. If there is nothing to flush, this function
 *                          should do nothing. In particular, it is called when the blitter's dispose
 *                          function is invoked.
 */
struct _GstImxBlitterClass
{
	GstObjectClass parent_class;

	gboolean (*set_input_video_info)(GstImxBlitter *blitter, GstVideoInfo const *input_video_info);
	gboolean (*set_output_video_info)(GstImxBlitter *blitter, GstVideoInfo const *output_video_info);

	gboolean (*set_input_region)(GstImxBlitter *blitter, GstImxRegion const *input_region);
	gboolean (*set_output_canvas)(GstImxBlitter *blitter, GstImxCanvas const *output_canvas);
	gboolean (*set_num_output_pages)(GstImxBlitter *blitter, guint num_output_pages);

	gboolean (*set_input_frame)(GstImxBlitter *blitter, GstBuffer *frame);
	gboolean (*set_output_frame)(GstImxBlitter *blitter, GstBuffer *frame);

	GstAllocator* (*get_phys_mem_allocator)(GstImxBlitter *blitter);

	gboolean (*fill_region)(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color);
	gboolean (*blit)(GstImxBlitter *blitter, guint8 alpha);
	void (*flush)(GstImxBlitter *blitter);
};


GType gst_imx_blitter_get_type(void);


/* Sets the input video info.
 *
 * A copy of this video info is placed in the blitter's input_video_info member.
 * @set_input_video_info is called if this vfunc is defined.
 * Also, this cleans up the existing internal physically contiguous buffer pool;
 * a new one is created to handle the new video info in the next blit() call.
 * Returns TRUE if the input video info is the same, or if @set_input_video_info
 * returned TRUE, or if @set_input_video_info is NULL, and returns FALSE otherwise.
 */
gboolean gst_imx_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info);
/* Sets the output video info.
 *
 * @set_output_video_info is called if this vfunc is defined.
 * Returns TRUE if @set_input_video_info returned TRUE, or if @set_input_video_info
 * is NULL, and returns FALSE otherwise.
 */
gboolean gst_imx_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info);

/* Sets the input region.
 *
 * @set_input_region is called if this vfunc is defined.
 * Returns TRUE if @set_input_video_info returned TRUE, or if @set_input_video_info
 * is NULL, and returns FALSE otherwise.
 */
gboolean gst_imx_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region);
/* Sets the output canvas.
 *
 * @set_output_canvas is called if this vfunc is defined.
 * Returns TRUE if @set_output_canvas returned TRUE, or if @set_output_canvas
 * is NULL, and returns FALSE otherwise.
 */
gboolean gst_imx_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas);
/* Sets the number of output pages.
 *
 * @set_num_output_pages is called if this vfunc is defined.
 * Returns TRUE if @set_num_output_pages returned TRUE, or if @set_num_output_pages
 * is NULL, and returns FALSE otherwise.
 */
gboolean gst_imx_blitter_set_num_output_pages(GstImxBlitter *blitter, guint num_output_pages);

/* Sets the input frame.
 *
 * Internally, this performs a copy of the frame if said buffer isn't physically
 * contiguous before calling @set_input_frame. See the @set_input_frame vfunc
 * documentation for further details.
 * Returns TRUE if copying the frame succeeded (if a copy is necessary), and
 * if @set_input_frame succeeded, FALSE otherwise.
 */
gboolean gst_imx_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *frame);
/* Sets the output frame.
 *
 * Internally, this calls @set_output_frame, and returns its return value.
 * See the @set_output_frame vfunc documentation for further details.
 */
gboolean gst_imx_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *frame);

/* Creates a buffer pool for physically contiguous buffers.
 *
 * This function is intended both for internal use inside GstImxBlitter,
 * and for code that uses blitters and needs a buffer pool (usually for allocating
 * output buffers.
 * caps, size, min_buffers, max_buffers are passed to @gst_buffer_pool_config_set_params.
 * If allocator is NULL, an allocator is retrieved by using the class' @get_phys_mem_allocator
 * function.
 * Returns the newly created buffer pool. As with other buffer pools, use @gst_object_unref
 * to unref the buffer when it is no longer needed. At refcount 0, all of its memory is freed.
 * If creating the buffer pool failed, it returns NULL.
 */
GstBufferPool* gst_imx_blitter_create_bufferpool(GstImxBlitter *blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params);

/* Retrieves a physical memory allocator from the blitter.
 * This call increases the allocator's refcount.
 *
 * Return pointer to a physical memory allocator, or NULL if an error occurred.
 */
GstAllocator* gst_imx_blitter_get_phys_mem_allocator(GstImxBlitter *blitter);

/* Fills a region in the output frame with the given color.
 *
 * Internally, this calls the @fill_region vfunc, and returns its return value.
 * See its documentation for details.
 */
gboolean gst_imx_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color);
/* Performs the actual blit operation.
 *
 * Internally, this calls the @blit vfunc, and returns its return value.
 * See its documentation for details.
 */
gboolean gst_imx_blitter_blit(GstImxBlitter *blitter, guint8 alpha);
/* Flush any temporary and/or cached data in the blitter.
 *
 * Internally, this calls the @flush vfunc if defined. See its documentation for details.
 */
void gst_imx_blitter_flush(GstImxBlitter *blitter);


#endif
