if not gstreamer_video_dep.found()
	# TODO: if VPU is set to "enabled", and gstvideo was not found, print an error and exit
	subdir_done()
endif

libimxvpuapi2_dep = dependency('libimxvpuapi2', version : '>=2.3.0', required : get_option('vpu'))

if libimxvpuapi2_dep.found()
	message('Found installed imxvpuapi library - VPU elements will be built')
else
	message('Installed imxvpuapi library not found, or user disabled VPU plugin compilation - VPU elements will not be built')
	subdir_done()
endif

source = [
	'gstimxvpucommon.c',
	'gstimxvpudecbufferpool.c',
	'gstimxvpudec.c',
	'gstimxvpudeccontext.c',
	'gstimxvpuenc.c',
	'gstimxvpuench263.c',
	'gstimxvpuench264.c',
	'gstimxvpuencjpeg.c',
	'gstimxvpuencmpeg4.c',
	'gstimxvpuencvp8.c',
	'plugin.c'
]


gstimxvpu = library(
	'gstimxvpu',
	source,
	install : true,
	install_dir: plugins_install_dir,
	include_directories: [configinc, libsinc],
	dependencies : [gstimxcommon_dep, gstreamer_video_dep, libimxvpuapi2_dep],
	link_with : [gstimxcommon]
)
plugins += [gstimxvpu]
