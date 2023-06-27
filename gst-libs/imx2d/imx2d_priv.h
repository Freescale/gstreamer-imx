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
typedef struct _Imx2dInternalBlitParams Imx2dInternalBlitParams;
typedef struct _Imx2dInternalFillRegionParams Imx2dInternalFillRegionParams;


struct _Imx2dSurface
{
	Imx2dSurfaceDesc desc;
	Imx2dRegion region;
	ImxDmaBuffer *dma_buffers[3];
	int dma_buffer_offsets[3];
};


struct _Imx2dBlitter
{
	Imx2dBlitterClass *blitter_class;
	Imx2dSurface *dest;
};


/* source_region and dest_region can be NULL, in which case
 * the entire source/dest surface region is used.
 * If expanded_dest_region is NULL, then the expanded region
 * equals the dest region (either dest_region or dest->region,
 * as explained above).
 * The alpha value in margin_fill_color is premultiplied. */
struct _Imx2dInternalBlitParams
{
	Imx2dSurface *source;
	Imx2dRegion const *source_region;
	Imx2dRegion const *dest_region;
	Imx2dRotation rotation;
	Imx2dRegion const *expanded_dest_region;
	int dest_surface_alpha;
	uint32_t margin_fill_color;
	Imx2dColorimetry colorimetry;
};


struct _Imx2dInternalFillRegionParams
{
	Imx2dRegion const *dest_region;
	uint32_t fill_color;
};


struct _Imx2dBlitterClass
{
	void (*destroy)(Imx2dBlitter *blitter);

	int (*start)(Imx2dBlitter *blitter);
	int (*finish)(Imx2dBlitter *blitter);

	int (*do_blit)(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params);
	int (*fill_region)(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params);

	Imx2dHardwareCapabilities const * (*get_hardware_capabilities)(Imx2dBlitter *blitter);
};


#ifdef __cplusplus
}
#endif


#endif /* IMX2D_PRIV_H */
