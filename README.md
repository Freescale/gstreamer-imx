gst-fsl
=======

IMPORTANT NOTE
--------------

**THIS SOFTWARE IS IN AN ALPHA STAGE. DO NOT USE FOR PRODUCTION YET.**

About
-----

This is a set of [GStreamer 1.0](http://gstreamer.freedesktop.org/) plugins for plugins for Freescale's
i.MX platform, with emphasis on video en/decoding using the i.MX VPU engine. Currently, a decoder is
implemented. Encoders, video sinks and more will follow soon.

Currently, this software has been tested on the i.MX 6 Sabre SD Dual Lite platform only.


License
-------

These plugins are licensed under the LGPL v2.


Available plugins
-----------------

* `fslvpudec` : video decoder plugin


Dependencies
------------

You'll need a GStreamer 1.0 installation, and Freescale's VPU wrapper library (at least version 1.0.17).


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

    ./waf configure --prefix=PREFIX

(The aforementioned environment variables are only necessary for this configure call.)
PREFIX defines the installation prefix, that is, where the built binaries will be installed.

Once configuration is complete, run:

    ./waf

This builds the plugins.
Finally, to install, run:

    ./waf install

