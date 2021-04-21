#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideobufferpool.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_buffer_pool_debug);
#define GST_CAT_DEFAULT imx_2d_video_buffer_pool_debug


struct _GstImx2dVideoBufferPool
{
	GstObject parent;

	GstAllocator *imx_dma_buffer_allocator;
	GstBufferPool *internal_dma_buffer_pool;
	GstBufferPool *output_video_buffer_pool;

	gboolean both_pools_same;
	gboolean video_meta_supported;

	GstVideoInfo intermediate_video_info;
	GstVideoInfo output_video_info;
};


struct _GstImx2dVideoBufferPoolClass
{
	GstObjectClass parent_class;
};


G_DEFINE_TYPE(GstImx2dVideoBufferPool, gst_imx_2d_video_buffer_pool, GST_TYPE_OBJECT)


static void gst_imx_2d_video_buffer_pool_dispose(GObject *object);


static void gst_imx_2d_video_buffer_pool_class_init(GstImx2dVideoBufferPoolClass *klass)
{
	GObjectClass *object_class;

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_buffer_pool_debug, "imx2dvideobufferpool", 0, "NXP i.MX 2D video buffer pool class");

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_2d_video_buffer_pool_dispose);
}


static void gst_imx_2d_video_buffer_pool_init(GstImx2dVideoBufferPool *self)
{
	self->internal_dma_buffer_pool = NULL;
	self->output_video_buffer_pool = NULL;

	self->both_pools_same = FALSE;
}


static void gst_imx_2d_video_buffer_pool_dispose(GObject *object)
{
	GstImx2dVideoBufferPool *self = GST_IMX_2D_VIDEO_BUFFER_POOL(object);

	if (self->internal_dma_buffer_pool != NULL)
	{
		if (!self->both_pools_same)
			gst_buffer_pool_set_active(self->internal_dma_buffer_pool, FALSE);

		gst_object_unref(GST_OBJECT(self->internal_dma_buffer_pool));
		self->internal_dma_buffer_pool = NULL;
	}

	if (self->output_video_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->output_video_buffer_pool));
		self->output_video_buffer_pool = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}

	G_OBJECT_CLASS(gst_imx_2d_video_buffer_pool_parent_class)->dispose(object);
}


