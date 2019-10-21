gstreamer-imx
=============

About
-----

This is a set of [GStreamer 1.0](http://gstreamer.freedesktop.org/) plugins for Freescale's
i.MX platform, which make use of the i.MX multimedia capabilities.

Currently, this software has been tested only with the i.MX6 and i.MX7 SoC families.


License
-------

These plugins are licensed under the LGPL v2.


Features
--------

* Zero-copy support
  "Zero-copy" refers to techniques to avoid unnecessary buffer copies, or at least unnecessary CPU-based
  copies (and copying via DMA instead). In gstreamer-imx, video elements try to avoid CPU-based frame copies
  as much as possible. As a result, it is possible to use playback and transcoding pipelines which run with
  very little CPU usage, since the bulk of the transfers are done via DMA. For futher details, see the
  [zerocopy and physical memory explanation](docs/zerocopy.md).
* Fully compatible with upstream GStreamer demuxers and parsers
  gstreamer-imx has been tested and used with the upstream demuxers and parsers. (Currently, there are no
  plans to add elements to gstreamer-imx which wrap Freescale parsers and demuxers.)
* Video transform elements for color space conversion and scaling
  The PxP, IPU, and G2D units in the imx SoC can perform these operations in one step. This is exposed
  via the pxp/ipu/g2d video transform elements. Not only are conversions much faster this way, they are also
  compatible with the zerocopy feature explained above.
* Deinterlacing via the imxipusink and imxipuvideotransform elements
* G2D/IPU/PxP compositor, compatible with the upstream compositor
  The imxipu/pxp/g2dcompositor elements mimic the properties of the compositor in GStreamer 1.5 and above.
  Command lines which use that compositor can make use of hardware-accelerated compositing simply by
  replacing "compositor" with "imxg2dcompositor", for example. Furthermore, the imx compositor elements
  also allow for 90-degree step rotations, alpha blending, aspect ratio correction, and empty region filling.
* Video4Linux source element with i.MX specific enhancements for zerocopy; can also be controlled via the
  GstUriHandler interface, making it possible to show camera feeds directly with uridecodebin and playbin
  simply by specifying a `imxv4l2://<camera device name>` URI
* G2D/IPU/PxP video sinks support tearing-free vsync output via page flipping


Additional reading
------------------

* [Frequently Asked Questions](docs/faq.md)
* [Zero-copy and physically contiguous memory](docs/zerocopy.md)
* [Blitter-based element architecture](docs/blitter-architecture.md)
* [Gateworks blog entries with usage examples](http://blog.gateworks.com/?p=223)


Compositing
-----------

The compositor is a new feature in gstreamer-imx 0.11.0. Just like with the compositor from gst-plugins-base 1.5.1
and newer, compositor elements support an arbitrary number of request sink pads, and one srcpad.

Example call:

    gst-launch-1.0   \
      imxg2dcompositor name=c background-color=0x223344 \
          sink_0::xpos=0 sink_0::ypos=90 sink_0::width=160 sink_0::height=110 sink_0::zorder=55 sink_0::fill_color=0xff00ff00 sink_0::alpha=0.39 sink_0::rotation=0 \
          sink_1::xpos=0 sink_1::ypos=20 sink_1::width=620 sink_1::height=380 sink_1::fill_color=0x44441133 ! \
        queue2 ! "video/x-raw, width=800, height=600" ! imxg2dvideosink \
      videotestsrc pattern=0 ! "video/x-raw, framerate=30/1" ! c.sink_0 \
      videotestsrc pattern=18 ! "video/x-raw, framerate=30/1" ! c.sink_1

This creates the following frame:

![compositor frame](compositor-example.png)

The compositor properties are accessible as usual by calling gst-inspect-1.0, like: `gst-inspect-1.0 imxg2dcompositor`

For the sinkpad properties are equal to that of the upstream compositor 

Most of the sink pad properties are the same as that of GstCompositorPad:

* `xpos`: The x-coordinate position of the top-left corner of the picture (gint)
* `ypos`: The y-coordinate position of the top-left corner of the picture (gint)
* `width`: The width of the picture; the input will be scaled if necessary (gint)
* `height`: The height of the picture; the input will be scaled if necessary (gint)
* `alpha`: The transparency of the picture; between 0.0 and 1.0. The blending is a simple copy when fully-transparent (0.0) and fully-opaque (1.0). (gdouble)
* `zorder`: The z-order position of the picture in the composition (guint)

In addition, the imx compositor pads have these properties:

* `left-margin` : Left margin in pixels, defining an empty space at the left side between the border of the outer frame and the actual inner video frame
* `top-margin` : Top margin in pixels, defining an empty space at the top side between the border of the outer frame and the actual inner video frame
* `right-margin` : Right margin in pixels, defining an empty space at the right side between the border of the outer frame and the actual inner video frame
* `bottom-margin` : Bottom margin in pixels, defining an empty space at the bottom side between the border of the outer frame and the actual inner video frame
* `rotation` : 90-degree step rotation mode for the inner video frame
* `keep-aspect-ratio` : If true, the aspect ratio of the inner video frame is maintained, potentially creating empty regions
* `input-crop` : If true, [GstVideoCropMeta](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-gstvideometa.html#GstVideoCropMeta)
                 data in input video frames will be supported; instead of blitting from the entire input video frame it then blits from the rectangle specified by this meta
* `fill-color` : What color to use to fill the aforementioned empty regions, specified as a 32-bit ABGR color value

The compositors have the notion of "inner" and "outer" frames. The "inner" frame is the actual video frame,
for example a movie. The "outer" frame is a superset of the inner one and also of any empty spaces. If for
example the outer frame is 1600x900 (16:9), and the inner frame is 1280x960 (4:3), and `keep-aspect-ratio`
is set to true, then the inner frame will be scaled to fit in the middle of the outer frame, and the leftover
spaces to the left and right are the "empty spaces". These get filled with the `fill-color`. If any of the
margin values are nonzero, then the empty spaces also include the margin regions. If `keep-aspect-ratio`
is false, no empty regions exist unless at least one the margins is nonzero.

Current limitations:
* The G2D compositor is the preferred one. The IPU compositor suffers from IPU peculiarities like "jumps"
  in the frame positioning. Also, the IPU compositor currently does not support deinterlacing.
* There is no PxP compositor at the moment, since the PxP engine always fills the entire output frames with
  black pixels, even if only a subset is drawn to.


Available plugins
-----------------

* `imxvpudec` : video decoder plugin
* `imxvpuenc_h263` : h.263 encoder
* `imxvpuenc_h264` : h.264 baseline profile Annex.B encoder
* `imxvpuenc_mpeg4` : MPEG-4 encoder
* `imxvpuenc_mjpeg` : Motion JPEG encoder
* `imxipuvideosink` : video sink using the IPU to output to Framebuffer (may not work well if X11 or Wayland are running)
* `imxipuvideotransform` : video transform element using the IPU, capable of scaling, deinterlacing, rotating (in 90 degree steps), flipping frames, and converting between color spaces
* `imxipucompositor` : video compositor element using the IPU for combining multiple input video streams into one output video stream
* `imxg2dvideosink` : video sink using the GPU's 2D core (through the G2D API) to output to Framebuffer (may not work well if X11 or Wayland are running)
* `imxg2dvideotransform` : video transform element using the GPU's 2D core (through the G2D API), capable of scaling, rotating (in 90 degree steps), flipping frames, and converting between color spaces
* `imxg2dcompositor` : video compositor element using the IPU for combining multiple input video streams into one output video stream
* `imxg2dtextoverlay` : Adds text strings on top of a video buffer using Pango and G2D
* `imxg2dtimeoverlay` : Overlays buffer time stamps on a video stream using Pango and G2D
* `imxg2dclockoverlay` : Overlays the current clock time on a video stream using Pango and G2D
* `imxg2dtextrender` : Renders a text string to an image bitmap using Pango and G2D
* `imxpxpvideosink` : video sink using the PxP engine to output to Framebuffer (may not work well if X11 or Wayland are running)
* `imxpxpvideotransform` : video transform element using the PxP engine, capable of scaling, rotating (in 90 degree steps), flipping frames, and converting between color spaces
* `imxeglvivsink` : custom OpenGL ES 2.x based video sink; using the Vivante direct textures, which allow for smooth playback
* `imxv4l2videosrc` : customized Video4Linux source with i.MX specific tweaks
* `imxv4l2videosink` : customized Video4Linux sink with i.MX specific tweaks
* `imxuniaudiodec` : audio decoder plugin based on Freescale's unified audio (UniAudio) architecture
* `imxmp3audioenc` : MP3 audio encoder plugin based on Freescale's MP3 encoder


Pango text overlay elements
---------------------------

The `imxg2dtextoverlay`, `imxg2dtimeoverlay`, `imxg2dclockoverlay`, `imxg2dtextrender` elements render text using
Pango and G2D. Currently these elements do _not_ support non-physically contiguous buffers. This means that for
example this pipeline won't work:

    videotestsrc ! imxg2dtimeoverlay ! imxg2dvideosink

For this reason, it is necessary to make sure that the video stream is made of physically contiguous buffers
(allocated with an allocator derived from GstPhysMemAllocator). Captured frames from `imxv4l2videosrc`, decoded
frames from `imxvpudec`, and transformed frames from any of the blitter-based transform elements will deliver
this type of buffers (however, the transform elements will do so only if they actually have something to transform;
if they switch to passthrough, data will be left untouched). This limitation will be lifted in later versions.


V4L2 elements
-------------

There are two V4L2 elements, `imxv4l2videosrc` and `imxg2dvideosink`. Both are necessary because they allow for
using physical memory addresses for the captured frames, thus enabling zerocopy. `imxv4l2videosrc` extract such
an address for each captured frame, while `imxg2dvideosink` draws the frame via DMA using that address. Note
however that `imxg2dvideosink` does _not_ support non-physically contiguous frames. This is because it is currently
not possible to allocate any temporary input DMA buffer inside `imxg2dvideosink` (due to a lack of an appropriate
allocator).


Integration with GStreamer and other external elements
------------------------------------------------------

There are two ways how gstreamer-imx video streams can be integrated into external elements:

* Use gstimxcommon: This library has been made public. This way, it is possible to use its headers to get
  access to the physical memory address of DMA-memory allocated buffers.
  NOTE: The ABI of this library is not yet stable, and may change.
* Use GstPhysMemory from gst-plugins-bad: This new interface from -bad is used inside gstreamer-imx if the
  GStreamer version it is built against has the new gstreamer-bad-allocators library.
  If so, then GstImxPhysMemAllocator will implement this interface.


Dependencies
------------

You'll need a GStreamer 1.2 installation, and the [libimxvpuapi library](https://github.com/Freescale/libimxvpuapi).
Also, the `videoparsersbad` plugin from the `gst-plugins-bad` package in GStreamer is needed, since
this plugin contains video parsers like `h264parse`, `mpegvideoparse` (for MPEG1 and MPEG2), and
`mpeg4videoparse` (for MPEG4).
You must also use a Linux kernel with i.MX additions for the VPU, GPU, IPU, PxP subsystems. Mainline kernels do not
contain these (yet).


Building and installing
-----------------------

This project uses the [waf meta build system](https://code.google.com/p/waf/). To configure , first set
the following environment variables to whatever is necessary for cross compilation for your platform:

* `CC`
* `CFLAGS`
* `LDFLAGS`
* `PKG_CONFIG_PATH`
* `PKG_CONFIG_SYSROOT_DIR`

Then, run:

    ./waf configure --prefix=PREFIX --kernel-headers=KERNEL-HEADER-PATH

(The aforementioned environment variables are only necessary for this configure call.)
PREFIX defines the installation prefix, that is, where the built binaries will be installed.
KERNEL-HEADER-PATH defines the path to the Linux kernel headers (where linux/ipu.h can be found).
It is currently unfortunately necessary to set this path if linux/ipu.h is not in the root filesystem's
include directory already. (Not to be confused with the ipu.h from the imx-lib.) Without this path,
the header is not found, and elements using the IPU will not be built.

If gstreamer-imx is to be built for Android, add the `--build-for-android` switch:

    ./waf configure --prefix=PREFIX --kernel-headers=KERNEL-HEADER-RPATH --build-for-android

Note that for Android, plugins are built as static libraries.

Once configuration is complete, run:

    ./waf

This builds the plugins.
Finally, to install, run:

    ./waf install

Further notes on how to build for some Linux distributions:

* [Debian / Ubuntu build instructions](docs/debian-ubuntu.md)

* Arch Linux ARM build instructions:

* Yocto / OpenEmbedded build instructions:
  An OpenEmbedded recipe for gstreamer-imx is included in [meta-freescale](https://git.yoctoproject.org/cgit/cgit.cgi/meta-freescale).
  Also check out the [Freescale Github space](http://freescale.github.io/).
  Add the meta-freescale layer to your setup's `bblayers.conf`. Then it should be possible to
  build the `gstreamer1.0-plugins-imx` recipe. This will also automatically build libimxvpuapi,
  which too has a recipe in meta-freescale.
