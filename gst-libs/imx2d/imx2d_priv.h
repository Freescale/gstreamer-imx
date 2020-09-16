#ifndef IMX2D_PRIV_H
#define IMX2D_PRIV_H

#include "imx2d.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifndef TRUE
#define TRUE (1)
#endif


#ifndef FALSE
#define FALSE (0)
#endif


#ifndef BOOL
#define BOOL int
#endif


#ifndef MIN
#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#endif


#ifndef MAX
#define MAX(a,b) (((a) >= (b)) ? (a) : (b))
#endif


#define IMX_2D_UNUSED_PARAM(x) ((void)(x))


#define IMX_2D_LOG_FULL(LEVEL, FILE_, LINE_, FUNCTION_, ...) \
	do \
	{ \
		if (imx_2d_cur_log_level_threshold >= IMX_2D_LOG_LEVEL_ ## LEVEL) \
		{ \
			imx_2d_cur_logging_fn(IMX_2D_LOG_LEVEL_ ## LEVEL,   FILE_, LINE_, FUNCTION_, __VA_ARGS__); \
		} \
	} while(0)


#define IMX_2D_LOG(LEVEL, ...) \
	IMX_2D_LOG_FULL(LEVEL, __FILE__, __LINE__, __func__, __VA_ARGS__)


extern Imx2dLogLevel imx_2d_cur_log_level_threshold;
extern Imx2dLoggingFunc imx_2d_cur_logging_fn;


typedef struct _Imx2dSurfaceClass Imx2dSurfaceClass;


struct _Imx2dSurface
{
	Imx2dSurfaceDesc desc;
	ImxDmaBuffer *dma_buffer;
};


struct _Imx2dBlitter
{
	Imx2dBlitterClass *blitter_class;
};


struct _Imx2dBlitterClass
{
	void (*destroy)(Imx2dBlitter *blitter);

	int (*start)(Imx2dBlitter *blitter);
	int (*finish)(Imx2dBlitter *blitter);

	int (*do_blit)(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params);
	int (*fill_region)(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRegion const *dest_region, uint32_t fill_color);

	Imx2dHardwareCapabilities const * (*get_hardware_capabilities)(Imx2dBlitter *blitter);
};


#ifdef __cplusplus
}
#endif


#endif /* IMX2D_PRIV_H */
