package=boost
$(package)_version=1.80.0
$(package)_download_path=https://archives.boost.io/release/$($(package)_version)/source
$(package)_file_name=boost_1_80_0.tar.bz2
$(package)_sha256_hash=1e19565d82e43bc59209a168f5ac899d3ba471d55c7610c677d4ccf2c9c500c0
$(package)_dependencies=native_b2

define $(package)_set_vars
$(package)_config_opts_release=variant=release
$(package)_config_opts_debug=variant=debug
$(package)_config_opts=--layout=tagged --build-type=complete --user-config=user-config.jam
$(package)_config_opts+=threading=multi link=static -sNO_COMPRESSION=1
$(package)_config_opts_linux=target-os=linux threadapi=pthread runtime-link=shared
$(package)_config_opts_darwin=target-os=darwin runtime-link=shared
$(package)_config_opts_mingw32=target-os=windows binary-format=pe threadapi=win32 runtime-link=static
$(package)_config_opts_x86_64=architecture=x86 address-model=64
$(package)_config_opts_i686=architecture=x86 address-model=32
$(package)_config_opts_aarch64=address-model=64
$(package)_config_opts_armv7a=address-model=32

unary_function=unary_function
ifneq (,$(findstring clang,$($(package)_cxx)))
$(package)_toolset_$(host_os)=clang
ifeq ($(build_os),darwin)
unary_function=__unary_function
endif
else
$(package)_toolset_$(host_os)=gcc
endif

$(package)_config_libraries=filesystem,test,system,thread,program_options
$(package)_cxxflags=-std=c++17 -fvisibility=hidden
$(package)_cxxflags_linux=-fPIC
$(package)_cxxflags_freebsd=-fPIC
$(package)_cxxflags_openbsd=-fPIC
$(package)_cxxflags_android=-fPIC
$(package)_cxxflags_x86_64=-fcf-protection=full
endef

define $(package)_preprocess_cmds
  sed -i.old "s/unary_function/$(unary_function)/" boost/container_hash/hash.hpp && \
  \
  if [ "$(host_os)" = "mingw32" ]; then \
    echo "using gcc : mingw32 : $($(package)_cxx) : <rc>$(host_WINDRES) <archiver>$(host_AR) <ranlib>$(host_RANLIB) <cflags>\"$($(package)_cflags)\" <cxxflags>\"$($(package)_cxxflags)\" <compileflags>\"$($(package)_cppflags)\" ;" > user-config.jam; \
  elif [ "$(host_os)" = "darwin" ]; then \
    echo "using clang : darwin : $($(package)_cxx) : <archiver>$(host_AR) <ranlib>$(host_RANLIB) <cflags>\"$($(package)_cflags)\" <cxxflags>\"$($(package)_cxxflags)\" <compileflags>\"$($(package)_cppflags)\" ;" > user-config.jam; \
  else \
    echo "using gcc : : $($(package)_cxx) : <archiver>$(host_AR) <ranlib>$(host_RANLIB) <cflags>\"$($(package)_cflags)\" <cxxflags>\"$($(package)_cxxflags)\" <compileflags>\"$($(package)_cppflags)\" ;" > user-config.jam; \
  fi
endef

define $(package)_config_cmds
  ./bootstrap.sh --without-icu --with-libraries=$($(package)_config_libraries) --with-toolset=$($(package)_toolset_$(host_os)) --with-bjam=b2
endef

define $(package)_build_cmds
  b2 -d2 -j2 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) stage
endef

define $(package)_stage_cmds
  b2 -d0 -j4 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) install
endef
