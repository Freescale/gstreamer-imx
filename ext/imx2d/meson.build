if not gstreamer_video_dep.found()
	subdir_done()
endif

source = [
	'gstimx2dcompositor.c',
	'gstimx2dmisc.c',
	'gstimx2dvideotransform.c',
	'plugin.c'
]


backend_source = []
backend_deps = []

if imx2d_backend_g2d_dep.found()
	backend_source += ['gstimxg2dvideotransform.c']
	backend_deps += [imx2d_backend_g2d_dep]
endif

if backend_source.length() > 0
	library(
		'gstimx2d',
		source + backend_source,
		install : true,
		install_dir: plugins_install_dir,
		include_directories: [configinc, libsinc],
		dependencies : [gstimxcommon_dep, gstreamer_video_dep, imx2d_dep] + backend_deps
	)
endif
