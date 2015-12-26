Zero-copy and physically contiguous memory
==========================================

Introduction
------------

Typically, allocate blocks from regular system memory. These blocks use a virtual
address space which may not be physically contiguous. The MMU can redirect
access to separate locations. The memory region might appear contiguous to the
application, even though it physically isn't.

In most cases, this is not a problem at all. But with hardware-accelerated blitters
and video codecs, the data transfer is performed via DMA (Direct Memory Access).
DMA allows these subsystems to transfer data without involving the CPU. Since DMA
does not know anything about virtual address spaces and has no connection to the
MMU, it can only transfer from and to memory blocks that are physically contiguous.

For this reason, gstreamer-imx understands the notion of "physically contiguous
memory", also referred to as "physical memory" and "DMA memory". These memory
blocks can be used for DMA transfers. They cannot be allocated with regular
allocation functions like malloc(), since these allocate regular system memory.
Instead, there are separate allocators for this task.

gstreamer-imx contains in the src/common/ directory a GstMeta subclass called
"GstImxPhysMemMeta". Among other things, it contains a "physical address", an
address in the physical memory address space. gstreamer-imx elements check for
the presence of GstImxPhysMemMeta in GstBuffers to see if its memory is physically
contiguous. For example, the VPU encoder base class "GstImxVpuEncoderBase"
performs this check to see if the input frame uses physically contiguous memory.
If so, the VPU can use DMA to access this frame's data. Otherwise, the encoder
first copies the pixels to an intermediate physically contiguous buffer, and
instructs the VPU to encode from there.

Physical memory allocators in gstreamer-imx are wrapped in subclasses of the
"GstImxPhysMemAllocator" class, which too is in the src/common/ directory.
Unfortunately, there is not one single allocator for all of the i.MX6 subsystems;
instead, the IPU has one, the VPU has one, G2D has one etc. However, DMA memory
allocated by a subsystem's allocator can be used in another subsystem without
problems. For example, the VPU decoder can use its allocator for DMA memory for
the decoded frames, and the IPU can read from that memory.

The end result is that data blocks aren't copied around by the CPU, which is
limited to a controller role. if the pipeline avoids CPU-based copies of
uncompressed video frames, it is considered to be a "zerocopy pipeline" or to
"use zerocopy". Technically, "zerocopy" isn't entirely correct, since copies
(transfers) can still happen, just via DMA instead of the CPU. For example, if
the VPU decodes to a DMA memory block, and the IPU blitter then blits this
frame on screen, then strictly speaking, the decoded frame is copied once
(by the blitter). Full zerocopy would require the HDMI scanout to directly read
from the DMA memory of the decoded frame, but this is problematic to set up and
often lacks colorspace conversion, scaling etc. capabilities. For this reason,
in gstreamer-imx, the more relaxed meaning of "zerocopy" is used, which includes
DMA-based but not CPU-based frame transfers/copies.

Zerocopy is critical for smooth video playback, especially with higher resolutions
and bitrates. The CPU can quickly become a bottleneck with regular pipelines on
the i.MX6. But with zerocopy pipelines, memory bandwidth is typically the main
bottleneck instead. Very often, people only care about the hardware-accelerated
video decoding, disregarding how the decoded pixels get transferred to the screen.
This is why zerocopy must be implemented not only in decoders, but also in
processing/transformation elements and in video sinks.


Elements supporting zerocopy
----------------------------

* `imxvpudec`
  Always enabled. The VPU decodes into framebuffers (which use DMA memory).
  It also has its own buffer pool. Buffers with decoded frames are then used
  downstream in the pipeline. `imxvpudec` never copies the pixels.

* `imxvpuenc_*`
  Automatically enabled whenever possible. As described in the GstImxPhysMemMeta
  description above, the encoders check if the incoming uncompressed video frame
  uses DMA memory. If so, the VPU encoder reads from that memory region directly,
  otherwise they copy the pixels with the CPU into an intermediate DMA buffer that
  the VPU then reads from.

* Blitter-based elements (IPU/PxP/G2D video sinks and transform elements)
  Automatically enabled whenever possible. Just like with the encoder elements,
  the incoming frames are checked for DMA memory. If the input frame uses DMA memory,
  then these elements can use DMA to blit the frame to the output region (and
  optionally perform colorspace conversion, scaling, alpha blending, 90-degree
  rotation during blitting). If the frame uses system memory, its pixels are copied
  to an intermediate DMA buffer first.

* `imxeglvivsink`
  Automatically enabled whenever possible. If the input frame uses DMA memory,
  The `glTexDirectVIVMap` function is used to instruct the GPU to read the pixels
  via DMA from the given memory region. If it doesn't use DMA memory, it instead
  calls the `glTexDirectVIV` function, which copies pixels.

* `imxv4l2videosrc`
  Always enabled. Captured frames are stored in DMA memory.
