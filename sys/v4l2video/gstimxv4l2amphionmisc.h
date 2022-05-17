#ifndef GST_IMX_V4L2_AMPHION_MISCs_H
#define GST_IMX_V4L2_AMPHION_MISC_H

#include <gst/gst.h>


G_BEGIN_DECLS


/* Extra V4L2 FourCC's specific to the Amphion Malone decoder. */

#ifndef V4L2_VPU_PIX_FMT_VP6
#define V4L2_VPU_PIX_FMT_VP6         v4l2_fourcc('V', 'P', '6', '0')
#endif

#ifndef V4L2_VPU_PIX_FMT_AVS
#define V4L2_VPU_PIX_FMT_AVS         v4l2_fourcc('A', 'V', 'S', '0')
#endif

#ifndef V4L2_VPU_PIX_FMT_RV
#define V4L2_VPU_PIX_FMT_RV          v4l2_fourcc('R', 'V', '0', '0')
#endif

#ifndef V4L2_VPU_PIX_FMT_VP6
#define V4L2_VPU_PIX_FMT_VP6         v4l2_fourcc('V', 'P', '6', '0')
#endif

#ifndef V4L2_VPU_PIX_FMT_SPK
#define V4L2_VPU_PIX_FMT_SPK         v4l2_fourcc('S', 'P', 'K', '0')
#endif

#ifndef V4L2_VPU_PIX_FMT_DIV3
#define V4L2_VPU_PIX_FMT_DIV3        v4l2_fourcc('D', 'I', 'V', '3')
#endif

#ifndef V4L2_VPU_PIX_FMT_DIVX
#define V4L2_VPU_PIX_FMT_DIVX        v4l2_fourcc('D', 'I', 'V', 'X')
#endif

#ifndef V4L2_PIX_FMT_NV12_10BIT
#define V4L2_PIX_FMT_NV12_10BIT      v4l2_fourcc('N', 'T', '1', '2') /*  Y/CbCr 4:2:0 for 10bit  */
#endif


#define GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH 512

typedef struct
{
	gboolean initialized;
	char decoder_filename[GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH];
	char encoder_filename[GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH];
}
GstImxV4L2AmphionDeviceFilenames;

extern GstImxV4L2AmphionDeviceFilenames gst_imx_v4l2_amphion_device_filenames;

void gst_imx_v4l2_amphion_device_filenames_init(void);


GstCaps* gst_imx_v4l2_amphion_get_caps_for_format(guint32 v4l2_pixelformat);


G_END_DECLS


#endif /* GST_IMX_V4L2_AMPHION_MISC_H */
