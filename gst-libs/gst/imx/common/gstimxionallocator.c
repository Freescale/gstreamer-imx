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
 * SECTION:gstimxionallocator
 * @title: GstImxIonAllocator
 * @short_description: ImxDmabuffer-backed allocator using the ION libimxdmabuffer allocator
 * @see_also: #GstMemory, #GstImxDmaBufAllocator
 */
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include <imxdmabuffer/imxdmabuffer_ion_allocator.h>
#include "gstimxionallocator.h"
#include "gstimxdmabufallocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_ion_allocator_debug);
#define GST_CAT_DEFAULT imx_ion_allocator_debug


enum
{
	PROP_0,
	PROP_EXTERNAL_ION_FD,
	PROP_ION_HEAP_ID_MASK,
	PROP_ION_HEAP_FLAGS
};


#define DEFAULT_EXTERNAL_ION_FD   IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_ION_FD
#define DEFAULT_ION_HEAP_ID_MASK  IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK
#define DEFAULT_ION_HEAP_FLAGS    IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS


struct _GstImxIonAllocator
{
	GstImxDmaBufAllocator parent;

	ImxDmaBufferAllocator *imxdmabuffer_allocator;

	int external_ion_fd;
	/* Bitmask for selecting ION heaps during allocations. See the
	 * libimxdmabuffer ION allocator documentation for more. */
	guint ion_heap_id_mask;
	/* Flags to pass to the ION heap during allocations. See the
	 * libimxdmabuffer ION allocator documentation for more. */
	guint ion_heap_flags;
};


struct _GstImxIonAllocatorClass
{
	GstImxDmaBufAllocatorClass parent_class;
};


G_DEFINE_TYPE(GstImxIonAllocator, gst_imx_ion_allocator, GST_TYPE_IMX_DMABUF_ALLOCATOR)

static void gst_imx_ion_allocator_dispose(GObject *object);
static void gst_imx_ion_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ion_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_ion_allocator_activate(GstImxDmaBufAllocator *allocator);
static guintptr gst_imx_ion_allocator_get_physical_address(GstImxDmaBufAllocator *allocator, int dmabuf_fd);
static ImxDmaBufferAllocator* gst_imx_ion_allocator_get_allocator(GstImxDmaBufAllocator *allocator);


