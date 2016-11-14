#!/usr/bin/env python


from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext, Logs
import re

top = '.'
out = 'build'

gstimx_version = "0.12.3"

# the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a
# compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the
# code being compiled causes a warning
c_cflag_check_code = """
int main()
{
	float f = 4.0;
	char c = f;
	return c - 4;
}
"""
def check_compiler_flag(conf, flag, lang):
	return conf.check(fragment = c_cflag_check_code, mandatory = 0, execute = 0, define_ret = 0, msg = 'Checking for compiler switch %s' % flag, cxxflags = conf.env[lang + 'FLAGS'] + [flag], okmsg = 'yes', errmsg = 'no')  
def check_compiler_flags_2(conf, cflags, ldflags, msg):
	Logs.pprint('NORMAL', msg)
	return conf.check(fragment = c_cflag_check_code, mandatory = 0, execute = 0, define_ret = 0, msg = 'Checking if building with these flags works', cxxflags = cflags, ldflags = ldflags, okmsg = 'yes', errmsg = 'no')


def add_compiler_flags(conf, env, flags, lang, compiler, uselib = ''):
	for flag in reversed(flags):
		if type(flag) == type(()):
			flag_candidate = flag[0]
			flag_alternative = flag[1]
		else:
			flag_candidate = flag
			flag_alternative = None

		if uselib:
			flags_pattern = lang + 'FLAGS_' + uselib
		else:
			flags_pattern = lang + 'FLAGS'

		if check_compiler_flag(conf, flag_candidate, compiler):
			env.prepend_value(flags_pattern, [flag_candidate])
		elif flag_alternative:
			if check_compiler_flag(conf, flag_alternative, compiler):
				env.prepend_value(flags_pattern, [flag_alternative])


def options(opt):
	opt.add_option('--enable-debug', action = 'store_true', default = False, help = 'enable debug build [default: disabled]')
	opt.add_option('--with-package-name', action = 'store', default = "Unknown package release", help = 'specify package name to use in plugin [default: %default]')
	opt.add_option('--with-package-origin', action = 'store', default = "Unknown package origin", help = 'specify package origin URL to use in plugin [default: %default]')
	opt.add_option('--plugin-install-path', action = 'store', default = "${PREFIX}/lib/gstreamer-1.0", help = 'where to install the plugin for GStreamer 1.0 [default: %default]')
	opt.add_option('--kernel-headers', action = 'store', default = None, help = 'specify path to the kernel headers')
	opt.add_option('--build-for-android', action = 'store_true', default = False, help = 'build with Android support [default: no Android support]')
	opt.load('compiler_c')
	opt.load('gnu_dirs')
	opt.recurse('src/g2d')
	opt.recurse('src/pxp')
	opt.recurse('src/ipu')
	opt.recurse('src/vpu')
	opt.recurse('src/eglvivsink')
	opt.recurse('src/v4l2src')
	opt.recurse('src/audio')


def check_linux_headers(conf):
	import os
	incpaths = []
	extra_incpaths = []
	notfound = None
	if conf.options.kernel_headers:
		kernel_headers_fullpath = os.path.abspath(os.path.expanduser(conf.options.kernel_headers))
		incpaths += [kernel_headers_fullpath]
		extra_incpaths += [os.path.abspath(os.path.expanduser(os.path.join(kernel_headers_fullpath, '..', 'usr', 'include')))]
	# for the test, make sure the right version.h is used by adding an include path to
	# <kernel_headers_fullpath>/../usr/include
	with_uapi = conf.check_cc(fragment = '''
		#include <linux/version.h>
		int main() {
		#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 5, 0)
			return 0;
		#else
		#error fail
		#endif
		}
		''',
		includes = incpaths + extra_incpaths,
		mandatory = False,
		execute = False,
		msg = 'checking whether or not the kernel version is greater than 3.5.0'
	)
	if conf.options.kernel_headers:
		if with_uapi:
			incpaths = [os.path.join(kernel_headers_fullpath, 'uapi')] + incpaths
	if incpaths:
		conf.env['INCLUDES_KERNEL_HEADERS'] = incpaths


