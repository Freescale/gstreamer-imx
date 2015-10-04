/* Common load/unload functions for the Freescale VPU device
 * Copyright (C) 2015  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <stdarg.h>
#include "device.h"
#include "imxvpuapi/imxvpuapi.h"




GST_DEBUG_CATEGORY_STATIC(imx_vpu_api_debug);
#define GST_CAT_DEFAULT imx_vpu_api_debug


/* GLib mutexes are implicitely initialized if they are global */
static GMutex load_mutex;
static gboolean head_functions_set_up = FALSE;
static gboolean logging_set_up = FALSE;


static void imx_vpu_setup_heap_allocator_functions(void);
static void* imx_vpu_heap_alloc_func(size_t const size, void *context, char const *file, int const line, char const *fn);
static void imx_vpu_heap_free_func(void *memblock, size_t const size, void *context, char const *file, int const line, char const *fn);

static void imx_vpu_logging_func(ImxVpuLogLevel level, char const *file, int const line, char const *fn, const char *format, ...);


gboolean gst_imx_vpu_decoder_load()
{
	gboolean ret;

	imx_vpu_setup_heap_allocator_functions();

	g_mutex_lock(&load_mutex);
	ret = (imx_vpu_dec_load() == IMX_VPU_DEC_RETURN_CODE_OK);
	g_mutex_unlock(&load_mutex);

	return ret;
}


void gst_imx_vpu_decoder_unload()
{
	g_mutex_lock(&load_mutex);
	imx_vpu_dec_unload();
	g_mutex_unlock(&load_mutex);
}


gboolean gst_imx_vpu_encoder_load()
{
	gboolean ret;

	imx_vpu_setup_heap_allocator_functions();

	g_mutex_lock(&load_mutex);
	ret = (imx_vpu_enc_load() == IMX_VPU_ENC_RETURN_CODE_OK);
	g_mutex_unlock(&load_mutex);

	return ret;
}


void gst_imx_vpu_encoder_unload()
{
	g_mutex_lock(&load_mutex);
	imx_vpu_enc_unload();
	g_mutex_unlock(&load_mutex);
}


void imx_vpu_setup_logging(void)
{
	g_mutex_lock(&load_mutex);
	if (!logging_set_up)
	{
		ImxVpuLogLevel level;

		GST_DEBUG_CATEGORY_INIT(imx_vpu_api_debug, "imxvpuapi", 0, "imxvpuapi library for controlling the Freescale i.MX VPU");
		GstDebugLevel gst_level = gst_debug_category_get_threshold(imx_vpu_api_debug);

		switch (gst_level)
		{
			case GST_LEVEL_ERROR:   level = IMX_VPU_LOG_LEVEL_ERROR;   break;
			case GST_LEVEL_WARNING: level = IMX_VPU_LOG_LEVEL_WARNING; break;
			case GST_LEVEL_INFO:    level = IMX_VPU_LOG_LEVEL_INFO;    break;
			case GST_LEVEL_DEBUG:   level = IMX_VPU_LOG_LEVEL_DEBUG;   break;
			case GST_LEVEL_LOG:     level = IMX_VPU_LOG_LEVEL_LOG;     break;
			case GST_LEVEL_TRACE:   level = IMX_VPU_LOG_LEVEL_TRACE;   break;
			default: level = IMX_VPU_LOG_LEVEL_TRACE;
		}

		imx_vpu_set_logging_threshold(level);
		imx_vpu_set_logging_function(imx_vpu_logging_func);
		logging_set_up = TRUE;
	}
	g_mutex_unlock(&load_mutex);
}


static void imx_vpu_setup_heap_allocator_functions(void)
{
	g_mutex_lock(&load_mutex);
	if (!head_functions_set_up)
	{
		imx_vpu_set_heap_allocator_functions(imx_vpu_heap_alloc_func, imx_vpu_heap_free_func, NULL);
		head_functions_set_up = TRUE;
	}
	g_mutex_unlock(&load_mutex);
}


static void* imx_vpu_heap_alloc_func(size_t const size, G_GNUC_UNUSED void *context, char const *file, int const line, char const *fn)
{
	void *ptr = g_slice_alloc(size);
	imx_vpu_logging_func(IMX_VPU_LOG_LEVEL_TRACE, file, line, fn, "allocated %zu byte, ptr: %p", size, ptr);
	return ptr;
}


static void imx_vpu_heap_free_func(void *memblock, size_t const size, G_GNUC_UNUSED void *context, char const *file, int const line, char const *fn)
{
	g_slice_free1(size, memblock);
	imx_vpu_logging_func(IMX_VPU_LOG_LEVEL_TRACE, file, line, fn, "freed %zu byte, ptr: %p", size, memblock);
}


static void imx_vpu_logging_func(ImxVpuLogLevel level, char const *file, int const line, char const *fn, const char *format, ...)
{
	GstDebugLevel gst_level;
	va_list args;

	switch (level)
	{
		case IMX_VPU_LOG_LEVEL_ERROR:   gst_level = GST_LEVEL_ERROR;   break;
		case IMX_VPU_LOG_LEVEL_WARNING: gst_level = GST_LEVEL_WARNING; break;
		case IMX_VPU_LOG_LEVEL_INFO:    gst_level = GST_LEVEL_INFO;    break;
		case IMX_VPU_LOG_LEVEL_DEBUG:   gst_level = GST_LEVEL_DEBUG;   break;
		case IMX_VPU_LOG_LEVEL_LOG:     gst_level = GST_LEVEL_LOG;     break;
		case IMX_VPU_LOG_LEVEL_TRACE:   gst_level = GST_LEVEL_TRACE;   break;
		default: gst_level = GST_LEVEL_LOG;
	}

	va_start(args, format);
	gst_debug_log_valist(imx_vpu_api_debug, gst_level, file, fn, line, NULL, format, args);
	va_end(args);
}
