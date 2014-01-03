#ifndef GST_IMX_EGL_MISC_H
#define GST_IMX_EGL_MISC_H

#include <EGL/egl.h>


char const *gst_imx_egl_viv_sink_egl_platform_get_last_error_string(void);
char const *gst_imx_egl_viv_sink_egl_platform_get_error_string(EGLint err);


#endif

