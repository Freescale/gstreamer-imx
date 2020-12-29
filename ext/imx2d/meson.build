if not gstreamer_video_dep.found()
	subdir_done()
endif

imx2d_compositor_option = get_option('imx2d-compositor')
imx2d_compositor_enabled = false
if imx2d_compositor_option.disabled()
	message('imx2d compositor elements disabled explicitely by command line option')
else
	is_gst_1_16 = gstreamer_video_dep.version().version_compare('>=1.16.0')
	if is_gst_1_16
		message('imx2d compositor elements enabled')
		imx2d_compositor_enabled = true
	else
		if imx2d_compositor_option.enabled()
			error('imx2d compositor elements enabled, but GStreamer version is too old (must be at least 1.16.0 for GstVideoAggregator in gst-plugins-base)')
		else
			message('imx2d compositor elements enabled, but GStreamer version is too old (must be at least 1.16.0 for GstVideoAggregator in gst-plugins-base); disabling compositor elements')
			imx2d_compositor_enabled = false
		endif
	endif
endif

imx2d_videosink_enabled = get_option('imx2d-videosink')
if imx2d_videosink_enabled
	message('imx2d videosink elements enabled')
else
	message('imx2d videosink elements disabled')
endif

source = [
	'gstimx2dmisc.c',
	'gstimx2dvideotransform.c',
	'gstimx2dvideooverlayhandler.c',
	'plugin.c'
]

if imx2d_compositor_enabled
	source += ['gstimx2dcompositor.c']
	conf_data.set('WITH_GST_IMX2D_COMPOSITOR', 1)
endif

if imx2d_videosink_enabled
	source += ['gstimx2dvideosink.c']
	conf_data.set('WITH_GST_IMX2D_VIDEOSINK', 1)
endif


backend_source = []
backend_deps = []

if imx2d_backend_g2d_dep.found()
	backend_source += [
		'gstimxg2dvideotransform.c'
	]
	if imx2d_compositor_enabled
		source += ['gstimxg2dcompositor.c']
	endif
	if imx2d_videosink_enabled
		source += ['gstimxg2dvideosink.c']
	endif
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
