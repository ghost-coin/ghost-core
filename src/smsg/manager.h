// Copyright (c) 2022-2023 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_SMSG_MANAGER_H
#define PARTICL_SMSG_MANAGER_H

#include <sync.h>
#include <smsg/types.h>
#include <kernel/cs_main.h>

#include <utility>
#include <memory>

class CTransaction;
class CBlockIndex;
class CBlock;
class uint256;

class SmsgManager
{
public:
    static std::unique_ptr<SmsgManager> make();
    virtual ~SmsgManager() { }

    virtual int StoreFundingTx(smsg::ChainSyncCache &cache, const CTransaction &tx, const CBlockIndex *pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;
    virtual int SetBestBlock(smsg::ChainSyncCache &cache, const uint256 &block_hash, int height, int64_t time) = 0;
    virtual int WriteCache(smsg::ChainSyncCache &cache) = 0;
    virtual bool ScanBlock(const CBlock &block) = 0;
    virtual int ReadBestBlock(uint256 &block_hash, int &height) = 0;
    virtual bool TrackFundingTxns() = 0;
    virtual bool IsEnabled() = 0;
};

#endif // PARTICL_SMSG_MANAGER_H
