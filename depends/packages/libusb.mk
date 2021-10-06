package=libusb
$(package)_version=1.0.24
$(package)_download_path=https://github.com/libusb/libusb/archive/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=b7724c272dfc5713dce88ff717efd60f021ca5b7c8e30f08ebb2c42d2eea08ae
$(package)_linux_dependencies=eudev

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --enable-udev
  $(package)_config_opts_linux=--with-pic
endef

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); ./bootstrap.sh
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
