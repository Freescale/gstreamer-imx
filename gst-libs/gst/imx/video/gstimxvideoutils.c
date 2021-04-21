#include "gstimxvideoutils.h"


gint gst_imx_video_utils_calculate_total_num_frame_rows(GstBuffer *video_frame_buffer, GstVideoInfo const *video_info)
{
	gint total_num_frame_rows;
	GstVideoMeta *video_meta = (video_frame_buffer != NULL) ? gst_buffer_get_video_meta(video_frame_buffer) : NULL;

	/* The number of plane rows are derived from the plane offsets.
	 * This assumes that the distance between the first and the second plane offsets
	 * is an integer multiple of the first plane's stride, because the first plane
	 * _has_ to fit in there, along with any additional padding rows.
	 * For single-plane formats, we assume that the buffer size is an integer
	 * multiple of the first plane's stride. */

	if (video_meta != NULL)
	{
		if (video_meta->n_planes > 1)
			total_num_frame_rows = (video_meta->offset[1] - video_meta->offset[0]) / video_meta->stride[0];
		else
			total_num_frame_rows = gst_buffer_get_size(video_frame_buffer) / video_meta->stride[0];
	}
	else
	{
		g_assert(video_info != NULL);

		if (GST_VIDEO_INFO_N_PLANES(video_info) > 1)
			total_num_frame_rows = (GST_VIDEO_INFO_PLANE_OFFSET(video_info, 1) - GST_VIDEO_INFO_PLANE_OFFSET(video_info, 0)) / GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
		else
			total_num_frame_rows = GST_VIDEO_INFO_SIZE(video_info) / GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
	}

	return total_num_frame_rows;
}