static void gst_imx_ion_allocator_class_init(GstImxIonAllocatorClass *klass)
{
	GObjectClass *object_class;
	GstImxDmaBufAllocatorClass *imx_dmabuf_allocator_class;

	GST_DEBUG_CATEGORY_INIT(imx_ion_allocator_debug, "imxionallocator", 0, "physical memory allocator based on ION and DMA-BUF");

	object_class = G_OBJECT_CLASS(klass);
	imx_dmabuf_allocator_class = GST_IMX_DMABUF_ALLOCATOR_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_property);

	imx_dmabuf_allocator_class->activate = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_activate);
	imx_dmabuf_allocator_class->get_physical_address = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_physical_address);
	imx_dmabuf_allocator_class->get_allocator = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_allocator);

	g_object_class_install_property(
		object_class,
		PROP_EXTERNAL_ION_FD,
		g_param_spec_int(
			"external-ion-fd",
			"External ION FD",
			"External, already existing ION file descriptor to use (-1 = internally open /dev/ion and get an FD for it)",
			-1, G_MAXINT,
			DEFAULT_EXTERNAL_ION_FD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ION_HEAP_ID_MASK,
		g_param_spec_uint(
			"ion-heap-id-mask",
			"ION heap ID mask",
			"Mask of ION heap IDs to allocate from",
			0, G_MAXUINT,
			DEFAULT_ION_HEAP_ID_MASK,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ION_HEAP_FLAGS,
		g_param_spec_uint(
			"ion-heap-flags",
			"ION heap flags",
			"Flags to pass to the ION heap (0 = automatically query for a heap that allocates via the DMA API; requires i.MX kernel 4.14.34 or newer)",
			0, G_MAXUINT,
			DEFAULT_ION_HEAP_FLAGS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_ion_allocator_init(GstImxIonAllocator *self)
{
	self->external_ion_fd = DEFAULT_EXTERNAL_ION_FD;
	self->ion_heap_id_mask = DEFAULT_ION_HEAP_ID_MASK;
	self->ion_heap_flags = DEFAULT_ION_HEAP_FLAGS;
}


static void gst_imx_ion_allocator_dispose(GObject *object)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(object);
	GST_TRACE_OBJECT(self, "finalizing ION GstAllocator %p", (gpointer)self);

	if (self->imxdmabuffer_allocator != NULL)
	{
		imx_dma_buffer_allocator_destroy(self->imxdmabuffer_allocator);
		self->imxdmabuffer_allocator = NULL;
	}

	G_OBJECT_CLASS(gst_imx_ion_allocator_parent_class)->dispose(object);
}


static void gst_imx_ion_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(object);

	GST_OBJECT_LOCK(object);
	if (gst_imx_dmabuf_allocator_is_active(GST_ALLOCATOR_CAST(self)))
	{
		GST_OBJECT_UNLOCK(object);
		GST_ERROR_OBJECT(self, "cannot set property; allocator already active");
		return;
	}

	switch (prop_id)
	{
		case PROP_EXTERNAL_ION_FD:
		{
			self->external_ion_fd = g_value_get_int(value);
			GST_DEBUG_OBJECT(self, "set external ION FD to %d", self->external_ion_fd);
			GST_OBJECT_UNLOCK(object);
			break;
		}

		case PROP_ION_HEAP_ID_MASK:
			self->ion_heap_id_mask = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_FLAGS:
			self->ion_heap_flags = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			GST_OBJECT_UNLOCK(object);
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ion_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(object);

	switch (prop_id)
	{
		case PROP_EXTERNAL_ION_FD:
			GST_OBJECT_LOCK(object);
			g_value_set_int(value, self->external_ion_fd);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_ID_MASK:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, self->ion_heap_id_mask);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_FLAGS:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, self->ion_heap_flags);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_ion_allocator_activate(GstImxDmaBufAllocator *allocator)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(allocator);
	int error;

	if (self->imxdmabuffer_allocator != NULL)
		return TRUE;

	self->imxdmabuffer_allocator = imx_dma_buffer_ion_allocator_new(
		self->external_ion_fd,
		self->ion_heap_id_mask,
		self->ion_heap_flags,
		&error
	);

	if (self->imxdmabuffer_allocator == NULL)
	{
		GST_ERROR_OBJECT(self, "could not create ION allocator: %s (%d)", strerror(error), error);
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "created ION allocator");

	return TRUE;
}


static guintptr gst_imx_ion_allocator_get_physical_address(GstImxDmaBufAllocator *allocator, int dmabuf_fd)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(allocator);
	guintptr physical_address;
	int error;

	physical_address = imx_dma_buffer_ion_get_physical_address_from_dmabuf_fd(imx_dma_buffer_ion_allocator_get_ion_fd(self->imxdmabuffer_allocator), dmabuf_fd, &error);
	if (physical_address == 0)
		GST_ERROR_OBJECT(allocator, "could not open get physical address from dmabuf FD: %s (%d)", strerror(error), error);

	return physical_address;
}


static ImxDmaBufferAllocator* gst_imx_ion_allocator_get_allocator(GstImxDmaBufAllocator *allocator)
{
	GstImxIonAllocator *self = GST_IMX_ION_ALLOCATOR(allocator);
	return self->imxdmabuffer_allocator;
}


GstAllocator* gst_imx_ion_allocator_new(void)
{
	GstAllocator *imx_ion_allocator = GST_ALLOCATOR_CAST(g_object_new(gst_imx_ion_allocator_get_type(), NULL));

	GST_DEBUG_OBJECT(imx_ion_allocator, "created new ION i.MX DMA allocator %s", GST_OBJECT_NAME(imx_ion_allocator));

	/* Clear floating flag */
	gst_object_ref_sink(GST_OBJECT(imx_ion_allocator));

	return imx_ion_allocator;
}