GstImx2dVideoBufferPool* gst_imx_2d_video_buffer_pool_new(
	GstAllocator *imx_dma_buffer_allocator,
	GstQuery *query,
	GstVideoInfo const *intermediate_video_info
)
{
	GstImx2dVideoBufferPool *self;
	GstAllocator *dma_buffer_allocator = NULL;
	GstAllocator *output_allocator;
	GstAllocationParams allocation_params;
	GstStructure *pool_config;
	GstCaps *negotiated_caps;
	GstVideoInfo negotiated_video_info;
	gboolean intermediate_buffers_are_tightly_packed;
	guint buffer_size;
	guint video_meta_index;
	guint i;


	/* Sanity checks. */

	g_assert(imx_dma_buffer_allocator != NULL);
	g_assert(query != NULL);
	g_assert(intermediate_video_info != NULL);


	/* Create the object. */

	self = g_object_new(gst_imx_2d_video_buffer_pool_get_type(), NULL);

	self->imx_dma_buffer_allocator = imx_dma_buffer_allocator;
	gst_object_ref(GST_OBJECT(imx_dma_buffer_allocator));

	gst_object_ref_sink(self);



	gst_query_parse_allocation(query, &negotiated_caps, NULL);

	GST_DEBUG_OBJECT(self, "negotiated caps in allocation query: %" GST_PTR_FORMAT, (gpointer)negotiated_caps);

	gst_video_info_init(&negotiated_video_info);
	if (!gst_video_info_from_caps(&negotiated_video_info, negotiated_caps))
	{
		GST_ERROR_OBJECT(
			self,
			"negotiated caps cannot be converted to a video info structure; caps: %" GST_PTR_FORMAT,
			(gpointer)negotiated_caps
		);
		goto error;
	}


	/* If the intermediate frames are tighly packed, then this means that
	 * their stride and plane offset values can be directly derived from
	 * negotiated_caps. In other words, negotiated_video_info and
	 * intermediate_video_info then are equal. (intermediate_video_info
	 * is a video info that includes stride and plane offset values as
	 * defined by the caller and the requirements of the blitter. This
	 * implies that if the blitter has alignment requirements, this video
	 * info will contain extra space for padding bytes in its stride and
	 * plane offset values). */
	intermediate_buffers_are_tightly_packed = gst_video_info_is_equal(&negotiated_video_info, intermediate_video_info);
	GST_DEBUG_OBJECT(self, "intermediate frames are tighly packed: %d", intermediate_buffers_are_tightly_packed);

	self->video_meta_supported = gst_query_find_allocation_meta(query, GST_VIDEO_META_API_TYPE, &video_meta_index);
	GST_DEBUG_OBJECT(self, "video meta supported by downstream: %d", self->video_meta_supported);


	/* Look for an allocator that is an ImxDmaBuffer allocator. */
	for (i = 0; i < gst_query_get_n_allocation_params(query); ++i)
	{
		GstAllocator *allocator = NULL;

		gst_query_parse_nth_allocation_param(query, i, &allocator, &allocation_params);
		if (allocator == NULL)
			continue;

		if (GST_IS_IMX_DMA_BUFFER_ALLOCATOR(allocator))
		{
			GST_DEBUG_OBJECT(self, "allocator #%u in allocation query can allocate DMA memory", i);
			dma_buffer_allocator = allocator;
			break;
		}
		else
			gst_object_unref(GST_OBJECT_CAST(allocator));
	}

	/* If no suitable allocator was found, use our own. */
	if (dma_buffer_allocator == NULL)
	{
		GST_DEBUG_OBJECT(self, "found no allocator in query that can allocate DMA memory, using our own");
		gst_allocation_params_init(&allocation_params);
		dma_buffer_allocator = gst_object_ref(self->imx_dma_buffer_allocator);
	}


	/* Set up the internal DMA buffer pool. */

	self->internal_dma_buffer_pool = gst_video_buffer_pool_new();
	GST_DEBUG_OBJECT(
		self,
		"created new internal DMA buffer pool %" GST_PTR_FORMAT,
		(gpointer)(self->internal_dma_buffer_pool)
	);

	buffer_size = GST_VIDEO_INFO_SIZE(intermediate_video_info);

	pool_config = gst_buffer_pool_get_config(self->internal_dma_buffer_pool);
	gst_buffer_pool_config_set_params(pool_config, negotiated_caps, buffer_size, 0, 0);
	gst_buffer_pool_config_set_allocator(pool_config, dma_buffer_allocator, &allocation_params);
	if (self->video_meta_supported)
		gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(self->internal_dma_buffer_pool, pool_config);


	/* Now set up the output video buffer pool. */

	if (self->video_meta_supported || intermediate_buffers_are_tightly_packed)
	{
		/* No need to have a separate pool; just use the internal DMA
		 * buffer pool as the output video buffer pool. */

		GST_DEBUG_OBJECT(self, "also using the internal DMA buffer pool as the output video buffer pool");

		gst_object_ref(GST_OBJECT(self->internal_dma_buffer_pool));
		self->output_video_buffer_pool = self->internal_dma_buffer_pool;

		output_allocator = dma_buffer_allocator;

		self->both_pools_same = TRUE;

		GST_DEBUG_OBJECT(self, "internal DMA buffer pool can directly be used as the output video buffer pool");
	}
	else
	{
		/* Intermediate buffers are not tightly packed, so we really do
		 * need a separate output video buffer pool whose acquired buffers
		 * are meant for tightly packed versions of intermediate frames.
		 * NOTE: This pool does _not_ add videometa to acquire buffers,
		 * since it is created when downstream can't handle those metas. */

		self->output_video_buffer_pool = gst_video_buffer_pool_new();
		GST_DEBUG_OBJECT(
			self,
			"created new output video buffer pool %" GST_PTR_FORMAT,
			(gpointer)(self->output_video_buffer_pool)
		);

		buffer_size = GST_VIDEO_INFO_SIZE(&negotiated_video_info);

		/* NULL selects the default sysmem allocator. */
		output_allocator = NULL;
		gst_allocation_params_init(&allocation_params);

		self->both_pools_same = FALSE;

		pool_config = gst_buffer_pool_get_config(self->output_video_buffer_pool);
		gst_buffer_pool_config_set_params(pool_config, negotiated_caps, buffer_size, 0, 0);
		gst_buffer_pool_config_set_allocator(pool_config, NULL, &allocation_params);
		gst_buffer_pool_set_config(self->output_video_buffer_pool, pool_config);

		gst_buffer_pool_set_active(self->internal_dma_buffer_pool, TRUE);

		GST_INFO_OBJECT(self, "need to copy blitter output frames since downstream cannot handle those directly; this may impact performance");
	}


	/* Update the query to favor our chosen output allocator
	 * and output video buffer pool. We do that by placing
	 * them as the first entries in the query. */

	if (gst_query_get_n_allocation_params(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation params in the allocation query; adding our params to it");
		gst_query_add_allocation_param(query, output_allocator, &allocation_params);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation params in the allocation query; setting our params as the first ones in the query");
		gst_query_set_nth_allocation_param(query, 0, output_allocator, &allocation_params);
	}

	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation pools in the allocation query; adding our buffer pool to it");
		gst_query_add_allocation_pool(query, self->output_video_buffer_pool, buffer_size, 0, 0);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation pools in the allocation query; setting our buffer pool as the first one in the query");
		gst_query_set_nth_allocation_pool(query, 0, self->output_video_buffer_pool, buffer_size, 0, 0);
	}

	gst_object_unref(GST_OBJECT(dma_buffer_allocator));


	/* Keep copies of these video infos to be able to copy frames later (if necessary). */
	memcpy(&(self->intermediate_video_info), intermediate_video_info, sizeof(GstVideoInfo));
	memcpy(&(self->output_video_info), &negotiated_video_info, sizeof(GstVideoInfo));


	return self;

error:
	gst_object_unref(GST_OBJECT(self));
	return NULL;
}


