// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_CHAINSTATEMANAGER_OPTS_H
#define BITCOIN_KERNEL_CHAINSTATEMANAGER_OPTS_H

#include <arith_uint256.h>
#include <dbwrapper.h>
#include <txdb.h>
#include <uint256.h>
#include <util/time.h>

#include <cstdint>
#include <functional>
#include <optional>

class CChainParams;

static constexpr bool DEFAULT_CHECKPOINTS_ENABLED{true};
static constexpr auto DEFAULT_MAX_TIP_AGE{24h};

static const bool DEFAULT_CSINDEX = false;
static const bool DEFAULT_ADDRESSINDEX = false;
static const bool DEFAULT_TIMESTAMPINDEX = false;
static const bool DEFAULT_SPENTINDEX = false;
static const bool DEFAULT_BALANCESINDEX = false;
static const unsigned int DEFAULT_DB_MAX_OPEN_FILES = 64; // set to 1000 for insight
static const bool DEFAULT_DB_COMPRESSION = false; // set to true for insight
static const unsigned int DEFAULT_BANSCORE_THRESHOLD = 100;
static const bool DEFAULT_ACCEPT_ANON_TX = true;
static const bool DEFAULT_ACCEPT_BLIND_TX = true;
static const bool DEFAULT_ANON_RESTRICTED = true;
static const bool DEFAULT_ANON_RESTRICTION_START_HEIGHT = 0;
static const unsigned int DEFAULT_LAST_ANON_INDEX = 0;
static const unsigned int DEFAULT_FULL_RESTRICTION_HEIGHT = 0;
static const CAmount DEFAULT_GVR_THRESHOLD = 20000 * COIN;
static const int DEFAULT_MIN_REWARD_RANGE_SPAN = 30 * 24 * 30; // Which is 21600 blocks per month
static const int DEFAULT_GVR_START_HEIGHT = 100000;


namespace kernel {

/**
 * An options struct for `ChainstateManager`, more ergonomically referred to as
 * `ChainstateManager::Options` due to the using-declaration in
 * `ChainstateManager`.
 */
struct ChainstateManagerOpts {
    const CChainParams& chainparams;
    fs::path datadir;
    const std::function<NodeClock::time_point()> adjusted_time_callback{nullptr};
    std::optional<bool> check_block_index{};
    bool checkpoints_enabled{DEFAULT_CHECKPOINTS_ENABLED};
    //! If set, it will override the minimum work we will assume exists on some valid chain.
    std::optional<arith_uint256> minimum_chain_work{};
    //! If set, it will override the block hash whose ancestors we will assume to have valid scripts without checking them.
    std::optional<uint256> assumed_valid_block{};
    //! If the tip is older than this, the node is considered to be in initial block download.
    std::chrono::seconds max_tip_age{DEFAULT_MAX_TIP_AGE};
    DBOptions block_tree_db{};
    DBOptions coins_db{};
    bool anonRestricted = DEFAULT_ANON_RESTRICTED;

    CoinsViewOptions coins_view{};
};

} // namespace kernel

#endif // BITCOIN_KERNEL_CHAINSTATEMANAGER_OPTS_H
