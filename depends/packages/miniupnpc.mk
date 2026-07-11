package=miniupnpc
$(package)_version=2.2.8
$(package)_download_path=http://miniupnp.free.fr/files/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=05b929679091b9921b6b6c1f25e39e4c8d1f4d46c8feb55a412aa697aee03a93
# Next time this package is updated, ensure that _WIN32_WINNT is still properly set.
# See discussion in https://github.com/bitcoin/bitcoin/pull/25964.

# 2.2.8: the main Makefile builds into build/, but Makefile.mingw (used for
# Windows) still uses the flat layout. Select the correct lib path per host.
ifeq ($(host_os),mingw32)
$(package)_lib_path=libminiupnpc.a
else
$(package)_lib_path=build/libminiupnpc.a
endif

define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
$(package)_build_opts_darwin=LIBTOOL="$($(package)_libtool)"
$(package)_build_opts_mingw32=-f Makefile.mingw CFLAGS="$($(package)_cflags) -D_WIN32_WINNT=0x0601"
$(package)_build_env+=CFLAGS="$($(package)_cflags) $($(package)_cppflags)" AR="$($(package)_ar)"
endef
define $(package)_build_cmds
	$(MAKE) $($(package)_lib_path) $($(package)_build_opts)
endef
define $(package)_stage_cmds
	mkdir -p $($(package)_staging_prefix_dir)/include/miniupnpc $($(package)_staging_prefix_dir)/lib &&\
	install include/*.h $($(package)_staging_prefix_dir)/include/miniupnpc &&\
	install $($(package)_lib_path) $($(package)_staging_prefix_dir)/lib
endef