GstBufferPool* gst_imx_2d_video_buffer_pool_get_internal_dma_buffer_pool(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool)
{
	g_assert(imx_2d_video_buffer_pool != NULL);
	g_assert(imx_2d_video_buffer_pool->internal_dma_buffer_pool != NULL);
	return imx_2d_video_buffer_pool->internal_dma_buffer_pool;
}


GstBufferPool* gst_imx_2d_video_buffer_pool_get_output_video_buffer_pool(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool)
{
	g_assert(imx_2d_video_buffer_pool != NULL);
	g_assert(imx_2d_video_buffer_pool->output_video_buffer_pool != NULL);
	return imx_2d_video_buffer_pool->output_video_buffer_pool;
}


GstFlowReturn gst_imx_2d_video_buffer_pool_acquire_intermediate_buffer(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool, GstBuffer *output_buffer, GstBuffer **intermediate_buffer)
{
	g_assert(imx_2d_video_buffer_pool != NULL);
	g_assert(output_buffer != NULL);
	g_assert(intermediate_buffer != NULL);

	if (imx_2d_video_buffer_pool->both_pools_same)
	{
		*intermediate_buffer = gst_buffer_ref(output_buffer);
		GST_LOG_OBJECT(
			imx_2d_video_buffer_pool,
			"buffer pools are the same -> ref'ing and using output buffer as intermediate_buffer; intermediate buffer: %" GST_PTR_FORMAT,
			(gpointer)output_buffer
		);
		return GST_FLOW_OK;
	}
	else
	{
		GstFlowReturn flow_ret = gst_buffer_pool_acquire_buffer(
			imx_2d_video_buffer_pool->internal_dma_buffer_pool,
			intermediate_buffer,
			NULL
		);

		if (flow_ret != GST_FLOW_OK)
		{
			GST_ERROR_OBJECT(imx_2d_video_buffer_pool, "could not acquire intermediate buffer from internal DMA buffer pool: %s", gst_flow_get_name(flow_ret));
			intermediate_buffer = NULL;
		}

		GST_LOG_OBJECT(
			imx_2d_video_buffer_pool,
			"buffer pools are not the same -> acquired intermediate buffer from DMA buffer pool; intermediate buffer: %" GST_PTR_FORMAT,
			(gpointer)intermediate_buffer
		);

		return flow_ret;
	}
}


