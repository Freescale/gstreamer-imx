/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#include <gst/gst.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideooverlayhandler.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_overlay_handler_debug);
#define GST_CAT_DEFAULT imx_2d_video_overlay_handler_debug


/* Structure for cached overlay data. A cached overlay is understood
 * to be "populated" when buffer is non-NULL. */
typedef struct
{
	GstBuffer *buffer;
	Imx2dSurface *surface;
}
CachedOverlay;


struct _GstImx2dVideoOverlayHandler
{
	GstObject parent;

	/* Uploader used when the input frame can directly be used for overlays. */
	GstImxDmaBufferUploader *uploader;
	/* The allocator retrieved from the uploader. This is used
	 * when a new buffer has to be created for a frame copy. */
	GstAllocator *dma_buffer_allocator;

	Imx2dBlitter *blitter;
	Imx2dHardwareCapabilities const *blitter_capabilities;

	/* Reference to the last composition we saw. This ensures that this
	 * composition cannot be altered (in-place modifications cannot happen
	 * in mini objects if the refcount is >1). Also, this allows us to
	 * compare compositions in newer buffers with older compositions to
	 * detect if said composition really is a new different one. If it
	 * is, we have to repopulate the overlay cache. */
	GstVideoOverlayComposition *previous_composition;

	/* The overlay cache. This is an array of CachedOverlay structures.
	 * It is populated by gst_imx_2d_video_overlay_handler_cache_buffers()
	 * and cleared by gst_imx_2d_video_overlay_handler_clear_cached_overlays_full().
	 * To reduce the amount of reallocations, there are two quantities
	 * associated with the cache: the total number of cached overlays
	 * and the number of actually populated cached overlays. When the
	 * cache is cleared, any non-NULL gstbuffers are unref'd. In some
	 * cases though the cached overlay structures and surfaces are kept
	 * around and later reused. In such cases, num_populated_cached_overlays
	 * is set to 0 (since due to unref'ing all the gstbuffers, none of
	 * the cached overlays are populated anymore), but total_num_cached_overlays
	 * remains unchanged. When done a full cache clearing though, the
	 * surfaces and cached_overlays array are also deallocated, and
	 * total_num_cached_overlays is also set to 0. */
	CachedOverlay *cached_overlays;
	guint num_populated_cached_overlays;
	guint total_num_cached_overlays;
};


struct _GstImx2dVideoOverlayHandlerClass
{
	GstObjectClass parent_class;
};


G_DEFINE_TYPE(GstImx2dVideoOverlayHandler, gst_imx_2d_video_overlay_handler, GST_TYPE_OBJECT)


static void gst_imx_2d_video_overlay_handler_dispose(GObject *object);

static gboolean gst_imx_2d_video_overlay_handler_cache_buffers(GstImx2dVideoOverlayHandler *self, GstVideoOverlayComposition *new_composition);
static void gst_imx_2d_video_overlay_handler_clear_cached_overlays_full(GstImx2dVideoOverlayHandler *video_overlay_handler, gboolean do_full_clearing);


static void gst_imx_2d_video_overlay_handler_class_init(GstImx2dVideoOverlayHandlerClass *klass)
{
	GObjectClass *object_class;

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_overlay_handler_debug, "imx2dvideooverlayhandler", 0, "NXP i.MX 2D video overlay handler");

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_2d_video_overlay_handler_dispose);
}


static void gst_imx_2d_video_overlay_handler_init(GstImx2dVideoOverlayHandler *self)
{
	self->cached_overlays = NULL;
	self->num_populated_cached_overlays = 0;
	self->total_num_cached_overlays = 0;
}


