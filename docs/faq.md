Frequently Asked Questions
==========================

This a list of issues and questions often encountered by gstreamer-imx users. In all cases, make sure you
use the latest stable imx kernel. In particular, avoid the 3.0.35 kernel, which is known to have a large
number of serious issues.


I observed a problem with gstreamer-imx, and want to send in logs for debugging
-------------------------------------------------------------------------------

Set the `GST_DEBUG` environment variable to `2,*imx*:9`. To enable additional debugging output in the
`imx-vpu` library itself, also set the `VPU_LIB_DBG` environment variable to `4`. Note that the volume of
the log output can be large with these values, so it is recommended to redirect stderr to a file.


VPU operation freezes, and "VPU blocking: timeout" messages appear
------------------------------------------------------------------
This is typically caused by an overclocked VPU or a problem in the imx-vpu library.

The VPU is clocked at 266 MHz by default (according to the VPU documentation). Some configurations clock
the VPU at 352 MHz, and can exhibit VPU timeout problems (most often occurring when using the `imxvpuenc_h264`
h.264 encoder element).

imx-vpu versions prior to 5.4.31 also have been observed to cause VPU timeouts. These seem to be related
to the 5.4.31 fix described as: "Fix VPU blocked in BWB module".


Physical memory (VPU/IPU/PxP/G2D) allocation fails
--------------------------------------------------

The reason for this often can be found in the CMA configuration. Keep in mind that HD video playback can
use quite a lot of DMA memory. Main/high profile h.264 streams can require up to 17 frames for decoding.
Additional 6 frames are needed to make sure the VPU never runs out of space to decode into, leading to a
total maximum of 23 frames. With full HD videos, one frame has 1920x1088 pixels (1088 because sizes have
to be aligned to 16-pixel boundaries). This leads to approximately 69 MB of DMA memory needed for those
23 frames. (1920x1088 pixels, 12 bits per pixel for I420 data, for 23 frames -> 72069120 bytes). If
multiple full HD decoding pipelines run in parallel, there must be 69 MB available for each one of them.

When debugging memory allocation problems, also consider enabling CMA debugging. CMA is the system in
the Linux kernel which will ultimately be responsible for the DMA memory allocations, no matter if it
is allocated through the VPU, IPU, G2D, PxP APIs. Enabled CMA debugging will print information about
every CMA call in the kernel log. In the kernel configuration, `CONFIG_CMA_DEBUG` must be set to `y` to
enable CMA debugging.

If you have been using `imxeglvivsink` as the video sink, try using `imxipuvideosink` or `imxg2dvideosink`
instead for tests. Some GPU driver releases do have memory leaks. The IPU however is not a part of the
GPU. Therefore, if no memory leaks appear (or they are significantly reduced) by running the GStreamer
pipeline with `imxipuvideosink` instead of `imxeglvivsink`, it is clear that the GPU drivers are the cause.
`imxg2dvideosink` too can be tried, but it is recommended to try `imxipuvideosink` first for these tests,
since G2D is the API for the GPU's 2D core, so it isn't really separate from the GPU (just separate from
its 3D core).

Similarly, if the GPU was somehow involved in your application (for example, in Qt5 based programs),
check if upgrading the GPU driver fixes the issues.

NOTE: There is a known memory leak in the imx-gpu-viv driver package version 5.0.11.p4.4. It affects
OpenGL ES, and therefore `imxeglvivsink`, but not `imxg2dvideosink`. Upgrading is strongly recommended.


Video tearing is visible
------------------------

Tearing occurs when the video output is not in sync with the display's refresh rate. The process of
synchronizing the output to the refresh rate is also referred to as "vsync".

If the video frames are displayed directly on the framebuffer, this is easy to fix. The blitter-based
video sinks (= the IPU, G2D, PxP sinks) have a "use-vsync" property, which is set to false by default.
If set to true, it reconfigures the framebuffer, enlarging its virtual height. It then performs page
flipping during playback. The page flipping is synchronized to the display's refresh rate, eliminating
the tearing effects. If `imxeglvivsink` is used, the `FB_MULTI_BUFFER` environment variable needs to be
set to `2`. This instructs the Vivante EGL libraries to set up the framebuffer in a way that is similar
to what the blitter-based sinks do.

Note that the framebuffer is reconfigured to fit three pages, not two. The reason for this is a hardcoded
assumption in the i.MX MXC framebuffer kernel driver. Its logic requires the presence of three pages.
Flipping between just two causes subtle tearing problems. So, instead, the flipping cycles through three
pages.

