#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimxvideodmabufferpool.h"


GST_DEBUG_CATEGORY_STATIC(imx_video_dma_buffer_pool_debug);
#define GST_CAT_DEFAULT imx_video_dma_buffer_pool_debug


struct _GstImxVideoDmaBufferPool
{
	GstBufferPool parent;

	GstVideoInfo video_info;
	gboolean create_multi_memory_buffers;

	GstAllocator *imx_dma_buffer_allocator;

	gsize plane_offsets[GST_VIDEO_MAX_PLANES];
	gsize plane_sizes[GST_VIDEO_MAX_PLANES];
};


struct _GstImxVideoDmaBufferPoolClass
{
	GstBufferPoolClass parent_class;
};


G_DEFINE_TYPE(GstImxVideoDmaBufferPool, gst_imx_video_dma_buffer_pool, GST_TYPE_BUFFER_POOL)


static void gst_imx_video_dma_buffer_pool_dispose(GObject *object);
static GstFlowReturn gst_imx_video_dma_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
// TODO: custom set_config that overrides the size argument in favor of GST_VIDEO_INFO_SIZE


static void gst_imx_video_dma_buffer_pool_class_init(GstImxVideoDmaBufferPoolClass *klass)
{
	GObjectClass *object_class;
	GstBufferPoolClass *buffer_pool_class;

	GST_DEBUG_CATEGORY_INIT(imx_video_dma_buffer_pool_debug, "imxvideodmabufferpool", 0, "NXP i.MX video DMA buffer pool");

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_video_dma_buffer_pool_dispose);

	buffer_pool_class = GST_BUFFER_POOL_CLASS(klass);
	buffer_pool_class->alloc_buffer = GST_DEBUG_FUNCPTR(gst_imx_video_dma_buffer_pool_alloc_buffer);
}


static void gst_imx_video_dma_buffer_pool_init(G_GNUC_UNUSED GstImxVideoDmaBufferPool *self)
{
}


static void gst_imx_video_dma_buffer_pool_dispose(GObject *object)
{
	GstImxVideoDmaBufferPool *self = GST_IMX_VIDEO_DMA_BUFFER_POOL(object);

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}

	G_OBJECT_CLASS(gst_imx_video_dma_buffer_pool_parent_class)->dispose(object);
}


static GstFlowReturn gst_imx_video_dma_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	gint plane_index;
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstImxVideoDmaBufferPool *self = GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(pool);

	/* TODO: It is currently not clear how to make use of GstAllocationParams
	 * here, so they are set to NULL at the moment. */

	/* Allocate the buffer. Note that this ignores the configured buffer pool
	 * buffer size. This is intentional - that size is not usable in this buffer
	 * pool, and the actual buffer size is already defined by the video info.
	 * The configured buffer size also does not work with multi-memory buffers. */

	if (self->create_multi_memory_buffers)
	{
		GST_DEBUG_OBJECT(self, "allocating multi-memory buffer");

		*buffer = gst_buffer_new();

		for (plane_index = 0; plane_index < (gint)GST_VIDEO_INFO_N_PLANES(&(self->video_info)); ++plane_index)
		{
			GstMemory *dma_buffer_memory;

			GST_DEBUG_OBJECT(
				self,
				"allocating DMA buffer for plane #%d ; plane size: %" G_GSIZE_FORMAT,
				plane_index,
				self->plane_sizes[plane_index]
			);

			dma_buffer_memory = gst_allocator_alloc(
				self->imx_dma_buffer_allocator,
				self->plane_sizes[plane_index],
				NULL
			);
			if (G_UNLIKELY(dma_buffer_memory == NULL))
			{
				GST_ERROR_OBJECT(self, "could not allocate memory for plane #%d", plane_index);
				goto error;
			}

			gst_buffer_append_memory(*buffer, dma_buffer_memory);
		}
	}
	else
	{
		GST_DEBUG_OBJECT(self, "allocating single-memory buffer");

		*buffer = gst_buffer_new_allocate(
			self->imx_dma_buffer_allocator,
			GST_VIDEO_INFO_SIZE(&(self->video_info)),
			NULL
		);

		if (G_UNLIKELY(*buffer == NULL))
		{
			GST_ERROR_OBJECT(self, "could not allocate buffer");
			goto error;
		}
	}

