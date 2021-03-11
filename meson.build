project('gstreamer-imx', 'c', default_options : ['c_std=c99'], version : '2.0.0')

message('Setting up build configuration for gstreamer-imx version ' + meson.project_version())


cc = meson.get_compiler('c')
pkg = import('pkgconfig')


# test for miscellaneous system libraries

libdl_dep = cc.find_library('dl', required : true)
libm_dep  = cc.find_library('m', required : true)


# test for libimxdmabuffer

libimxdmabuffer_dep = dependency('libimxdmabuffer', required : true, method : 'pkg-config')
libimxdmabuffer_ion = libimxdmabuffer_dep.get_pkgconfig_variable('libimxdmabuffer_ion', default : '')
ion_support = (libimxdmabuffer_ion != '0') and (libimxdmabuffer_ion != '')
if ion_support
	message('libimxdmabuffer supports ION allocation - enabling ION GstAllocator')
else
	message('libimxdmabuffer does not support ION allocation - not enabling ION GstAllocator')
endif


# test for GStreamer libraries

gstreamer_dep            = dependency('gstreamer-1.0',            version : '>=1.14.0', required : true)
gstreamer_base_dep       = dependency('gstreamer-base-1.0',       version : '>=1.14.0', required : true)
gstreamer_allocators_dep = dependency('gstreamer-allocators-1.0', version : '>=1.14.0', required : true)
gstreamer_audio_dep      = dependency('gstreamer-audio-1.0',      version : '>=1.14.0', required : false)
gstreamer_video_dep      = dependency('gstreamer-video-1.0',      version : '>=1.14.0', required : false)

if gstreamer_audio_dep.found()
	message('found gstaudio library - building audio plugins')
else
	message('could not find gstaudio library - not building audio plugins')
endif

if gstreamer_video_dep.found()
	message('found gstvideo library - building video plugins')
else
	message('could not find gstvideo library - not building video plugins')
endif


plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')


imx_headers_path = get_option('imx-headers-path')


configinc = include_directories('.')
libsinc = include_directories('gst-libs')


conf_data = configuration_data()
conf_data.set_quoted('GST_PACKAGE_NAME', get_option('package-name'))
conf_data.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))
conf_data.set_quoted('PACKAGE', 'gstreamer-imx')
conf_data.set_quoted('PACKAGE_BUGREPORT', 'https://github.com/Freescale/gstreamer-imx')
conf_data.set_quoted('VERSION', meson.project_version())
if ion_support
	conf_data.set('WITH_GST_ION_ALLOCATOR', 1)
endif


subdir('gst-libs/imx2d')
subdir('gst-libs/gst/imx/common')
subdir('ext/vpu')
subdir('ext/audio')
subdir('ext/imx2d')
subdir('sys/v4l2video')


configure_file(output : 'config.h', configuration : conf_data)
