Blitter-based element architecture
==================================

Introduction
------------

The first version of the IPU and G2D elements used their own separate classes,
and did not use common code. During development, it quickly became apparent that
their individual codebases were very similar, and shared a lot of concepts. Later,
these elements were rewritten and based on an abstract "blitter" concept.

The basic idea is that the blitter is the main difference. The blitter is what
copies and/or processes pixels from a source to a destination framebuffer (or a
rectangular region in a framebuffer). The i.MX6 has three blitters: one in the IPU,
one in G2D, and one in the PxP system. The new "GstImxBlitter" base class was
introduced as a common interface for blitting operations. Additional code for
blitter-based video sinks and transform elements also exist: "GstImxBlitterVideoSink"
and "GstImxBlitterVideoTransform". When the composition code was introduced, yet
another element type was written: "GstImxBlitterCompositor". Hardware-specific code
mostly worries about subclassing GstImxBlitter, and just uses an instance of the
derived blitter in subclasses of GstImxBlitterVideoSink, GstImxBlitterVideoTransform,
GstImxBlitterCompositor. For example, with G2D, the main G2D code is found in
GstImxG2DBlitter. The GstImxG2DVideoSink is a subclass of GstImxBlitterVideoSink, but
its code is very small - it essentially just instantiates GstImxG2DBlitter and passes
this instance to the GstImxBlitterVideoSink base class.

The blitter base code is located in the src/blitter/ directory.


Existing blitters
-----------------

* G2D: located in src/g2d/ . G2D is a 2D API for the Vivante GPU 2D core. It consumes
  much less power than the 3D core, making it a good choice if power consumption and/or
  heat is a concern. The G2D blitter is the most robust one, and available on all
  i.MX6 variants that have a GPU. However, it can only convert to RGB(A) colorspaces.

* IPU: located in src/ipu/ . The IPU is a basic part of the i.MX6. All variants have
  an IPU. The IPU's blitter is the only one which can deinterlace. The IPU can also convert
  to several YUV colorspaces as well as to RGB ones. However, the IPU is quite "picky"
  and refuses to operate with certain source frame / destination frame combinations. For
  example, if the destination frame's width or height are 1/8th of the source frame's
  width/height, or smaller, the IPU will not blit. So, blitting a source 512x256 frame
  to a 256x256 destination frame will work, but not to a 64x256 frame. Furthermore,
  sometimes the IPU decides to shift coordinates by +- 1 pixel, making pixel perfect
  positioning difficult. Future versions of the IPU blitter code will implement a variety
  of workarounds to address these issues.

* PxP: located in src/pxp/ . Only some i.MX6 variants support PxP. It is generally more
  useful for transformations instead of display. This is the only blitter which can
  support Y42B (= YUV 4:2:2 planar) data as input. Y42B is often used by USB webcams.
  However, even though the list of formats listed in the PxP API is large, tests have
  shown that only a subset of source/destination format combinations actually produce
  correct results. Most importantly, only a few RGB formats and grayscale output are
  supported.


Limitations of blitter-based elements
-------------------------------------

When the destination is an off-screen buffer, it is generally no problem to use
blitter-based elements. This refers to the G2D/PxP/IPU video transform elements.

With video sinks, the situation is more complicated. If the destination is the main
Linux framebuffer, and only the video output shall be shown, then it is simple to
just let the blitter-based video sink blit the video frames on the framebuffer.
But if a desktop enviroment is used, the blit operation can easily collide with the
drawings of the display server, window manager, etc. This is because the blitter
completely bypasses whatever render paths the display server might have set up.
As a result, frames that are blitted on screen can "flicker" when the display
server decides to paint to the same screen region.

Without desktop compositing, it is theoretically possible to work around this
problem. There could be a window which has special flags that instruct the display
server to not ever paint anything inside the window region. Then, the blitter could
paint the pixels into that region. This only works if the window isn't partially
obscured by another window. Also, with desktop compositing, this won't work, because
compositing will cause the display server to draw all windows into separate offscreen
regions, and only in the end, the desktop compositor composes all of these offscreen
buffers together to form one complete frame. For these reason, using blitter-based
video sinks in desktop enviroments is generally not recommended. Consider using
`imxeglvivsink` then instead, which works well with desktop enviroments (but uses
the GPU's more power hungry 3D core for rendering).
