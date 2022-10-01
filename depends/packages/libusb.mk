package=libusb
$(package)_version=1.0.26
$(package)_download_path=https://github.com/libusb/libusb/releases/download/v$($(package)_version)
$(package)_file_name=libusb-$($(package)_version).tar.bz2
$(package)_sha256_hash=12ce7a61fc9854d1d2a1ffe095f7b5fac19ddba095c259e6067a46500381b5a5
$(package)_linux_dependencies=eudev

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --enable-udev
  $(package)_config_opts_linux=--with-pic
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
