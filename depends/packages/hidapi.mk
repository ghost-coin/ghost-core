package=hidapi
$(package)_version=0.12.0
$(package)_download_path=https://github.com/libusb/hidapi/archive/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=28ec1451f0527ad40c1a4c92547966ffef96813528c8b184a665f03ecbb508bc
$(package)_linux_dependencies=libusb

define $(package)_set_vars
  $(package)_config_opts=--disable-shared
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); ./bootstrap
endef

define $(package)_config_cmds
 $($(package)_autoconf)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
