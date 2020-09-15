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

#include "config.h"

#include <unistd.h>
#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include "gstimxdmabufferuploader.h"
#include "gstimxdmabufferallocator.h"
#ifdef WITH_GST_ION_ALLOCATOR
#include "gstimxionallocator.h"
#endif



/**
 * Some functions here may be called before the GstImxDmaBufferUploaderClass
 * class_init function is called. And since this is part of a library, not
 * a plugin, there is no equivalent to a plugin_init function where the
 * GST_DEBUG_CATEGORY_INIT() macro could be called. For this reason, the
 * debug category here is set up in this unusual manner.
 */

#ifndef GST_DISABLE_GST_DEBUG

#define GST_CAT_DEFAULT gst_imx_dma_buffer_uploader_ensure_debug_category()

static GstDebugCategory* gst_imx_dma_buffer_uploader_ensure_debug_category()
{
	static gsize cat_gonce = 0;

	if (g_once_init_enter(&cat_gonce))
	{
		GstDebugCategory *cat = NULL;
		GST_DEBUG_CATEGORY_INIT(cat, "imxdmabufferupload", 0, "NXP i.MX DMA buffer upload");
		g_once_init_leave(&cat_gonce, (gsize)cat);
	}

	return (GstDebugCategory *)cat_gonce;
}

#endif /* GST_DISABLE_GST_DEBUG */




#define GST_FLOW_COULD_NOT_UPLOAD  GST_FLOW_CUSTOM_SUCCESS




struct _GstImxDmaBufferUploadMethodType
{
	gchar const *name;

	GstImxDmaBufferUploadMethodContext* (*create)(GstImxDmaBufferUploader *uploader);
	void (*destroy)(GstImxDmaBufferUploadMethodContext *upload_method_context);
	GstFlowReturn (*perform)(GstImxDmaBufferUploadMethodContext *upload_method_context, GstBuffer *input_buffer, GstBuffer **output_buffer);
};


struct _GstImxDmaBufferUploadMethodContext
{
	GstImxDmaBufferUploader *uploader;
};




struct DirectImxDmaBufferUploadMethodContext
{
	GstImxDmaBufferUploadMethodContext parent;
};


static GstImxDmaBufferUploadMethodContext* direct_imx_dma_buffer_upload_method_create(GstImxDmaBufferUploader *uploader)
{
	struct DirectImxDmaBufferUploadMethodContext *upload_method_context = g_new0(struct DirectImxDmaBufferUploadMethodContext, 1);

	upload_method_context->parent.uploader = uploader;

	return (GstImxDmaBufferUploadMethodContext*)upload_method_context;
}


static void direct_imx_dma_buffer_upload_method_destroy(GstImxDmaBufferUploadMethodContext *upload_method_context)
{
	g_free(upload_method_context);
}


static GstFlowReturn direct_imx_dma_buffer_upload_method_perform(GstImxDmaBufferUploadMethodContext *upload_method_context, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	struct DirectImxDmaBufferUploadMethodContext *self;

	self = (struct DirectImxDmaBufferUploadMethodContext *)upload_method_context;

	if (!gst_imx_is_imx_dma_buffer_memory(gst_buffer_peek_memory(input_buffer, 0)))
		return GST_FLOW_COULD_NOT_UPLOAD;

	GST_LOG_OBJECT(self->parent.uploader, "this is the DirectImxDmaBuffer upload method; not actually uploading anything - just ref'ing the input buffer: %" GST_PTR_FORMAT, (gpointer)input_buffer);

	/* We ref the input buffer, since we don't actually create a copy
	 * and upload the input buffer data to said copy. But callers expect
	 * that gst_imx_dma_buffer_uploader_perform() does not cause the input
	 * buffer to be deallocated. So, to avoid having to do a copy, and
	 * to maintain these expectations, we ref the buffer. */
	gst_buffer_ref(input_buffer);
	*output_buffer = input_buffer;

	return GST_FLOW_OK;
}


static const GstImxDmaBufferUploadMethodType direct_imx_dma_buffer_upload_method_type = {
	"DirectImxDmaBuffer",

	direct_imx_dma_buffer_upload_method_create,
	direct_imx_dma_buffer_upload_method_destroy,
	direct_imx_dma_buffer_upload_method_perform
};




struct RawBufferUploadMethodContext
{
	GstImxDmaBufferUploadMethodContext parent;

	/* Buffer pool to use for creating ImxDmaBuffer backed
	 * GstBuffers that we upload incoming data into. */
	GstBufferPool *buffer_pool;

	/* This one keeps track of the buffer size. If it changes,
	 * we have to create a new buffer pool, since a buffer pool
	 * expects a constant buffer size. */
	guint last_buffer_size;
};


