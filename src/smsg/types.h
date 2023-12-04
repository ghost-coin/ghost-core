// Copyright (c) 2022-2023 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_SMSG_TYPES_H
#define PARTICL_SMSG_TYPES_H

#include <uint256.h>
#include <inttypes.h>
#include <vector>

namespace smsg {

const int32_t ACCEPT_FUNDING_TX_DEPTH = 1;
const int64_t KEEP_FUNDING_TX_DATA = 86400 * 31;
const int64_t PRUNE_FUNDING_TX_DATA = 3600;

static const int MIN_SMSG_PROTO_VERSION = 90010;

class TxFundingData
{
public:
    TxFundingData(const uint256 &tx_hash, int tx_height, const std::vector<uint8_t> &db_data) : tx_hash(tx_hash), tx_height(tx_height), db_data(db_data) {};
    uint256 tx_hash;
    int tx_height;
    std::vector<uint8_t> db_data;
};

class ChainSyncCache
{
public:
    void Clear();

    bool m_skip = false;  // Don't commit if data is expired

    uint256 best_block_hash;
    int best_block_height;
    std::vector<TxFundingData> funding_data;
};

} // namespace smsg

#endif // PARTICL_SMSG_TYPES_H
