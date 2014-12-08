/* Freescale i.MX uniaudio codec functions and structures
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include <config.h>
#include <dlfcn.h>
#include "uniaudio_codec.h"


GST_DEBUG_CATEGORY_STATIC(imx_audio_uniaudio_codec_debug);
#define GST_CAT_DEFAULT imx_audio_uniaudio_codec_debug

#define UNIA_CODEC_ENTRYPOINT_FUNCTION "UniACodecQueryInterface"


static GList *codec_table = NULL;
static GstCaps *codec_table_caps = NULL;


static gpointer gst_imx_audio_uniaudio_codec_table_init_internal(G_GNUC_UNUSED gpointer data);
static gboolean gst_imx_audio_uniaudio_codec_add_codec(gchar const *library_filename, GstCaps *caps);
static GstImxAudioUniaudioCodec* gst_imx_audio_uniaudio_codec_load_codec(gchar const *library_filename, GstCaps *caps);
static void gst_imx_audio_uniaudio_codec_unload_codec(GstImxAudioUniaudioCodec *codec);


typedef struct
{
	gchar const *filename;
	gchar const *gstcaps;
}
codec_entry;




static gpointer gst_imx_audio_uniaudio_codec_table_init_internal(G_GNUC_UNUSED gpointer data)
{
	codec_entry const *entry;
	static codec_entry const codec_entries[] = UNIAUDIO_CODEC_ENTRIES;

	GST_DEBUG_CATEGORY_INIT(imx_audio_uniaudio_codec_debug, "imxuniaudiocodec", 0, "Freescale i.MX uniaudio codecs");

	codec_table = NULL;
	codec_table_caps = gst_caps_new_empty();

	for (entry = codec_entries; entry->filename != NULL; ++entry)
	{
		GstCaps *caps = gst_caps_from_string(entry->gstcaps);
		GST_DEBUG("Caps: %" GST_PTR_FORMAT, (gpointer)caps);
		if (gst_imx_audio_uniaudio_codec_add_codec(entry->filename, gst_caps_ref(caps)))
			gst_caps_append(codec_table_caps, caps);
	}

	return NULL;
}


static gboolean gst_imx_audio_uniaudio_codec_add_codec(gchar const *library_filename, GstCaps *caps)
{
	GstImxAudioUniaudioCodec *codec = gst_imx_audio_uniaudio_codec_load_codec(library_filename, caps);
	gst_caps_unref(caps);

	if (codec != NULL)
	{
		codec_table = g_list_append(codec_table, codec);
		return TRUE;
	}
	else
		return FALSE;
}


static GstImxAudioUniaudioCodec* gst_imx_audio_uniaudio_codec_load_codec(gchar const *library_filename, GstCaps *caps)
{
	GstImxAudioUniaudioCodec *codec = g_malloc0(sizeof(GstImxAudioUniaudioCodec));

	GST_DEBUG("trying to load library %s", library_filename);
	
	if ((codec->dlhandle = dlopen(library_filename, RTLD_LAZY | RTLD_LOCAL)) == NULL)
	{
		GST_ERROR("loading library %s failed: %s", library_filename, dlerror());
		gst_imx_audio_uniaudio_codec_unload_codec(codec);
		return NULL;
	}

	codec->caps = gst_caps_copy(caps);

	codec->query_interface = dlsym(codec->dlhandle, UNIA_CODEC_ENTRYPOINT_FUNCTION);
	if (codec->query_interface == NULL)
	{
		GST_ERROR("getting %s function from library %s failed: %s", UNIA_CODEC_ENTRYPOINT_FUNCTION, library_filename, dlerror());
		gst_imx_audio_uniaudio_codec_unload_codec(codec);
		return NULL;
	}

#define INIT_CODEC_FUNCTION(ID, DESC, FUNC) \
	do \
	{ \
		int32 ret = codec->query_interface((ID), (void **)(&(codec->FUNC))); \
		if (ret != ACODEC_SUCCESS) \
		{ \
			GST_ERROR("loading %s from library %s failed", DESC, library_filename); \
			gst_imx_audio_uniaudio_codec_unload_codec(codec); \
			return NULL; \
		} \
	} \
	while (0)

	INIT_CODEC_FUNCTION(ACODEC_API_GET_VERSION_INFO, "UniAUniaCodecVersionInfo",     get_version_info);
	INIT_CODEC_FUNCTION(ACODEC_API_CREATE_CODEC,     "UniAUniaCodecCreate",          create_codec);
	INIT_CODEC_FUNCTION(ACODEC_API_DELETE_CODEC,     "UniAUniaCodecDelete",          delete_codec);
	INIT_CODEC_FUNCTION(ACODEC_API_SET_PARAMETER,    "UniAUniaCodecSetParameter",    set_parameter);
	INIT_CODEC_FUNCTION(ACODEC_API_GET_PARAMETER,    "UniAUniaCodecGetParameter",    get_parameter);
	INIT_CODEC_FUNCTION(ACODEC_API_DEC_FRAME,        "UniAUniaCodec_decode_frame",   decode_frame);
	INIT_CODEC_FUNCTION(ACODEC_API_RESET_CODEC,      "UniAUniaCodecReset",           reset);
	INIT_CODEC_FUNCTION(ACODEC_API_GET_LAST_ERROR,   "UniAUniaCodec_get_last_error", get_last_error);

#undef INIT_CODEC_FUNCTION

	return codec;
}


static void gst_imx_audio_uniaudio_codec_unload_codec(GstImxAudioUniaudioCodec *codec)
{
	if (codec->caps != NULL)
		gst_caps_unref(codec->caps);
	if (codec->dlhandle != NULL)
		dlclose(codec->dlhandle);
	g_free(codec);
}




void gst_imx_audio_uniaudio_codec_table_init(void)
{
	static GOnce init_once = G_ONCE_INIT;
	g_once(&init_once, gst_imx_audio_uniaudio_codec_table_init_internal, NULL);
}


GstCaps* gst_imx_audio_uniaudio_codec_table_get_caps(void)
{
	return codec_table_caps;
}


GstImxAudioUniaudioCodec* gst_imx_audio_uniaudio_codec_table_get_codec(GstCaps *caps)
{
	GList *l;
	GST_DEBUG("trying to find suitable codec for caps %" GST_PTR_FORMAT, (gpointer)caps);
	for (l = codec_table; l != NULL; l = l->next)
	{
		GstImxAudioUniaudioCodec *codec = (GstImxAudioUniaudioCodec *)(l->data);
		gboolean ok = gst_caps_is_always_compatible(caps, codec->caps);
		GST_DEBUG("codec caps %" GST_PTR_FORMAT " compatible: %s", (gpointer)(codec->caps), ok ? "yes" : "no");
		if (ok)
			return codec;
	}

	GST_WARNING("no suitable codec found");

	return NULL;
}