static GstImxDmaBufferUploadMethodContext* raw_buffer_upload_method_create(GstImxDmaBufferUploader *uploader)
{
	struct RawBufferUploadMethodContext *upload_method_context = g_new0(struct RawBufferUploadMethodContext, 1);

	upload_method_context->parent.uploader = uploader;
	upload_method_context->buffer_pool = NULL;
	upload_method_context->last_buffer_size = 0;

	return (GstImxDmaBufferUploadMethodContext*)upload_method_context;
}


static void raw_buffer_upload_method_destroy(GstImxDmaBufferUploadMethodContext *upload_method_context)
{
	struct RawBufferUploadMethodContext *self = (struct RawBufferUploadMethodContext *)upload_method_context;

	if (self != NULL)
	{
		if (self->buffer_pool != NULL)
			gst_object_unref(GST_OBJECT(self->buffer_pool));

		g_free(self);
	}
}


static GstFlowReturn raw_buffer_upload_method_perform(GstImxDmaBufferUploadMethodContext *upload_method_context, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	struct RawBufferUploadMethodContext *self = (struct RawBufferUploadMethodContext *)upload_method_context;
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstMapInfo map_info;

	gst_buffer_map(input_buffer, &map_info, GST_MAP_READ);

	/* Buffer pools are created on-demand, if either it does not exist yet,
	 * or if the size of the incoming buffer is now different to the last one. */
	if (G_UNLIKELY((self->buffer_pool == NULL) || (self->last_buffer_size != map_info.size)))
	{
		GstStructure *pool_config;

		GST_DEBUG_OBJECT(
			self,
			"buffer pool does not yet exist, or we got a buffer to upload whose size is now different (last buffer size: %u new size: %" G_GSIZE_FORMAT "); (re)creating buffer pool",
			self->last_buffer_size, map_info.size
		);

		/* Unref any existing buffer pool first to prevent a memleak. */
		if (self->buffer_pool != NULL)
			gst_object_unref(GST_OBJECT(self->buffer_pool));

		self->buffer_pool = gst_buffer_pool_new();

		GST_DEBUG_OBJECT(
			self,
			"buffer pool config: size: %" G_GSIZE_FORMAT " output caps: %" GST_PTR_FORMAT,
			map_info.size,
			(gpointer)(self->parent.uploader->output_caps)
		);

		pool_config = gst_buffer_pool_get_config(self->buffer_pool);
		gst_buffer_pool_config_set_params(pool_config, self->parent.uploader->output_caps, map_info.size, 0, 0);
		gst_buffer_pool_config_set_allocator(pool_config, self->parent.uploader->imx_dma_buffer_allocator, NULL);
		gst_buffer_pool_set_config (self->buffer_pool, pool_config);

		gst_buffer_pool_set_active(self->buffer_pool, TRUE);

		self->last_buffer_size = map_info.size;
	}

	flow_ret = gst_buffer_pool_acquire_buffer(self->buffer_pool, output_buffer, NULL);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
	{
		GST_ERROR_OBJECT(self->parent.uploader, "could not acquire buffer");
		goto error;
	}

	/* This is the actual upload here. The metadata (timestamps etc.)
	 * is copied to the output buffer 1:1. The actual contents are
	 * copied separately to make sure nothing about the output buffer
	 * GstMemory is changed in any way (since the whole point about this
	 * operation is to use ImxDmaBuffer based GstMemory as output). */
	gst_buffer_copy_into(*output_buffer, input_buffer, GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META, 0, -1);
	gst_buffer_fill(*output_buffer, 0, map_info.data, map_info.size);

	GST_LOG_OBJECT(self, "copied data from: %" GST_PTR_FORMAT, (gpointer)input_buffer);
	GST_LOG_OBJECT(self, "              to: %" GST_PTR_FORMAT, (gpointer)(*output_buffer));

finish:
	gst_buffer_unmap(input_buffer, &map_info);
	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static const GstImxDmaBufferUploadMethodType raw_buffer_upload_method_type = {
	"RawBufferUpload",

	raw_buffer_upload_method_create,
	raw_buffer_upload_method_destroy,
	raw_buffer_upload_method_perform
};




#ifdef WITH_GST_ION_ALLOCATOR


struct DmabufUploadMethodContext
{
	GstImxDmaBufferUploadMethodContext parent;

	GstBufferPool *buffer_pool;

	/* This one keeps track of the buffer size. If it changes,
	 * we have to create a new buffer pool, since a buffer pool
	 * expects a constant buffer size. */
	guint last_buffer_size;
};


static GstImxDmaBufferUploadMethodContext* dmabuf_upload_method_create(GstImxDmaBufferUploader *uploader)
{
	struct DmabufUploadMethodContext *upload_method_context;

	g_assert(GST_IS_IMX_ION_ALLOCATOR(uploader->imx_dma_buffer_allocator));

	upload_method_context = g_new0(struct DmabufUploadMethodContext, 1);

	upload_method_context->parent.uploader = uploader;
	upload_method_context->buffer_pool = NULL;
	upload_method_context->last_buffer_size = 0;

	return (GstImxDmaBufferUploadMethodContext*)upload_method_context;
}


static void dmabuf_upload_method_destroy(GstImxDmaBufferUploadMethodContext *upload_method_context)
{
	struct DmabufUploadMethodContext *self = (struct DmabufUploadMethodContext *)upload_method_context;

	if (self != NULL)
	{
		if (self->buffer_pool != NULL)
			gst_object_unref(GST_OBJECT(self->buffer_pool));

		g_free(self);
	}
}


static GstFlowReturn dmabuf_upload_method_perform(GstImxDmaBufferUploadMethodContext *upload_method_context, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	int dmabuf_fd, dup_dmabuf_fd;
	gsize size;
	GstMemory *input_memory;
	GstMemory *output_memory;
	struct DmabufUploadMethodContext *self = (struct DmabufUploadMethodContext *)upload_method_context;

	input_memory = gst_buffer_peek_memory(input_buffer, 0);

	if (!gst_is_dmabuf_memory(input_memory))
		return GST_FLOW_COULD_NOT_UPLOAD;

	/* We do not actually copy the bytes, like the raw upload method does.
	 * Instead, we dup() the DMA-BUF FD so we can share ownership over it
	 * and close() our FD when we are done with it. Then, we wrap the FD
	 * in an GstImxIonAllocator-allocated GstMemory. In other words, the FD
	 * is wrapped in a custom ImxDmaBuffer. This is how we "upload". */

	size = input_memory->size;
	dmabuf_fd = gst_dmabuf_memory_get_fd(input_memory);
	g_assert(dmabuf_fd > 0);

	dup_dmabuf_fd = dup(dmabuf_fd);
	if (G_UNLIKELY(dup_dmabuf_fd < 0))
	{
		GST_ERROR_OBJECT(self->parent.uploader, "could not duplicate dmabuf FD: %s (%d)", strerror(errno), errno);
			return GST_FLOW_ERROR;
	}

	GST_LOG_OBJECT(
		self->parent.uploader,
		"wrapping duplicated DMA-BUF FD as part of the upload process; original FD: %d duplicated FD: %d size: %" G_GSIZE_FORMAT " maxsize: %" G_GSIZE_FORMAT " align: %" G_GSIZE_FORMAT " offset: %" G_GSIZE_FORMAT,
		dmabuf_fd,
		dup_dmabuf_fd,
		size,
		input_memory->maxsize,
		input_memory->align,
		input_memory->offset
	);

	output_memory = gst_imx_ion_allocator_wrap_dmabuf(self->parent.uploader->imx_dma_buffer_allocator, dup_dmabuf_fd, size);
	output_memory->maxsize = input_memory->maxsize;
	output_memory->align = input_memory->align;
	output_memory->offset = input_memory->offset;

	// TODO: use buffer pool
	*output_buffer = gst_buffer_new();
	gst_buffer_append_memory(*output_buffer, output_memory);

	return GST_FLOW_OK;
}


static const GstImxDmaBufferUploadMethodType dmabuf_upload_method_type = {
	"DmabufUpload",

	dmabuf_upload_method_create,
	dmabuf_upload_method_destroy,
	dmabuf_upload_method_perform
};


#endif




static GstImxDmaBufferUploadMethodType const *upload_method_types[] = {
	&direct_imx_dma_buffer_upload_method_type,
#ifdef WITH_GST_ION_ALLOCATOR
	&dmabuf_upload_method_type,
#endif
	&raw_buffer_upload_method_type
};

gint num_upload_method_types = G_N_ELEMENTS(upload_method_types);




G_DEFINE_TYPE(GstImxDmaBufferUploader, gst_imx_dma_buffer_uploader, GST_TYPE_OBJECT)


static void gst_imx_dma_buffer_uploader_finalize(GObject *object);
static void gst_imx_dma_buffer_uploader_destroy_upload_method_contexts(GstImxDmaBufferUploader *uploader);


static void gst_imx_dma_buffer_uploader_class_init(GstImxDmaBufferUploaderClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_dma_buffer_uploader_finalize);
}