def configure(conf):
	import os

	conf.load('compiler_c')
	conf.load('gnu_dirs')

	# check and add compiler flags

	if conf.env['CFLAGS'] and conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], conf.env['LINKFLAGS'], "Need to test compiler flags %s and linker flags %s" % (' '.join(conf.env['CFLAGS']), ' '.join(conf.env['LINKFLAGS'])))
	elif conf.env['CFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], '', "Need to test compiler flags %s" % ' '.join(conf.env['CFLAGS']))
	elif conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, '', conf.env['LINKFLAGS'], "Need to test linker flags %s" % ' '.join(conf.env['LINKFLAGS']))

	compiler_flags = ['-Wextra', '-Wall', '-std=gnu99', '-fPIC', '-DPIC']
	if conf.options.enable_debug:
		compiler_flags += ['-O0', '-g3', '-ggdb']
	else:
		compiler_flags += ['-O2']

	add_compiler_flags(conf, conf.env, compiler_flags, 'C', 'C')

	add_compiler_flags(conf, conf.env, ['-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Wno-sign-compare'], 'C', 'C', 'GSTBACKPORT')

	# Disable warning about including kernel headers. This is generally not a good idea, but some APIs like IPU and PxP
	# leave no other choice, since there are no (reliable) userspace libraries/headers for these.
	add_compiler_flags(conf, conf.env, ['-Wno-cpp'], 'C', 'C', 'KERNEL_HEADERS')


	# some extra output for Android
	conf.msg('Building for Android', result = 'yes' if conf.options.build_for_android else 'no', color = 'GREEN' if conf.options.build_for_android else 'YELLOW')
	if conf.options.build_for_android:
		conf.define('BUILD_FOR_ANDROID', 1)
		conf.define('GST_PLUGIN_BUILD_STATIC', 1)
		conf.env['BUILD_FOR_ANDROID'] = True
		conf.env['CLIBTYPE'] = 'cstlib' # Cannot use shared library builds of gstreamer-imx with Android
	else:
		conf.env['CLIBTYPE'] = 'cshlib'


	# test for miscellaneous system libraries

	conf.check_cc(lib = 'dl', uselib_store = 'DL', mandatory = 1)
	conf.check_cc(lib = 'm', uselib_store = 'M', mandatory = 1)

	# Android's libc (called "bionic") includes the pthreads library, however it still needs the -pthread flag
	if conf.options.build_for_android or conf.check_cc(lib = 'pthread', uselib_store = 'PTHREAD', mandatory = 1):
		conf.env['CFLAGS_PTHREAD'] += ['-pthread']


	# test for GStreamer libraries

	gst_version_str = conf.check_cfg(package = 'gstreamer-1.0 >= 1.2.0', modversion = "gstreamer-1.0", uselib_store = 'GSTREAMER', args = '--cflags --libs', mandatory = 1)
	gst_version = [int(x) for x in re.match('(\d*)\.(\d*)\.(\d*)', gst_version_str).groups()]
	conf.env['GSTREAMER_VERSION'] = gst_version

	conf.check_cfg(package = 'gstreamer-1.0 >= 1.2.0', uselib_store = 'GSTREAMER', args = '--cflags --libs', mandatory = 1)
	conf.check_cfg(package = 'gstreamer-base-1.0 >= 1.2.0', uselib_store = 'GSTREAMER_BASE', args = '--cflags --libs', mandatory = 1)
	if conf.check_cfg(package = 'gstreamer-audio-1.0 >= 1.2.0', uselib_store = 'GSTREAMER_AUDIO', args = '--cflags --libs', mandatory = 0):
		conf.env['WITH_GSTAUDIO'] = True
	else:
		Logs.pprint('RED', 'could not find gstaudio library - not building audio plugins')
	if conf.check_cfg(package = 'gstreamer-video-1.0 >= 1.2.0', uselib_store = 'GSTREAMER_VIDEO', args = '--cflags --libs', mandatory = 0):
		conf.env['WITH_GSTVIDEO'] = True
	else:
		Logs.pprint('RED', 'could not find gstvideo library - not building video plugins')
	if conf.check_cc(lib = 'gstphotography-1.0', uselib_store = 'GSTPHOTOGRAPHY', mandatory = 0):
		conf.env['WITH_GSTPHOTOGRAPHY'] = True


	# check the kernel header path

	check_linux_headers(conf)


	# misc definitions & env vars

	conf.env['PLUGIN_INSTALL_PATH'] = os.path.expanduser(conf.options.plugin_install_path)
	if conf.env['BUILD_FOR_ANDROID']:
		conf.env['PLUGIN_INSTALL_PATH'] = os.path.join(conf.env['PLUGIN_INSTALL_PATH'], 'static')

	conf.define('GST_PACKAGE_NAME', conf.options.with_package_name)
	conf.define('GST_PACKAGE_ORIGIN', conf.options.with_package_origin)
	conf.define('PACKAGE', "gstreamer-imx")
	conf.define('PACKAGE_BUGREPORT', "https://github.com/Freescale/gstreamer-imx")
	conf.define('VERSION', gstimx_version)

	conf.env['GSTIMX_VERSION'] = gstimx_version
	conf.env['COMMON_USELIB'] = ['GSTREAMER', 'GSTREAMER_BASE', 'GSTREAMER_AUDIO', 'GSTREAMER_VIDEO', 'PTHREAD', 'M']


	conf.recurse('src/common')
	if not conf.options.build_for_android:
		conf.recurse('src/g2d')
	conf.recurse('src/pxp')
	conf.recurse('src/ipu')
	conf.recurse('src/vpu')
	conf.recurse('src/eglvivsink')
	if not conf.options.build_for_android:
		conf.recurse('src/v4l2src')
	conf.recurse('src/audio')


	conf.write_config_header('config.h')



def build(bld):
	bld.recurse('src/common')
	if not bld.env['BUILD_FOR_ANDROID']:
		bld.recurse('src/g2d')
	bld.recurse('src/pxp')
	bld.recurse('src/ipu')
	bld.recurse('src/vpu')
	bld.recurse('src/eglvivsink')
	if not bld.env['BUILD_FOR_ANDROID']:
		bld.recurse('src/v4l2src')
	bld.recurse('src/audio')
	bld.recurse('src/blitter')
	bld.recurse('src/compositor')

