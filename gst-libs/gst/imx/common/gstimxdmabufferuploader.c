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
#ifdef GST_DMABUF_ALLOCATOR_AVAILABLE
#include "gstimxdmabufallocator.h"
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




struct _GstImxDmaBufferUploader
{
	GstObject parent;

	/*< private >*/

	GstImxDmaBufferUploadMethodContext **upload_method_contexts;

	GstAllocator *imx_dma_buffer_allocator;
};


struct _GstImxDmaBufferUploaderClass
{
	GstObjectClass parent_class;
};


struct _GstImxDmaBufferUploadMethodType
{
	gchar const *name;

	/* check_if_compatible can be set to NULL, in which case the allocator is assumed to always be compatible. */
	gboolean (*check_if_compatible)(GstAllocator *imx_dma_buffer_allocator);
	GstImxDmaBufferUploadMethodContext* (*create)(GstImxDmaBufferUploader *uploader);
	void (*destroy)(GstImxDmaBufferUploadMethodContext *upload_method_context);
	GstFlowReturn (*perform)(GstImxDmaBufferUploadMethodContext *upload_method_context, GstMemory *input_memory, GstMemory **output_memory);
};


struct _GstImxDmaBufferUploadMethodContext
{
	GstImxDmaBufferUploader *uploader;
};




struct RawBufferUploadMethodContext
{
	GstImxDmaBufferUploadMethodContext parent;
};


static GstImxDmaBufferUploadMethodContext* raw_buffer_upload_method_create(GstImxDmaBufferUploader *uploader)
{
	struct RawBufferUploadMethodContext *upload_method_context = g_new0(struct RawBufferUploadMethodContext, 1);

	upload_method_context->parent.uploader = uploader;

	return (GstImxDmaBufferUploadMethodContext*)upload_method_context;
}


static void raw_buffer_upload_method_destroy(GstImxDmaBufferUploadMethodContext *upload_method_context)
{
	g_free(upload_method_context);
}


