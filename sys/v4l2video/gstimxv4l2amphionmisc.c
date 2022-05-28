#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <linux/videodev2.h>
#include <gst/gst.h>
#include "gstimxv4l2amphionmisc.h"


GST_DEBUG_CATEGORY_EXTERN(imx_v4l2_amphion_misc_debug);
#define GST_CAT_DEFAULT imx_v4l2_amphion_misc_debug


GstImxV4L2AmphionDeviceFilenames gst_imx_v4l2_amphion_device_filenames;

static GMutex device_fn_mutex;

void gst_imx_v4l2_amphion_device_filenames_init(void)
{
	DIR *dir = NULL;
	char tempstr[GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH];
	static char const device_node_fn_prefix[] = "/dev/video";
	static size_t const device_node_fn_prefix_length = sizeof(device_node_fn_prefix) - 1;
	struct dirent *dir_entry;

	g_mutex_lock(&device_fn_mutex);

	if (gst_imx_v4l2_amphion_device_filenames.initialized)
		goto finish;

	GST_DEBUG("scanning for VPU device nodes");

	memset(&gst_imx_v4l2_amphion_device_filenames, 0, sizeof(gst_imx_v4l2_amphion_device_filenames));

	dir = opendir("/dev");
	if (dir == NULL)
	{
		GST_ERROR("could not open /dev/ directory to look for V4L2 device nodes: %s (%d)", strerror(errno), errno);
		goto finish;
	}

	while ((dir_entry = readdir(dir)) != NULL)
	{
		int index;
		int fd = -1;
		gboolean is_valid_decoder;
		gboolean is_valid_encoder;
		struct stat entry_stat;
		struct v4l2_capability capability;
		struct v4l2_fmtdesc format_desc;

		snprintf(tempstr, sizeof(tempstr), "/dev/%s", dir_entry->d_name);

		/* Run stat() on the file with filename tempstr, and perform
		 * checks on that call's output to filter out candidates. */

		if (stat(tempstr, &entry_stat) < 0)
		{
			switch (errno)
			{
				case EACCES:
					GST_DEBUG("skipping \"%s\" while looking for V4L2 device nodes since access was denied", tempstr);
					break;
				default:
					GST_ERROR("stat() call on \"%s\" failed: %s (%d)", tempstr, strerror(errno), errno);
					break;
			}

			goto next;
		}

		if (!S_ISCHR(entry_stat.st_mode))
			goto next;

		if (strncmp(tempstr, device_node_fn_prefix, device_node_fn_prefix_length) != 0)
			goto next;

		/* This might be a valid en/decoder. Open a FD and perform
		 * V4L2 queries to further analyze this device node. */

		fd = open(tempstr, O_RDWR);
		if (fd < 0)
		{
			GST_DEBUG("could not open device node \"%s\": %s (%d) - skipping", tempstr, strerror(errno), errno);
			goto next;
		}

		if (ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0)
		{
			GST_DEBUG("could not query V4L2 capability from device node \"%s\": %s (%d) - skipping", tempstr, strerror(errno), errno);
			goto next;
		}

		if ((capability.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) == 0)
		{
			GST_DEBUG("skipping V4L2 device \"%s\" since it does not support multi-planar mem2mem processing", tempstr);
			goto next;
		}

		if ((capability.capabilities & V4L2_CAP_STREAMING) == 0)
		{
			GST_DEBUG("skipping V4L2 device \"%s\" since it does not support frame streaming", tempstr);
			goto next;
		}

		is_valid_decoder = FALSE;
		is_valid_encoder = FALSE;

		GST_DEBUG("analyzing device node \"%s\"", tempstr);

		/* Check if this device node is a valid decoder. Do this by
		 * looking at the input formats it supports. The Malone
		 * decoder supports h.264 as input, so check for that. */

		for (index = 0; ; ++index)
		{
			memset(&format_desc, 0, sizeof(format_desc));
			format_desc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			format_desc.index = index;

			if (ioctl(fd, VIDIOC_ENUM_FMT, &format_desc) < 0)
			{
				/* EINVAL is not an actual error. It just denotes that
				 * we have reached the list of supported formats. */
				if (errno != EINVAL)
					GST_DEBUG("could not query output format (index %d) from from decoder candidate \"%s\": %s (%d) - skipping", index, tempstr, strerror(errno), errno);

				break;
			}

			GST_DEBUG("  input format query returned fourCC for format at index %d: %" GST_FOURCC_FORMAT, index, GST_FOURCC_ARGS(format_desc.pixelformat));
			if (format_desc.pixelformat == V4L2_PIX_FMT_H264)
			{
				is_valid_decoder = TRUE;
				break;
			}
		}

		if (is_valid_decoder)
			memcpy(gst_imx_v4l2_amphion_device_filenames.decoder_filename, tempstr, GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH);

		/* Check if this device node is a valid encoder. Do this by
		 * looking at the output formats it supports. The Windsor
		 * encoder supports h.264 as output, so check for that. */

		for (index = 0; ; ++index)
		{
			memset(&format_desc, 0, sizeof(format_desc));
			format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			format_desc.index = index;

			if (ioctl(fd, VIDIOC_ENUM_FMT, &format_desc) < 0)
			{
				/* EINVAL is not an actual error. It just denotes that
				 * we have reached the list of supported formats. */
				if (errno != EINVAL)
					GST_DEBUG("could not query capture format (index %d) from from encoder candidate \"%s\": %s (%d) - skipping", index, tempstr, strerror(errno), errno);

				break;
			}

			GST_DEBUG("  output format query returned fourCC for format at index %d: %" GST_FOURCC_FORMAT, index, GST_FOURCC_ARGS(format_desc.pixelformat));
			if (format_desc.pixelformat == V4L2_PIX_FMT_H264)
			{
				is_valid_encoder = TRUE;
				break;
			}
		}

		if (is_valid_encoder)
			memcpy(gst_imx_v4l2_amphion_device_filenames.encoder_filename, tempstr, GST_IMX_V4L2_AMPHION_DEVICE_FILENAME_LENGTH);

		if (is_valid_encoder)
			GST_DEBUG("device node \"%s\" is a valid encoder", tempstr);
		else if (is_valid_decoder)
			GST_DEBUG("device node \"%s\" is a valid decoder", tempstr);
		else
			GST_DEBUG("device node \"%s\" is neither a valid encoder nor a valid decoder", tempstr);

next:
		if (fd >= 0)
			close(fd);
	}

	gst_imx_v4l2_amphion_device_filenames.initialized = TRUE;

finish:
	if (dir != NULL)
		closedir(dir);

	g_mutex_unlock(&device_fn_mutex);
}


