/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

/**
 * SECTION:gstimxdmaheapallocator
 * @title: GstImxDmaHeapAllocator
 * @short_description: ImxDmabuffer-backed allocator using the dma-heap libimxdmabuffer allocator
 * @see_also: #GstMemory, #GstImxDmaBufAllocator
 */
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include <imxdmabuffer/imxdmabuffer_config.h>
#include <imxdmabuffer/imxdmabuffer_dma_heap_allocator.h>
#include "gstimxdmaheapallocator.h"
#include "gstimxdmabufallocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_dma_heap_allocator_debug);
#define GST_CAT_DEFAULT imx_dma_heap_allocator_debug


enum
{
	PROP_0,
	PROP_EXTERNAL_DMA_HEAP_FD,
	PROP_HEAP_FLAGS,
	PROP_FD_FLAGS
};


#define DEFAULT_EXTERNAL_DMA_HEAP_FD   (-1)


struct _GstImxDmaHeapAllocator
{
	GstImxDmaBufAllocator parent;

	ImxDmaBufferAllocator *imxdmabuffer_allocator;

	int external_dma_heap_fd;
	guint heap_flags;
	guint fd_flags;
};


struct _GstImxDmaHeapAllocatorClass
{
	GstImxDmaBufAllocatorClass parent_class;
};


G_DEFINE_TYPE(GstImxDmaHeapAllocator, gst_imx_dma_heap_allocator, GST_TYPE_IMX_DMABUF_ALLOCATOR)

static void gst_imx_dma_heap_allocator_dispose(GObject *object);
static void gst_imx_dma_heap_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_dma_heap_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_dma_heap_allocator_activate(GstImxDmaBufAllocator *allocator);
static guintptr gst_imx_dma_heap_allocator_get_physical_address(GstImxDmaBufAllocator *allocator, int dmabuf_fd);
static ImxDmaBufferAllocator* gst_imx_dma_heap_allocator_get_allocator(GstImxDmaBufAllocator *allocator);


