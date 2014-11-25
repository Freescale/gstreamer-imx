gstreamer-imx
=============

About
-----

This is a set of [GStreamer 1.0](http://gstreamer.freedesktop.org/) plugins for plugins for Freescale's
i.MX platform, with emphasis on video en/decoding using the i.MX VPU engine.

Currently, this software has been tested only with the i.MX6 SoC family.

The software as a whole is currently in beta stage.


License
-------

These plugins are licensed under the LGPL v2.


Available plugins
-----------------

* `imxvpudec` : video decoder plugin
* `imxvpuenc_h263` : h.263 encoder
* `imxvpuenc_h264` : h.264 baseline profile Annex.B encoder
* `imxvpuenc_mpeg4` : MPEG-4 encoder
* `imxvpuenc_mjpeg` : Motion JPEG encoder
* `imxipuvideosink` : video sink using the IPU to output to Framebuffer (may not work well if X11 or Wayland are running)
* `imxipuvideotransform` : video transform element using the IPU, capable of scaling, deinterlacing, rotating (in 90 degree steps), flipping frames, and converting between color spaces
* `imxg2dvideosink` : video sink using the GPU's 2D core (through the G2D API) to output to Framebuffer (may not work well if X11 or Wayland are running)
* `imxg2dvideotransform` : video transform element using the GPU's 2D core (through the G2D API), capable of scaling, rotating (in 90 degree steps), flipping frames, and converting between color spaces
* `imxeglvivsink` : custom OpenGL ES 2.x based video sink; using the Vivante direct textures, which allow for smooth playback
* `imxv4l2src` : customized Video4Linux source with i.MX specific tweaks


Dependencies
------------

You'll need a GStreamer 1.2 installation, and Freescale's VPU wrapper library (at least version 1.0.45).


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

Once configuration is complete, run:

    ./waf

This builds the plugins.
Finally, to install, run:

    ./waf install

