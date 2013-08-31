/* Video transform element using the Freescale IPU
 * Copyright (C) 2013  Carlos Rafael Giani
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


#include "videotransform.h"

#include <config.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/mxcfb.h>
#include <linux/ipu.h>

#include "../common/phys_mem_meta.h"
#include "../allocator.h"
#include "../buffer_pool.h"




GST_DEBUG_CATEGORY_STATIC(ipuvideotransform_debug);
#define GST_CAT_DEFAULT ipuvideotransform_debug




#define IPU_VIDEO_FORMATS \
	" { " \
	"   RGB15 " \
	" , RGB16 " \
	" , BGR " \
	" , RGB " \
	" , BGRx " \
	" , BGRA " \
	" , RGBx " \
	" , RGBA " \
	" , ABGR " \
	" , UYVY " \
	" , YVYU " \
	" , IYU1 " \
	" , v308 " \
	" , NV12 " \
	" , GRAY8 " \
	" , YVU9 " \
	" , YUV9 " \
	" , YV12 " \
	" , I420 " \
	" , Y42B " \
	" , Y444 " \
	" } "

//GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(IPU_VIDEO_FORMATS))
#define IPU_VIDEO_TRANSFORM_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " IPU_VIDEO_FORMATS ", " \
		"width = (int) [ 64, MAX ], " \
		"height = (int) [ 64, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)

static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	IPU_VIDEO_TRANSFORM_CAPS
);

static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	IPU_VIDEO_TRANSFORM_CAPS
);


struct _GstFslIpuVideoTransformPrivate
{
	int ipu_fd;
	struct ipu_task task;

	/* only used if upstream isn't sending in buffers with physical memory */
	gpointer display_mem_block;
	gsize display_mem_block_size;
};


G_DEFINE_TYPE(GstFslIpuVideoTransform, gst_fsl_ipu_video_transform, GST_TYPE_VIDEO_FILTER)


static void gst_fsl_ipu_video_transform_finalize(GObject *object);
static gboolean gst_ipu_video_transform_src_event(GstBaseTransform *transform, GstEvent *event);
static GstCaps* gst_ipu_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static gboolean gst_fsl_ipu_video_transform_propose_allocation(GstBaseTransform *transform, GstQuery *decide_query, GstQuery *query);
static gboolean gst_fsl_ipu_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query);
static gboolean gst_ipu_video_transform_set_info(GstVideoFilter *filter, GstCaps *in, GstVideoInfo *in_info, GstCaps *out, GstVideoInfo *out_info);
static GstFlowReturn gst_ipu_video_transform_transform_frame(GstVideoFilter *filter, GstVideoFrame *in, GstVideoFrame *out);




void gst_fsl_ipu_video_transform_class_init(GstFslIpuVideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstBaseTransformClass *base_transform_class;
	GstVideoFilterClass *video_filter_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(ipuvideotransform_debug, "ipuvideotransform", 0, "Freescale IPU video transform");

	object_class = G_OBJECT_CLASS(klass);
	base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
	video_filter_class = GST_VIDEO_FILTER_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video transform element",
		"Filter/Converter/Video/Scaler",
		"Video frame transfomrations using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->finalize                   = GST_DEBUG_FUNCPTR(gst_fsl_ipu_video_transform_finalize);
	base_transform_class->transform_caps     = GST_DEBUG_FUNCPTR(gst_ipu_video_transform_transform_caps);
	base_transform_class->src_event          = GST_DEBUG_FUNCPTR(gst_ipu_video_transform_src_event);
	base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_fsl_ipu_video_transform_propose_allocation);
	base_transform_class->decide_allocation  = GST_DEBUG_FUNCPTR(gst_fsl_ipu_video_transform_decide_allocation);
	video_filter_class->set_info             = GST_DEBUG_FUNCPTR(gst_ipu_video_transform_set_info);
	video_filter_class->transform_frame      = GST_DEBUG_FUNCPTR(gst_ipu_video_transform_transform_frame);

	base_transform_class->passthrough_on_same_caps = TRUE;
}


void gst_fsl_ipu_video_transform_init(GstFslIpuVideoTransform *ipu_video_transform)
{
	ipu_video_transform->priv = g_slice_alloc(sizeof(GstFslIpuVideoTransformPrivate));

	ipu_video_transform->priv->display_mem_block = NULL;
	ipu_video_transform->priv->display_mem_block_size = 0;

	ipu_video_transform->priv->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_video_transform->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_video_transform, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/mxc_ipu: %s", strerror(errno)), (NULL));
		return;
	}

	memset(&(ipu_video_transform->priv->task), 0, sizeof(struct ipu_task));
}


