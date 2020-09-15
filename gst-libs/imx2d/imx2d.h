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
	/* Packed RGB(A) / grayscale */
	IMX_2D_PIXEL_FORMAT_RGB565 = 0,
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

	IMX_2D_NUM_PIXEL_FORMATS,

	IMX_2D_PIXEL_FORMAT_UNKNOWN
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
 * Imx2dFlipMode:
 * @IMX_2D_FLIP_MODE_NONE: No flipping.
 * @IMX_2D_FLIP_MODE_HORIZONTAL: Frame is flipped horizontally.
 * @IMX_2D_FLIP_MODE_VERTICAL: Frame is flipped vertically.
 *
 * The flip mode to use when blitting.
 */
typedef enum
{
	IMX_2D_FLIP_MODE_NONE = 0,
	IMX_2D_FLIP_MODE_HORIZONTAL,
	IMX_2D_FLIP_MODE_VERTICAL
}
Imx2dFlipMode;

/**
 * imx_2d_flip_mode_to_string:
 * @flip_mode: Flip mode to return a string for.
 *
 * Returns a human-readable string representation of the given flip mode.
 *
 * This string is not suitable as an ID and is meant purely for logging and for information on user interfaces.
 *
 * Returns: Human-readable string representation
 */
char const * imx_2d_flip_mode_to_string(Imx2dFlipMode flip_mode);


/**
 * Imx2dRotation:
 * @IMX_2D_FLIP_MODE_NONE: No rotation.
 * @IMX_2D_ROTATION_90: 90-degree rotation.
 * @IMX_2D_ROTATION_180: 180-degree rotation.
 * @IMX_2D_ROTATION_270: 270-degree rotation.
 *
 * The clockwise rotation to use when blitting.
 */
typedef enum
{
	IMX_2D_ROTATION_NONE = 0,
	IMX_2D_ROTATION_90,
	IMX_2D_ROTATION_180,
	IMX_2D_ROTATION_270
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


typedef struct _Imx2dRect Imx2dRect;
typedef struct _Imx2dPixelFormatInfo Imx2dPixelFormatInfo;


/**
 * Imx2dRect:
 * @x1: Left end of the rectangle, in pixels.
 * @y1: Top end of the rectangle, in pixels.
 * @x2: One past the right end of the rectangle, in pixels.
 * @y2: One past the bottom end of the rectangle, in pixels.
 *
 * Describes a rectangular region. Used for blitting
 * from/to subregions.
 *
 * x2,y2 are exactly one pixel to the right and one pixel
 * below the bottom right corner of the rectangle, and
 * x1,y1 are the top left corner of the rectangle.
 *
 * From that follows:
 * rectangle width = x2 - x1
 * rectangle height = y2 - y1
 *
 * x1 must always be <= x2.
 * y1 must always be <= y2.
 */
struct _Imx2dRect
{
	int x1, y1, x2, y2;
};


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
};





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
 * @plane_stride: Plane stride values, in bytes.
 * @plane_offset: Plane offset values, in bytes.
 * @format: Pixel format of the surface.
 *
 * Describes a surface by specifying metrics like width, height,
 * plane offsets etc.
 *
 * RGB(A) and grayscale formats always have only one plane.
 * YUV formats typically have two of three planes. The number
 * of planes defines how many of the values in the @plane_stride
 * and @plane_offset arrays are used.
 */
struct _Imx2dSurfaceDesc
{
	int width, height;
	int plane_stride[3], plane_offset[3];
	Imx2dPixelFormat format;
};

/**
 * imx_2d_surface_desc_calculate_strides_and_offsets:
 * @desc: Surface description to fill with calculated values.
 * @capabilities: Hardware capabilities to use for the calculations.
 *
 * This calculates the plane stride and offset values in the
 * description. Any existing values are overwritten.
 *
 * This function is useful for computing these values if only
 * width, height, and format are specified. These three values
 * must be valid in @desc.
 */
void imx_2d_surface_desc_calculate_strides_and_offsets(Imx2dSurfaceDesc *desc, Imx2dHardwareCapabilities const *capabilities);

/**
 * imx_2d_surface_desc_calculate_framesize:
 * @desc: Surface description to use in the calculation.
 *
 * Calculates the size of a frame based on the given surface
 * description. This is useful for determining the size of
 * DMA buffers to allocate when setting up frame buffers.
 *
 * All values in @desc must be valid.
 *
 * Returns: Recommended frame size in bytes.
 */
int imx_2d_surface_desc_calculate_framesize(Imx2dSurfaceDesc const *desc);


/**
 * imx_2d_surface_create:
 * @dma_buffer: DMA buffer to associate with the newly created surface.
 * @desc: Description for the newly created surface.
 *
 * Creates a new surface that is associated with this blitter.
 *
 * @dma_buffer can be set to NULL creates a surface with no assigned
 * DMA buffer. With such a surface, @imx_2d_surface_set_dma_buffer
 * must be called at least once prior to using this surface with a
 * blitter. Same goes for @desc - if it is set to NULL, then
 * @imx_2d_surface_set_desc must be called at least once before using
 * this surface for blitting.
 *
 * Use @imx_2d_surface_destroy to destroy the surface when
 * it is no longer needed.
 *
 * Returns: Pointer to the new surface.
 */
