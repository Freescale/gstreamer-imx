#include <stdint.h>
#include "imxvpuapi_parse_jpeg.h"
#include "imxvpuapi_priv.h"


/* Start Of Frame markers, non-differential, Huffman coding */
#define SOF0      0xc0  /* Baseline DCT */
#define SOF1      0xc1  /* Extended sequential DCT */
#define SOF2      0xc2  /* Progressive DCT */
#define SOF3      0xc3  /* Lossless */

/* Start Of Frame markers, differential, Huffman coding */
#define SOF5      0xc5
#define SOF6      0xc6
#define SOF7      0xc7

/* Start Of Frame markers, non-differential, arithmetic coding */
#define JPG       0xc8  /* Reserved */
#define SOF9      0xc9
#define SOF10     0xca
#define SOF11     0xcb

/* Start Of Frame markers, differential, arithmetic coding */
#define SOF13     0xcd
#define SOF14     0xce
#define SOF15     0xcf

/* Restart interval termination */
#define RST0      0xd0  /* Restart ... */
#define RST1      0xd1
#define RST2      0xd2
#define RST3      0xd3
#define RST4      0xd4
#define RST5      0xd5
#define RST6      0xd6
#define RST7      0xd7

#define SOI       0xd8  /* Start of image */
#define EOI       0xd9  /* End Of Image */
#define SOS       0xda  /* Start Of Scan */

#define DHT       0xc4  /* Huffman Table(s) */
#define DAC       0xcc  /* Algorithmic Coding Table */
#define DQT       0xdb  /* Quantisation Table(s) */
#define DNL       0xdc  /* Number of lines */
#define DRI       0xdd  /* Restart Interval */
#define DHP       0xde  /* Hierarchical progression */
#define EXP       0xdf

#define APP0      0xe0  /* Application marker */
#define APP1      0xe1
#define APP2      0xe2
#define APP13     0xed
#define APP14     0xee
#define APP15     0xef

#define JPG0      0xf0  /* Reserved ... */
#define JPG13     0xfd
#define COM       0xfe  /* Comment */

#define TEM       0x01



int imx_vpu_parse_jpeg_header(void *jpeg_data, size_t jpeg_data_size, unsigned int *width, unsigned int *height, ImxVpuColorFormat *color_format)
{
	uint8_t *jpeg_data_start = jpeg_data;
	uint8_t *jpeg_data_end = jpeg_data_start + jpeg_data_size;
	uint8_t *jpeg_data_cur = jpeg_data_start;
	int found_info = 0;

#define READ_UINT8(value) do \
	{ \
		(value) = *jpeg_data_cur; \
		++jpeg_data_cur; \
	} \
	while (0)
#define READ_UINT16(value) do \
	{ \
		uint16_t w = *((uint16_t *)jpeg_data_cur); \
		jpeg_data_cur += 2; \
		(value) = ( ((w & 0xff) << 8) | ((w & 0xff00) >> 8) ); \
	} \
	while (0)

	while (jpeg_data_cur < jpeg_data_end)
	{
		uint8_t marker;

		/* Marker is preceded by byte 0xFF */
		if (*(jpeg_data_cur++) != 0xff)
			break;

		READ_UINT8(marker);
		if (marker == SOS)
			break;

		switch (marker)
		{
			case SOI:
				break;
			case DRI:
				jpeg_data_cur += 4;
				break;

			case SOF2:
			{
				IMX_VPU_ERROR("progressive JPEGs are not supported");
				return 0;
			}

			case SOF0:
			{
				uint16_t length;
				uint8_t num_components;
				uint8_t block_width[3], block_height[3];

				READ_UINT16(length);
				length -= 2;
				IMX_VPU_LOG("marker: %#lx length: %u", (unsigned long)marker, length);

				jpeg_data_cur++;
				READ_UINT16(*height);
				READ_UINT16(*width);

				if ((*width) > 8192)
				{
					IMX_VPU_ERROR("width of %u pixels exceeds the maximum of 8192", *width);
					return 0;
				}

				if ((*height) > 8192)
				{
					IMX_VPU_ERROR("height of %u pixels exceeds the maximum of 8192", *height);
					return 0;
				}

				READ_UINT8(num_components);

				if (num_components <= 3)
				{
					for (int i = 0; i < num_components; ++i)
					{
						uint8_t b;
						++jpeg_data_cur;
						READ_UINT8(b);
						block_width[i] = (b & 0xf0) >> 4;
						block_height[i] = (b & 0x0f);
						++jpeg_data_cur;
					}
				}

				if (num_components > 3)
				{
					IMX_VPU_ERROR("JPEGs with %d components are not supported", (int)num_components);
					return 0;
				}
				if (num_components == 3)
				{
					int temp = (block_width[0] * block_height[0]) / (block_width[1] * block_height[1]);

					if ((temp == 4) && (block_height[0] == 2))
						*color_format = IMX_VPU_COLOR_FORMAT_YUV420;
					else if ((temp == 2) && (block_height[0] == 1))
						*color_format = IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL;
					else if ((temp == 2) && (block_height[0] == 2))
						*color_format = IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL;
					else if ((temp == 1) && (block_height[0] == 1))
						*color_format = IMX_VPU_COLOR_FORMAT_YUV444;
					else
						*color_format = IMX_VPU_COLOR_FORMAT_YUV420;
				}
				else
					*color_format = IMX_VPU_COLOR_FORMAT_YUV400;

				IMX_VPU_LOG("width: %u  height: %u  number of components: %d", *width, *height, (int)num_components);

				found_info = 1;

				break;
			}

			default:
			{
				uint16_t length;
				READ_UINT16(length);
				length -= 2;
				IMX_VPU_LOG("marker: %#lx length: %u", (unsigned long)marker, length);
				jpeg_data_cur += length;
			}
		}
	}

	return found_info;
}