static void gst_imx_2d_video_overlay_handler_dispose(GObject *object)
{
	GstImx2dVideoOverlayHandler *self = GST_IMX_2D_VIDEO_OVERLAY_HANDLER(object);

	/* Clear the cached overlays entirely and free all structures.
	 * (Normally, only parts are cleared, since the structures may be reused.) */
	gst_imx_2d_video_overlay_handler_clear_cached_overlays_full(self, TRUE);

	/* Unref the allocator here since the gst_imx_dma_buffer_uploader_get_allocator()
	 * in gst_imx_2d_video_overlay_handler_new() refs it. */
	if (self->dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->dma_buffer_allocator));
		self->dma_buffer_allocator = NULL;
	}

	if (self->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(self->uploader));
		self->uploader = NULL;
	}

	G_OBJECT_CLASS(gst_imx_2d_video_overlay_handler_parent_class)->dispose(object);
}


GstImx2dVideoOverlayHandler* gst_imx_2d_video_overlay_handler_new(GstImxDmaBufferUploader *uploader, Imx2dBlitter *blitter)
{
	GstImx2dVideoOverlayHandler *video_overlay_handler;

	g_assert(uploader != NULL);
	g_assert(blitter != NULL);

	video_overlay_handler = g_object_new(gst_imx_2d_video_overlay_handler_get_type(), NULL);
	video_overlay_handler->uploader = gst_object_ref(uploader);
	video_overlay_handler->dma_buffer_allocator = gst_imx_dma_buffer_uploader_get_allocator(uploader);
	video_overlay_handler->blitter = blitter;
	video_overlay_handler->blitter_capabilities = imx_2d_blitter_get_hardware_capabilities(blitter);
	g_assert(video_overlay_handler->blitter_capabilities != NULL);

	return video_overlay_handler;
}


void gst_imx_2d_video_overlay_handler_clear_cached_overlays(GstImx2dVideoOverlayHandler *video_overlay_handler)
{
	/* Clear the overlay gstbuffers, but retain the surface and CachedOverlay structures,
	 * since we may need those again when new composition data comes in. */
	gst_imx_2d_video_overlay_handler_clear_cached_overlays_full(video_overlay_handler, FALSE);
}


gboolean gst_imx_2d_video_overlay_handler_render(GstImx2dVideoOverlayHandler *video_overlay_handler, GstBuffer *buffer)
{
	gboolean retval = TRUE;
	guint rectangle_idx, num_rectangles;
	GstVideoOverlayCompositionMeta *composition_meta;
	GstVideoOverlayComposition *composition;
	Imx2dBlitParams blit_params;
	Imx2dRegion dest_region;

	g_assert(video_overlay_handler != NULL);
	g_assert(buffer != NULL);


	/* Prerequisites. */

	composition_meta = gst_buffer_get_video_overlay_composition_meta(buffer);
	if (composition_meta == NULL)
	{
		GST_LOG_OBJECT(video_overlay_handler, "buffer has no composition meta; nothing to render; skipping buffer");
		goto finish;
	}

	composition = composition_meta->overlay;
	if (G_UNLIKELY(composition == NULL))
	{
		GST_WARNING_OBJECT(video_overlay_handler, "buffer has composition meta but no actual composition; cannot render anything; skipping");
		goto finish;
	}

	num_rectangles = gst_video_overlay_composition_n_rectangles(composition);
	if (G_UNLIKELY(num_rectangles == 0))
	{
		GST_DEBUG_OBJECT(video_overlay_handler, "buffer has composition meta but no overlay rectangles; cannot render anything; skipping");
		goto finish;
	}


	/* Check whether or not the composition changed.
	 * If so, we have to repopulate the cache. */

	if (composition != video_overlay_handler->previous_composition)
	{
		if (!gst_imx_2d_video_overlay_handler_cache_buffers(video_overlay_handler, composition))
			goto error;
	}


	/* Now we can draw the cached overlays onto the frame. */

	memset(&blit_params, 0, sizeof(blit_params));

	GST_LOG_OBJECT(video_overlay_handler, "rendering %u overlay rectangle(s)", num_rectangles);

	for (rectangle_idx = 0; rectangle_idx < num_rectangles; ++rectangle_idx)
	{
		GstVideoOverlayRectangle *rectangle = gst_video_overlay_composition_get_rectangle(composition, rectangle_idx);
		CachedOverlay *cached_overlay = &(video_overlay_handler->cached_overlays[rectangle_idx]);
		gint x, y;
		guint w, h;
		gboolean rect_ret;
		gfloat alpha;
		int blit_ret;

		rect_ret = gst_video_overlay_rectangle_get_render_rectangle(rectangle, &x, &y, &w, &h);
		g_assert(rect_ret); /* This is only false if "rectangle" is not a valid overlay rectangle. */

		alpha = gst_video_overlay_rectangle_get_global_alpha(rectangle);
		if (G_UNLIKELY(alpha < 0.0f))
		{
			GST_WARNING_OBJECT(video_overlay_handler, "overlay rectangle #%u has global alpha value %f that is below 0.0; truncating", rectangle_idx, alpha);
			alpha = 0.0f;
		}
		else if (G_UNLIKELY(alpha > 1.0f))
		{
			GST_WARNING_OBJECT(video_overlay_handler, "overlay rectangle #%u has global alpha value %f that is above 1.0; truncating", rectangle_idx, alpha);
			alpha = 1.0f;
		}

		dest_region.x1 = x;
		dest_region.y1 = y;
		dest_region.x2 = x + w;
		dest_region.y2 = y + h;

		blit_params.dest_region = &dest_region;
		/* We need alpha in the 0 .. 255 range, while the alpha value from the
		 * GstVideoOverlayRectangle object is in the 0.0 .. 1.0 range. */
		blit_params.alpha = (int)(alpha * 255);
		blit_params.rotation = IMX_2D_ROTATION_NONE;

		blit_ret = imx_2d_blitter_do_blit(video_overlay_handler->blitter, cached_overlay->surface, &blit_params);
		if (!blit_ret)
		{
			GST_ERROR_OBJECT(video_overlay_handler, "blitting failed");
			goto error;
		}
	}

finish:
	return retval;

error:
	retval = FALSE;
	goto finish;
}


