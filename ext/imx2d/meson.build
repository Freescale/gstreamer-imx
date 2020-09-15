if not gstreamer_video_dep.found()
	subdir_done()
endif

source = [
	'gstimx2dcompositor.c',
	'gstimx2dmisc.c',
	'gstimx2dvideotransform.c',
	'gstimxg2dvideotransform.c',
	'plugin.c'
]


library(
	'gstimx2d',
	source,
	install : true,
	install_dir: plugins_install_dir,
	include_directories: [configinc, libsinc],
	dependencies : [gstimxcommon_dep, gstreamer_video_dep, imx2d_dep, imx2d_backend_g2d_dep]
)
