#ifndef IMX_2D_MAIN_HEADER_H
#define IMX_2D_MAIN_HEADER_H

#include <stdint.h>
#include <stddef.h>

#include <imxdmabuffer/imxdmabuffer.h>


#ifdef __cplusplus
extern "C" {
#endif


/* Logging */


/**
 * Imx2dLogLevel:
 * @IMX_2D_LOG_LEVEL_ERROR: Nonrecoverable / fatal errors.
 * @IMX_2D_LOG_LEVEL_WARNING: Errors that the logic can internally recover from; potentially problematic situations.
 * @IMX_2D_LOG_LEVEL_INFO: Important information.
 * @IMX_2D_LOG_LEVEL_DEBUG: Verbose debug information.
 * @IMX_2D_LOG_LEVEL_TRACE: Information about normal operation, used for tracing what the code is doing.
 *
 * Priority levels for a logging operation. ERROR has the highest priority.
 */
typedef enum
{
	IMX_2D_LOG_LEVEL_ERROR = 0,
	IMX_2D_LOG_LEVEL_WARNING = 1,
	IMX_2D_LOG_LEVEL_INFO = 2,
	IMX_2D_LOG_LEVEL_DEBUG = 3,
	IMX_2D_LOG_LEVEL_TRACE = 4
}
Imx2dLogLevel;

/**
 * Imx2dLoggingFunc:
 * @level: Log level.
 * @file: Name of the source file.
 * @line: Source file line number.
 * @function_name: Name of the function where the logging happened.
 * @format printf: style format string.
 * @...: printf arguments for @format.
 *
 * Function pointer type for logging functions.
 *
 * This function is used by internal imx2d logging macros. This macros also pass the name
 * of the source file, the line in that file, and the function name where the logging occurs
 * to this logging function (over the file, line, and function_name arguments, respectively).
 * Together with the log level, custom logging functions can output this metadata, or use
 * it for log filtering etc.
 */
typedef void (*Imx2dLoggingFunc)(Imx2dLogLevel level, char const *file, int const line, char const *function_name, const char *format, ...);

/**
 * imx_2d_set_logging_threshold:
 * @threshold: Logging threshold.
 *
 * Defines the threshold for logging. Logs with lower priority are discarded.
 * By default, the threshold is set to IMX_2D_LOG_LEVEL_INFO.
 */
void imx_2d_set_logging_threshold(Imx2dLogLevel threshold);

/**
 * imx_2d_set_logging_function:
 * @logging_function Function pointer to logging to function.
 * 
 * Defines a custom logging function.
 * If logging_function is NULL, logging is disabled. This is the default value.
 */
void imx_2d_set_logging_function(Imx2dLoggingFunc logging_function);




/* Miscellaneous enums and structures */


/**
 * Imx2dPixelFormat:
 * @IMX_2D_PIXEL_FORMAT_RGB565: 16-bit RGB 5:6:5 packed format.
 * @IMX_2D_PIXEL_FORMAT_BGR565: 16-bit BGR 5:6:5 packed format.
 * @IMX_2D_PIXEL_FORMAT_RGB888: 24-bit RGB 8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_BGR888: 24-bit RGB 8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_RGBX8888: 32-bit RGBX 8:8:8:8 packed format (X = unused padding bits).
 * @IMX_2D_PIXEL_FORMAT_RGBA8888: 32-bit RGBA 8:8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_BGRX8888: 32-bit BGRX 8:8:8:8 packed format (X = unused padding bits).
 * @IMX_2D_PIXEL_FORMAT_BGRA8888: 32-bit BGRA 8:8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_XRGB8888: 32-bit XRGB 8:8:8:8 packed format (X = unused padding bits).
 * @IMX_2D_PIXEL_FORMAT_ARGB8888: 32-bit ARGB 8:8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_XBGR8888: 32-bit XBGR 8:8:8:8 packed format (X = unused padding bits).
 * @IMX_2D_PIXEL_FORMAT_ABGR8888: 32-bit ABGR 8:8:8:8 packed format.
 * @IMX_2D_PIXEL_FORMAT_GRAY8: 8-bit grayscale format.
 * @IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: YUV 4:2:2 packed format (U0-Y0-V0-Y1 U2-Y2-V2-Y3 U4 ...).
 * @IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: YUV 4:2:2 packed format (Y0-U0-Y1-V0 Y2-U2-Y3-V2 Y4 ...), also known as YUY2.
 * @IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU: YUV 4:2:2 packed format (Y0-V0-Y1-U0 Y2-V2-Y3-U2 Y4 ...).
 * @IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY: YUV 4:2:2 packed format (V0-Y0-U0-Y1 V2-Y2-U2-Y3 V4 ...).
 * @IMX_2D_PIXEL_FORMAT_PACKED_YUV444: YUV 4:4:4 packed format.
 * @IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: YUV 4:2:0 planar format, one Y and one interleaved UV plane.
 * @IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21: YUV 4:2:0 planar format, one Y and one interleaved VU plane.
 * @IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16: YUV 4:2:2 planar format, one Y and one interleaved UV plane.
 * @IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61: YUV 4:2:2 planar format, one Y and one interleaved VU plane.
 * @IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: YUV 4:2:0 planar format, U and V planes swapped.
 * @IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: YUV 4:2:0 planar format.
 * @IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B: YUV 4:2:2 planar format.
 * @IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444: YUV 4:4:4 planar format.
 * @IMX_2D_NUM_PIXEL_FORMATS: Special enum specifying the number of valid pixel formats.
 * @IMX_2D_PIXEL_FORMAT_UNKNOWN: Special enum for specifying an unknown pixel format.
 *
 * Pixel format to use in @Imx2dSurfaceDesc. Also used in @Imx2dHardwareCapabilities
 * to specify what pixel formats are supported by the underlying hardware.
 */
typedef enum
{
	IMX_2D_PIXEL_FORMAT_UNKNOWN = 0,

	/* Packed RGB(A) / grayscale */
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_BGR565,
	IMX_2D_PIXEL_FORMAT_RGB888,
	IMX_2D_PIXEL_FORMAT_BGR888,
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_XRGB8888,
	IMX_2D_PIXEL_FORMAT_ARGB8888,
	IMX_2D_PIXEL_FORMAT_XBGR8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,
	IMX_2D_PIXEL_FORMAT_GRAY8,

	/* Packed YUV */
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV, /* also known as YUY2 */
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV444,

	/* Semi-planar YUV */
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61,

	/* Planar YUV */
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444,

	/* Tiled layouts */
	IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128,
	IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128,

	IMX_2D_NUM_PIXEL_FORMATS
}
Imx2dPixelFormat;

/**
 * imx_2d_pixel_format_to_string:
 * @format: Pixel format to return a string for.
 *
 * Returns a human-readable string representation of the given pixel format.
 *
 * This string is not suitable as an ID and is meant purely for logging and for information on user interfaces.
 *
 * Returns: Human-readable string representation
 */
char const * imx_2d_pixel_format_to_string(Imx2dPixelFormat format);


/**
 * Imx2dRotation:
 * @IMX_2D_ROTATION_NONE: No rotation.
 * @IMX_2D_ROTATION_90: 90-degree rotation.
 * @IMX_2D_ROTATION_180: 180-degree rotation.
 * @IMX_2D_ROTATION_270: 270-degree rotation.
 * @IMX_2D_ROTATION_FLIP_HORIZONTAL: Frame is flipped horizontally.
 * @IMX_2D_ROTATION_FLIP_VERTICAL: Frame is flipped vertically.
 * @IMX_2D_ROTATION_UL_LR: Frame is flipped across the upper left/lower right diagonal.
 * @IMX_2D_ROTATION_UR_LL: Frame is flipped across the upper right/lower left diagonal.
 *
 * The clockwise rotation to use when blitting.
 */
typedef enum
{
	IMX_2D_ROTATION_NONE = 0,
	IMX_2D_ROTATION_90,
	IMX_2D_ROTATION_180,
	IMX_2D_ROTATION_270,
	IMX_2D_ROTATION_FLIP_HORIZONTAL,
	IMX_2D_ROTATION_FLIP_VERTICAL,
	IMX_2D_ROTATION_UL_LR,
	IMX_2D_ROTATION_UR_LL
}
Imx2dRotation;

/**
 * imx_2d_rotation_to_string:
 * @rotation: Rotation to return a string for.
 *
 * Returns a human-readable string representation of the given rotation.
 *
 * This string is not suitable as an ID and is meant purely for logging and for information on user interfaces.
 *
 * Returns: Human-readable string representation
 */
char const * imx_2d_rotation_to_string(Imx2dRotation rotation);


typedef struct _Imx2dPixelFormatInfo Imx2dPixelFormatInfo;


/**
 * Imx2dPixelFormatInfo:
 * @description: Human-readable description for this pixel format.
 * @num_planes: Number of planes in a frame with this format.
 *     Maximum number is 3.
 * @num_first_plane_bpp: Bits per pixel in the first plane. (BPP
 *     of other planes are not necessary, since in multiplanar
 *     formats, the BPP of all planes are the same.)
 * @x_subsampling: Subsampling in X direction for chroma planes.
 *     (Unused in grayscale and RGB(A) formats.)
 * @y_subsampling: Subsampling in Y direction for chroma planes.
 *     (Unused in grayscale and RGB(A) formats.)
 *
 * Information about a pixel format.
 */
struct _Imx2dPixelFormatInfo
{
	char const *description;
	int num_planes;
	int num_first_plane_bpp;
	int x_subsampling, y_subsampling;
	int is_semi_planar;
};


/**
 * imx_2d_get_pixel_format_info:
 * @format Pixel format to return information about.
 *
 * Returns: Const pointer to a static Imx2dPixelFormatInfo instance
 *     describing the format, or NULL if @format is invalid.
 */
Imx2dPixelFormatInfo const * imx_2d_get_pixel_format_info(Imx2dPixelFormat format);


typedef struct _Imx2dHardwareCapabilities Imx2dHardwareCapabilities;


/**
 * Imx2dHardwareCapabilities:
 * @supported_source_pixel_formats: Array of pixel formats
 *     supported as source format in blitter operations.
 * @num_supported_source_pixel_formats: Number of pixel formats
 *     in the @supported_source_pixel_formats array.
 * @supported_dest_pixel_formats: Array of pixel formats
 *     supported as destination format in blitter operations.
 * @num_supported_dest_pixel_formats: Number of pixel formats
 *     in the @supported_dest_pixel_formats array.
 * @min_width: Minimum allowed surface width, in pixels.
 * @max_width: Maximum allowed surface width, in pixels.
 * @width_step_size: Step size for width values, in pixels.
 * @min_height: Minimum allowed surface height, in pixels.
 * @max_height: Maximum allowed surface height, in pixels.
 * @height_step_size: Step size for height values, in pixels.
 * @stride_alignment: Required stride alignment, in bytes.
 *     This is always a power-of-two value. Stride sizes must
 *     be an integer multiple of this value.
 * @can_handle_multi_buffer_surfaces: Nonzero if the hardware
 *     supports blitting from/to multi-buffer surfaces. If a
 *     surface uses a different DMA buffer for at least one
 *     of its planes, it is considered a multi-buffer surface.
 *
 * Describes the capabilities of the underlying 2D hardware.
 *
 * @width_step_size and @height_step_size describe the step
 * sizes (or increments) for width and height values. For
 * example, if @width_step_size is set to 4, and @min_height
 * is set to 8, then valid widths are 8, 12, 16, 20, 24 etc.
 */
struct _Imx2dHardwareCapabilities
{
	Imx2dPixelFormat const *supported_source_pixel_formats;
	int num_supported_source_pixel_formats;

	Imx2dPixelFormat const *supported_dest_pixel_formats;
	int num_supported_dest_pixel_formats;

	int min_width, max_width, width_step_size;
	int min_height, max_height, height_step_size;

	int stride_alignment;

	int can_handle_multi_buffer_surfaces;
};




/* Rectangular region handling */


typedef struct _Imx2dRegion Imx2dRegion;


/**
 * Imx2dRegion:
 * @x1: Left end of the region, in pixels.
 * @y1: Top end of the region, in pixels.
 * @x2: One past the right end of the region, in pixels.
 * @y2: One past the bottom end of the region, in pixels.
 *
 * Describes a rectangular region. Used for blitting
 * from/to subregions.
 *
 * x2,y2 are exactly one pixel to the right and one pixel
 * below the bottom right corner of the region, and
 * x1,y1 are the top left corner of the region.
 *
 * From that follows:
 * region width = x2 - x1
 * region height = y2 - y1
 *
 * x1 must always be <= x2.
 * y1 must always be <= y2.
 */
struct _Imx2dRegion
{
	int x1, y1, x2, y2;
};


/**
 * Imx2dRegionInclusion:
 *
 * To what degree one region includes another.
 */
typedef enum
{
	IMX_2D_REGION_INCLUSION_NONE = 0,
	IMX_2D_REGION_INCLUSION_PARTIAL,
	IMX_2D_REGION_INCLUSION_FULL
}
Imx2dRegionInclusion;


/**
 * IMX_2D_REGION_FORMAT: (skip):
 *
 * format type used for printing out @Imx2dRegion
 * coordinates with printf-style functions.
 */
#define IMX_2D_REGION_FORMAT "d,%d ... %d,%d"

/**
 * IMX_2D_REGION_ARGS: (skip):
 * @REGION: an @Imx2dRegion
 *
 * Format @REGION for the @GST_TIME_FORMAT format string.
 *
 * Note: @REGION will be evaluated more than once.
 */
#define IMX_2D_REGION_ARGS(REGION) (REGION)->x1,(REGION)->y1,(REGION)->x2,(REGION)->y2


/**
 * imx_2d_region_check_inclusion:
 * @first_region: First region to use in the check.
 * @second_region: Second region to use in the check.
 *
 * Checks if and to what degree @second_region includes @first_region.
 *
 * @first_region can be included fully, partially, or not at all
 * in @second_region .
 *
 * Both pointers must be non-NULL.
 *
 * Returns: Result of the check.
 */
Imx2dRegionInclusion imx_2d_region_check_inclusion(Imx2dRegion const *first_region, Imx2dRegion const *second_region);

/**
 * imx_2d_region_check_if_equal:
 * @first_region: First region to use in the comparison.
 * @second_region: Second region to use in the comparison.
 *
 * Checks if two regions are equal.
 *
 * Both pointers must be non-NULL.
 *
 * Returns: Nonzero if the regions are equal.
 */
int imx_2d_region_check_if_equal(Imx2dRegion const *first_region, Imx2dRegion const *second_region);

/**
 * imx_2d_region_intersect:
 * @intersection: Region that will be filled with the result
 *     of the intersection.
 * @first_region: First region to use in the intersection.
 * @second_region: Second region to use in the intersection.
 *
 * Calculates the intersection of two regions. The result is a
 * region that encompasses the subset of the two regions that is
 * contained in both.
 *
 * If one region fully contains the other, then the resulting
 * region equals the fully contained region. If the regions
 * do not intersect at all, the result is undefined. For this reason,
 * if it is not certain that these two regions always overlap,
 * it is recommended to use @imx_2d_region_check_inclusion first to see
 * if there is an intersection. (Also, if the result from that function
 * is @IMX_2D_REGION_INCLUSION_FULL, then calculating an intersection is
 * unnecessary.)
 *
 * All pointers must be non-NULL.
 */
void imx_2d_region_intersect(Imx2dRegion *intersection, Imx2dRegion const *first_region, Imx2dRegion const *second_region);

/**
 * imx_2d_region_merge:
 * @merged_region Region that will be filled with the result
 *     of the merge.
 * @first_region: First region to use in the merge.
 * @second_region: Second region to use in the merge.
 *
 * Calculates the merge of two regions. The result is
 * a region that encompasses both regions.
 *
 * All pointers must be non-NULL.
 */
void imx_2d_region_merge(Imx2dRegion *merged_region, Imx2dRegion const *first_region, Imx2dRegion const *second_region);




/* Surfaces */


typedef struct _Imx2dSurfaceDesc Imx2dSurfaceDesc;


/**
 * Imx2dSurface:
 *
 * An entity representing a memory region that contains pixels,
 * typically pixels from a video frame.
 *
 * Surfaces do not have their own actual memory buffer. Instead,
 * DMA buffers need to be associated with them. A surface cannot
 * be used for any blitter operation if no DMA buffer is assigned
 * to it. This is done by calling @imx_2d_surface_set_dma_buffer.
 *
 * In addition to a DMA buffer, a surface also needs a description
 * that specifies the structure of the frame. The DMA buffer alone
 * is not enough, since it has no information like width, height
 * etc. - it is just a generic physically contiguous memory block.
 * The description (see @Imx2dSurfaceDesc) provides this extra
 * information. It is assigned by calling @imx_2d_surface_set_desc.
 *
 * DMA buffers and descriptions can also be assigned right at time
 * of surface creation by passing these as arguments to the
 * @imx_2d_surface_create function.
 */
typedef struct _Imx2dSurface Imx2dSurface;


/**
 * Imx2dSurfaceDesc:
 * @width: Width of the surface, in pixels.
 * @height: Height of the surface, in pixels.
 * @plane_strides: Plane stride values, in bytes.
 * @num_padding_rows: Number of extra padding rows at the bottom.
 * @format: Pixel format of the surface.
 *
 * Describes a surface by specifying metrics like width, height,
 * plane strides etc.
 *
 * Plane offsets are not present, since they implicitely exist
 * as the DMA buffers + buffer offset in surfaces (see
 * @imx_2d_surface_set_dma_buffer for details).
 *
 * RGB(A) and grayscale formats always have only one plane.
 * YUV formats typically have two of three planes. The number
 * of planes defines how many of the values in the @plane_stride
 * and @plane_offset arrays are used.
 */
struct _Imx2dSurfaceDesc
{
	int width, height;
	int plane_strides[3];
	int num_padding_rows;
	Imx2dPixelFormat format;
};


/**
 * imx_2d_surface_create:
 * @desc: Description for the newly created surface.
 *
 * Creates a new surface that is associated with this blitter.
 *
 * @desc can be set to NULL. This creates a surface with no assigned
 * description. With such a surface, @imx_2d_surface_set_desc must be
 * called at least once before using this surface for blitting.
 *
 * In addition, @imx_2d_surface_set_dma_buffer must be called at
 * least once for every plane to properly set up the surface.
 *
 * Use @imx_2d_surface_destroy to destroy the surface when
 * it is no longer needed.
 *
 * Returns: Pointer to the new surface.
 */
Imx2dSurface* imx_2d_surface_create(Imx2dSurfaceDesc const *desc);

/**
 * imx_2d_surface_destroy:
 * @surface: Surface to destroy.
 *
 * Destroys the given surface. The surface pointer is invalid
 * after this call and must not be used anymore.
 */
void imx_2d_surface_destroy(Imx2dSurface *surface);

/**
 * imx_2d_surface_set_desc:
 * @surface: Surface that shall have its description set.
 * @desc: New description to use with this surface.
 *
 * Copies the specified description into the surface.
 *
 * The description may have to be changed due to hardware
 * limitations. For example, the stride values may need
 * readjusting if the hardware requires a certain alignment.
 * These modifications will be performed on the internal
 * copy of the description. Therefore, to retrieve the
 * adjusted description, use @imx_2d_surface_get_desc.
 */
void imx_2d_surface_set_desc(Imx2dSurface *surface, Imx2dSurfaceDesc const *desc);

/**
 * imx_2d_surface_get_desc:
 * @surface: Surface to get a description from.
 *
 * Retrieves the description of this surface.
 *
 * The description is only valid after @imx_2d_surface_set_desc
 * was called at least once.
 *
 * Returns: Const pointer to the surface description.
 *     This pointer remains valid until the surface is destroyed.
 */
Imx2dSurfaceDesc const * imx_2d_surface_get_desc(Imx2dSurface *surface);

/**
 * imx_2d_surface_set_dma_buffer:
 * @surface: Surface that shall have one of its DMA buffers set.
 * @dma_buffer: DMA buffer to use with this surface for the given plane.
 * @plane_nr: Number of the plane to use the DMA buffer for.
 * @offset: Offset within the DMA buffer to start reading data at.
 *
 * Sets the surface's DMA buffer for the given plane.
 *
 * The surface does not take ownership over the DMA buffer.
 * The DMA buffer must continue to exist at least until
 * the surface is destroyed or a different DMA buffer is set
 * for the same plane.
 */
void imx_2d_surface_set_dma_buffer(Imx2dSurface *surface, ImxDmaBuffer *dma_buffer, int plane_nr, int offset);

/**
 * imx_2d_surface_get_dma_buffer:
 * @surface: Surface to get a DMA buffer from.
 * @plane_nr: Number of the plane to get the DMA buffer of.
 *
 * Retrieves the DMA buffer of this surface for the given plane.
 *
 * The return value is NULL until @imx_2d_surface_set_dma_buffer
 * is called for this plane.
 *
 * Returns: Pointer to the DMA buffer.
 */
ImxDmaBuffer* imx_2d_surface_get_dma_buffer(Imx2dSurface *surface, int plane_nr);

/**
 * imx_2d_surface_get_dma_buffer_offset:
 * @surface: Surface to get the DMA buffer offset of.
 * @plane_nr: Number of the plane to get the DMA buffer offset of.
 *
 * Retrieves the DMA buffer offset of this surface for the given plane.
 *
 * The return value is 0 until @imx_2d_surface_set_dma_buffer
 * is called for this plane.
 *
 * Returns: Pointer to the DMA buffer.
 */
int imx_2d_surface_get_dma_buffer_offset(Imx2dSurface *surface, int plane_nr);

/**
 * imx_2d_surface_get_region:
 *
 * Retrieves the @Imx2dRegion that encompasses the entire surface.
 * This is useful for region based calculations for example.
 *
 * The region is set to 0,0 .. surface_width,surface_height.
 *
 * Returns: Const pointer to the surface's region.
 */
Imx2dRegion const * imx_2d_surface_get_region(Imx2dSurface *surface);




/* Blitter */


typedef struct _Imx2dBlitMargin Imx2dBlitMargin;
typedef struct _Imx2dBlitParams Imx2dBlitParams;


/**
 * Imx2dBlitter:
 *
 * The entity that performs "blitting" operations. "Blitting"
 * refers to the bulk transfer of pixels from one rectangular
 * region to another, between two different memory buffers.
 *
 * Creating a blitter is API specific. There is one function
 * for creating a G2D blitter, for example.
 *
 * Blitters use "surfaces" (see @Imx2dSurface). Blitting
 * operations are performed between surfaces. When blitting,
 * there is a "source" and a "destination" surface.
 */
typedef struct _Imx2dBlitter Imx2dBlitter;
typedef struct _Imx2dBlitterClass Imx2dBlitterClass;


/**
 * Imx2dBlitMargin:
 * @left_margin: Size of the margin to the left of the dest region.
 * @top_margin: Size of the margin above the dest region.
 * @right_margin: Size of the margin to the right of the dest region.
 * @bottom_margin: Size of the margin below the dest region.
 * @color: ARGB color the margin pixels shall be set to.
 *     Layout is 0xAARRGGBB.
 *
 * Describes a margin region around the rectangular destination
 * region of the blitter operation. Pixels in that margin get filled
 * with the specified color.
 *
 * One example for where margins are useful is when the caller
 * wants to implement letterboxing. To add the letterbox black
 * bars at the top and bottom, the top & bottom margin sizes
 * can be set to a nonzero value.
 *
 * The alpha byte in @color is multiplied together with the alpha
 * value in @Imx2dBlitParams, and the result normalized to the
 * 0..255 range.
 *
 * All margin sizes must be >= 0.
 */
struct _Imx2dBlitMargin
{
	int left_margin;
	int top_margin;
	int right_margin;
	int bottom_margin;
	uint32_t color;
};


/**
 * Imx2dBlitParams:
 * @source_region: Region describing the source region to blit from.
 *     NULL means use the entire source surface.
 * @dest_region: Region describing the destination region to blit to.
 *     NULL means use the entire destination surface.
 * @rotation: Rotation to use in the blitter operation.
 * @margin: Margin to use around @dest_region. If this is set to NULL,
 *     no margin will be used in the blitter operation. The margin
 *     is only used if @dest_region is not NULL.
 * @alpha: Global alpha value. Valid range goes from 0 (fully transparent)
 *     to 255 (fully opaque).
 */
struct _Imx2dBlitParams
{
	Imx2dRegion const *source_region;
	Imx2dRegion const *dest_region;

	Imx2dRotation rotation;

	Imx2dBlitMargin const *margin;

	int alpha;
};


/**
 * imx_2d_blitter_destroy:
 * @blitter Blitter to destroy.
 *
 * Destroys the given blitter. The blitter pointer is invalid
 * after this call and must not be used anymore.
 */
void imx_2d_blitter_destroy(Imx2dBlitter *blitter);

/**
 * imx_2d_blitter_start:
 * @blitter: Blitter to use.
 * @dest: Surface to blit pixels to.
 *
 * Starts a sequence of blitter operations.
 *
 * This must be called before any @imx_2d_blitter_do_blit,
 * @imx_2d_blitter_fill_region or @imx_2d_blitter_finish calls.
 *
 * If this is called again after a sequence was already started,
 * this function does nothing and returns nonzero.
 *
 * All subsequent blit / fill_region calls will write pixels into
 * @dest. The @dest surface must exist until @imx_2d_blitter_finish
 * is called, because the actual blit / fill_region operations
 * may run asynchronously until the sequence is finished.
 *
 * Blitters can cache the destination DMA buffer physical address(es)
 * when starting the sequence, so these must not change while a
 * sequence is ongoing. If they change, start a new sequence.
 *
 * IMPORTANT: All functions that are part of a sequence must
 * be called from the same thread. These functions are:
 *
 * - @imx_2d_blitter_start
 * - @imx_2d_blitter_finish
 * - @imx_2d_blitter_do_blit
 * - @imx_2d_blitter_fill_region
 *
 * This limitation is present in some underlying APIs such as G2D.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_start(Imx2dBlitter *blitter, Imx2dSurface *dest);

/**
 * imx_2d_blitter_finish:
 * @blitter: Blitter to use.
 *
 * Finishes any queued operations in the current sequence, blocking
 * until they are done, and then ends the sequence. To continue to
 * do blitter operations, a new sequence must be started with an
 * @imx_2d_blitter_start call.
 *
 * A sequence must have been started with @imx_2d_blitter_start
 * prior to this call, otherwise it will fail.
 *
 * See @imx_2d_blitter_start for an important note about calling
 * this from a particular thread.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_finish(Imx2dBlitter *blitter);

/**
 * imx_2d_blitter_do_blit:
 * @blitter: Blitter to use.
 * @source: Surface to blit pixels from.
 * @params: Optional blitter parameters. NULL sets default ones.
 *
 * This is the main function of a blitter. It blits (copies)
 * pixels from @source to the destination surface (passed to
 * @imx_2d_blitter_start). When doing that, it also performs
 * flipping, rotation, blending, and scaling.
 *
 * The blitter transfers pixels from a source region in the @source
 * surface to a destination region in the destination surface.
 * The regions are determined by the source and destination
 * regions in @params. If the source region pointer there
 * is set to NULL, then the entire @source surface is the source
 * region. Same goes for the destination surface and the destination
 * region in @params. If @params itself is set to NULL, this
 * function behaves as if an @Imx2dBlitParams structure were
 * passed with the source and destination regions set to NULL.
 *
 * The source surface must exist until the sequence that was begun
 * with @imx_2d_blitter_start is ended with @imx_2d_blitter_finish.
 * This is because blitters can queue up commands internally and
 * execute them asynchronously.
 *
 * Scaling is determined by the source and destination regions.
 * The pixels from the source region are transferred into the
 * destination region. If these two regions do not have the
 * same size, then the source pixels are scaled to make sure
 * they fit in the destination region.
 *
 * If @params is set to NULL, default parameters are used.
 * These are: NULL source and destination regions,
 * @IMX_2D_ROTATION_NONE as rotation, and 255 as the alpha value.
 * In other words, the default parameters produce a simple blit
 * operation with scaling as-needed (as explained above).
 *
 * See @imx_2d_blitter_start for an important note about calling
 * this from a particular thread.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dBlitParams const *params);

/**
 * imx_2d_blitter_fill_region:
 * @blitter: Blitter to use.
 * @dest_region: Region in the destination surface to fill.
 * @fill_color: Color to use for filling.
 *
 * This fills the @dest_region in the destination surface with
 * pixels whose color is set to @fill_color. Should @dest_region
 * extend past the boundaries of the destination surface, it will
 * be clipped. If @dest_region is fully outside of the surface,
 * this function does nothing and just returns nonzero. If
 * @dest_region is NULL, the entire surface is filled.
 *
 * The @fill_color is specified as a 32-bit unsigned integer.
 * the layout being 0x00RRGGBB, that is, the LSB is the byte
 * with the blue channel value.
 *
 * See @imx_2d_blitter_start for an important note about calling
 * this from a particular thread.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dRegion const *dest_region, uint32_t fill_color);

/**
 * imx_2d_blitter_get_hardware_capabilities:
 * @blitter: Blitter to get hardware capabilities from.
 *
 * Provides hardware capabilities that describe what the 2D
 * hardware associated with @blitter is capable of.
 *
 * Returns: Const pointer to @Imx2dHardwareCapabilities structure.
 */
Imx2dHardwareCapabilities const * imx_2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);


#ifdef __cplusplus
}
#endif


#endif /* IMX_2D_MAIN_HEADER_H */
