#include <stddef.h>
#include <gst/gst.h>
#include "gl_headers.h"

GST_DEBUG_CATEGORY_EXTERN(imx_gles2renderer_debug);
#define GST_CAT_DEFAULT imx_gles2renderer_debug


PFNGLTEXDIRECTVIVMAP EglVivSink_TexDirectVIVMap = NULL;
PFNGLTEXDIRECTVIV EglVivSink_TexDirectVIV = NULL;
PFNGLTEXDIRECTINVALIDATEVIV EglVivSink_TexDirectInvalidateVIV = NULL;


int gst_imx_egl_viv_sink_init_viv_direct_texture(void)
{
#define GET_VIV_PROC_ADDRESS(FNTYPE, NAME) \
	do { \
		EglVivSink_ ## NAME = (FNTYPE) eglGetProcAddress("gl" # NAME); \
		if (EglVivSink_ ## NAME == NULL) \
		{ \
			GST_ERROR("could not get address for proc %s", #NAME); \
			return 0; \
		} \
	} while (0)

	GET_VIV_PROC_ADDRESS(PFNGLTEXDIRECTVIV, TexDirectVIV);
	GET_VIV_PROC_ADDRESS(PFNGLTEXDIRECTVIVMAP, TexDirectVIVMap);
	GET_VIV_PROC_ADDRESS(PFNGLTEXDIRECTINVALIDATEVIV, TexDirectInvalidateVIV);

	return 1;

#undef GET_VIV_PROC_ADDRESS
}