GstCaps* gst_imx_v4l2_amphion_get_caps_for_format(guint32 v4l2_pixelformat)
{
	GstStructure *structure = NULL;

	switch (v4l2_pixelformat)
	{
		case V4L2_PIX_FMT_MJPEG:
			structure = gst_structure_new(
				"image/jpeg",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				NULL
			);
			break;

		case V4L2_PIX_FMT_MPEG2:
			structure = gst_structure_new(
				"video/mpeg",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				"systemstream", G_TYPE_BOOLEAN, FALSE,
				"mpegversion", GST_TYPE_INT_RANGE, 1, 2,
				NULL
			);
			break;

		case V4L2_PIX_FMT_MPEG4:
			structure = gst_structure_new(
				"video/mpeg",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				"mpegversion", G_TYPE_INT, 4,
				NULL
			);
			break;

		case V4L2_PIX_FMT_H263:
			structure = gst_structure_new(
				"video/x-h263",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				"variant", G_TYPE_STRING, "itu",
				NULL
			);
			break;

		case V4L2_PIX_FMT_H264:
		{
			GValue list_value = G_VALUE_INIT;
			GValue string_value = G_VALUE_INIT;

			structure = gst_structure_new(
				"video/x-h264",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				"stream-format", G_TYPE_STRING, "byte-stream",
				"alignment", G_TYPE_STRING, "au",
				NULL
			);

			{
				g_value_init(&list_value, GST_TYPE_LIST);
				g_value_init(&string_value, G_TYPE_STRING);

				g_value_set_static_string(&string_value, "constrained-baseline");
				gst_value_list_append_value(&list_value, &string_value);

				g_value_set_static_string(&string_value, "baseline");
				gst_value_list_append_value(&list_value, &string_value);

				g_value_set_static_string(&string_value, "main");
				gst_value_list_append_value(&list_value, &string_value);

				g_value_set_static_string(&string_value, "high");
				gst_value_list_append_value(&list_value, &string_value);

				gst_structure_set_value(structure, "profile", &list_value);

				g_value_unset(&list_value);
				g_value_unset(&string_value);
			}

			break;
		}

		case V4L2_PIX_FMT_HEVC:
		{
			GValue list_value = G_VALUE_INIT;
			GValue string_value = G_VALUE_INIT;

			structure = gst_structure_new(
				"video/x-h265",
				"parsed", G_TYPE_BOOLEAN, TRUE,
				"stream-format", G_TYPE_STRING, "byte-stream",
				"alignment", G_TYPE_STRING, "au",
				NULL
			);

			{
				g_value_init(&list_value, GST_TYPE_LIST);
				g_value_init(&string_value, G_TYPE_STRING);

				g_value_set_static_string(&string_value, "main");
				gst_value_list_append_value(&list_value, &string_value);

				g_value_set_static_string(&string_value, "main-10");
				gst_value_list_append_value(&list_value, &string_value);

				gst_structure_set_value(structure, "profile", &list_value);

				g_value_unset(&list_value);
				g_value_unset(&string_value);
			}

			break;
		}

		case V4L2_PIX_FMT_VC1_ANNEX_G:
			structure = gst_structure_new(
				"video/x-wmv",
				"wmvversion", G_TYPE_INT, 3,
				"format", G_TYPE_STRING, "WMV3",
				NULL
			);
			break;

		case V4L2_PIX_FMT_VC1_ANNEX_L:
			structure = gst_structure_new(
				"video/x-wmv",
				"wmvversion", G_TYPE_INT, 3,
				"format", G_TYPE_STRING, "WVC1",
				NULL
			);
			break;

		case V4L2_VPU_PIX_FMT_VP6:
			structure = gst_structure_new_empty("video/x-vp6");
			break;

		case V4L2_PIX_FMT_VP8:
			structure = gst_structure_new_empty("video/x-vp8");
			break;

		case V4L2_PIX_FMT_VP9:
			structure = gst_structure_new_empty("video/x-vp9");
			break;

		case V4L2_VPU_PIX_FMT_AVS:
			structure = gst_structure_new_empty("video/x-cavs");
			break;

		case V4L2_VPU_PIX_FMT_RV:
			structure = gst_structure_new(
				"video/x-pn-realvideo",
				"rmversion", GST_TYPE_INT_RANGE, 3, 4,
				NULL
			);
			break;

		case V4L2_VPU_PIX_FMT_DIV3:
			structure = gst_structure_new(
				"video/x-divx",
				"divxversion", G_TYPE_INT, 3,
				NULL
			);
			break;

		case V4L2_VPU_PIX_FMT_DIVX:
			structure = gst_structure_new(
				"video/x-divx",
				"divxversion", GST_TYPE_INT_RANGE, 4, 5,
				NULL
			);
			break;

		case V4L2_VPU_PIX_FMT_SPK:
			structure = gst_structure_new(
				"video/x-flash-video",
				"flvversion", G_TYPE_INT, 1,
				NULL
			);
			break;

		default:
			gst_structure_free(structure);
			return FALSE;
	}

	g_assert(structure != NULL);

	return gst_caps_new_full(structure, NULL);
}
