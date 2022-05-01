// Copyright (c) 2022 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <smsg/manager.h>
#include <smsg/smessage.h>

namespace smsg {

class SmsgManagerImpl final : public SmsgManager
{
public:
    int StoreFundingTx(ChainSyncCache &cache, const CTransaction &tx, const CBlockIndex *pindex) override EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        return smsgModule.StoreFundingTx(cache, tx, pindex);
    }

    int SetBestBlock(ChainSyncCache &cache, const uint256 &block_hash, int height, int64_t time) override
    {
        return smsgModule.SetBestBlock(cache, block_hash, height, time);
    }

    int WriteCache(ChainSyncCache &cache) override
    {
        return smsgModule.WriteCache(cache);
    }

    bool ScanBlock(const CBlock &block) override
    {
        return smsgModule.ScanBlock(block);
    }

    int ReadBestBlock(uint256 &block_hash, int &height) override
    {
        return smsgModule.ReadBestBlock(block_hash, height);
    }

    bool TrackFundingTxns() override
    {
        return smsgModule.m_track_funding_txns;
    }

    bool IsEnabled() override
    {
        return fSecMsgEnabled;
    }
};

} // namespace smsg


std::unique_ptr<SmsgManager> SmsgManager::make()
{
    return std::make_unique<smsg::SmsgManagerImpl>();
}