Furthermore, the framebuffer is only reconfigured if it can't hold three pages. If it can, it won't
reconfigure. In some cases this might be desirable, for example, when combining blitter-based video sinks
with a Qt5 application by drawing Qt5 content on an overlayed framebuffer and the video frame on the
bottom framebuffer. (See [this Github issue](https://github.com/Freescale/gstreamer-imx/issues/98) for
an example.)

In X11, vsync is not doable from the gstreamer-imx side. It would require changes to the existing i.MX6
X11 driver. So far, no such change has been made, meaning that as of now, tearing-free video playback
in X11 is not possible.

In Wayland, vsync is possible when using Weston as the Wayland compositor. Weston can use OpenGL ES for
rendering and also Vivante's G2D. With OpenGL ES, the `FB_MULTI_BUFFER` approach mentioned above enables
vsync for Weston output. This means that the `export FB_MULTI_BUFFER=2` line needs to be added to the
Weston init script. `imxeglvivsink` can then be used to display video in Wayland, and it will automatically
be in sync with the display's refresh rate.


When using IPU elements, `ipu_task` failures occur
--------------------------------------------------

Unfortunately, the IPU is quite picky when it comes to input and output frames. There are input and output
combinations that work, others that don't. It is not trivial to discern which ones are which. A future
version of the IPU elements will include more fallbacks and workarounds to avoid the combinations that
don't work.

One known limitation is that downscaling won't work if the ratio source/destination is >= 8. This applies
to both width and height. So, the ratios `src_width`/`dest_width` and `src_height`/`dest_height` must both
be smaller than 8. Scaling from 1024x1024 to 256x512 is possible, while scaling to 128x512 is not, because
1024/128 = 8.

Another issue arises with source frames larger than 1024x1024 and rotation and/or deinterlacing are enabled.
The IPU generally can't handle that.

1024x1024 is also the maximum size of one IPU tile. The driver will try to automatically decompose the
source frame into multiple tiles if either the width or height are larger than 1024. Visible artifacts have
occurred occasionally with tiling. Future IPU element versions will manually handle the tiling.

Furthermore, positioning isn't perfect with the IPU. Sometimes, if the blitter is instructed to blit a frame
to coordinates x1,y1-x2,y2, it shifts those coordinates by +-1 pixel instead.


Why does the `imxv4l2videosrc` element exist? Why not just use GStreamer's v4l2src?
---------------------------------------------------------------------------------

The `imxv4l2videosrc` element is necessary to achieve [zerocopy](zerocopy.md). The v4l CSI drivers in the
imx kernel have some extra calls that allow one to retrieve the physical address that corresponds to a
v4l buffer. Since the IPU, G2D, VPU, PxP APIs all use physical addresses to access data via DMA, this
allows for zerocopy; the data does not have to be copied by the CPU out of the v4l buffer into a
different framebuffer in order for these other subsystems to be able to make use of the captured frame.


The `imxv4l2videosrc` element was not built
-----------------------------------------

Check the output of the `./waf configure` call that was made before. The `gstphotography` library is a
dependency of `imxv4l2videosrc`. If it is not found, the element will not be built.


What is this "The GstPhotography interface is unstable API and may change in future" compiler warning?
------------------------------------------------------------------------------------------------------

The GstPhotography API is in fact considered stable. However, there was no time for the GStreamer
developers to do a final review and move it to gst-plugins-good, declaring it a stable API. This is
expected to be done for GStreamer 1.8. The warning can be ignored, since compatibility-breaking API
changes are not expected to occur.


Why are there these "gst-backport" sources inside src/compositor?
-----------------------------------------------------------------

Like GstPhotography, the composition APIs are marked as unstable. Unlike GstPhotography, these APIs
*are* expected to change. Furthermore, their headers are not exported during installation; they are
not yet considered public. To avoid breakage in the future and to work around the header problem,
these APIs were copied and placed into gst-backport. Their symbols were renamed to `GstBP*` instead
of `Gst` to avoid potential name collisions in the future. Since gstreamer-imx should remain
compatible with older GStreamer versions, these backports will be around for a long while.


What video formats are supported for en- and decoding by the VPU?
-----------------------------------------------------------------

See the [libimxvpuapi section about supported formats](https://github.com/Freescale/libimxvpuapi/blob/master/README.md#supported-formats)
for a list of formats the VPU elements can handle.


Can I use a constant bitrate with the `imxvpuenc_mjpeg` Motion JPEG encoder element?
------------------------------------------------------------------------------------

So far, this does not appear to be possible. Constant bitrate encoding requires a rate control mechanism.
The VPU's motion JPEG encoding interface is distinctly different from the other encoders, and does not
accept a target bitrate. Nor does it accept a constant quality parameter. What it does accept is a custom
quantization matrix. This matrix must be defined when the encoder is initialized, and cannot be modified
or updated during encoding. This means that even some sort of manual rate control mechanism inside the
`imxvpuenc_mjpeg` element is not possible. Only constant quality encoding is doable; the IJG's standard
quantization matrix is used as a basis, and its coefficients are scaled according to the quality parameter.
This also means that adjusting the quality during encoding too is not possible.


Why do the VPU elements use libimxvpuapi instead of libfslvpuwrap, as they did in the past?
-------------------------------------------------------------------------------------------

libfslvpuwrap's API has several problems that make it an improper choice for the VPU elements.

One big difference is that it does not allow for assigning user-defined values to input frames, which is
how frame delays and reordering are usually handled in GStreamer to ensure correct PTS/DTS values in decoded
frames. Basically, the input frame gets some sort of ID assigned. This ID is then passed by the decoder to
the corresponding decoded frame. This way, GStreamer knows what encoded frame corresponds to what decoded
one, and can assign the encoded frame's PTS and DTS values. FFmpeg/libav and Chromium's media framework use
very similar methods. libfslvpuwrap instead documents in several places what to do with regards to timestamps,
which is a different method. libimxvpuapi does support such user-defined values, and therefore is much easier
to integrate in GStreamer.

Also, libimxvpuapi development is open, its API is more thoroughly documented, and its logging system is easy
to integrate into the GStreamer logger.


I have a complex pipeline, and observed freezes with `imxvpudec`
----------------------------------------------------------------

The VPU has an important limitation which needs to be respected when creating complex pipelines with lots of
branching. The VPU *cannot* reallocate additional buffers for decoded output frames on the fly. When video
decoding begins, the VPU requests a number of DMA buffers from the caller. The caller (in this case, `imxvpudec`)
has to allocate at least this many DMA buffers. Once done, these DMA buffers are registered with the VPU
decoder instance. From that moment on, decoding continues, but it is *not* possible to register additional
DMA buffers afterwards. This means that the `imxvpudec` decoder element has to use a fixed-size pool. If all
of the pool's buffers are queued up somewhere, and do not get released, a deadlock might occur, since then,
the VPU decode has nowhere to decode to (all registered DMA buffers are occupied). This is a fundamental
problem of incompatible approaches: the VPU's fixed size buffer pool on one side, and the much more flexible
and dynamic nature of complex GStreamer pipelines on the other. For regular video watching, this is not a
problem, but it may be if lots of branching and queuing is used in custom designed complex pipelines.


The `imxvpuenc_h264` element has a `gop-size` and an `idr-interval` property. What is the difference?
-----------------------------------------------------------------------------------------------------

h.264 actually does not have GOP (Group-of-Picture) as part of the format. If the `gop-size` property
is set to a value of N, then after N frames, the VPU will produce an I frame, but not an IDR frame.
`idr-interval` forces the periodic generation of an IDR frame. This can reduce image quality, but also
makes the encoded bitstream easier to seek, and is also useful if receivers shall be able to start
receiving even after the sender has been encoding and transmitting for a while.
In addition, libimxvpuapi prepends I and IDR frames with SPS/PPS data.


Can `imxvpuenc_h264` use h.264 profiles other than the baseline one?
--------------------------------------------------------------------

No, it cannot. This is a hard VPU limitation.


The `imxvpuenc_h264` element only outputs data in the Annex.B format. Can AVCC be used instead?
-----------------------------------------------------------------------------------------------

Not at this time. It is unclear if it can be done at all.


There is an open file descriptor to `/dev/mxc_vpu` left even after I shut down the decoder pipeline
---------------------------------------------------------------------------------------------------

`/dev/mxc_vpu` is used by the imx-vpu library for communicating with the VPU. Amongst other operations,
this is also the interface for DMA memory allocations by the VPU. If any GstBuffer is still around that
houses a frame that was decoded by the VPU earlier, this GstBuffer will keep the file descriptor to that
device open. It has to, because the call to free its DMA memory cannot be performed unless the descriptor
is open. Therefore, in such cases, check if a decoded video GstBuffer leaked somewhere.


Some Div.X / XviD videos do not decode properly
-----------------------------------------------

It appears that additional firmware is needed in some cases. The default MPEG-4 decoder in the VPU
is not sufficient. This firmware needs to be licensed from Freescale/NXP. Since I do not have such
licenses, I couldn't integrate these.


Why isn't AC-3 supported in `imxuniaudiodec`?
---------------------------------------------

Again, for licensing reasons. Freescale/NXP will not hand out the AC-3 decoder unless there is a license
agreement with Dolby.


I see bits and pieces in the code which are Android specific. Does this mean Android is supported?
--------------------------------------------------------------------------------------------------

So far, gstreamer-imx support for Android is not finished. Therefore, gstreamer-imx currently does
not yet list Android support as one of its features. This is planned for a future release.
Code for rendering with OpenGL ES to an Android surface is in place, and other build script
improvements have been made, but the libtool .la file generation (needed by GStreamer's Cerbero)
isn't finished, and Cerbero scripts for gstreamer-imx aren't fully completed yet.

Here is a rough overview: Android applications that use GStreamer typically use static linking
to add the GStreamer libraries and plugins. The result is one big binary. Therefore, gstreamer-imx
too needs to be linked statically. Also, the Freescale imx-vpu library needs to be linked against.
It is already present in the Freescale Android rootfs. One way to perform the linking is to copy
the imx-vpu libraries from the rootfs to the Cerbero build's lib/ directory, and the headers to its
include/ directory (from the imx-vpu Freescale package). libimxvpuapi also needs to be built,
but this one can already be built statically with the `--enable-static` configure switch.

[This article](http://blogs.s-osg.org/kickstart-gstreamer-android-development-6-easy-steps/) is
a very good starting point about how to build GStreamer for Android and use it in apps.


I get an "unsupported format BGR" error with `imxg2dvideosink`
--------------------------------------------------------------

G2D does not support 24 bpp formats. Check if the framebuffer is configured to use a 24 or 32 bpp
format. The `fbset` tool shows the framebuffer configuration, including the bits per pixel.
