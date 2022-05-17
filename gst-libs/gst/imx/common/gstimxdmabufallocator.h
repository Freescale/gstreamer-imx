/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2022  Carlos Rafael Giani
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

#ifndef GST_IMX_DMABUF_ALLOCATOR_H
#define GST_IMX_DMABUF_ALLOCATOR_H

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <imxdmabuffer/imxdmabuffer.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_DMABUF_ALLOCATOR             (gst_imx_dmabuf_allocator_get_type())
#define GST_IMX_DMABUF_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_DMABUF_ALLOCATOR, GstImxDmaBufAllocator))
#define GST_IMX_DMABUF_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_DMABUF_ALLOCATOR, GstImxDmaBufAllocatorClass))
#define GST_IMX_DMABUF_ALLOCATOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_DMABUF_ALLOCATOR, GstImxDmaBufAllocatorClass))
#define GST_IMX_DMABUF_ALLOCATOR_CAST(obj)        ((GstImxDmaBufAllocator *)(obj))
#define GST_IS_IMX_DMABUF_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_DMABUF_ALLOCATOR))
#define GST_IS_IMX_DMABUF_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_DMABUF_ALLOCATOR))


typedef struct _GstImxDmaBufAllocator GstImxDmaBufAllocator;
typedef struct _GstImxDmaBufAllocatorClass GstImxDmaBufAllocatorClass;
typedef struct _GstImxDmaBufAllocatorPrivate GstImxDmaBufAllocatorPrivate;


struct _GstImxDmaBufAllocator
{
    GstDmaBufAllocator parent;

    /*< private >*/

    GstImxDmaBufAllocatorPrivate *priv;
};


struct _GstImxDmaBufAllocatorClass
{
    GstDmaBufAllocatorClass parent_class;

    gboolean (*activate)(GstImxDmaBufAllocator *allocator);
    guintptr (*get_physical_address)(GstImxDmaBufAllocator *allocator, int dmabuf_fd);
    ImxDmaBufferAllocator* (*get_allocator)(GstImxDmaBufAllocator *allocator);
};


GType gst_imx_dmabuf_allocator_get_type(void);

/**
 * gst_imx_dmabuf_allocator_get_physical_address:
 * @allocator: Allocator to use.
 * @dmabuf_fd: DMA-BUF FD to get a physical address from. Must be valid.
 *
 * Retrieves a physical address for the given DMA-BUF file descriptor.
 *
 * Returns: The physical address to the physically contiguous DMA memory
 *          block represented by the DMA-BUF FD, or 0 if retrieving the
 *          physical address fails.
 */
guintptr gst_imx_dmabuf_allocator_get_physical_address(GstImxDmaBufAllocator *allocator, int dmabuf_fd);

/**
 * gst_imx_dmabuf_allocator_wrap_dmabuf:
 * @allocator: Allocator to use.
 * @dmabuf_fd: DMA-BUF FD to wrap. Must be valid.
 * @dmabuf_size: Size of the DMA-BUF buffer, in bytes. Must be greater than zero.
 *
 * Wraps the specified DMA-BUF FD in an ImxDmaBuffer that is in turn
 * contained in a GstMemory. That GstMemory will have @allocator set
 * as its allocator. @allocator must be based on #GstImxDmaBufAllocator.
 *
 * Note that the GstMemory will take ownership over the DMA-BUF FD,
 * meaning that the FD will be closed when the memory is disposed of.
 * To make sure this does not deallocate the DMA-BUF, use the POSIX
 * dup() call to create a duplicate FD.
 *
 * Returns: GstMemory containing an ImxDmaBuffer which in turn wraps the
 *          @dmabuf_fd duplicate created internally by this function.
 */
GstMemory* gst_imx_dmabuf_allocator_wrap_dmabuf(GstAllocator *allocator, int dmabuf_fd, gsize dmabuf_size);

/**
 * gst_imx_dmabuf_allocator_is_active:
 * @allocator: Allocator to check.
 *
 * Checks if this i.MX DMA-BUF allocator is active. An active DMA-BUF
 * allocator is one whose activate vmethod has been called. This is
 * done exactly once during the lifetime of the allocator, and is where
 * the allocator opens a device FD etc.
 *
 * @allocator must be based on #GstImxDmaBufAllocator.
 *
 * Returns: true if the allocator is active.
 */
gboolean gst_imx_dmabuf_allocator_is_active(GstAllocator *allocator);

/**
 * gst_imx_ion_allocator_new:
 *
 * Creates a new #GstAllocator that is based on #GstImxDmaBufAllocator.
 *
 * At least one such allocator must be enabled in the libimxdmabuffer
 * build configuration. If none is enabled, an assertion is raised.
 * If one such allocator is available, but creating it fails, this
 * function returns NULL.
 *
 * Returns: (transfer full) (nullable): Newly created allocator, or NULL in case of failure.
 */
GstAllocator* gst_imx_dmabuf_allocator_new(void);


G_END_DECLS


#endif /* GST_IMX_DMABUF_ALLOCATOR_H */
