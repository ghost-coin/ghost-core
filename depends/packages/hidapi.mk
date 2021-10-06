package=hidapi
$(package)_version=0.11.0
$(package)_download_path=https://github.com/libusb/hidapi/archive/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=391d8e52f2d6a5cf76e2b0c079cfefe25497ba1d4659131297081fc0cd744632
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
