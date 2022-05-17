source = [
	'gstimxvideobufferpool.c',
	'gstimxvideodmabufferpool.c',
	'gstimxvideouploader.c',
	'gstimxvideoutils.c'
]
public_headers = [
	'gstimxvideobufferpool.h',
	'gstimxvideodmabufferpool.h',
	'gstimxvideouploader.h',
	'gstimxvideoutils.h'
]

gstimxvideo = library(
	'gstimxvideo',
	source,
	version : meson.project_version(),
	install : true,
	include_directories: [configinc],
	dependencies : [gstimxcommon_dep, gstreamer_video_dep]
)

gstimxvideo_dep = declare_dependency(
	dependencies : [gstimxcommon_dep, gstreamer_video_dep],
	include_directories : libsinc,
	link_with : [gstimxvideo]
)

install_headers(public_headers, subdir : 'gstreamer-1.0/gst/imx/video')

pkg.generate(
	gstimxvideo,
	name : 'gstimxvideo',
	description : 'GStreamer IMX common video code'
)
