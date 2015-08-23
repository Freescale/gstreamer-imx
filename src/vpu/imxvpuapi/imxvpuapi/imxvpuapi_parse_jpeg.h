#ifndef IMXVPUAPI_PARSE_JPEG_H
#define IMXVPUAPI_PARSE_JPEG_H

#include "imxvpuapi.h"


#ifdef __cplusplus
extern "C" {
#endif


int imx_vpu_parse_jpeg_header(void *jpeg_data, size_t jpeg_data_size, unsigned int *width, unsigned int *height, ImxVpuColorFormat *color_format);


#ifdef __cplusplus
}
#endif


#endif