static void gst_imx_dma_heap_allocator_class_init(GstImxDmaHeapAllocatorClass *klass)
{
	GObjectClass *object_class;
	GstImxDmaBufAllocatorClass *imx_dmabuf_allocator_class;

	GST_DEBUG_CATEGORY_INIT(imx_dma_heap_allocator_debug, "imxdmaheapallocator", 0, "physical memory allocator based on DMA-BUF heaps");

	object_class = G_OBJECT_CLASS(klass);
	imx_dmabuf_allocator_class = GST_IMX_DMABUF_ALLOCATOR_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_get_property);

	imx_dmabuf_allocator_class->activate = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_activate);
	imx_dmabuf_allocator_class->get_physical_address = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_get_physical_address);
	imx_dmabuf_allocator_class->get_allocator = GST_DEBUG_FUNCPTR(gst_imx_dma_heap_allocator_get_allocator);

	g_object_class_install_property(
		object_class,
		PROP_EXTERNAL_DMA_HEAP_FD,
		g_param_spec_int(
			"external-dma-heap",
			"External dma-heap FD",
			"External, already existing dma-heap file descriptor to use (-1 = internally open a DMA-BUF heap and get an FD for it)",
			-1, G_MAXINT,
			DEFAULT_EXTERNAL_DMA_HEAP_FD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_HEAP_FLAGS,
		g_param_spec_uint(
			"heap-flags",
			"Heap flags",
			"Flags for the dma-heap itself",
			0, G_MAXUINT,
			IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_HEAP_FLAGS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FD_FLAGS,
		g_param_spec_uint(
			"fd-flags",
			"FD flags",
			"Flags for the DMA-BUF FD of newly allocated buffers",
			0, G_MAXUINT,
			IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_FD_FLAGS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_dma_heap_allocator_init(GstImxDmaHeapAllocator *self)
{
	self->imxdmabuffer_allocator = NULL;
	self->external_dma_heap_fd = DEFAULT_EXTERNAL_DMA_HEAP_FD;
	self->heap_flags = IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_HEAP_FLAGS;
	self->fd_flags = IMX_DMA_BUFFER_DMA_HEAP_ALLOCATOR_DEFAULT_FD_FLAGS;
}


static void gst_imx_dma_heap_allocator_dispose(GObject *object)
{
	GstImxDmaHeapAllocator *self = GST_IMX_DMA_HEAP_ALLOCATOR(object);
	GST_TRACE_OBJECT(self, "finalizing dma-heap GstAllocator %p", (gpointer)self);

	if (self->imxdmabuffer_allocator != NULL)
	{
		imx_dma_buffer_allocator_destroy(self->imxdmabuffer_allocator);
		self->imxdmabuffer_allocator = NULL;
	}

	G_OBJECT_CLASS(gst_imx_dma_heap_allocator_parent_class)->dispose(object);
}


static void gst_imx_dma_heap_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxDmaHeapAllocator *self = GST_IMX_DMA_HEAP_ALLOCATOR(object);

	GST_OBJECT_LOCK(object);
	if (gst_imx_dmabuf_allocator_is_active(GST_ALLOCATOR_CAST(self)))
	{
		GST_OBJECT_UNLOCK(object);
		GST_ERROR_OBJECT(self, "cannot set property; allocator already active");
		return;
	}

	switch (prop_id)
	{
		case PROP_EXTERNAL_DMA_HEAP_FD:
		{
			self->external_dma_heap_fd = g_value_get_int(value);
			GST_DEBUG_OBJECT(self, "set external dma-heap FD to %d", self->external_dma_heap_fd);
			GST_OBJECT_UNLOCK(object);
			break;
		}

		case PROP_HEAP_FLAGS:
			self->heap_flags = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_FD_FLAGS:
			self->fd_flags = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			GST_OBJECT_UNLOCK(object);
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_dma_heap_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxDmaHeapAllocator *self = GST_IMX_DMA_HEAP_ALLOCATOR(object);

	switch (prop_id)
	{
		case PROP_EXTERNAL_DMA_HEAP_FD:
			GST_OBJECT_LOCK(object);
			g_value_set_int(value, self->external_dma_heap_fd);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_HEAP_FLAGS:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, self->heap_flags);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_FD_FLAGS:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, self->fd_flags);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_dma_heap_allocator_activate(GstImxDmaBufAllocator *allocator)
{
	GstImxDmaHeapAllocator *self = GST_IMX_DMA_HEAP_ALLOCATOR(allocator);
	int error;

	if (self->imxdmabuffer_allocator != NULL)
		return TRUE;

	self->imxdmabuffer_allocator = imx_dma_buffer_dma_heap_allocator_new(
		self->external_dma_heap_fd,
		self->heap_flags,
		self->fd_flags,
		&error
	);

	if (self->imxdmabuffer_allocator == NULL)
	{
		GST_ERROR_OBJECT(self, "could not create dma-heap allocator: %s (%d)", strerror(error), error);
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "created dma-heap allocator");

	return TRUE;
}


static guintptr gst_imx_dma_heap_allocator_get_physical_address(GstImxDmaBufAllocator *allocator, int dmabuf_fd)
{
	guintptr physical_address;
	int error;

	physical_address = imx_dma_buffer_dma_heap_get_physical_address_from_dmabuf_fd(dmabuf_fd, &error);
	if (physical_address == 0)
		GST_ERROR_OBJECT(allocator, "could not open get physical address from dmabuf FD: %s (%d)", strerror(error), error);

	return physical_address;
}


static ImxDmaBufferAllocator* gst_imx_dma_heap_allocator_get_allocator(GstImxDmaBufAllocator *allocator)
{
	GstImxDmaHeapAllocator *self = GST_IMX_DMA_HEAP_ALLOCATOR(allocator);
	return self->imxdmabuffer_allocator;
}


GstAllocator* gst_imx_dma_heap_allocator_new(void)
{
	GstAllocator *imx_dma_heap_allocator = GST_ALLOCATOR_CAST(g_object_new(gst_imx_dma_heap_allocator_get_type(), NULL));

	GST_DEBUG_OBJECT(imx_dma_heap_allocator, "created new dma-heap i.MX DMA allocator %s", GST_OBJECT_NAME(imx_dma_heap_allocator));

	/* Clear floating flag */
	gst_object_ref_sink(GST_OBJECT(imx_dma_heap_allocator));

	return imx_dma_heap_allocator;
}