static void gst_fsl_ipu_video_transform_finalize(GObject *object)
{
	GstFslIpuVideoTransform *ipu_video_transform = GST_FSL_IPU_VIDEO_TRANSFORM(object);

	if (ipu_video_transform->priv != NULL)
	{
		if (ipu_video_transform->priv->display_mem_block != 0)
			gst_fsl_ipu_free_phys_mem(ipu_video_transform->priv->ipu_fd, ipu_video_transform->priv->display_mem_block);
		if (ipu_video_transform->priv->ipu_fd >= 0)
			close(ipu_video_transform->priv->ipu_fd);
		g_slice_free1(sizeof(GstFslIpuVideoTransformPrivate), ipu_video_transform->priv);
	}

	G_OBJECT_CLASS(gst_fsl_ipu_video_transform_parent_class)->finalize(object);
}


static gboolean gst_ipu_video_transform_src_event(GstBaseTransform *transform, GstEvent *event)
{
	gdouble a;
	GstStructure *structure;
	GstVideoFilter *filter = GST_VIDEO_FILTER_CAST(transform);

	GST_DEBUG_OBJECT(transform, "handling %s event", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_NAVIGATION:
			if ((filter->in_info.width != filter->out_info.width) || (filter->in_info.height != filter->out_info.height))
			{
				event = GST_EVENT(gst_mini_object_make_writable(GST_MINI_OBJECT(event)));

				structure = (GstStructure *)gst_event_get_structure(event);
				if (gst_structure_get_double(structure, "pointer_x", &a))
				{
					gst_structure_set(
						structure,
						"pointer_x",
						G_TYPE_DOUBLE,
						a * filter->in_info.width / filter->out_info.width,
						NULL
					);
				}
				if (gst_structure_get_double(structure, "pointer_y", &a))
				{
					gst_structure_set(
						structure,
						"pointer_y",
						G_TYPE_DOUBLE,
						a * filter->in_info.height / filter->out_info.height,
						NULL
					);
				}
			}
			break;
		default:
			break;
	}

	return GST_BASE_TRANSFORM_CLASS(gst_fsl_ipu_video_transform_parent_class)->src_event(transform, event);
}


