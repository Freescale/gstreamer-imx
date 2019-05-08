gstreamer-imx
=============

About
-----

This is a set of [GStreamer 1.0](http://gstreamer.freedesktop.org/) plugins for NXP's
i.MX platform, which make use of the i.MX multimedia capabilities.

**NOTE:** This is a major new release, in development, currently focused on finishing VPU support
for i.MX6 and i.MX8 devices. Since not all i.MX8 devices have been relesed yet, and some of
them have different hardware video codecs, it is not 100% guaranteed that these plugins will work
on all i.MX6 and i.MX8 variants at the moment.

Currently tested on: i.MX6DL, i.MX6Q, i.MX8m, i.MX8m mini


License
-------

These plugins are licensed under the LGPL v2.


Available decoder and encoder elements
--------------------------------------

There is one decoder element for each format the i.MX SoC supports. The list of supported formats depends on
the i.MX variant. For example, if h.264 is supported, there will be a `imxvpudec_h264` element.
The same is true of the encoder elements.

For the full list of formats that could theoretically be supported, check out the [libimxvpuapi library](https://github.com/Freescale/libimxvpuapi)
version 2.0.0 or later.


Other elements
--------------

* `imxuniaudiodec` : audio decoder plugin based on NXP's unified audio (UniAudio) architecture
* `imxmp3audioenc` : MP3 audio encoder plugin based on NXP's MP3 encoder


Integration with GStreamer and other external elements
------------------------------------------------------

All DMA buffer allocators that are used by the elements implement the [GstPhysMemoryAllocator interface](https://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gst-plugins-base-libs/html/gst-plugins-base-libs-GstPhysMemoryAllocator.html).
This makes it possible to integrate them with other GStreamer elements, as long as said elements can make
use of the GstPhysMemory functionality to extract physical addresses. In addition, if the i.MX Soc BSP
supports ION based DMA buffer allocation, the elements will use an allocator based on
[GstDmaBufAllocator](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-dmabuf.html).
This allocator also implemnts the GstPhysMemoryAllocator interface, so with this, it is possible to
access the underlying DMA buffer by its DMA-BUF file descriptor and also by its physical address, making
integration even easier.

This is also why the old `imxeglvivsink` element is not present anymore - it is unnecessary by now, since
glimagesink and waylandsink both are full replacements.

Furthermore, if applications or other GStreamer elements wish to access some of the gstreamer-imx specific
functionality (particularly its allocators and interfaces), the gstimxcommon library can be used for this
purpose, since its API is public.


Dependencies
------------

* GStreamer version 1.14 or later
* [libimxdmabuffer](https://github.com/Freescale/libimxdmabuffer) version 0.9.1 or later
* [libimxvpuapi](https://github.com/Freescale/libimxvpuapi) version 2.0.0 or later (might require accessing
  its v2 branch instead of master unless v2 was merged into its master branch already)

Also, the `videoparsersbad` plugin from the `gst-plugins-bad` package in GStreamer is needed, since
this plugin contains video parsers like `h265parse`, `h264parse`, `mpegvideoparse` (for MPEG1 and MPEG2),
and `mpeg4videoparse` (for MPEG4).

You must also use a Linux kernel with i.MX additions for the VPU and IPU subsystems.


Building and installing
-----------------------

gstreamer-imx uses [meson](https://mesonbuild.com) as its build system. Amongst other reasons, this makes
integration with existing GStreamer build setups easier, such as [Cerbero](https://gitlab.freedesktop.org/gstreamer/cerbero).

First, set up [the necessary cross compilation configuration for meson](https://mesonbuild.com/Cross-compilation.html).

Then, create a build directory for an out-of-tree build:

    make build
    cd build

Now set up the build by running meson:

    meson ..

You might want to look into the `--buildtype=plain` flag if the compiler flags Meson adds are a problem.
This is particularly useful for packagers. [Read here for more.](https://mesonbuild.com/Quick-guide.html#using-meson-as-a-distro-packager)

Also, you might be interested in the `-Dprefix` and `-Dlibdir` arguments to control where to install the
resulting binaries. Again, this is particularly useful for packagers.

Finally, build and install the code by running ninja:

    ninja install


TODO
----

* Android support
* Debian packaging support
* Reintroduce IPU/PxP/G2D based 2D functionality (IPU/PxP not relevant for i.MX8)
* Submit new Yocto recipe
* Reintroduce special imxv4l2videosrc element (currently not relevant for i.MX8)
* More documentation in wiki