finish:
	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;

	gst_buffer_replace(buffer, NULL);

	goto finish;
}


GstBufferPool* gst_imx_video_dma_buffer_pool_new(
	GstAllocator *imx_dma_buffer_allocator,
	GstVideoInfo *video_info,
	gboolean create_multi_memory_buffers,
	gsize *plane_sizes
)
{
	/* Important: This also automatically configures the buffer, since
	 * its configuration is fully defined by the video_info already. */

	GstStructure *pool_config;
	GstImxVideoDmaBufferPool *pool;
	gint plane_index;
	gint num_planes;
	GstCaps *video_caps;

	g_assert(imx_dma_buffer_allocator != NULL);
	g_assert(GST_IS_IMX_DMA_BUFFER_ALLOCATOR(imx_dma_buffer_allocator));
	g_assert(video_info != NULL);

	pool = g_object_new(gst_imx_video_dma_buffer_pool_get_type(), NULL);
	g_assert(pool != NULL);

	/* Keep a reference to the allocator around since our alloc_buffer
	 * vfunc performs the actual allocation on its own with it. */
	pool->imx_dma_buffer_allocator = imx_dma_buffer_allocator;
	gst_object_ref(GST_OBJECT(pool->imx_dma_buffer_allocator));

	/* Make a copy of the video_info and create_multi_memory_buffers
	 * function arguments since we need them later. */
	memcpy(&(pool->video_info), video_info, sizeof(GstVideoInfo));
	pool->create_multi_memory_buffers = create_multi_memory_buffers;

	/* Create the caps out of the video_info. We'll need the caps
	 * for logging and further below for configuring the buffer pool. */
	video_caps = gst_video_info_to_caps(video_info);

	GST_DEBUG_OBJECT(
		pool,
		"creating new video DMA buffer pool %" GST_PTR_FORMAT " with caps %" GST_PTR_FORMAT,
		(gpointer)pool,
		(gpointer)video_caps
	);

	/* Next, retrieve and compute the plane offsets and sizes. The
	 * plane offsets are useful for single-memory buffers only.
	 * The plane sizes are needed for copying frames per-plane. */

	num_planes = GST_VIDEO_INFO_N_PLANES(video_info);

	for (plane_index = 0; plane_index < num_planes; ++plane_index)
		pool->plane_offsets[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(video_info, plane_index);

	/* The plane sizes can be specified manually. This is useful if for
	 * example the driver specifies the required sizes. If no such
	 * manual plane sizes are given, we estimate the plane sizes out
	 * of the plane offsets. The idea is that any additional padding
	 * rows must be placed in between planes, so their offsets must
	 * contain the rows, while calculating (stride*height) won't. */
	if (plane_sizes != NULL)
	{
		gsize total_size = 0;

		GST_DEBUG_OBJECT(pool, "using manually specified plane sizes");

		memcpy(pool->plane_sizes, plane_sizes, sizeof(gsize) * num_planes);

		/* If plane sizes are manually specified, calculate their sum.
		 * It is possible that their total sum exceeds the size field
		 * in video_info. In such a case, we must update that video_info
		 * field, otherwise there'll be subtle bugs later on. */

		for (plane_index = 0; plane_index < num_planes; ++plane_index)
			total_size += pool->plane_sizes[plane_index];

		if (total_size > GST_VIDEO_INFO_SIZE(video_info))
		{
			GST_DEBUG_OBJECT(
				pool,
				"sum of manually specified plane sizes %" G_GSIZE_FORMAT " exceeds video info size %" G_GSIZE_FORMAT "; adjusting video info",
				total_size,
				GST_VIDEO_INFO_SIZE(video_info)
			);
			GST_VIDEO_INFO_SIZE(video_info) = total_size;
		}
	}
	else
	{
		/* As mentioned above, calculate the plane sizes by computing
		 * the distance between plane offsets. For the last plane,
		 * since there is no offset beyond it, we subtract its offset
		 * from the total video_info size instead. */

		GST_DEBUG_OBJECT(pool, "no plane sizes manually specified; calculating sizes out of video info instead");

		pool->plane_sizes[num_planes - 1] = GST_VIDEO_INFO_SIZE(video_info) - pool->plane_offsets[num_planes - 1];

		for (plane_index = 0; plane_index < (num_planes - 1); ++plane_index)
			pool->plane_sizes[plane_index] = pool->plane_offsets[plane_index + 1] - pool->plane_offsets[plane_index];
	}

	for (plane_index = 0; plane_index < num_planes; ++plane_index)
	{
		GST_DEBUG_OBJECT(
			pool,
			"plane #%d:  offset: %" G_GSIZE_FORMAT "  size: %" G_GSIZE_FORMAT,
			plane_index,
			pool->plane_offsets[plane_index],
			pool->plane_sizes[plane_index]
		);
	}

	pool_config = gst_buffer_pool_get_config(GST_BUFFER_POOL_CAST(pool));
	gst_buffer_pool_config_set_params(pool_config, video_caps, GST_VIDEO_INFO_SIZE(video_info), 0, 0);
	gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(GST_BUFFER_POOL_CAST(pool), pool_config);

	gst_caps_unref(video_caps);

	return GST_BUFFER_POOL_CAST(pool);
}


GstVideoInfo const * gst_imx_video_dma_buffer_pool_get_video_info(GstBufferPool *imx_video_dma_buffer_pool)
{
	g_assert(imx_video_dma_buffer_pool != NULL);
	g_assert(GST_IS_IMX_VIDEO_DMA_BUFFER_POOL(imx_video_dma_buffer_pool));
	return &(GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(imx_video_dma_buffer_pool)->video_info);
}


gboolean gst_imx_video_dma_buffer_pool_creates_multi_memory_buffers(GstBufferPool *imx_video_dma_buffer_pool)
{
	g_assert(imx_video_dma_buffer_pool != NULL);
	g_assert(GST_IS_IMX_VIDEO_DMA_BUFFER_POOL(imx_video_dma_buffer_pool));
	return GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(imx_video_dma_buffer_pool)->create_multi_memory_buffers;
}


gsize gst_imx_video_dma_buffer_pool_get_plane_offset(GstBufferPool *imx_video_dma_buffer_pool, gint plane_index)
{
	GstImxVideoDmaBufferPool *self;

	g_assert(imx_video_dma_buffer_pool != NULL);
	g_assert(GST_IS_IMX_VIDEO_DMA_BUFFER_POOL(imx_video_dma_buffer_pool));

	self = GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(imx_video_dma_buffer_pool);
	g_assert((plane_index >= 0) && (plane_index < (gint)GST_VIDEO_INFO_N_PLANES(&(self->video_info))));
	return self->plane_offsets[plane_index];
}


gsize gst_imx_video_dma_buffer_pool_get_plane_size(GstBufferPool *imx_video_dma_buffer_pool, gint plane_index)
{
	GstImxVideoDmaBufferPool *self;

	g_assert(imx_video_dma_buffer_pool != NULL);
	g_assert(GST_IS_IMX_VIDEO_DMA_BUFFER_POOL(imx_video_dma_buffer_pool));

	self = GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(imx_video_dma_buffer_pool);
	g_assert((plane_index >= 0) && (plane_index < (gint)GST_VIDEO_INFO_N_PLANES(&(self->video_info))));
	return self->plane_sizes[plane_index];
}
