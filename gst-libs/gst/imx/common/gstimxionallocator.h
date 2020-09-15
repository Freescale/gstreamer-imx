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

#ifndef GST_IMX_ION_ALLOCATOR_H
#define GST_IMX_ION_ALLOCATOR_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_ION_ALLOCATOR             (gst_imx_ion_allocator_get_type())
#define GST_IMX_ION_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_ION_ALLOCATOR, GstImxIonAllocator))
#define GST_IMX_ION_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_ION_ALLOCATOR, GstImxIonAllocatorClass))
#define GST_IMX_ION_ALLOCATOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_ION_ALLOCATOR, GstImxIonAllocatorClass))
#define GST_IMX_ION_ALLOCATOR_CAST(obj)        ((GstImxIonAllocator *)(obj))
#define GST_IS_IMX_ION_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_ION_ALLOCATOR))
#define GST_IS_IMX_ION_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_ION_ALLOCATOR))


typedef struct _GstImxIonAllocator GstImxIonAllocator;
typedef struct _GstImxIonAllocatorClass GstImxIonAllocatorClass;


GType gst_imx_ion_allocator_get_type(void);

/**
 * gst_imx_ion_allocator_new:
 *
 * Creates a new #GstAllocator using the libimxdmabuffer ION allocator.
 *
 * Returns: (transfer full) (nullable): Newly created allocator, or NULL in case of failure.
 */
GstAllocator* gst_imx_ion_allocator_new(void);

/**
 * gst_imx_ion_allocator_wrap_dmabuf:
 * @allocator: ION allocator to use.
 * @dmabuf_fd: DMA-BUF FD to wrap. Must be valid.
 * @dmabuf_size: Size of the DMA-BUF buffer, in bytes. Must be greater than zero.
 *
 * Wraps the specified DMA-BUF FD in an ImxDmaBuffer.
 * The returned GstMemory will have @allocator set as its allocator.
 * @allocator must be an GstImxIonAllocator instance.
 *
 * Note that the GstMemory will take ownership over the DMA-BUF FD,
 * meaning that the FD will be closed when the memory is disposed of.
 * To make sure this does not deallocate the DMA-BUF, use the POSIX
 * dup() call to create a duplicate FD.
 *
 * Returns: GstMemory containing an ImxDmaBuffer which in turn wraps the @dmabuf_fd
 * duplicate created internally by this function.
 */
GstMemory* gst_imx_ion_allocator_wrap_dmabuf(GstAllocator *allocator, int dmabuf_fd, gsize dmabuf_size);


G_END_DECLS


#endif /* GST_IMX_ION_ALLOCATOR_H */
