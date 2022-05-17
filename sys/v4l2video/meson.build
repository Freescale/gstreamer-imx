source = []
dependencies = [gstimxcommon_dep, gstreamer_video_dep]

# mxc_v4l2 elements, available on i.MX6 and i.MX7 SoCs

v4l2_mxc_source_sink_enabled = get_option('v4l2') or get_option('v4l2-mxc-source-sink')
if v4l2_mxc_source_sink_enabled
	message('mxc_v4l2 Video4Linux2 source and sink elements enabled')

	conf_data.set('WITH_IMX_V4L2_VIDEO_SRC', 1)
	conf_data.set('WITH_IMX_V4L2_VIDEO_SINK', 1)

	source += [
		'gstimxv4l2context.c',
		'gstimxv4l2object.c',
		'gstimxv4l2videosrc.c',
		'gstimxv4l2videosink.c',
	]
else
	message('mxc_v4l2 Video4Linux2 source and sink elements disabled')
endif

# V4L2 ISI (Image Sensing Interface) mem2mem transform element, available on i.MX8 QuadMax/QuadXPlus SoCs

v4l2_isi_enabled = get_option('v4l2-isi')
if v4l2_isi_enabled
	message('i.MX8 ISI Video4Linux2 mem2mem transform element enabled')

	conf_data.set('WITH_IMX_V4L2_ISI_VIDEO_TRANSFORM', 1)

	source += [
		'gstimxv4l2isivideotransform.c',
	]
	dependencies += [gstimxvideo_dep]
else
	message('i.MX8 ISI Video4Linux2 mem2mem transform element disabled')
endif

# V4L2 Amphion Malone mem2mem video decoder element, available on i.MX8 QuadMax/QuadXPlus SoCs

v4l2_amphion_option = get_option('v4l2-amphion')
v4l2_amphion_enabled = false
if v4l2_amphion_option.disabled()
	message('Amphion Malone Video4Linux2 mem2mem decoder element disabled')
else
	if imx2d_backend_g2d_dep.found()
		message('Amphion Malone Video4Linux2 mem2mem decoder element enabled')
		v4l2_amphion_enabled = true
	else
		if v4l2_amphion_option.enabled()
			error('Amphion Malone Video4Linux2 mem2mem decoder element enabled, but G2D is not available')
		else
			message('Amphion Malone Video4Linux2 mem2mem decoder element enabled, but G2D is not available; disabling decoder element')
		endif
	endif
endif

if v4l2_amphion_enabled
	conf_data.set('WITH_IMX_V4L2_AMPHION_DECODER', 1)

	source += [
		'gstimxv4l2amphiondec.c',
		'gstimxv4l2amphionmisc.c',
	]
	dependencies += [imx2d_dep, imx2d_backend_g2d_dep, gstimxvideo_dep]
endif

# Common code and the actual GStreamer plugin shared object

if v4l2_mxc_source_sink_enabled or v4l2_isi_enabled or v4l2_amphion_enabled
	source += [
		'gstimxv4l2videoformat.c',
		'plugin.c'
	]
endif

if source != []
	library(
		'gstimxv4l2video',
		source,
		install : true,
		install_dir: plugins_install_dir,
		include_directories: [configinc, libsinc],
		dependencies: dependencies
	)
endif
