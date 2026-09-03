package=gmp
$(package)_version=6.3.0
$(package)_download_path=https://ftp.gnu.org/gnu/gmp
$(package)_file_name=gmp-$($(package)_version).tar.gz
$(package)_sha256_hash=e56fd59d76810932a0555aa15a14b61c16bed66110d3c75cc2ac49ddaa9ab24c
$(package)_patches=include_ldflags_in_configure.patch

define $(package)_set_vars
$(package)_config_opts+=--enable-cxx --enable-fat --with-pic --disable-shared
$(package)_cflags_armv7l_linux+=-march=armv7-a
$(package)_cflags_aarch64_darwin+=-march=armv8-a
# ld64.lld (both 18 and 19) rejects gmp's hand-written x86_64 assembly with "BRANCH
# relocation has width 1 bytes, but must be 4 bytes" in add_n.o / sub_n.o. cctools' ld64
# accepted it, but the macOS toolchain now uses lld. Only Intel macOS is affected --
# arm64 assembly links fine -- so drop to the C implementation there alone.
$(package)_config_opts_x86_64_darwin+=--disable-fat --disable-assembly
endef

# gmp's configure runs its compiler test without LDFLAGS, so on a cross-compiled darwin build
# -fuse-ld=lld is absent and clang falls back to GNU ld, which cannot link Mach-O.
define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/include_ldflags_in_configure.patch
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm lib/*.la
endef
