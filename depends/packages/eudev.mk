package=eudev
$(package)_version=3.2.10
$(package)_download_path=https://github.com/eudev-project/eudev/archive/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=6492629da4024d2d21bb1a79d724e013d4152956099a5c63b09c8ee4da7f9b2b

define $(package)_set_vars
  $(package)_config_opts=--disable-static --disable-manpages --disable-programs
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); autoreconf -f -i -s
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
