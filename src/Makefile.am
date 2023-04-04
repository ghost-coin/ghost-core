# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Pattern rule to print variables, e.g. make print-top_srcdir
print-%: FORCE
	@echo '$*'='$($*)'

DIST_SUBDIRS = secp256k1

AM_LDFLAGS = $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) $(GPROF_LDFLAGS) $(SANITIZER_LDFLAGS) $(LTO_LDFLAGS) $(CORE_LDFLAGS)
AM_CXXFLAGS = $(DEBUG_CXXFLAGS) $(HARDENED_CXXFLAGS) $(WARN_CXXFLAGS) $(NOWARN_CXXFLAGS) $(ERROR_CXXFLAGS) $(GPROF_CXXFLAGS) $(SANITIZER_CXXFLAGS) $(LTO_CXXFLAGS) $(CORE_CXXFLAGS)
AM_CPPFLAGS = $(DEBUG_CPPFLAGS) $(HARDENED_CPPFLAGS) $(CORE_CPPFLAGS)
AM_CFLAGS = $(AM_CXXFLAGS)
AM_LIBTOOLFLAGS = --preserve-dup-deps
PTHREAD_FLAGS = $(PTHREAD_CFLAGS) $(PTHREAD_LIBS)
EXTRA_LIBRARIES =

lib_LTLIBRARIES =
noinst_LTLIBRARIES =

bin_PROGRAMS =
noinst_PROGRAMS =
check_PROGRAMS =
TESTS =
BENCHMARKS =

BITCOIN_INCLUDES=-I$(builddir) -I$(srcdir)/$(MINISKETCH_INCLUDE_DIR_INT) -I$(srcdir)/secp256k1/include -I$(srcdir)/$(UNIVALUE_INCLUDE_DIR_INT) $(LEVELDB_CPPFLAGS)

LIBPARTICL_NODE=libparticl_node.a
LIBPARTICL_COMMON=libparticl_common.a
LIBPARTICL_CONSENSUS=libparticl_consensus.a
LIBPARTICL_CLI=libparticl_cli.a
LIBPARTICL_UTIL=libparticl_util.a
LIBPARTICL_CRYPTO_BASE=crypto/libparticl_crypto_base.la
LIBPARTICLQT=qt/libparticlqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la
LIBPARTICL_SMSG=libparticl_smsg.a
LIBPARTICL_MNEMONIC=libparticl_mnemonic.a

if ENABLE_ZMQ
LIBPARTICL_ZMQ=libparticl_zmq.a
endif
if BUILD_BITCOIN_LIBS
LIBPARTICLCONSENSUS=libparticlconsensus.la
endif
if BUILD_BITCOIN_KERNEL_LIB
LIBPARTICLKERNEL=libparticlkernel.la
endif
if ENABLE_WALLET
LIBPARTICL_WALLET=libparticl_wallet.a
LIBPARTICL_WALLET_TOOL=libparticl_wallet_tool.a
endif
if ENABLE_USBDEVICE
LIBPARTICL_USBDEVICE=libparticl_usbdevice.a
endif

LIBPARTICL_CRYPTO = $(LIBPARTICL_CRYPTO_BASE)
if ENABLE_SSE41
LIBPARTICL_CRYPTO_SSE41 = crypto/libparticl_crypto_sse41.la
LIBPARTICL_CRYPTO += $(LIBPARTICL_CRYPTO_SSE41)
endif
if ENABLE_AVX2
LIBPARTICL_CRYPTO_AVX2 = crypto/libparticl_crypto_avx2.la
LIBPARTICL_CRYPTO += $(LIBPARTICL_CRYPTO_AVX2)
endif
if ENABLE_X86_SHANI
LIBPARTICL_CRYPTO_X86_SHANI = crypto/libparticl_crypto_x86_shani.la
LIBPARTICL_CRYPTO += $(LIBPARTICL_CRYPTO_X86_SHANI)
endif
if ENABLE_ARM_SHANI
LIBPARTICL_CRYPTO_ARM_SHANI = crypto/libparticl_crypto_arm_shani.la
LIBPARTICL_CRYPTO += $(LIBPARTICL_CRYPTO_ARM_SHANI)
endif
noinst_LTLIBRARIES += $(LIBPARTICL_CRYPTO)

