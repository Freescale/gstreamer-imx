#ifndef IMX_2D_LINUX_FRAMEBUFFER_H
#define IMX_2D_LINUX_FRAMEBUFFER_H

#include "imx2d.h"


#ifdef __cplusplus
extern "C" {
#endif


typedef struct _Imx2dLinuxFramebuffer Imx2dLinuxFramebuffer;


Imx2dLinuxFramebuffer* imx_2d_linux_framebuffer_create(char const *device_name, int use_vsync);
void imx_2d_linux_framebuffer_destroy(Imx2dLinuxFramebuffer *linux_framebuffer);

Imx2dSurface* imx_2d_linux_framebuffer_get_surface(Imx2dLinuxFramebuffer *linux_framebuffer);


#ifdef __cplusplus
}
#endif


#endif /* IMX_2D_LINUX_FRAMEBUFFER_H */