static GstCaps* gst_ipu_video_transform_transform_caps(GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
	GstCaps *tmpcaps1, *tmpcaps2, *result;
	GstStructure *structure;
	gint i, n;

	tmpcaps1 = gst_caps_new_empty();
	n = gst_caps_get_size(caps);
	for (i = 0; i < n; i++)
	{
		structure = gst_caps_get_structure(caps, i);

		/* If this is already expressed by the existing caps
		 * skip this structure */
		if ((i > 0) && gst_caps_is_subset_structure(tmpcaps1, structure))
			continue;

		/* make copy */
		structure = gst_structure_copy(structure);
		gst_structure_set(
			structure,
			"width", GST_TYPE_INT_RANGE, 64, G_MAXINT,
			"height", GST_TYPE_INT_RANGE, 64, G_MAXINT,
			NULL
		);

		gst_structure_remove_fields(structure, "format", "colorimetry", "chroma-site", NULL);

		/* if pixel aspect ratio, make a range of it */
		if (gst_structure_has_field(structure, "pixel-aspect-ratio"))
		{
			gst_structure_set(
				structure,
				"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
				NULL
			);
		}
		gst_caps_append_structure(tmpcaps1, structure);
	}

	if (filter != NULL)
	{
		tmpcaps2 = gst_caps_intersect_full(filter, tmpcaps1, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref(tmpcaps1);
		tmpcaps1 = tmpcaps2;
	}

	result = tmpcaps1;

	GST_DEBUG_OBJECT(transform, "transformed %" GST_PTR_FORMAT " into %" GST_PTR_FORMAT, caps, result);

	return result;
}


static gboolean gst_fsl_ipu_video_transform_propose_allocation(GstBaseTransform *transform, G_GNUC_UNUSED GstQuery *decide_query, GstQuery *query)
{
	return gst_pad_peer_query(GST_BASE_TRANSFORM_SRC_PAD(transform), query);
}


static gboolean gst_fsl_ipu_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query)
{
	GstFslIpuVideoTransform *ipu_video_transform = GST_FSL_IPU_VIDEO_TRANSFORM(transform);
	GstCaps *outcaps;
	GstBufferPool *pool = NULL;
	guint size, min = 0, max = 0;
	GstStructure *config;
	GstVideoInfo vinfo;
	gboolean update_pool;

	gst_query_parse_allocation(query, &outcaps, NULL);
	gst_video_info_init(&vinfo);
	gst_video_info_from_caps(&vinfo, outcaps);

	GST_DEBUG_OBJECT(ipu_video_transform, "num allocation pools: %d", gst_query_get_n_allocation_pools(query));

	/* Look for an allocator which can allocate physical memory buffers */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		for (guint i = 0; i < gst_query_get_n_allocation_pools(query); ++i)
		{
			gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
			if (gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_FSL_PHYS_MEM))
				break;
		}

		size = MAX(size, vinfo.size);
		update_pool = TRUE;
	}
	else
	{
		pool = NULL;
		size = vinfo.size;
		min = max = 0;
		update_pool = FALSE;
	}

	/* Either no pool or no pool with the ability to allocate physical memory buffers
	 * has been found -> create a new pool */
	if ((pool == NULL) || !gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_FSL_PHYS_MEM))
	{
		if (pool == NULL)
			GST_DEBUG_OBJECT(ipu_video_transform, "no pool present; creating new pool");
		else
			GST_DEBUG_OBJECT(ipu_video_transform, "no pool supports physical memory buffers; creating new pool");
		pool = gst_fsl_ipu_buffer_pool_new(ipu_video_transform->priv->ipu_fd, FALSE);
	}

	GST_DEBUG_OBJECT(
		ipu_video_transform,
		"pool config:  outcaps: %" GST_PTR_FORMAT "  size: %u  min buffers: %u  max buffers: %u",
		outcaps,
		size,
		min,
		max
	);

	/* Now configure the pool. */
	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_FSL_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	if (update_pool)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	if (pool != NULL)
		gst_object_unref(pool);

	return TRUE;
}


static u32 gst_ipu_video_transform_conv_format(GstVideoFormat format)
{
	switch (format)
	{
		case GST_VIDEO_FORMAT_RGB15: return IPU_PIX_FMT_RGB555;
		case GST_VIDEO_FORMAT_RGB16: return IPU_PIX_FMT_RGB565;
		case GST_VIDEO_FORMAT_BGR: return IPU_PIX_FMT_BGR24;
		case GST_VIDEO_FORMAT_RGB: return IPU_PIX_FMT_RGB24;
		case GST_VIDEO_FORMAT_BGRx: return IPU_PIX_FMT_BGR32;
		case GST_VIDEO_FORMAT_BGRA: return IPU_PIX_FMT_BGRA32;
		case GST_VIDEO_FORMAT_RGBx: return IPU_PIX_FMT_RGB32;
		case GST_VIDEO_FORMAT_RGBA: return IPU_PIX_FMT_RGBA32;
		case GST_VIDEO_FORMAT_ABGR: return IPU_PIX_FMT_ABGR32;
		case GST_VIDEO_FORMAT_UYVY: return IPU_PIX_FMT_UYVY;
		case GST_VIDEO_FORMAT_YVYU: return IPU_PIX_FMT_YVYU;
		case GST_VIDEO_FORMAT_IYU1: return IPU_PIX_FMT_Y41P;
		case GST_VIDEO_FORMAT_v308: return IPU_PIX_FMT_YUV444;
		case GST_VIDEO_FORMAT_NV12: return IPU_PIX_FMT_NV12;
		case GST_VIDEO_FORMAT_GRAY8: return IPU_PIX_FMT_GREY;
		case GST_VIDEO_FORMAT_YVU9: return IPU_PIX_FMT_YVU410P;
		case GST_VIDEO_FORMAT_YUV9: return IPU_PIX_FMT_YUV410P;
		case GST_VIDEO_FORMAT_YV12: return IPU_PIX_FMT_YVU420P;
		case GST_VIDEO_FORMAT_I420: return IPU_PIX_FMT_YUV420P;
		case GST_VIDEO_FORMAT_Y42B: return IPU_PIX_FMT_YUV422P;
		case GST_VIDEO_FORMAT_Y444: return IPU_PIX_FMT_YUV444P;
		default: return 0;
	}
}


