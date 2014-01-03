#include "egl_misc.h"


char const *gst_imx_egl_viv_sink_egl_platform_get_last_error_string(void)
{
	return gst_imx_egl_viv_sink_egl_platform_get_error_string(eglGetError());
}


char const *gst_imx_egl_viv_sink_egl_platform_get_error_string(EGLint err)
{
	if (err == EGL_SUCCESS)
		return "success";

	switch (err)
	{
		case EGL_NOT_INITIALIZED: return "not initialized";
		case EGL_BAD_ACCESS: return "bad access";
		case EGL_BAD_ALLOC: return "bad alloc";
		case EGL_BAD_ATTRIBUTE: return "bad attribute";
		case EGL_BAD_CONTEXT: return "bad context";
		case EGL_BAD_CONFIG: return "bad config";
		case EGL_BAD_CURRENT_SURFACE: return "bad current surface";
		case EGL_BAD_DISPLAY: return "bad display";
		case EGL_BAD_SURFACE: return "bad surface";
		case EGL_BAD_MATCH: return "bad match";
		case EGL_BAD_PARAMETER: return "bad parameter";
		case EGL_BAD_NATIVE_PIXMAP: return "bad native pixmap";
		case EGL_BAD_NATIVE_WINDOW: return "bad native window";
		case EGL_CONTEXT_LOST: return "context lost";
		default: return "<unknown error>";
	}
}