$(LIBSECP256K1): $(wildcard secp256k1/src/*.h) $(wildcard secp256k1/src/*.c) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBPARTICL_UTIL) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_CONSENSUS) \
  $(LIBPARTICL_NODE) \
  $(LIBPARTICL_CLI) \
  $(LIBPARTICL_IPC) \
  $(LIBPARTICL_WALLET) \
  $(LIBPARTICL_WALLET_TOOL) \
  $(LIBPARTICL_USBDEVICE) \
  $(LIBPARTICL_SMSG) \
  $(LIBPARTICL_MNEMONIC) \
  $(LIBPARTICL_ZMQ)

if BUILD_BITCOIND
  bin_PROGRAMS += particld
endif

if BUILD_BITCOIN_NODE
  bin_PROGRAMS += particl-node
endif

if BUILD_BITCOIN_CLI
  bin_PROGRAMS += particl-cli
endif

if BUILD_BITCOIN_TX
  bin_PROGRAMS += particl-tx
endif

if ENABLE_WALLET
if BUILD_BITCOIN_WALLET
  bin_PROGRAMS += particl-wallet
endif
endif

if BUILD_BITCOIN_UTIL
  bin_PROGRAMS += particl-util
endif

if BUILD_BITCOIN_CHAINSTATE
  bin_PROGRAMS += particl-chainstate
endif

.PHONY: FORCE check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
  addrdb.h \
  rctindex.h \
  addrman.h \
  addrman_impl.h \
  attributes.h \
  banman.h \
  base58.h \
  bech32.h \
  blockencodings.h \
  blockfilter.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  chain/chainparamsimport.h \
  chain/ct_tainted.h \
  chain/tx_blacklist.h \
  chain/tx_whitelist.h \
  checkqueue.h \
  clientversion.h \
  coins.h \
  common/bloom.h \
  common/init.h \
  common/run_command.h \
  common/url.h \
  compat/assumptions.h \
  compat/byteswap.h \
  compat/compat.h \
  compat/cpuid.h \
  compat/endian.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_check.h \
  consensus/tx_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  dbwrapper.h \
  deploymentinfo.h \
  deploymentstatus.h \
  external_signer.h \
  flatfile.h \
  headerssync.h \
  httprpc.h \
  httpserver.h \
  i2p.h \
  index/base.h \
  index/blockfilterindex.h \
  index/coinstatsindex.h \
  index/disktxpos.h \
  index/txindex.h \
  indirectmap.h \
  init.h \
  anon.h \
  blind.h \
  init/common.h \
  interfaces/chain.h \
  interfaces/echo.h \
  interfaces/handler.h \
  interfaces/init.h \
  interfaces/ipc.h \
  interfaces/node.h \
  interfaces/wallet.h \
  kernel/blockmanager_opts.h \
  kernel/chain.h \
  kernel/chainparams.h \
  kernel/chainstatemanager_opts.h \
  kernel/checks.h \
  kernel/coinstats.h \
  kernel/context.h \
  kernel/cs_main.h \
  kernel/mempool_entry.h \
  kernel/mempool_limits.h \
  kernel/mempool_options.h \
  kernel/mempool_persist.h \
  kernel/validation_cache_sizes.h \
  key.h \
  key/stealth.h \
  key/extkey.h \
  key/types.h \
  key/keyutil.h \
  key/crypter.h \
  key_io.h \
  logging.h \
  logging/timer.h \
  mapport.h \
  memusage.h \
  merkleblock.h \
  net.h \
  net_permissions.h \
  net_processing.h \
  net_types.h \
  netaddress.h \
  netbase.h \
  netgroup.h \
  netmessagemaker.h \
  node/blockmanager_args.h \
  node/blockstorage.h \
  node/caches.h \
  node/chainstate.h \
  node/chainstatemanager_args.h \
  node/coin.h \
  node/coins_view_args.h \
  node/connection_types.h \
  node/context.h \
  node/database_args.h \
  node/eviction.h \
  node/interface_ui.h \
  node/mempool_args.h \
  node/mempool_persist_args.h \
  node/miner.h \
  node/minisketchwrapper.h \
  node/psbt.h \
  node/transaction.h \
  node/txreconciliation.h \
  node/utxo_snapshot.h \
  node/validation_cache_args.h \
  noui.h \
  outputtype.h \
  policy/feerate.h \
  policy/fees.h \
  policy/fees_args.h \
  policy/packages.h \
  policy/policy.h \
  policy/rbf.h \
  policy/settings.h \
  pow.h \
  pos/kernel.h \
  pos/miner.h \
  protocol.h \
  psbt.h \
  random.h \
  randomenv.h \
  rest.h \
  reverse_iterator.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/mempool.h \
  rpc/mining.h \
  rpc/protocol.h \
  rpc/rawtransaction_util.h \
  rpc/register.h \
  rpc/request.h \
  rpc/server.h \
  rpc/server_util.h \
  rpc/util.h \
  rpc/rpcutil.h \
  scheduler.h \
  script/descriptor.h \
  script/keyorigin.h \
  script/miniscript.h \
  script/sigcache.h \
  script/sign.h \
  script/signingprovider.h \
  script/standard.h \
  script/ismine.h \
  shutdown.h \
  signet.h \
  streams.h \
  smsg/db.h \
  smsg/net.h \
  smsg/types.h \
  smsg/crypter.h \
  smsg/smessage.h \
  smsg/manager.h \
  smsg/rpcsmessage.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  txorphanage.h \
  txrequest.h \
  undo.h \
  util/asmap.h \
  util/bip32.h \
  util/bitdeque.h \
  util/bytevectorhash.h \
  util/check.h \
  util/epochguard.h \
  util/error.h \
  util/exception.h \
  util/fastrange.h \
  util/fees.h \
  util/fs.h \
  util/fs_helpers.h \
  util/getuniquepath.h \
  util/golombrice.h \
  util/hash_type.h \
  util/hasher.h \
  util/macros.h \
  util/message.h \
  util/moneystr.h \
  util/overflow.h \
  util/overloaded.h \
  util/rbf.h \
  util/readwritefile.h \
  util/result.h \
  util/serfloat.h \
  util/settings.h \
  util/sock.h \
  util/spanparsing.h \
  util/string.h \
  util/syscall_sandbox.h \
  util/syserror.h \
  util/system.h \
  util/thread.h \
  util/threadinterrupt.h \
  util/threadnames.h \
  util/time.h \
  util/tokenpipe.h \
  util/trace.h \
  util/translation.h \
  util/types.h \
  util/ui_change_type.h \
  util/vector.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  wallet/bdb.h \
  wallet/coincontrol.h \
  wallet/coinselection.h \
  wallet/context.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/dump.h \
  wallet/external_signer_scriptpubkeyman.h \
  wallet/feebumper.h \
  wallet/fees.h \
  wallet/ismine.h \
  wallet/load.h \
  wallet/receive.h \
  wallet/rpc/util.h \
  wallet/rpc/wallet.h \
  wallet/salvage.h \
  wallet/scriptpubkeyman.h \
  wallet/spend.h \
  wallet/sqlite.h \
  wallet/transaction.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  wallet/wallettool.h \
  wallet/walletutil.h \
  walletinitinterface.h \
  wallet/hdwalletdb.h \
  wallet/hdwallettypes.h \
  wallet/hdwallet.h \
  wallet/rpchdwallet.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  zmq/zmqrpc.h \
  zmq/zmqutil.h \
  usbdevice/usbdevice.h \
  usbdevice/rpcusbdevice.h \
  insight/addressindex.h \
  insight/spentindex.h \
  insight/timestampindex.h \
  insight/balanceindex.h \
  insight/csindex.h \
  insight/insight.h \
  insight/rpc.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libparticl_util_a-clientversion.$(OBJEXT): obj/build.h

# node #
libparticl_node_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(BOOST_CPPFLAGS) $(MINIUPNPC_CPPFLAGS) $(NATPMP_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libparticl_node_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_node_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  banman.cpp \
  blockencodings.cpp \
  blockfilter.cpp \
  chain.cpp \
  consensus/tx_verify.cpp \
  dbwrapper.cpp \
  deploymentstatus.cpp \
  flatfile.cpp \
  headerssync.cpp \
  httprpc.cpp \
  httpserver.cpp \
  i2p.cpp \
  index/base.cpp \
  index/blockfilterindex.cpp \
  index/coinstatsindex.cpp \
  index/txindex.cpp \
  init.cpp \
  kernel/chain.cpp \
  kernel/checks.cpp \
  kernel/coinstats.cpp \
  kernel/context.cpp \
  kernel/cs_main.cpp \
  kernel/mempool_persist.cpp \
  mapport.cpp \
  net.cpp \
  net_processing.cpp \
  netgroup.cpp \
  node/blockmanager_args.cpp \
  node/blockstorage.cpp \
  node/caches.cpp \
  node/chainstate.cpp \
  node/chainstatemanager_args.cpp \
  node/coin.cpp \
  node/coins_view_args.cpp \
  node/connection_types.cpp \
  node/context.cpp \
  node/database_args.cpp \
  node/eviction.cpp \
  node/interface_ui.cpp \
  node/interfaces.cpp \
  node/mempool_args.cpp \
  node/mempool_persist_args.cpp \
  node/miner.cpp \
  node/minisketchwrapper.cpp \
  node/psbt.cpp \
  node/transaction.cpp \
  node/txreconciliation.cpp \
  node/utxo_snapshot.cpp \
  node/validation_cache_args.cpp \
  noui.cpp \
  policy/fees.cpp \
  policy/fees_args.cpp \
  policy/packages.cpp \
  policy/rbf.cpp \
  policy/settings.cpp \
  pow.cpp \
  pos/kernel.cpp \
  rest.cpp \
  rpc/anon.cpp \
  rpc/blockchain.cpp \
  rpc/fees.cpp \
  rpc/mempool.cpp \
  rpc/mining.cpp \
  rpc/net.cpp \
  rpc/node.cpp \
  rpc/output_script.cpp \
  rpc/rawtransaction.cpp \
  rpc/server.cpp \
  rpc/client.cpp \
  rpc/rpcutil.cpp \
  rpc/util.cpp \
  rpc/server_util.cpp \
  rpc/signmessage.cpp \
  rpc/txoutproof.cpp \
  script/sigcache.cpp \
  shutdown.cpp \
  signet.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  txorphanage.cpp \
  txrequest.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  insight/insight.cpp \
  insight/rpc.cpp \
  $(BITCOIN_CORE_H)

if ENABLE_WALLET
libparticl_node_a_SOURCES += wallet/init.cpp
libparticl_node_a_CPPFLAGS += $(BDB_CPPFLAGS)
endif
if !ENABLE_WALLET
libparticl_node_a_SOURCES += dummywallet.cpp
endif
#

# zmq #
if ENABLE_ZMQ
libparticl_zmq_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libparticl_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp \
  zmq/zmqrpc.cpp \
  zmq/zmqutil.cpp
endif
#

# usbdevice #
if ENABLE_USBDEVICE
libparticl_usbdevice_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(AM_CPPFLAGS) $(PROTOBUF_CFLAGS)
libparticl_usbdevice_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_usbdevice_a_CFLAGS = -DHAVE_HIDAPI $(AM_CFLAGS) $(PIE_FLAGS)
libparticl_usbdevice_a_SOURCES = \
  usbdevice/usbwrapper.cpp \
  usbdevice/usbwrapper.h \
  usbdevice/ledger/btchipApdu.h \
  usbdevice/ledger/ledgerLayer.h \
  usbdevice/ledger/ledgerLayer.c \
  usbdevice/ledger/dongleCommHidHidapi.h \
  usbdevice/ledger/dongleCommHidHidapi.c \
  usbdevice/usbdevice.cpp \
  usbdevice/debugdevice.h \
  usbdevice/debugdevice.cpp \
  usbdevice/ledgerdevice.h \
  usbdevice/ledgerdevice.cpp \
  usbdevice/trezor/messages.proto \
  usbdevice/trezor/messages-common.proto \
  usbdevice/trezor/messages-management.proto \
  usbdevice/trezor/messages-bitcoin.proto \
  usbdevice/trezordevice.h \
  usbdevice/trezordevice.cpp \
  usbdevice/rpcusbdevice.cpp
nodist_libparticl_usbdevice_a_SOURCES = \
  usbdevice/trezor/messages.pb.h \
  usbdevice/trezor/messages.pb.cc \
  usbdevice/trezor/messages-common.pb.h \
  usbdevice/trezor/messages-common.pb.cc \
  usbdevice/trezor/messages-management.pb.h \
  usbdevice/trezor/messages-management.pb.cc \
  usbdevice/trezor/messages-bitcoin.pb.h \
  usbdevice/trezor/messages-bitcoin.pb.cc
BUILT_SOURCES = $(nodist_libparticl_usbdevice_a_SOURCES)
endif
#

# wallet #
libparticl_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(BOOST_CPPFLAGS) $(BDB_CPPFLAGS) $(SQLITE_CFLAGS)
libparticl_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_wallet_a_SOURCES = \
  wallet/rpchdwallet.cpp \
  wallet/hdwallet.cpp \
  wallet/hdwallettypes.cpp \
  wallet/hdwalletdb.cpp \
  wallet/coincontrol.cpp \
  wallet/context.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/dump.cpp \
  wallet/external_signer_scriptpubkeyman.cpp \
  wallet/feebumper.cpp \
  wallet/fees.cpp \
  wallet/interfaces.cpp \
  wallet/load.cpp \
  wallet/receive.cpp \
  wallet/rpc/addresses.cpp \
  wallet/rpc/backup.cpp \
  wallet/rpc/coins.cpp \
  wallet/rpc/encrypt.cpp \
  wallet/rpc/spend.cpp \
  wallet/rpc/signmessage.cpp \
  wallet/rpc/transactions.cpp \
  wallet/rpc/util.cpp \
  wallet/rpc/wallet.cpp \
  wallet/scriptpubkeyman.cpp \
  wallet/spend.cpp \
  wallet/transaction.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  blind.cpp \
  pos/kernel.cpp \
  pos/miner.cpp \
  policy/rbf.cpp \
  wallet/walletutil.cpp \
  wallet/coinselection.cpp \
  $(BITCOIN_CORE_H)

if USE_SQLITE
libparticl_wallet_a_SOURCES += wallet/sqlite.cpp
endif
if USE_BDB
libparticl_wallet_a_SOURCES += wallet/bdb.cpp wallet/salvage.cpp
endif
#

# wallet tool #
libparticl_wallet_tool_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(BOOST_CPPFLAGS)
libparticl_wallet_tool_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_wallet_tool_a_SOURCES = \
  wallet/wallettool.cpp \
  $(BITCOIN_CORE_H)
#

# crypto #
crypto_libparticl_crypto_base_la_CPPFLAGS = $(AM_CPPFLAGS)

# Specify -static in both CXXFLAGS and LDFLAGS so libtool will only build a
# static version of this library. We don't need a dynamic version, and a dynamic
# version can't be used on windows anyway because the library doesn't currently
# export DLL symbols.
crypto_libparticl_crypto_base_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -static
crypto_libparticl_crypto_base_la_LDFLAGS = $(AM_LDFLAGS) -static

crypto_libparticl_crypto_base_la_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha_poly_aead.h \
  crypto/chacha_poly_aead.cpp \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hkdf_sha256_32.cpp \
  crypto/hkdf_sha256_32.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/poly1305.h \
  crypto/poly1305.cpp \
  crypto/muhash.h \
  crypto/muhash.cpp \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha3.cpp \
  crypto/sha3.h \
  crypto/sha512.cpp \
  crypto/sha512.h \
  crypto/siphash.cpp \
  crypto/siphash.h

if USE_ASM
crypto_libparticl_crypto_base_la_SOURCES += crypto/sha256_sse4.cpp
endif

# See explanation for -static in crypto_libparticl_crypto_base_la's LDFLAGS and
# CXXFLAGS above
crypto_libparticl_crypto_sse41_la_LDFLAGS = $(AM_LDFLAGS) -static
crypto_libparticl_crypto_sse41_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -static
crypto_libparticl_crypto_sse41_la_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libparticl_crypto_sse41_la_CXXFLAGS += $(SSE41_CXXFLAGS)
crypto_libparticl_crypto_sse41_la_CPPFLAGS += -DENABLE_SSE41
crypto_libparticl_crypto_sse41_la_SOURCES = crypto/sha256_sse41.cpp

# See explanation for -static in crypto_libparticl_crypto_base_la's LDFLAGS and
# CXXFLAGS above
crypto_libparticl_crypto_avx2_la_LDFLAGS = $(AM_LDFLAGS) -static
crypto_libparticl_crypto_avx2_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -static
crypto_libparticl_crypto_avx2_la_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libparticl_crypto_avx2_la_CXXFLAGS += $(AVX2_CXXFLAGS)
crypto_libparticl_crypto_avx2_la_CPPFLAGS += -DENABLE_AVX2
crypto_libparticl_crypto_avx2_la_SOURCES = crypto/sha256_avx2.cpp

# See explanation for -static in crypto_libparticl_crypto_base_la's LDFLAGS and
# CXXFLAGS above
crypto_libparticl_crypto_x86_shani_la_LDFLAGS = $(AM_LDFLAGS) -static
crypto_libparticl_crypto_x86_shani_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -static
crypto_libparticl_crypto_x86_shani_la_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libparticl_crypto_x86_shani_la_CXXFLAGS += $(X86_SHANI_CXXFLAGS)
crypto_libparticl_crypto_x86_shani_la_CPPFLAGS += -DENABLE_X86_SHANI
crypto_libparticl_crypto_x86_shani_la_SOURCES = crypto/sha256_x86_shani.cpp

# See explanation for -static in crypto_libparticl_crypto_base_la's LDFLAGS and
# CXXFLAGS above
crypto_libparticl_crypto_arm_shani_la_LDFLAGS = $(AM_LDFLAGS) -static
crypto_libparticl_crypto_arm_shani_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -static
crypto_libparticl_crypto_arm_shani_la_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libparticl_crypto_arm_shani_la_CXXFLAGS += $(ARM_SHANI_CXXFLAGS)
crypto_libparticl_crypto_arm_shani_la_CPPFLAGS += -DENABLE_ARM_SHANI
crypto_libparticl_crypto_arm_shani_la_SOURCES = crypto/sha256_arm_shani.cpp
#

# consensus #
libparticl_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_consensus_a_SOURCES = \
  arith_uint256.cpp \
  arith_uint256.h \
  consensus/amount.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/tx_check.cpp \
  consensus/validation.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/bitcoinconsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  span.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  util/strencodings.cpp \
  util/strencodings.h \
  version.h
#

# common #
libparticl_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(BOOST_CPPFLAGS)
libparticl_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_common_a_SOURCES = \
  base58.cpp \
  bech32.cpp \
  chainparams.cpp \
  coins.cpp \
  common/bloom.cpp \
  common/init.cpp \
  common/interfaces.cpp \
  common/run_command.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  deploymentinfo.cpp \
  external_signer.cpp \
  init/common.cpp \
  kernel/chainparams.cpp \
  key.cpp \
  anon.cpp \
  blind.cpp \
  key_io.cpp \
  key/crypter.cpp \
  key/keyutil.cpp \
  key/extkey.cpp \
  key/stealth.cpp \
  merkleblock.cpp \
  net_types.cpp \
  netaddress.cpp \
  netbase.cpp \
  net_permissions.cpp \
  outputtype.cpp \
  policy/feerate.cpp \
  policy/policy.cpp \
  protocol.cpp \
  psbt.cpp \
  rpc/external_signer.cpp \
  rpc/rawtransaction_util.cpp \
  rpc/request.cpp \
  rpc/util.cpp \
  scheduler.cpp \
  script/descriptor.cpp \
  script/miniscript.cpp \
  script/sign.cpp \
  script/signingprovider.cpp \
  script/standard.cpp \
  warnings.cpp \
  $(BITCOIN_CORE_H)

if USE_LIBEVENT
libparticl_common_a_CPPFLAGS += $(EVENT_CFLAGS)
libparticl_common_a_SOURCES += common/url.cpp
endif
#

# util #
libparticl_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  logging.cpp \
  random.cpp \
  randomenv.cpp \
  support/cleanse.cpp \
  sync.cpp \
  util/asmap.cpp \
  util/bip32.cpp \
  util/bytevectorhash.cpp \
  util/check.cpp \
  util/error.cpp \
  util/exception.cpp \
  util/fees.cpp \
  util/fs.cpp \
  util/fs_helpers.cpp \
  util/getuniquepath.cpp \
  util/hasher.cpp \
  util/sock.cpp \
  util/syserror.cpp \
  util/system.cpp \
  util/message.cpp \
  util/moneystr.cpp \
  util/rbf.cpp \
  util/readwritefile.cpp \
  util/settings.cpp \
  util/thread.cpp \
  util/threadinterrupt.cpp \
  util/threadnames.cpp \
  util/serfloat.cpp \
  util/spanparsing.cpp \
  util/strencodings.cpp \
  util/string.cpp \
  util/syscall_sandbox.cpp \
  util/time.cpp \
  util/tokenpipe.cpp \
  $(BITCOIN_CORE_H)
#

# smsg #
libparticl_smsg_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_smsg_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_smsg_a_CFLAGS = $(AM_CFLAGS) $(PIE_FLAGS)
libparticl_smsg_a_SOURCES = \
  lz4/lz4.h \
  lz4/lz4.c \
  xxhash/xxhash.h \
  xxhash/xxhash.c \
  common/interfaces.cpp \
  smsg/crypter.cpp \
  smsg/keystore.h \
  smsg/keystore.cpp \
  smsg/db.cpp \
  smsg/smessage.cpp \
  smsg/manager.cpp \
  smsg/rpcsmessage.cpp
#

# mnemonic #
libparticl_mnemonic_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_mnemonic_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_mnemonic_a_CFLAGS = $(AM_CFLAGS) $(PIE_FLAGS)
libparticl_mnemonic_a_SOURCES = \
  unilib/uninorms.h \
  unilib/utf8.h \
  unilib/uninorms.cpp \
  unilib/utf8.cpp \
  key/wordlists/chinese_simplified.h \
  key/wordlists/chinese_traditional.h \
  key/wordlists/english.h \
  key/wordlists/french.h \
  key/wordlists/japanese.h \
  key/wordlists/spanish.h \
  key/wordlists/italian.h \
  key/wordlists/korean.h \
  key/wordlists/czech.h \
  key/mnemonic.h \
  key/mnemonic.cpp \
  rpc/mnemonic.cpp
#

# cli #
libparticl_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libparticl_cli_a_SOURCES = \
  compat/stdin.h \
  compat/stdin.cpp \
  rpc/client.cpp \
  $(BITCOIN_CORE_H)


nodist_libparticl_util_a_SOURCES = $(srcdir)/obj/build.h
#

# bitcoind & bitcoin-node binaries #
particl_daemon_sources = bitcoind.cpp
particl_bin_cppflags = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
particl_bin_cxxflags = $(AM_CXXFLAGS) $(PIE_FLAGS)
particl_bin_ldflags = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
particl_daemon_sources += bitcoind-res.rc
endif

particl_bin_ldadd = \
  $(LIBPARTICL_WALLET) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_UTIL) \
  $(LIBUNIVALUE) \
  $(LIBPARTICL_ZMQ) \
  $(LIBPARTICL_CONSENSUS) \
  $(LIBPARTICL_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  $(LIBPARTICL_SMSG) \
  $(LIBPARTICL_MNEMONIC) \
  $(LIBPARTICL_USBDEVICE)

particl_bin_ldadd += $(BDB_LIBS) $(MINIUPNPC_LIBS) $(NATPMP_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS) $(SQLITE_LIBS)
if ENABLE_USBDEVICE
particl_bin_ldadd += $(USB_LIBS) $(HIDAPI_LIBS) $(PROTOBUF_LIBS)
endif

particld_SOURCES = $(particl_daemon_sources) init/bitcoind.cpp
particld_CPPFLAGS = $(particl_bin_cppflags)
particld_CXXFLAGS = $(particl_bin_cxxflags)
particld_LDFLAGS = $(particl_bin_ldflags)
particld_LDADD = $(LIBPARTICL_NODE) $(particl_bin_ldadd)

particl_node_SOURCES = $(particl_daemon_sources) init/bitcoin-node.cpp
particl_node_CPPFLAGS = $(particl_bin_cppflags)
particl_node_CXXFLAGS = $(particl_bin_cxxflags)
particl_node_LDFLAGS = $(particl_bin_ldflags)
particl_node_LDADD = $(LIBPARTICL_NODE) $(particl_bin_ldadd) $(LIBPARTICL_IPC) $(LIBMULTIPROCESS_LIBS)

# bitcoin-cli binary #
particl_cli_SOURCES = bitcoin-cli.cpp
particl_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
particl_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
particl_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
particl_cli_SOURCES += bitcoin-cli-res.rc
endif

particl_cli_LDADD = \
  $(LIBPARTICL_CLI) \
  $(LIBUNIVALUE) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_UTIL) \
  $(LIBPARTICL_CRYPTO)

particl_cli_LDADD += $(EVENT_LIBS)
#

# bitcoin-tx binary #
particl_tx_SOURCES = bitcoin-tx.cpp
particl_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
particl_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
particl_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
particl_tx_SOURCES += bitcoin-tx-res.rc
endif

particl_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_UTIL) \
  $(LIBPARTICL_CONSENSUS) \
  $(LIBPARTICL_CRYPTO) \
  $(LIBSECP256K1) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42)

#

# bitcoin-wallet binary #
particl_wallet_SOURCES = bitcoin-wallet.cpp
particl_wallet_SOURCES += init/bitcoin-wallet.cpp
particl_wallet_CPPFLAGS = $(particl_bin_cppflags)
particl_wallet_CXXFLAGS = $(particl_bin_cxxflags)
particl_wallet_LDFLAGS = $(particl_bin_ldflags)
particl_wallet_LDADD = \
  $(LIBPARTICL_WALLET_TOOL) \
  $(LIBPARTICL_WALLET) \
  $(LIBPARTICL_SMSG) \
  $(LIBPARTICL_MNEMONIC) \
  $(LIBPARTICL_USBDEVICE) \
  $(LIBPARTICL_NODE) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_CONSENSUS) \
  $(LIBPARTICL_UTIL) \
  $(LIBPARTICL_CRYPTO) \
  $(LIBPARTICL_ZMQ) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  $(LIBUNIVALUE)
particl_wallet_LDADD += $(BDB_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(MINIUPNPC_LIBS) $(ZMQ_LIBS) $(SQLITE_LIBS)
if ENABLE_USBDEVICE
particl_wallet_LDADD += $(USB_LIBS) $(HIDAPI_LIBS) $(PROTOBUF_LIBS)
endif

if TARGET_WINDOWS
particl_wallet_SOURCES += bitcoin-wallet-res.rc
endif
#

# bitcoin-util binary #
particl_util_SOURCES = bitcoin-util.cpp
particl_util_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
particl_util_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
particl_util_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
particl_util_SOURCES += bitcoin-util-res.rc
endif

particl_util_LDADD = \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_UTIL) \
  $(LIBUNIVALUE) \
  $(LIBPARTICL_CONSENSUS) \
  $(LIBPARTICL_CRYPTO) \
  $(LIBSECP256K1)
#

# bitcoin-chainstate binary #
particl_chainstate_SOURCES = bitcoin-chainstate.cpp
particl_chainstate_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(BOOST_CPPFLAGS)
particl_chainstate_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

particl_chainstate_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(PTHREAD_FLAGS) $(LIBTOOL_APP_LDFLAGS) -static
particl_chainstate_LDADD = $(LIBPARTICLKERNEL) \
  $(LIBPARTICL_SMSG) \
  $(LIBPARTICL_NODE) \
  $(LIBPARTICL_COMMON) \
  $(LIBPARTICL_UTIL) \
  $(LIBPARTICL_CRYPTO) \
  $(LIBUNIVALUE) \
  $(LIBSECP256K1) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV)

# libtool is unable to calculate this indirect dependency, presumably because it's a subproject.
# libsecp256k1 only needs to be linked in when libbitcoinkernel is static.
#particl_chainstate_LDADD += $(LIBSECP256K1)
#

# bitcoinkernel library #
if BUILD_BITCOIN_KERNEL_LIB
lib_LTLIBRARIES += $(LIBPARTICLKERNEL)

libparticlkernel_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS) $(PTHREAD_FLAGS)
libparticlkernel_la_LIBADD = $(LIBPARTICL_CRYPTO) $(LIBUNIVALUE) $(LIBLEVELDB) $(LIBMEMENV) $(LIBSECP256K1)
libparticlkernel_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) -I$(srcdir)/$(UNIVALUE_INCLUDE_DIR_INT)

# libbitcoinkernel requires default symbol visibility, explicitly specify that
# here so that things still work even when user configures with
#   --enable-reduce-exports
#
# Note this is a quick hack that will be removed as we incrementally define what
# to export from the library.
libparticlkernel_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -fvisibility=default

# TODO: libbitcoinkernel is a work in progress consensus engine library, as more
#       and more modules are decoupled from the consensus engine, this list will
#       shrink to only those which are absolutely necessary.
libparticlkernel_la_SOURCES = \
  kernel/bitcoinkernel.cpp \
  arith_uint256.cpp \
  chain.cpp \
  chainparamsbase.cpp \
  chainparams.cpp \
  clientversion.cpp \
  coins.cpp \
  compressor.cpp \
  consensus/merkle.cpp \
  consensus/tx_check.cpp \
  consensus/tx_verify.cpp \
  core_read.cpp \
  dbwrapper.cpp \
  deploymentinfo.cpp \
  deploymentstatus.cpp \
  flatfile.cpp \
  hash.cpp \
  kernel/chain.cpp \
  kernel/checks.cpp \
  kernel/chainparams.cpp \
  kernel/coinstats.cpp \
  kernel/context.cpp \
  kernel/cs_main.cpp \
  kernel/mempool_persist.cpp \
  key.cpp \
  anon.cpp \
  blind.cpp \
  key_io.cpp \
  bech32.cpp \
  base58.cpp \
  common/bloom.cpp \
  node/transaction.cpp \
  index/txindex.cpp \
  pos/kernel.cpp \
  pos/miner.cpp \
  key/stealth.cpp \
  key/keyutil.cpp \
  key/extkey.cpp \
  key/crypter.cpp \
  logging.cpp \
  node/blockstorage.cpp \
  node/chainstate.cpp \
  node/interface_ui.cpp \
  node/utxo_snapshot.cpp \
  policy/feerate.cpp \
  policy/fees.cpp \
  policy/packages.cpp \
  policy/policy.cpp \
  policy/rbf.cpp \
  policy/settings.cpp \
  pow.cpp \
  primitives/block.cpp \
  primitives/transaction.cpp \
  pubkey.cpp \
  random.cpp \
  randomenv.cpp \
  scheduler.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  script/script_error.cpp \
  script/sigcache.cpp \
  script/standard.cpp \
  shutdown.cpp \
  signet.cpp \
  support/cleanse.cpp \
  support/lockedpool.cpp \
  sync.cpp \
  txdb.cpp \
  txmempool.cpp \
  uint256.cpp \
  util/check.cpp \
  util/exception.cpp \
  util/fs.cpp \
  util/fs_helpers.cpp \
  util/getuniquepath.cpp \
  util/hasher.cpp \
  util/moneystr.cpp \
  util/rbf.cpp \
  util/serfloat.cpp \
  util/settings.cpp \
  util/strencodings.cpp \
  util/string.cpp \
  util/syscall_sandbox.cpp \
  util/syserror.cpp \
  util/system.cpp \
  util/thread.cpp \
  util/threadnames.cpp \
  util/time.cpp \
  util/tokenpipe.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  warnings.cpp \
  insight/insight.cpp

# Required for obj/build.h to be generated first.
# More details: https://www.gnu.org/software/automake/manual/html_node/Built-Sources-Example.html
libparticlkernel_la-clientversion.l$(OBJEXT): obj/build.h
endif # BUILD_BITCOIN_KERNEL_LIB
#

# bitcoinconsensus library #
if BUILD_BITCOIN_LIBS
lib_LTLIBRARIES += $(LIBPARTICLCONSENSUS)

include_HEADERS = script/particlconsensus.h
libparticlconsensus_la_SOURCES = support/cleanse.cpp $(crypto_libparticl_crypto_base_la_SOURCES) $(libparticl_consensus_a_SOURCES)

libparticlconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libparticlconsensus_la_LIBADD = $(LIBSECP256K1)
libparticlconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL
libparticlconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crc32c/src/*.gcda crc32c/src/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += index/*.gcda index/*.gcno
CLEANFILES += interfaces/*.gcda interfaces/*.gcno
CLEANFILES += node/*.gcda node/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += rpc/*.gcda rpc/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += util/*.gcda util/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno
CLEANFILES += obj/build.h

EXTRA_DIST = $(CTAES_DIST)


config/bitcoin-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/bitcoin-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/bitcoin-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/bitcoin-config.h.in

clean-local:
	-$(MAKE) -C secp256k1 clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
	@echo "Running symbol and dynamic library checks..."
	$(AM_V_at) $(PYTHON) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) $(PYTHON) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

if ENABLE_USBDEVICE
%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<
endif

libparticl_ipc_mpgen_input = \
  ipc/capnp/echo.capnp \
  ipc/capnp/init.capnp
EXTRA_DIST += $(libparticl_ipc_mpgen_input)
%.capnp:

# Explicitly list dependencies on generated headers as described in
# https://www.gnu.org/software/automake/manual/html_node/Built-Sources-Example.html#Recording-Dependencies-manually
ipc/capnp/libparticl_ipc_a-protocol.$(OBJEXT): $(libparticl_ipc_mpgen_input:=.h)

if BUILD_MULTIPROCESS
LIBPARTICL_IPC=libparticl_ipc.a
libparticl_ipc_a_SOURCES = \
  ipc/capnp/context.h \
  ipc/capnp/init-types.h \
  ipc/capnp/protocol.cpp \
  ipc/capnp/protocol.h \
  ipc/context.h \
  ipc/exception.h \
  ipc/interfaces.cpp \
  ipc/process.cpp \
  ipc/process.h \
  ipc/protocol.h
libparticl_ipc_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libparticl_ipc_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(LIBMULTIPROCESS_CFLAGS)

include $(MPGEN_PREFIX)/include/mpgen.mk
libparticl_ipc_mpgen_output = \
  $(libparticl_ipc_mpgen_input:=.c++) \
  $(libparticl_ipc_mpgen_input:=.h) \
  $(libparticl_ipc_mpgen_input:=.proxy-client.c++) \
  $(libparticl_ipc_mpgen_input:=.proxy-server.c++) \
  $(libparticl_ipc_mpgen_input:=.proxy-types.c++) \
  $(libparticl_ipc_mpgen_input:=.proxy-types.h) \
  $(libparticl_ipc_mpgen_input:=.proxy.h)
nodist_libparticl_ipc_a_SOURCES = $(libparticl_ipc_mpgen_output)
CLEANFILES += $(libparticl_ipc_mpgen_output)
endif

%.raw.h: %.raw
	@$(MKDIR_P) $(@D)
	@{ \
	 echo "static unsigned const char $(*F)_raw[] = {" && \
	 $(HEXDUMP) -v -e '8/1 "0x%02x, "' -e '"\n"' $< | $(SED) -e 's/0x  ,//g' && \
	 echo "};"; \
	} > "$@.new" && mv -f "$@.new" "$@"
	@echo "Generated $@"

include Makefile.minisketch.include

include Makefile.crc32c.include
include Makefile.leveldb.include

include Makefile.test_util.include
include Makefile.test_fuzz.include

include Makefile.test.include

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif

include Makefile.univalue.include