static gboolean gst_ipu_video_transform_set_info(GstVideoFilter *filter, G_GNUC_UNUSED GstCaps *in, GstVideoInfo *in_info, G_GNUC_UNUSED GstCaps *out, GstVideoInfo *out_info)
{
	GstFslIpuVideoTransform *ipu_video_transform = GST_FSL_IPU_VIDEO_TRANSFORM(filter);

	if (ipu_video_transform->priv->display_mem_block != NULL)
	{
		gst_fsl_ipu_free_phys_mem(ipu_video_transform->priv->ipu_fd, ipu_video_transform->priv->display_mem_block);
		ipu_video_transform->priv->display_mem_block = NULL;
	}

	return TRUE;
}


static GstFlowReturn gst_ipu_video_transform_transform_frame(GstVideoFilter *filter, GstVideoFrame *in, GstVideoFrame *out)
{
	GstFslIpuVideoTransform *ipu_video_transform;
	GstFslPhysMemMeta *out_phys_mem_meta;

	ipu_video_transform = GST_FSL_IPU_VIDEO_TRANSFORM(filter);

	out_phys_mem_meta = GST_FSL_PHYS_MEM_META_GET(out->buffer);

	ipu_video_transform->priv->task.input.format = gst_ipu_video_transform_conv_format(GST_VIDEO_INFO_FORMAT(&(in->info)));
	ipu_video_transform->priv->task.input.width = GST_VIDEO_INFO_WIDTH(&(in->info));
	ipu_video_transform->priv->task.input.height = GST_VIDEO_INFO_HEIGHT(&(in->info));

	ipu_video_transform->priv->task.output.format = gst_ipu_video_transform_conv_format(GST_VIDEO_INFO_FORMAT(&(out->info)));
	ipu_video_transform->priv->task.output.width = GST_VIDEO_INFO_PLANE_STRIDE(&(out->info), 0);
	ipu_video_transform->priv->task.output.height = GST_VIDEO_INFO_HEIGHT(&(out->info));
	ipu_video_transform->priv->task.output.crop.w = GST_VIDEO_INFO_WIDTH(&(out->info));
	ipu_video_transform->priv->task.output.crop.h = GST_VIDEO_INFO_HEIGHT(&(out->info));

	{
		GstMapInfo in_map_info;
		void *dispmem;
		gsize dispmem_size;

		dispmem_size = gst_buffer_get_size(in->buffer);

		if ((ipu_video_transform->priv->display_mem_block == NULL) || (ipu_video_transform->priv->display_mem_block_size != dispmem_size))
		{
			ipu_video_transform->priv->display_mem_block_size = dispmem_size;
			if (ipu_video_transform->priv->display_mem_block != NULL)
				gst_fsl_ipu_free_phys_mem(ipu_video_transform->priv->ipu_fd, ipu_video_transform->priv->display_mem_block);
			ipu_video_transform->priv->display_mem_block = gst_fsl_ipu_alloc_phys_mem(ipu_video_transform->priv->ipu_fd, dispmem_size);
			if (ipu_video_transform->priv->display_mem_block == NULL)
				return GST_FLOW_ERROR;
		}

		gst_buffer_map(in->buffer, &in_map_info, GST_MAP_READ);
		dispmem = mmap(0, dispmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipu_video_transform->priv->ipu_fd, (dma_addr_t)(ipu_video_transform->priv->display_mem_block));

		GST_DEBUG_OBJECT(ipu_video_transform, "copying %u bytes from incoming buffer to display mem block", dispmem_size);
		memcpy(dispmem, in_map_info.data, dispmem_size);

		munmap(dispmem, dispmem_size);
		gst_buffer_unmap(in->buffer, &in_map_info);

		ipu_video_transform->priv->task.input.paddr = (dma_addr_t)(ipu_video_transform->priv->display_mem_block);
	}

	ipu_video_transform->priv->task.output.paddr = (dma_addr_t)(out_phys_mem_meta->phys_addr);

	if (ioctl(ipu_video_transform->priv->ipu_fd, IPU_QUEUE_TASK, &(ipu_video_transform->priv->task)) == -1)
	{
		GST_ERROR_OBJECT(ipu_video_transform, "queuing IPU task failed: %s", strerror(errno));
		return GST_FLOW_ERROR;
	}

	return GST_FLOW_OK;
}