static void gst_imx_dma_buffer_uploader_init(GstImxDmaBufferUploader *uploader)
{
	uploader->upload_method_contexts = NULL;
	uploader->imx_dma_buffer_allocator = NULL;
	uploader->output_caps = NULL;
}


static void gst_imx_dma_buffer_uploader_finalize(GObject *object)
{
	GstImxDmaBufferUploader *self = GST_IMX_DMA_BUFFER_UPLOADER(object);

	gst_imx_dma_buffer_uploader_destroy_upload_method_contexts(self);

	gst_caps_replace(&(self->output_caps), NULL);
	gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));

	GST_DEBUG_OBJECT(self, "destroyed GstImxDmaBufferUploader instance %" GST_PTR_FORMAT, (gpointer)self);

	G_OBJECT_CLASS(gst_imx_dma_buffer_uploader_parent_class)->finalize(object);
}


GstImxDmaBufferUploader* gst_imx_dma_buffer_uploader_new(GstAllocator *imx_dma_buffer_allocator)
{
	gint i;
	GstImxDmaBufferUploader *uploader;

	g_assert(imx_dma_buffer_allocator != NULL);
	g_assert(GST_IS_IMX_DMA_BUFFER_ALLOCATOR(imx_dma_buffer_allocator));
#ifdef WITH_GST_ION_ALLOCATOR
	g_assert(GST_IS_IMX_ION_ALLOCATOR(imx_dma_buffer_allocator));
#endif

	uploader = g_object_new(gst_imx_dma_buffer_uploader_get_type(), NULL);
	uploader->imx_dma_buffer_allocator = gst_object_ref(imx_dma_buffer_allocator);

	GST_DEBUG_OBJECT(
		uploader,
		"created new GstImxDmaBufferUploader instance %" GST_PTR_FORMAT ", using ImxDmaBuffer allocator %" GST_PTR_FORMAT,
		(gpointer)uploader,
		(gpointer)imx_dma_buffer_allocator
	);

	uploader->upload_method_contexts = g_malloc0(sizeof(GstImxDmaBufferUploadMethodContext *) * num_upload_method_types);

	for (i = 0; i < num_upload_method_types; ++i)
	{
		GstImxDmaBufferUploadMethodContext *context = upload_method_types[i]->create(uploader);
		if (context == NULL)
		{
			GST_ERROR_OBJECT(uploader, "failed to create %s upload method context", upload_method_types[i]->name);
			goto error;
		}

		uploader->upload_method_contexts[i] = context;
	}

finish:
	return uploader;

error:
	gst_object_unref(GST_OBJECT(uploader));
	uploader = NULL;
	goto finish;
}


