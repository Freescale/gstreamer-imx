#ifndef IMX2D_BACKEND_IPU_BLITTER_H
#define IMX2D_BACKEND_IPU_BLITTER_H

#include <imx2d/imx2d.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * imx_2d_backend_ipu_blitter_create:
 *
 * Creates a new @Imx2dBlitter that uses the i.MX6 IPU for blitting.
 *
 * To destroy the created blitter, use @imx_2d_blitter_destroy.
 *
 * Returns: Pointer to a newly created IPU blitter, or NULL in case of failure.
 */
Imx2dBlitter* imx_2d_backend_ipu_blitter_create(void);

/**
 * imx_2d_backend_ipu_get_hardware_capabilities:
 *
 * Returns a const pointer to a static structure that contains
 * information about the IPU-based hardware capabilities.
 *
 * @Returns Const pointer to the @Imx2dHardwareCapabilities structure.
 *     This structure is static, and does not have to be freed in any way.
 */
Imx2dHardwareCapabilities const * imx_2d_backend_ipu_get_hardware_capabilities(void);


#ifdef __cplusplus
}
#endif


#endif /* IMX2D_BACKEND_IPU_BLITTER_H */