gboolean gst_imx_2d_video_buffer_pool_transfer_to_output_buffer(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool, GstBuffer *intermediate_buffer, GstBuffer *output_buffer)
{
	gboolean ret = TRUE;
	GstVideoFrame intermediate_video_frame;
	gboolean intermediate_video_frame_mapped;
	GstVideoFrame output_video_frame;
	gboolean output_video_frame_mapped;

	if (imx_2d_video_buffer_pool->both_pools_same)
	{
		GST_LOG_OBJECT(
			imx_2d_video_buffer_pool,
			"both buffer pools are the same -> no need to transfer anything, intermediate and output buffer are the same, just unref intermediate buffer"
		);

		gst_buffer_unref(intermediate_buffer);
		return TRUE;
	}

	intermediate_video_frame_mapped = FALSE;
	output_video_frame_mapped = FALSE;

	if (!gst_video_frame_map(
		&intermediate_video_frame,
		&(imx_2d_video_buffer_pool->intermediate_video_info),
		intermediate_buffer,
		GST_MAP_READ
	))
	{
		GST_ERROR_OBJECT(imx_2d_video_buffer_pool, "could not map intermediate video frame");
		goto error;
	}
	intermediate_video_frame_mapped = TRUE;

	if (!gst_video_frame_map(
		&output_video_frame,
		&(imx_2d_video_buffer_pool->output_video_info),
		output_buffer,
		GST_MAP_WRITE
	))
	{
		GST_ERROR_OBJECT(imx_2d_video_buffer_pool, "could not map output video frame");
		goto error;
	}
	output_video_frame_mapped = TRUE;

	if (!gst_video_frame_copy(&output_video_frame, &intermediate_video_frame))
	{
		GST_ERROR_OBJECT(imx_2d_video_buffer_pool, "could not copy pixels from intermediate buffer into output buffer");
		goto error;
	}

	GST_LOG_OBJECT(
		imx_2d_video_buffer_pool,
		"copied pixels from intermediate buffer into output buffer"
	);

finish:
	if (output_video_frame_mapped)
		gst_video_frame_unmap(&output_video_frame);
	if (intermediate_video_frame_mapped)
		gst_video_frame_unmap(&intermediate_video_frame);

	gst_buffer_unref(intermediate_buffer);

	return ret;

error:
	ret = FALSE;
	goto finish;
}


gboolean gst_imx_2d_video_buffer_pool_are_both_pools_same(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool)
{
	g_assert(imx_2d_video_buffer_pool != NULL);
	return imx_2d_video_buffer_pool->both_pools_same;
}


gboolean gst_imx_2d_video_buffer_pool_video_meta_supported(GstImx2dVideoBufferPool *imx_2d_video_buffer_pool)
{
	g_assert(imx_2d_video_buffer_pool != NULL);
	return imx_2d_video_buffer_pool->video_meta_supported;
}