Imx2dSurface* imx_2d_surface_create(ImxDmaBuffer *dma_buffer, Imx2dSurfaceDesc const *desc);

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
 * @surface: Surface that shall have its DMA buffer set.
 * @dma_buffer: DMA buffer to use with this surface.
 *
 * Sets the surface's DMA buffer.
 *
 * The surface does not take ownership over the DMA buffer.
 * The DMA buffer must continue to exist at least until
 * the surface is destroyed or a different DMA buffer is set.
 */
void imx_2d_surface_set_dma_buffer(Imx2dSurface *surface, ImxDmaBuffer *dma_buffer);

/**
 * imx_2d_surface_get_dma_buffer:
 * @surface: Surface to get a DMA buffer from.
 *
 * Retrieves the DMA buffer of this surface.
 *
 * The return value is NULL until @imx_2d_surface_set_dma_buffer
 * is called.
 *
 * Returns: Pointer to the DMA buffer.
 */
ImxDmaBuffer* imx_2d_surface_get_dma_buffer(Imx2dSurface *surface);




/* Blitter */


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
 * Imx2dBlitParams:
 * @source_rect: Rectangle describing the source region to blit from.
 *     NULL means use the entire source surface.
 * @dest_rect: Rectangle describing the destination region to blit to.
 *     NULL means use the entire destination surface.
 * @flip_mode: Flip mode to use in the blitter operation.
 * @rotation: Rotation to use in the blitter operation.
 * @alpha: Global alpha value. Valid range goes from 0 (fully transparent)
 *     to 255 (fully opaque).
 */
struct _Imx2dBlitParams
{
	Imx2dRect const *source_rect;
	Imx2dRect const *dest_rect;

	Imx2dFlipMode flip_mode;
	Imx2dRotation rotation;

	int alpha;
};


/**
 * imx_2d_blitter_destroy:
 * @param blitter Blitter to destroy.
 *
 * Destroys the given blitter. The blitter pointer is invalid
 * after this call and must not be used anymore.
 */
void imx_2d_blitter_destroy(Imx2dBlitter *blitter);

/**
 * imx_2d_blitter_start:
 * @blitter: Blitter to use.
 *
 * Starts a sequence of blitter operations.
 *
 * This must be called before any @imx_2d_blitter_do_blit,
 * @imx_2d_blitter_fill_rect or @imx_2d_blitter_finish calls.
 *
 * If this is again after a sequence was already started,
 * this function does nothing and returns nonzero.
 *
 * IMPORTANT: All functions that are part of a sequence must
 * be called from the same thread. These functions are:
 *
 * - @imx_2d_blitter_start
 * - @imx_2d_blitter_finish
 * - @imx_2d_blitter_do_blit
 * - @imx_2d_blitter_fill_rect
 *
 * This limitation is present in some underlying APIs such as G2D.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_start(Imx2dBlitter *blitter);

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
 * @dest: Surface to blit pixels to.
 * @params: Optional blitter parameters. NULL sets default ones.
 *
 * This is the main function of a blitter. It blits (copies)
 * pixels from @source to @dest. When doing that, it also
 * performs flipping, rotation, blending, and scaling.
 *
 * The blitter transfers pixels from a source region in the
 * @source surface to a destination region in the @dest surface.
 * The regions are determined by the source and destination
 * rectangles in @params. If the source rectangle pointer there
 * is set to NULL, then the entire @source surface is the source
 * region. Same goes for the @dest surface and the destination
 * rectangle in @params. If @params itself is set to NULL, this
 * function behaves as if an @Imx2dBlitParams structure were
 * passed, with the source and destination rectangles set to NULL.
 *
 * Scaling is determined by the source and destination regions.
 * The pixels from the source region are transferred into the
 * destination region. If these two regions do not have the
 * same size, then the source pixels are scaled to make sure
 * they fit in the destination region.
 *
 * If @params is set to NULL, default parameters are used.
 * These are: NULL source and destination rectangles,
 * @IMX_2D_FLIP_MODE_NONE as flip mode, IMX_2D_ROTATION_NONE
 * as rotation, and 255 as the alpha value. In other words,
 * the default parameters produce a simple blit operation
 * with scaling as-needed (as explained above).
 *
 * See @imx_2d_blitter_start for an important note about calling
 * this from a particular thread.
 *
 * Returns: Nonzero if the call succeeds, zero on failure.
 */
int imx_2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params);

/**
 * imx_2d_blitter_fill_rect:
 * @blitter: Blitter to use.
 * @dest: Surface that shall be filled.
 * @dest_rect: Rectangular region in the @dest surface to fill.
 * @fill_color: Color to use for filling.
 *
 * This fills the @dest_region in @dest with pixels whose
 * color is set to @fill_color. Should @dest_region extend
 * past the boundaries of @dest, it will be clipped. If
 * @dest_rect is fully outside of @dest, this function does
 * nothing and just returns nonzero.
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
int imx_2d_blitter_fill_rect(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRect const *dest_rect, uint32_t fill_color);

Imx2dHardwareCapabilities const * imx_2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);


#ifdef __cplusplus
}
#endif


#endif /* IMX_2D_MAIN_HEADER_H */
