#ifndef IMX_2D_LINUX_FRAMEBUFFER_H
#define IMX_2D_LINUX_FRAMEBUFFER_H

#include "imx2d.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Imx2dLinuxFramebuffer:
 *
 * Wrapper for Linux framebuffer devices. This incorporates
 * an Imx2dSurface that can be used as a target for blitting.
 *
 * This also allows for page flipping, which is useful for
 * preventing tearing by enabling vertical blank ("vblank")
 * synchronization ("vsync"). Page flipping is tied to vsync.
 * A new frame can be written into page A while page B is
 * being displayed, and then, the framebuffer can be made
 * to switch to page A at the time of the next vblank.
 * Note that using page flipping will change the physical
 * address of the wrapped DMA buffer of the surface, so
 * do not do page flipping while an imx2d sequene is ongoing
 * (see @imx_2d_blitter_start).
 */
typedef struct _Imx2dLinuxFramebuffer Imx2dLinuxFramebuffer;

/**
 * imx_2d_linux_framebuffer_create:
 * @device_name: Device name of the framebuffer to access.
 * @enable_page_flipping: Whether or not to enable page flipping (nonzero = yes, zero = no).
 *
 * Creates a new framebuffer wrapper.
 *
 * See @Imx2dLinuxFramebuffer for notes about page flipping.
 * If @enable_page_flipping is nonzero, the Linux framebuffer
 * specified by @device_name has its virtual height enlarged
 * to accomodate for sufficient pages (unless said virtual
 * height is large enough already). Page flipping is done by
 * setting the write position in the framebuffer and the
 * display Y offset the framebuffer reads pixels from.
 * Both of these are reset back to zero when this framebuffer
 * wrapper is destroyed by @imx_2d_linux_framebuffer_destroy.
 * The virtual framebuffer height is also reset to its original
 * size (if it was adjusted earlier).
 *
 * Returns: Pointer to new framebuffer wrapper, or NULL if
 *          an error occurred (in this case, any changes made
 *          to the framebuffer configuration are rolled back
 *          before this function finishes).
 */
Imx2dLinuxFramebuffer* imx_2d_linux_framebuffer_create(char const *device_name, int enable_page_flipping);

/**
 * imx_2d_linux_framebuffer_destroy:
 * @linux_framebuffer: Framebuffer wrapper to destroy.
 *
 * Destroys the given framebuffer wrapper. Any adjustments
 * made to the Linux framebuffer are rolled back.
 *
 * The pointer to the wrapper is invalid after this call
 * and must not be used anymore.
 */
void imx_2d_linux_framebuffer_destroy(Imx2dLinuxFramebuffer *linux_framebuffer);

/**
 * imx_2d_linux_framebuffer_get_surface:
 * @linux_framebuffer: Framebuffer wrapper to get a surface from.
 *
 * This returns the surface that represents the region of the
 * framebuffer that can be written to. If page flipping is not
 * used (see @imx_2d_linux_framebuffer_create), this is the same
 * region as the one that is currently being displayed.
 *
 * Returns: The @Imx2dSurface that wraps the framebuffer.
 */
Imx2dSurface* imx_2d_linux_framebuffer_get_surface(Imx2dLinuxFramebuffer *linux_framebuffer);

/**
 * imx_2d_linux_framebuffer_get_num_fb_pages:
 * @linux_framebuffer: Framebuffer wrapper to get the number of available pages from.
 *
 * This return value never changes after creating the framebuffer wrapper,
 * so it can be safely cached. If page flipping is not enabled (see
 * @imx_2d_linux_framebuffer_create), the return value is 1.
 *
 * Returns: Number of pages available for writing / displaying.
 */
int imx_2d_linux_framebuffer_get_num_fb_pages(Imx2dLinuxFramebuffer *linux_framebuffer);

/**
 * @imx_2d_linux_framebuffer_set_write_fb_page:
 * @linux_framebuffer: Framebuffer wrapper to set the write target FB page of.
 * @page Page number to set as the write taget.
 *
 * This is only useful if page flipping is enabled (see @imx_2d_linux_framebuffer_create).
 *
 * This sets the target of write (= blit) operations. @page must be a number in the
 * range 0 .. (num-pages - 1), where num-pages is the return value of
 * @imx_2d_linux_framebuffer_get_num_fb_pages.
 *
 * IMPORTANT: This modifies the physical address of the surface associated with
 * this framebuffer wrapper (see @imx_2d_linux_framebuffer_get_surface). Do not call
 * this while a sequence is ongoing (see @imx_2d_blitter_start).
 */
void imx_2d_linux_framebuffer_set_write_fb_page(Imx2dLinuxFramebuffer *linux_framebuffer, int page);

/**
 * @imx_2d_linux_framebuffer_set_write_fb_page:
 * @linux_framebuffer: Framebuffer wrapper to set the display FB page of.
 * @page Page number to set as the one to display.
 *
 * This is only useful if page flipping is enabled (see @imx_2d_linux_framebuffer_create).
 *
 * This sets the page that the framebuffer shall show on screen. @page must be
 * a number in the range 0 .. (num-pages - 1), where num-pages is the return value
 * of @imx_2d_linux_framebuffer_get_num_fb_pages.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_linux_framebuffer_set_display_fb_page(Imx2dLinuxFramebuffer *linux_framebuffer, int page);


#ifdef __cplusplus
}
#endif


#endif /* IMX_2D_LINUX_FRAMEBUFFER_H */
