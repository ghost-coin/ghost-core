// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include <coins.h>
#include <dbwrapper.h>
#include <kernel/cs_main.h>
#include <sync.h>
#include <util/fs.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


// Particl
#include <insight/addressindex.h>
#include <insight/spentindex.h>
#include <insight/timestampindex.h>
#include <insight/balanceindex.h>
#include <rctindex.h>
#include <primitives/block.h>

class CBlockFileInfo;
class CBlockIndex;
class uint256;
namespace Consensus {
struct Params;
};
struct bilingual_str;

const char DB_RCTOUTPUT = 'A';
const char DB_RCTOUTPUT_LINK = 'L';
const char DB_RCTKEYIMAGE = 'K';
const char DB_SPENTCACHE = 'S';


//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! -dbbatchsize default (bytes)
static const int64_t nDefaultDbBatchSize = 16 << 20;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference: https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxTxIndexCache = 1024;
//! Max memory allocated to all block filter index caches combined in MiB.
static const int64_t max_filter_index_cache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 8;

//! User-controlled performance and debug options.
struct CoinsViewOptions {
    //! Maximum database write batch size in bytes.
    size_t batch_write_bytes = nDefaultDbBatchSize;
    //! If non-zero, randomly exit when the database is flushed with (1/ratio)
    //! probability.
    int simulate_crash_ratio = 0;
};

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB final : public CCoinsView
{
protected:
    DBParams m_db_params;
    CoinsViewOptions m_options;
    std::unique_ptr<CDBWrapper> m_db;
public:
    explicit CCoinsViewDB(DBParams db_params, CoinsViewOptions options);

    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, bool erase = true) override;
    std::unique_ptr<CCoinsViewCursor> Cursor() const override;

    //! Whether an unsupported database format is used.
    bool NeedsUpgrade();
    size_t EstimateSize() const override;

    //! Dynamically alter the underlying leveldb cache size.
    void ResizeCache(size_t new_cache_size) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! @returns filesystem path to on-disk storage or std::nullopt if in memory.
    std::optional<fs::path> StoragePath() { return m_db->StoragePath(); }
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    using CDBWrapper::CDBWrapper;
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &info);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindexing);
    void ReadReindexing(bool &fReindexing);

    bool ReadSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value);
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    bool ReadAddressUnspentIndex(uint256 addressHash, int type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect);
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    bool ReadAddressIndex(uint256 addressHash, int type,
                          std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                          int start = 0, int end = 0);
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    bool ReadTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<std::pair<uint256, unsigned int> > &vect) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts);
    bool ReadTimestampBlockIndex(const uint256 &hash, unsigned int &logicalTS);

    bool WriteBlockBalancesIndex(const uint256 &key, const BlockBalances &value);
    bool ReadBlockBalancesIndex(const uint256 &key, BlockBalances &value);

    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts(const Consensus::Params& consensusParams, std::function<CBlockIndex*(const uint256&)> insertBlockIndex)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    size_t CountBlockIndex();


    bool ReadRCTOutput(int64_t i, CAnonOutput &ao);
    bool WriteRCTOutput(int64_t i, const CAnonOutput &ao);
    bool EraseRCTOutput(int64_t i);

    bool ReadRCTOutputLink(const CCmpPubKey &pk, int64_t &i);
    bool WriteRCTOutputLink(const CCmpPubKey &pk, int64_t i);
    bool EraseRCTOutputLink(const CCmpPubKey &pk);

    bool ReadRCTKeyImage(const CCmpPubKey &ki, CAnonKeyImageInfo &data);
    bool EraseRCTKeyImage(const CCmpPubKey &ki);
    bool EraseRCTKeyImagesAfterHeight(int height);

    bool ReadSpentCache(const COutPoint &outpoint, SpentCoin &coin);
    bool EraseSpentCache(const COutPoint &outpoint);

    //bool WriteRCTOutputBatch(std::vector<std::pair<int64_t, CAnonOutput> > &vao);
};

std::optional<bilingual_str> CheckLegacyTxindex(CBlockTreeDB& block_tree_db);

#endif // BITCOIN_TXDB_H