static GstFlowReturn raw_buffer_upload_method_perform(GstImxDmaBufferUploadMethodContext *upload_method_context, GstMemory *input_memory, GstMemory **output_memory)
{
	struct RawBufferUploadMethodContext *self = (struct RawBufferUploadMethodContext *)upload_method_context;
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstMapInfo in_map_info, out_map_info;

	gst_memory_map(input_memory, &in_map_info, GST_MAP_READ);

	*output_memory = gst_allocator_alloc(self->parent.uploader->imx_dma_buffer_allocator, in_map_info.size, NULL);
	if (G_UNLIKELY((*output_memory) == NULL))
	{
		GST_ERROR_OBJECT(self->parent.uploader, "could not allocate imxdmabuffer memory");
		goto error;
	}

	gst_memory_map(*output_memory, &out_map_info, GST_MAP_WRITE);

	memcpy(out_map_info.data, in_map_info.data, in_map_info.size);

	GST_LOG_OBJECT(self->parent.uploader, "copied %" G_GSIZE_FORMAT " byte(s) from memory %p to memory %p", in_map_info.size, (gpointer)input_memory, (gpointer)(*output_memory));

finish:
	if ((*output_memory) != NULL)
		gst_memory_unmap(*output_memory, &out_map_info);

	gst_memory_unmap(input_memory, &in_map_info);
	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static const GstImxDmaBufferUploadMethodType raw_buffer_upload_method_type = {
	"RawBufferUpload",

	NULL,
	raw_buffer_upload_method_create,
	raw_buffer_upload_method_destroy,
	raw_buffer_upload_method_perform
};




#ifdef GST_DMABUF_ALLOCATOR_AVAILABLE


struct DmabufUploadMethodContext
{
	GstImxDmaBufferUploadMethodContext parent;

	GstBufferPool *buffer_pool;

	/* This one keeps track of the buffer size. If it changes,
	 * we have to create a new buffer pool, since a buffer pool
	 * expects a constant buffer size. */
	guint last_buffer_size;
};


static gboolean dmabuf_upload_method_check_if_compatible(GstAllocator *imx_dma_buffer_allocator)
{
	return GST_IS_IMX_DMABUF_ALLOCATOR(imx_dma_buffer_allocator);
}


static GstImxDmaBufferUploadMethodContext* dmabuf_upload_method_create(GstImxDmaBufferUploader *uploader)
{
	struct DmabufUploadMethodContext *upload_method_context;

	g_assert(GST_IS_IMX_DMABUF_ALLOCATOR(uploader->imx_dma_buffer_allocator));

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


static GstFlowReturn dmabuf_upload_method_perform(GstImxDmaBufferUploadMethodContext *upload_method_context, GstMemory *input_memory, GstMemory **output_memory)
{
	int dmabuf_fd, dup_dmabuf_fd;
	gsize size;
	struct DmabufUploadMethodContext *self = (struct DmabufUploadMethodContext *)upload_method_context;

	if (!gst_is_dmabuf_memory(input_memory))
		return GST_FLOW_COULD_NOT_UPLOAD;

	/* We do not actually copy the bytes, like the raw upload method does.
	 * Instead, we dup() the DMA-BUF FD so we can share ownership over it
	 * and close() our FD when we are done with it. Then, we wrap the FD
	 * in an GstImxDmaBufAllocator-allocated GstMemory. In other words,
	 * the FD is wrapped in a custom ImxDmaBuffer. This is how we "upload". */

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

	*output_memory = gst_imx_dmabuf_allocator_wrap_dmabuf(self->parent.uploader->imx_dma_buffer_allocator, dup_dmabuf_fd, size);
	(*output_memory)->maxsize = input_memory->maxsize;
	(*output_memory)->align = input_memory->align;
	(*output_memory)->offset = input_memory->offset;

	return GST_FLOW_OK;
}


static const GstImxDmaBufferUploadMethodType dmabuf_upload_method_type = {
	"DmabufUpload",

	dmabuf_upload_method_check_if_compatible,
	dmabuf_upload_method_create,
	dmabuf_upload_method_destroy,
	dmabuf_upload_method_perform
};


#endif




static GstImxDmaBufferUploadMethodType const *upload_method_types[] = {
#ifdef GST_DMABUF_ALLOCATOR_AVAILABLE
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
}


static void gst_imx_dma_buffer_uploader_finalize(GObject *object)
{
	GstImxDmaBufferUploader *self = GST_IMX_DMA_BUFFER_UPLOADER(object);

	gst_imx_dma_buffer_uploader_destroy_upload_method_contexts(self);

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
		if ((upload_method_types[i]->check_if_compatible != NULL) && !(upload_method_types[i]->check_if_compatible(imx_dma_buffer_allocator)))
		{
			GST_DEBUG_OBJECT(
				uploader,
				"upload method type \"%s\" is NOT compatible with allocator %" GST_PTR_FORMAT "; skipping this type",
				upload_method_types[i]->name,
				(gpointer)imx_dma_buffer_allocator
			);
			uploader->upload_method_contexts[i] = NULL;
			continue;
		}

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
	gint memory_idx, method_idx;
	GstFlowReturn flow_ret = GST_FLOW_OK;

	g_assert(input_buffer != NULL);
	g_assert(output_buffer != NULL);

	if (gst_buffer_n_memory(input_buffer) == 0)
	{
		/* No point in using any upload method here, since there are no
		 * contents to upload. Just ref the input buffer and return it. */
		*output_buffer = gst_buffer_ref(input_buffer);
		return GST_FLOW_OK;
	}

	/* Check if we can simply ref and passthrough the input buffer.
	 * This is the case if it consists entirely of imxdmabuffers. */
	{
		gboolean is_all_imxdmabuffer_memory = TRUE;

		for (memory_idx = 0; memory_idx < (gint)gst_buffer_n_memory(input_buffer); ++memory_idx)
		{
			GstMemory *memory = gst_buffer_peek_memory(input_buffer, memory_idx);

			if (!gst_imx_is_imx_dma_buffer_memory(memory))
			{
				is_all_imxdmabuffer_memory = FALSE;
				break;
			}
		}

		if (is_all_imxdmabuffer_memory)
		{
			GST_LOG_OBJECT(uploader, "input buffer consists only of imxdmabuffer memory blocks; passing through buffer");
			*output_buffer = gst_buffer_ref(input_buffer);
			return GST_FLOW_OK;
		}
	}

	// TODO: Use a buffer pool and reuse memory blocks as much as possible

	*output_buffer = gst_buffer_new();

	for (memory_idx = 0; memory_idx < (gint)gst_buffer_n_memory(input_buffer); ++memory_idx)
	{
		GST_LOG_OBJECT(uploader, "performing upload for memory #%d of input buffer", memory_idx);

		for (method_idx = 0; method_idx < num_upload_method_types; ++method_idx)
		{
			GstMemory *input_memory = NULL;
			GstMemory *output_memory = NULL;
			GstImxDmaBufferUploadMethodType const *upload_method_type;

			/* If the context is NULL, then the associated upload method
			 * type was found to be incompatible with the allocator. */
			if (uploader->upload_method_contexts[method_idx] == NULL)
				continue;

			upload_method_type = upload_method_types[method_idx];
			g_assert(upload_method_type != NULL);

			input_memory = gst_buffer_peek_memory(input_buffer, memory_idx);

			flow_ret = upload_method_type->perform(uploader->upload_method_contexts[method_idx], input_memory, &output_memory);
			if (flow_ret == GST_FLOW_OK)
			{
				gst_buffer_append_memory(*output_buffer, output_memory);
				break;
			}
			else if (flow_ret != GST_FLOW_COULD_NOT_UPLOAD)
				break;
		}

		if (flow_ret == GST_FLOW_COULD_NOT_UPLOAD)
		{
			GST_ERROR_OBJECT(uploader, "could not upload memory #%d from input buffer since none of the upload methods support that memory; buffer: %" GST_PTR_FORMAT, memory_idx, (gpointer)input_buffer);
			goto error;
		}
		else if (flow_ret != GST_FLOW_OK)
			goto error;
	}

	gst_buffer_copy_into(*output_buffer, input_buffer, GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META, 0, -1);
	GST_BUFFER_FLAG_UNSET(*output_buffer, GST_BUFFER_FLAG_TAG_MEMORY);

finish:
	return flow_ret;

error:
	gst_buffer_replace(output_buffer, NULL);
	if ((flow_ret == GST_FLOW_OK) || (flow_ret == GST_FLOW_COULD_NOT_UPLOAD))
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


GstAllocator* gst_imx_dma_buffer_uploader_get_allocator(GstImxDmaBufferUploader *uploader)
{
	return gst_object_ref(GST_OBJECT(uploader->imx_dma_buffer_allocator));
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