static gboolean gst_imx_2d_video_overlay_handler_cache_buffers(GstImx2dVideoOverlayHandler *self, GstVideoOverlayComposition *new_composition)
{
	GstFlowReturn flow_ret;
	CachedOverlay *new_cached_overlays;
	guint rectangle_idx, previous_total_num_cached_overlays, num_rectangles;

	/* Prerequisites. */

	previous_total_num_cached_overlays = self->total_num_cached_overlays;

	num_rectangles = gst_video_overlay_composition_n_rectangles(new_composition);
	GST_DEBUG_OBJECT(self, "about to cache %u overlay(s) (previously cached amount: %u)", num_rectangles, previous_total_num_cached_overlays);


	/* Get rid of any previously cached data (since we want to repoulate the cache). */

	GST_DEBUG_OBJECT(self, "first, discarding old cached data (if any is present)");
	gst_imx_2d_video_overlay_handler_clear_cached_overlays(self);


	/* Check if we need to expand or contract the cached_overlays array. */

	if (num_rectangles > previous_total_num_cached_overlays)
	{
		/* We need more space than is available. Expand the array and
		 * make sure the new CachedOverlay instances are initialized. */

		GST_DEBUG_OBJECT(self, "need to make room for %u more cached overlay(s)", num_rectangles - previous_total_num_cached_overlays);

		new_cached_overlays = (CachedOverlay *)g_try_realloc(self->cached_overlays, sizeof(CachedOverlay) * num_rectangles);

		if (G_UNLIKELY(new_cached_overlays == NULL))
		{
			GST_ERROR_OBJECT(self, "could not (re)allocate cached overlays array");
			return FALSE;
		}

		for (rectangle_idx = previous_total_num_cached_overlays; rectangle_idx < num_rectangles; ++rectangle_idx)
		{
			CachedOverlay *cached_overlay = &(new_cached_overlays[rectangle_idx]);
			memset(cached_overlay, 0, sizeof(CachedOverlay));
		}

		self->cached_overlays = new_cached_overlays;
		self->total_num_cached_overlays = num_rectangles;
	}
	else if (num_rectangles < previous_total_num_cached_overlays)
	{
		/* We need less space than is available. Contract the array and
		 * make sure imx2d surface instances in the excess CachedOverlay
		 * instances are destroyed to avoid memory leaks. */

		GST_DEBUG_OBJECT(self, "need to erase the last %u cached overlay(s)", previous_total_num_cached_overlays - num_rectangles);

		for (rectangle_idx = num_rectangles; rectangle_idx < previous_total_num_cached_overlays; ++rectangle_idx)
		{
			CachedOverlay *cached_overlay = &(self->cached_overlays[rectangle_idx]);

			/* Destroy the surface of the cached overlays that we need to get rid of.
			 * (Note that at this point, the cached overlay's gstbuffer is always NULL,
			 * since the gst_imx_2d_video_overlay_handler_clear_cached_overlays() call
			 * above takes care of getting rid of those gstbuffers.) */
			g_assert(cached_overlay->buffer == NULL);

			if (cached_overlay->surface != NULL)
			{
				imx_2d_surface_destroy(cached_overlay->surface);
				cached_overlay->surface = NULL;
			}
		}

		/* Reallocate the cached_overlays array. */
		new_cached_overlays = (CachedOverlay *)g_try_realloc(self->cached_overlays, sizeof(CachedOverlay) * num_rectangles);
		if (G_UNLIKELY(new_cached_overlays == NULL))
		{
			GST_ERROR_OBJECT(self, "could not (re)allocate cached overlays array");
			return FALSE;
		}

		self->cached_overlays = new_cached_overlays;
		self->total_num_cached_overlays = num_rectangles;
	}
	/* Do nothing in case the amount of rectangles and the array size are the same. */


	/* Perform the actual gstbuffer upload now and set up
	 * the imx2d surface for each overlay. */

	GST_DEBUG_OBJECT(self, "now uploading incoming overlay gstbuffers and storing the uploaded versions in the cached overlays");

	for (rectangle_idx = 0; rectangle_idx < num_rectangles; ++rectangle_idx)
	{
		guint plane_idx;
		Imx2dSurfaceDesc surface_desc;
		GstVideoMeta *video_meta;
		GstVideoOverlayRectangle *rectangle = gst_video_overlay_composition_get_rectangle(new_composition, rectangle_idx);
		GstBuffer *rectangle_buffer = gst_video_overlay_rectangle_get_pixels_raw(rectangle, GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
		GstBuffer *uploaded_buffer;
		CachedOverlay *cached_overlay = &(self->cached_overlays[rectangle_idx]);
		GstVideoInfo video_info, adjusted_video_info;
		GstVideoAlignment video_alignment;
		gboolean must_copy_frame = FALSE;
		Imx2dPixelFormat imx2d_format;
		gint stride_alignment;

		GST_DEBUG_OBJECT(self, "uploading gstbuffer of overlay #%u", rectangle_idx);

		/* The GstBuffer of an overlay rectangle _must_ have a video meta. This
		 * is a requirement as per the GstVideoOverlayRectangle documentation. */
		video_meta = gst_buffer_get_video_meta(rectangle_buffer);
		if (G_UNLIKELY(video_meta == NULL))
		{
			GST_ERROR_OBJECT(self, "overlay rectangle has a gstbuffer without video meta; gstbuffer: %" GST_PTR_FORMAT, (gpointer)rectangle_buffer);
			return FALSE;
		}

		imx2d_format = gst_imx_2d_convert_from_gst_video_format(video_meta->format, NULL);
		stride_alignment = gst_imx_2d_get_stride_alignment_for(imx2d_format, self->blitter_capabilities);

		/* Fill video_info, make a copy of it, and then align the copy's
		 * stride values to the alignment the imx2d blitter requires.
		 * That way, we can compare the stride values of both video info
		 * structures. If the ones from the copy got changed, we know
		 * that the original stride sizes are unsuitable for the imx2d
		 * blitter, and we must do an adjusted frame copy. Otherwise, we
		 * can use the input frame directly, and pass it to the uploader. */

		gst_video_info_set_format(&video_info, video_meta->format, video_meta->width, video_meta->height);
		gst_video_alignment_reset(&video_alignment);

		for (plane_idx = 0; plane_idx < video_meta->n_planes; ++plane_idx)
		{
			gint w_sub = GST_VIDEO_FORMAT_INFO_W_SUB(video_info.finfo, plane_idx);

			if (G_LIKELY(video_meta->stride[plane_idx] > 0))
				GST_VIDEO_INFO_PLANE_STRIDE(&video_info, plane_idx) = video_meta->stride[plane_idx];
			if (G_LIKELY(video_meta->offset[plane_idx] > 0))
				GST_VIDEO_INFO_PLANE_OFFSET(&video_info, plane_idx) = video_meta->offset[plane_idx];

			video_alignment.stride_align[plane_idx] = GST_VIDEO_SUB_SCALE(w_sub, stride_alignment) - 1;
			GST_DEBUG_OBJECT(self, "plane #%u gstvideoalignment stride_align value: %u", plane_idx, video_alignment.stride_align[plane_idx]);
		}

		/* Create the video_info copy and adjust it by aligning the strides. */
		memcpy(&adjusted_video_info, &video_info, sizeof(GstVideoInfo));
		gst_video_info_align(&adjusted_video_info, &video_alignment);

		/* Now check if the stride sizes were actually changed. */
		for (plane_idx = 0; plane_idx < video_meta->n_planes; ++plane_idx)
		{
			GST_LOG_OBJECT(
				self,
				"checking plane %u: original stride %d adjusted stride %d",
				plane_idx,
				GST_VIDEO_INFO_PLANE_STRIDE(&video_info, plane_idx),
				GST_VIDEO_INFO_PLANE_STRIDE(&adjusted_video_info, plane_idx)
			);

			if (GST_VIDEO_INFO_PLANE_STRIDE(&adjusted_video_info, plane_idx) != GST_VIDEO_INFO_PLANE_STRIDE(&video_info, plane_idx))
			{
				GST_LOG_OBJECT(self, "stride was modified; need to do a frame copy");
				must_copy_frame = TRUE;
				break;
			}
		}

		/* The actual upload / copy. */

		if (must_copy_frame)
		{
			GstVideoFrame in_frame, out_frame;

			GST_LOG_OBJECT(self, "copying the overlay frame to produce a frame that meets the imx2d blitter stride alignment requirements");

			uploaded_buffer = gst_buffer_new_allocate(
				self->dma_buffer_allocator,
				GST_VIDEO_INFO_SIZE(&adjusted_video_info),
				NULL
			);

			gst_video_frame_map(&in_frame, &(video_info), rectangle_buffer, GST_MAP_READ);
			gst_video_frame_map(&out_frame, &(adjusted_video_info), uploaded_buffer, GST_MAP_WRITE);

			gst_video_frame_copy(&out_frame, &in_frame);

			gst_video_frame_unmap(&out_frame);
			gst_video_frame_unmap(&in_frame);
		}
		else
		{
			GST_LOG_OBJECT(self, "uploading the overlay frame");

			flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, rectangle_buffer, &uploaded_buffer);
			if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			{
				GST_ERROR_OBJECT(self, "could not upload gstbuffer for overlaay #%u: %s", rectangle_idx, gst_flow_get_name(flow_ret));
				return FALSE;
			}
		}

		if (uploaded_buffer != rectangle_buffer)
		{
			GST_LOG_OBJECT(self, "frame was copied or uploaded; adding video meta with data from adjusted video info");

			gst_buffer_add_video_meta_full(
				uploaded_buffer,
				video_meta->flags,
				GST_VIDEO_INFO_FORMAT(&adjusted_video_info),
				GST_VIDEO_INFO_WIDTH(&adjusted_video_info),
				GST_VIDEO_INFO_HEIGHT(&adjusted_video_info),
				GST_VIDEO_INFO_N_PLANES(&adjusted_video_info),
				&(GST_VIDEO_INFO_PLANE_OFFSET(&adjusted_video_info, 0)),
				&(GST_VIDEO_INFO_PLANE_STRIDE(&adjusted_video_info, 0))
			);
		}


		cached_overlay->buffer = uploaded_buffer;


		/* Now set up the surface. */

		if (cached_overlay->surface == NULL)
			cached_overlay->surface = imx_2d_surface_create(NULL);

		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.width = video_meta->width;
		surface_desc.height = video_meta->height;
		surface_desc.format = imx2d_format;

		gst_imx_2d_assign_input_buffer_to_surface(
			uploaded_buffer,
			cached_overlay->surface,
			&surface_desc,
			NULL
		);

		imx_2d_surface_set_desc(cached_overlay->surface, &(surface_desc));
	}


	self->num_populated_cached_overlays = num_rectangles;

	/* Ref the new composition to avoid modifications (taking
	 * advantage of the copy-on-write mechanism in miniobject
	 * based entities) and to be able to compare future
	 * compositions with this one to detect changes. */
	GST_DEBUG_OBJECT(self, "ref'ing new video overlay composition %" GST_PTR_FORMAT, (gpointer)new_composition);
	self->previous_composition = gst_video_overlay_composition_ref(new_composition);


	GST_DEBUG_OBJECT(self, "uploading complete");


	return TRUE;
}


