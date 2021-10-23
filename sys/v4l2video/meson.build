v4l2_enabled = get_option('v4l2')

if v4l2_enabled
	message('mxc_v4l2 Video4Linux2 source and sink elements enabled')

	source = [
		'gstimxv4l2context.c',
		'gstimxv4l2object.c',
		'gstimxv4l2videoformat.c',
		'gstimxv4l2videosrc.c',
		'gstimxv4l2videosink.c',
		'gstimxv4l2videotransform.c',
		'plugin.c'
	]

	library(
		'gstimxv4l2video',
		source,
		install : true,
		install_dir: plugins_install_dir,
		include_directories: [configinc, libsinc],
		dependencies : [gstimxcommon_dep, gstimxvideo_dep, gstreamer_allocators_dep, gstreamer_video_dep]
	)
else
	message('mxc_v4l2 Video4Linux2 source and sink elements disabled')
endif