GstFlowReturn gst_imx_dma_buffer_uploader_perform(GstImxDmaBufferUploader *uploader, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	gint i;
	GstFlowReturn flow_ret;

	g_assert(input_buffer != NULL);
	g_assert(output_buffer != NULL);

	if (gst_buffer_n_memory(input_buffer) == 0)
	{
		/* No point in using any upload method here, since there are no
		 * contents to upload. Just ref the input buffer and return it. */
		*output_buffer = gst_buffer_ref(input_buffer);
		return GST_FLOW_OK;
	}

	for (i = 0; i < num_upload_method_types; ++i)
	{
		GstImxDmaBufferUploadMethodType const *upload_method_type = upload_method_types[i];

		g_assert(upload_method_type != NULL);

		flow_ret = upload_method_type->perform(uploader->upload_method_contexts[i], input_buffer, output_buffer);
		if (flow_ret == GST_FLOW_COULD_NOT_UPLOAD)
			continue;
		else
			break;
	}

	if (flow_ret == GST_FLOW_COULD_NOT_UPLOAD)
	{
		GST_ERROR_OBJECT(uploader, "could not upload buffer since none of the upload methods support its memory; buffer: %" GST_PTR_FORMAT, (gpointer)input_buffer);
		flow_ret = GST_FLOW_ERROR;
	}

	return flow_ret;
}


static void gst_imx_dma_buffer_uploader_destroy_upload_method_contexts(GstImxDmaBufferUploader *uploader)
{
	gint i;

	if (uploader->upload_method_contexts == NULL)
		return;

	for (i = 0; i < num_upload_method_types; ++i)
	{
		GstImxDmaBufferUploadMethodType const *upload_method_type = upload_method_types[i];
		g_assert(upload_method_type != NULL);
		GST_DEBUG_OBJECT(uploader, "destroying upload method context of type \"%s\"", upload_method_type->name);
		upload_method_type->destroy(uploader->upload_method_contexts[i]);
	}

	g_free(uploader->upload_method_contexts);

	uploader->upload_method_contexts = NULL;
}