static void gst_imx_2d_video_overlay_handler_clear_cached_overlays_full(GstImx2dVideoOverlayHandler *video_overlay_handler, gboolean do_full_clearing)
{
	g_assert(video_overlay_handler != NULL);

	GST_DEBUG_OBJECT(
		video_overlay_handler,
		"about to clear cached overlays:  num populated: %u  total num: %u  do full clearing: %d",
		video_overlay_handler->num_populated_cached_overlays,
		video_overlay_handler->total_num_cached_overlays,
		do_full_clearing
	);

	if (video_overlay_handler->cached_overlays != NULL)
	{
		guint i;

		/* First, unref any non-NULL overlay gstbuffer. This is done
		 * no matter what the do_full_clearing is. */
		for (i = 0; i < video_overlay_handler->num_populated_cached_overlays; ++i)
		{
			CachedOverlay *cached_overlay = &(video_overlay_handler->cached_overlays[i]);

			if (cached_overlay->buffer != NULL)
			{
				GST_DEBUG_OBJECT(
					video_overlay_handler,
					"unref'ing non-NULL gst buffer from cached overlay #%u; gstbuffer: %" GST_PTR_FORMAT,
					i,
					(gpointer)(cached_overlay->buffer)
				);

				gst_buffer_unref(cached_overlay->buffer);
				cached_overlay->buffer = NULL;
			}
		}

		video_overlay_handler->num_populated_cached_overlays = 0;

		/* If requested, also deallocate the cached_overlays array and the imx2d
		 * surfaces in each CachedOverlay entry. This is typically done when
		 * disposing of this GstImx2dVideoOverlayHandler. */
		if (do_full_clearing)
		{
			for (i = 0; i < video_overlay_handler->total_num_cached_overlays; ++i)
			{
				CachedOverlay *cached_buf = &(video_overlay_handler->cached_overlays[i]);
				if (cached_buf->surface != NULL)
					imx_2d_surface_destroy(cached_buf->surface);
			}

			g_free(video_overlay_handler->cached_overlays);
			video_overlay_handler->cached_overlays = NULL;
			video_overlay_handler->total_num_cached_overlays = 0;
		}
	}

	if (video_overlay_handler->previous_composition != NULL)
	{
		GST_DEBUG_OBJECT(video_overlay_handler, "unref'ing old overlay composition %" GST_PTR_FORMAT, (gpointer)(video_overlay_handler->previous_composition));
		gst_video_overlay_composition_unref(video_overlay_handler->previous_composition);
		video_overlay_handler->previous_composition = NULL;
	}
}
