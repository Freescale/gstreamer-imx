Debian / Ubuntu build instructions
==================================

NOTE: The original version of these build instructions can be found in the
[Gateworks wiki](http://trac.gateworks.com/wiki/ventana/ubuntu#AddingGStreamerIPUVPUGPUsupportviagstreamer-imx).
Many thanks to them for writing this!


Prerequisites
-------------

Necessary Freescale libraries and packages:

* firmware-imx-3.14.28-1.0.0.bin - Freescale VPU firmware (EULA required)
* imx-vpu-5.4.31.bin - Freescale `lib_vpu` (EULA required) - this is the low-level documented API that works with the (undocumented) VPU kernel driver API. You can think of this as a kernel driver in userspace.
* libfslcodec-4.0.3.bin - Freescale Codec Library (EULA required)
* imx-gpu-viv-5.0.11.p4.5-hfp.bin - Freescale libg2d (EULA required) - this is the low-level documented API that works with the (undocumented) Vivante Galcore GPU kernel driver API. You can think of this as a kernel driver in userspace.

In addition, the dependencies from the main README Dependencies section are needed.


Build steps
-----------

1. Install build deps (~165MB)

        apt-get install build-essential autoconf libtool wget python pkg-config git


2. Install gstreamer1.x (~200MB)

        apt-get install gstreamer1.0-x gstreamer1.0-tools
        # install videoparserbad for video parsers like h264parse, mpegvideoparse and mpeg4videoparse
        apt-get install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
        # install alsa plugin
        apt-get install gstreamer1.0-alsa

   Note that the gstreamer1.0-plugins-bad meta-package is in the multiverse package feed.
   Also, at this point you could use 'gst-launch-1.0 videotestsrc ! fbdevsink' but it will not stretch
   to the display, and will not be hardware accelerated.


3. Install Freescale VPU firmware (firmware-imx) (EULA required)

        wget http://www.freescale.com/lgfiles/NMG/MAD/YOCTO/firmware-imx-3.14.28-1.0.0.bin
        /bin/sh firmware-imx-*.bin
        # install firmware
        mkdir -p /lib/firmware/vpu
        cp firmware-imx-*/firmware/vpu/vpu_fw_imx6*.bin /lib/firmware/vpu


4. Build and install Freescale `lib_vpu` (EULA required)

        wget http://www.freescale.com/lgfiles/NMG/MAD/YOCTO/imx-vpu-5.4.31.bin
        /bin/sh imx-vpu-*.bin
        cd imx-vpu-*
        make PLATFORM=IMX6Q all
        make install # installs vpu_lib.h and vpu_io.h in /usr/include and libvpu.* in /usr/lib
        cd ..


5. (Optional) build and install Freescale Codec Library (libfslcodec) (EULA required)

        wget http://www.freescale.com/lgfiles/NMG/MAD/YOCTO/libfslcodec-4.0.3.bin
        /bin/sh libfslcodec-*.bin
        cd libfslcodec-*
        ./autogen.sh --prefix=/usr --enable-fhw --enable-vpu
        make all
        make install
        # move the libs where gstreamer plugins will find them
        mv /usr/lib/imx-mm/video-codec/* /usr/lib
        mv /usr/lib/imx-mm/audio-codec/* /usr/lib
        rm -rf /usr/lib/imx-mm/
        cd ..

   This is optional and is needed if you want the audio codec support in gstreamer-imx.
   Note the Makefile will install the libs into $prefix/lib/imx-mm which is undesirable
   so we move them after 'make install'.


6. (Optional) install Freescale libg2d (EULA required)

        wget http://www.freescale.com/lgfiles/NMG/MAD/YOCTO/imx-gpu-viv-5.0.11.p4.5-hfp.bin
        /bin/sh imx-gpu-viv-*.bin
        cd imx-gpu-viv-*
        # install just the g2d headers/libs
        cp g2d/usr/include/* /usr/include/
        cp -d g2d/usr/lib/* /usr/lib/
        # install gpu-core headers/libs
        cp -d gpu-core/usr/lib/*.so* /usr/lib/
        cp -Pr gpu-core/usr/include/* /usr/include/
        # optional: install demos
        cp -r gpu-demos/opt /
        # optional: install gpu tools
        cp -axr gpu-tools/gmem-info/usr/bin/* /usr/bin/
        cd ..

   This is part of the Freescale Vivante GPU driver and apps (imx-gpu-viv) package which
   provides libgl/libgles1/libgles2/wayland-egl/libgal-x11/egl/libopenvg/libg2d.
   It comes in soft-float (sfp) and hard-float (hfp) - we want the hard-float as we are
   using an armhf rootfs this is not required but needed for the gstreamer-imx G2D transform
   and sink plugins.


7. Build and install libimxvpuapi library

   This library provides a community based open-source API to the Freescale imx-vpu
   library (the low-level IMX6 VPU interface). It is a replacement for Freescale's
   closed-development libfslvapwrapper library.

        git clone git://github.com/Freescale/libimxvpuapi.git
        cd libimxvpuapi
        ./waf configure --prefix=/usr
        ./waf
        ./waf install
        cd ..


8. Build and install gstreamer-imx

        apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-good1.0-dev # +70MB
        git clone git://github.com/Freescale/gstreamer-imx.git
        cd gstreamer-imx
        ln -s /usr/lib/arm-linux-gnueabihf/gstreamer-1.0/ /usr/lib/gstreamer-1.0
        ./waf configure --prefix=/usr --kernel-headers=/include
        ./waf
        ./waf install
        cd ..

   Notes:
   * './waf install' installs artifacts to its prefix + /lib/gstreamer-1.0 but they need to be installed to /usr/lib/arm-linux-gnueabihf/gstreamer-1.0 which is why we created a symlink above before installing
   * The uniaudio decoder codecs are from Freescale (found in the fsl-mm-codeclib package) and you do not need these unless you want to use FSL's audio codecs instead of the GStreamer ones
   * g2d lib required to build G2D
   * Linux kernel headers are required to build PxP and IPU
   * X11 library is required to build EGL sink with Vivante direct textures (only needed for X11 support)
   * libfslaudiocodec is required to build audio plugins

   After this step you should be able to see several plugins with gst-inspect-1.0:

        # gst-inspect-1.0 | grep imx
        imxv4l2videosrc:  imxv4l2videosrc: V4L2 CSI Video Source
        imxipu:  imxipucompositor: Freescale IPU video compositor
        imxipu:  imxipuvideosink: Freescale IPU video sink
        imxipu:  imxipuvideotransform: Freescale IPU video transform
        imxpxp:  imxpxpvideotransform: Freescale PxP video transform
        imxpxp:  imxpxpvideosink: Freescale PxP video sink
        imxvpu:  imxvpuenc_mjpeg: Freescale VPU motion JPEG video encoder
        imxvpu:  imxvpuenc_mpeg4: Freescale VPU MPEG-4 video encoder
        imxvpu:  imxvpuenc_h264: Freescale VPU h.264 video encoder
        imxvpu:  imxvpuenc_h263: Freescale VPU h.263 video encoder
        imxvpu:  imxvpudec: Freescale VPU video decoder
        imxg2d:  imxg2dcompositor: Freescale G2D video compositor
        imxg2d:  imxg2dvideotransform: Freescale G2D video transform
        imxg2d:  imxg2dvideosink: Freescale G2D video sink
