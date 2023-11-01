package=native_protobuf
$(package)_version=3.21.7
$(package)_download_path=https://github.com/google/protobuf/releases/download/v21.7
$(package)_file_name=protobuf-cpp-$($(package)_version).tar.gz
$(package)_sha256_hash=70de993af0b4f2ddacce59e62ba6d7b7e48faf48beb1b0d5f1ac0e1fb0a68423

define $(package)_set_vars
$(package)_config_opts=--disable-shared --without-zlib
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C src protoc
endef

define $(package)_stage_cmds
  $(MAKE) -C src DESTDIR=$($(package)_staging_dir) install-strip
endef

define $(package)_postprocess_cmds
  rm -rf lib include
endef
