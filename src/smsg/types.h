// Copyright (c) 2022 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_SMSG_TYPES_H
#define PARTICL_SMSG_TYPES_H

#include <leveldb/write_batch.h>

namespace smsg {

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
