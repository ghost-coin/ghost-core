// Copyright (c) 2022 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_SMSG_TYPES_H
#define PARTICL_SMSG_TYPES_H

#include <leveldb/write_batch.h>

namespace smsg {

const int32_t ACCEPT_FUNDING_TX_DEPTH = 1;
const int64_t KEEP_FUNDING_TX_DATA = 86400 * 31;
const int64_t PRUNE_FUNDING_TX_DATA = 3600;

static const int MIN_SMSG_PROTO_VERSION = 90010;

class ChainSyncCache
{
public:
    void Clear() {
        m_skip = false;
        m_connect_block_batch.Clear();
    }

    bool m_skip = false;  // Don't commit if data is expired
    leveldb::WriteBatch m_connect_block_batch;
};

} // namespace smsg

#endif // PARTICL_SMSG_TYPES_H
