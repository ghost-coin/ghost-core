package=eudev
$(package)_version=3.2.11
$(package)_download_path=https://github.com/eudev-project/eudev/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=19847cafec67897da855fde56f9dc7d92e21c50e450aa79068a7e704ed44558b

define $(package)_set_vars
  $(package)_config_opts=--disable-static --disable-manpages --disable-programs
  $(package)_config_opts_linux=--with-pic
  $(package)_cflags=-pipe
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
