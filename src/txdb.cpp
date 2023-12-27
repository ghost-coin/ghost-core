// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <chain.h>
#include <logging.h>
#include <pow.h>
#include <random.h>
#include <shutdown.h>
#include <uint256.h>
#include <util/signalinterrupt.h>

#include <util/translation.h>
#include <util/vector.h>

#include <validation.h>
#include <insight/insight.h>
#include <chainparams.h>

#include <stdint.h>

static constexpr uint8_t DB_COIN{'C'};
//static constexpr uint8_t DB_COINS{'c'};
static constexpr uint8_t DB_BLOCK_FILES{'f'};
//static constexpr uint8_t DB_TXINDEX{'t'};
static constexpr uint8_t DB_ADDRESSINDEX{'a'};
static constexpr uint8_t DB_ADDRESSUNSPENTINDEX{'u'};
static constexpr uint8_t DB_TIMESTAMPINDEX{'s'};
static constexpr uint8_t DB_BLOCKHASHINDEX{'z'};
static constexpr uint8_t DB_SPENTINDEX{'p'};
static constexpr uint8_t DB_BALANCESINDEX{'i'};
//static constexpr uint8_t DB_TXINDEX_BLOCK{'T'};
static constexpr uint8_t DB_BLOCK_INDEX{'b'};

static constexpr uint8_t DB_BEST_BLOCK{'B'};
static constexpr uint8_t DB_HEAD_BLOCKS{'H'};
static constexpr uint8_t DB_FLAG{'F'};
static constexpr uint8_t DB_REINDEX_FLAG{'R'};
static constexpr uint8_t DB_LAST_BLOCK{'l'};

/*
static constexpr uint8_t DB_RCTOUTPUT = 'A';
static constexpr uint8_t DB_RCTOUTPUT_LINK = 'L';
static constexpr uint8_t DB_RCTKEYIMAGE = 'K';
static constexpr uint8_t DB_SPENTCACHE = 'S';
*/

// Keys used in previous version that might still be found in the DB:
static constexpr uint8_t DB_COINS{'c'};
static constexpr uint8_t DB_TXINDEX_BLOCK{'T'};
//               uint8_t DB_TXINDEX{'t'}

util::Result<void> CheckLegacyTxindex(CBlockTreeDB& block_tree_db)
{
    CBlockLocator ignored{};
    if (block_tree_db.Read(DB_TXINDEX_BLOCK, ignored)) {
        return util::Error{_("The -txindex upgrade started by a previous version cannot be completed. Restart with the previous version or run a full -reindex.")};
    }
    bool txindex_legacy_flag{false};
    block_tree_db.ReadFlag("txindex", txindex_legacy_flag);
    if (txindex_legacy_flag) {
        // Disable legacy txindex and warn once about occupied disk space
        if (!block_tree_db.WriteFlag("txindex", false)) {
            return util::Error{Untranslated("Failed to write block index db flag 'txindex'='0'")};
        }
        return util::Error{_("The block index db contains a legacy 'txindex'. To clear the occupied disk space, run a full -reindex, otherwise ignore this error. This error message will not be displayed again.")};
    }
    return {};
}


bool CCoinsViewDB::NeedsUpgrade()
{
    std::unique_ptr<CDBIterator> cursor{m_db->NewIterator()};
    // DB_COINS was deprecated in v0.15.0, commit
    // 1088b02f0ccd7358d2b7076bb9e122d59d502d02
    cursor->Seek(std::make_pair(DB_COINS, uint256{}));
    return cursor->Valid();
}

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    uint8_t key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    SERIALIZE_METHODS(CoinEntry, obj) { READWRITE(obj.key, obj.outpoint->hash, VARINT(obj.outpoint->n)); }
};

} // namespace

CCoinsViewDB::CCoinsViewDB(DBParams db_params, CoinsViewOptions options) :
    m_db_params{std::move(db_params)},
    m_options{std::move(options)},
    m_db{std::make_unique<CDBWrapper>(m_db_params)} { }

void CCoinsViewDB::ResizeCache(size_t new_cache_size)
{
    // We can't do this operation with an in-memory DB since we'll lose all the coins upon
    // reset.
    if (!m_db_params.memory_only) {
        // Have to do a reset first to get the original `m_db` state to release its
        // filesystem lock.
        m_db.reset();
        m_db_params.cache_bytes = new_cache_size;
        m_db_params.wipe_data = false;
        m_db = std::make_unique<CDBWrapper>(m_db_params);
    }
}

bool CCoinsViewDB::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return m_db->Read(CoinEntry(&outpoint), coin);
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return m_db->Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!m_db->Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!m_db->Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, bool erase) {
    CDBBatch batch(*m_db);
    size_t count = 0;
    size_t changed = 0;
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, Vector(hashBlock, old_tip));

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        it = erase ? mapCoins.erase(it) : std::next(it);
        if (batch.SizeEstimate() > m_options.batch_write_bytes) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db->WriteBatch(batch);
            batch.Clear();
            if (m_options.simulate_crash_ratio) {
                static FastRandomContext rng;
                if (rng.randrange(m_options.simulate_crash_ratio) == 0) {
                    LogPrintf("Simulating a crash. Goodbye.\n");
                    _Exit(0);
                }
            }
        }
    }

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = m_db->WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return m_db->EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(std::make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, uint8_t{'1'});
    else
        return Erase(DB_REINDEX_FLAG);
}

void CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    // Prefer using CCoinsViewDB::Cursor() since we want to perform some
    // cache warmup on instantiation.
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256&hashBlockIn):
        CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    ~CCoinsViewDBCursor() = default;

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;

    bool Valid() const override;
    void Next() override;

private:
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB::Cursor() const
{
    auto i = std::make_unique<CCoinsViewDBCursor>(
        const_cast<CDBWrapper&>(*m_db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value) {
    return Read(std::make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CSpentIndexKey,CSpentIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(std::make_pair(DB_SPENTINDEX, it->first));
        } else {
            batch.Write(std::make_pair(DB_SPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(std::make_pair(DB_ADDRESSUNSPENTINDEX, it->first));
        } else {
            batch.Write(std::make_pair(DB_ADDRESSUNSPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndex(uint256 addressHash, int type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs) {
    const std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));

    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<uint8_t, CAddressUnspentKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSUNSPENTINDEX && key.second.hashBytes == addressHash) {
            CAddressUnspentValue nValue;
            if (pcursor->GetValue(nValue)) {
                unspentOutputs.push_back(std::make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address unspent value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(std::make_pair(DB_ADDRESSINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(std::make_pair(DB_ADDRESSINDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressIndex(uint256 addressHash, int type,
                                    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                                    int start, int end) {
    const std::unique_ptr<CDBIterator> pcursor(NewIterator());

    if (start > 0 && end > 0) {
        pcursor->Seek(std::make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(type, addressHash, start)));
    } else {
        pcursor->Seek(std::make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<uint8_t, CAddressIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_ADDRESSINDEX && key.second.hashBytes == addressHash) {
            if (end > 0 && key.second.blockHeight > end) {
                break;
            }
            CAmount nValue;
            if (pcursor->GetValue(nValue)) {
                addressIndex.push_back(std::make_pair(key.second, nValue));
                pcursor->Next();
            } else {
                return error("failed to get address index value");
            }
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector<std::pair<uint256, unsigned int> > &hashes)
{
    const std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low)));

    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<uint8_t, CTimestampIndexKey> key;
        if (pcursor->GetKey(key) && key.first == DB_TIMESTAMPINDEX && key.second.timestamp < high) {
            hashes.push_back(std::make_pair(key.second.blockHash, key.second.timestamp));
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts) {
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_BLOCKHASHINDEX, blockhashIndex), logicalts);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampBlockIndex(const uint256 &hash, unsigned int &ltimestamp) {

    CTimestampBlockIndexValue lts;
    if (!Read(std::make_pair(DB_BLOCKHASHINDEX, hash), lts)) {
        return false;
    }

    ltimestamp = lts.ltimestamp;
    return true;
}

bool CBlockTreeDB::WriteBlockBalancesIndex(const uint256 &key, const BlockBalances &value)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_BALANCESINDEX, key), value);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadBlockBalancesIndex(const uint256 &key, BlockBalances &value)
{
    return Read(std::make_pair(DB_BALANCESINDEX, key), value);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? uint8_t{'1'} : uint8_t{'0'});
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    uint8_t ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == uint8_t{'1'};
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(const Consensus::Params& consensusParams, std::function<CBlockIndex*(const uint256&)> insertBlockIndex, const util::SignalInterrupt& interrupt)
{
    AssertLockHeld(::cs_main);
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));

    // Load m_block_index
    while (pcursor->Valid()) {
        if (interrupt) return false;
        std::pair<uint8_t, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = insertBlockIndex(diskindex.ConstructBlockHash());
                pindexNew->pprev          = insertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                pindexNew->hashWitnessMerkleRoot    = diskindex.hashWitnessMerkleRoot;
                pindexNew->nFlags                   = diskindex.nFlags & (uint32_t)~BLOCK_DELAYED;
                pindexNew->bnStakeModifier          = diskindex.bnStakeModifier;
                pindexNew->prevoutStake             = diskindex.prevoutStake;
                //pindexNew->hashProof                = diskindex.hashProof;

                pindexNew->nMoneySupply             = diskindex.nMoneySupply;
                pindexNew->nAnonOutputs             = diskindex.nAnonOutputs;

                if (pindexNew->nHeight == 0
                    && pindexNew->GetBlockHash() != Params().GetConsensus().hashGenesisBlock)
                    return error("LoadBlockIndex(): Genesis block hash incorrect: %s", pindexNew->ToString());

                if (fParticlMode) {
                    // only CheckProofOfWork for genesis blocks
                    if (diskindex.hashPrev.IsNull() && !CheckProofOfWork(pindexNew->GetBlockHash(),
                        pindexNew->nBits, Params().GetConsensus(), 0, Params().GetLastImportHeight()))
                        return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());
                } else
                if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, consensusParams)) {
                    return error("%s: CheckProofOfWork failed: %s", __func__, pindexNew->ToString());
                }

                pcursor->Next();
            } else {
                return error("%s: failed to read value", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}

size_t CBlockTreeDB::CountBlockIndex()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));

    size_t num_blocks = 0;
    // Load m_block_index
    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<uint8_t, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            num_blocks += 1;
            pcursor->Next();
        } else {
            break;
        }
    }

    return num_blocks;
}

bool CBlockTreeDB::ReadRCTOutput(int64_t i, CAnonOutput &ao)
{
    std::pair<uint8_t, int64_t> key = std::make_pair(DB_RCTOUTPUT, i);
    return Read(key, ao);
};

bool CBlockTreeDB::WriteRCTOutput(int64_t i, const CAnonOutput &ao)
{
    std::pair<uint8_t, int64_t> key = std::make_pair(DB_RCTOUTPUT, i);
    CDBBatch batch(*this);
    batch.Write(key, ao);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTOutput(int64_t i)
{
    std::pair<uint8_t, int64_t> key = std::make_pair(DB_RCTOUTPUT, i);
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
};


bool CBlockTreeDB::ReadRCTOutputLink(const CCmpPubKey &pk, int64_t &i)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTOUTPUT_LINK, pk);
    return Read(key, i);
};

bool CBlockTreeDB::WriteRCTOutputLink(const CCmpPubKey &pk, int64_t i)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTOUTPUT_LINK, pk);
    CDBBatch batch(*this);
    batch.Write(key, i);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTOutputLink(const CCmpPubKey &pk)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTOUTPUT_LINK, pk);
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
};

bool CBlockTreeDB::ReadRCTKeyImage(const CCmpPubKey &ki, CAnonKeyImageInfo &data)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTKEYIMAGE, ki);
    // Versions before 0.19.2.15 store only the txid
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    if (!ReadStream(key, ssValue)) {
        return false;
    }
    try {
        if (ssValue.size() < 36) {
            ssValue >> data.txid;
            data.height = -1; // unset
        } else {
            ssValue >> data;
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
};

bool CBlockTreeDB::EraseRCTKeyImage(const CCmpPubKey &ki)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTKEYIMAGE, ki);
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
};

bool CBlockTreeDB::EraseRCTKeyImagesAfterHeight(int height)
{
    std::pair<uint8_t, CCmpPubKey> key = std::make_pair(DB_RCTKEYIMAGE, CCmpPubKey());

    CDBBatch batch(*this);
    size_t total = 0, removing = 0;
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(key);

    while (pcursor->Valid()) {
        if (ShutdownRequested()) return false;
        std::pair<uint8_t, CCmpPubKey> key;
        if (pcursor->GetKey(key) && key.first == DB_RCTKEYIMAGE) {
            CAnonKeyImageInfo ki_data;
            total++;
            if (pcursor->GetValueSize() >= 36) {
                if (pcursor->GetValue(ki_data)) {
                    if (height < ki_data.height) {
                        removing++;
                        std::pair<uint8_t, CCmpPubKey> erase_key = std::make_pair(DB_RCTKEYIMAGE, key.second);
                        batch.Erase(erase_key);
                    }
                } else {
                    return error("%s: failed to read value", __func__);
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    LogPrintf("Removing %d key images after height %d, total %d.\n", removing, height, total);
    if (removing < 1) {
        return true;
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadSpentCache(const COutPoint &outpoint, SpentCoin &coin)
{
    std::pair<uint8_t, COutPoint> key = std::make_pair(DB_SPENTCACHE, outpoint);
    return Read(key, coin);
}

bool CBlockTreeDB::EraseSpentCache(const COutPoint &outpoint)
{
    std::pair<uint8_t, COutPoint> key = std::make_pair(DB_SPENTCACHE, outpoint);
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
}

bool CBlockTreeDB::HaveBlindedFlag(const uint256 &txid) const {
    std::pair<uint8_t, uint256> key = std::make_pair(DB_HAS_BLINDED_TXIN, txid);
    return Exists(key);
}

bool CBlockTreeDB::WriteBlindedFlag(const uint256 &txid)
{
    std::pair<uint8_t, uint256> key = std::make_pair(DB_HAS_BLINDED_TXIN, txid);
    CDBBatch batch(*this);
    batch.Write(key, 1);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseBlindedFlag(const uint256 &txid)
{
    std::pair<uint8_t, uint256> key = std::make_pair(DB_HAS_BLINDED_TXIN, txid);
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
}


bool CBlockTreeDB::WriteLastTrackedHeight(std::int64_t lastHeight) {
    CDBBatch batch(*this);
    LogPrintf("%s Writting last tracked height %d\n", __func__, lastHeight);

    batch.Write(std::make_pair<uint8_t, int64_t>(DB_LAST_TRACKED_HEIGHT, 0), lastHeight);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadLastTrackedHeight(std::int64_t& rv) {
    return Read(std::make_pair<uint8_t, int64_t>(DB_LAST_TRACKED_HEIGHT, 0), rv);
}

bool CBlockTreeDB::EraseLastTrackedHeight() {
    CDBBatch batch(*this);
    LogPrintf("%s Erasing last tracked height\n", __func__);
    batch.Erase(std::make_pair<uint8_t, int64_t>(DB_LAST_TRACKED_HEIGHT, 0));
    return WriteBatch(batch);
}


bool CBlockTreeDB::EraseRewardTrackerUndo(int nHeight)
{
    CDBBatch batch(*this);
    batch.Erase(std::make_pair<uint8_t, int64_t>(DB_TRACKER_INPUTS_UNDO, nHeight));
    batch.Erase(std::make_pair<uint8_t, int64_t>(DB_TRACKER_OUTPUTS_UNDO, nHeight));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadRewardTrackerUndo(ColdRewardUndo& rewardUndo, int nHeight)
{

    const std::unique_ptr<CDBIterator> pcursor(NewIterator());
    
    pcursor->Seek(std::make_pair((uint8_t)DB_TRACKER_INPUTS_UNDO, std::vector<std::pair<ColdRewardTracker::AddressType, CAmount>>()));

    while (pcursor->Valid()) {
        if (ShutdownRequested()) {
            return false;
        }
        std::pair<uint8_t, int> key;
        if (pcursor->GetKey(key) && key.first == DB_TRACKER_INPUTS_UNDO) {
            std::vector<std::pair<ColdRewardTracker::AddressType, CAmount>> inputs;
            if (pcursor->GetValue(inputs)) {
                rewardUndo.inputs[key.second] = std::move(inputs);
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    pcursor->Seek(std::make_pair((uint8_t)DB_TRACKER_OUTPUTS_UNDO, std::vector<std::pair<ColdRewardTracker::AddressType, CAmount>>()));

    while (pcursor->Valid()) {
        if (ShutdownRequested()) {
            return false;
        }
        std::pair<uint8_t, int> key;
        if (pcursor->GetKey(key) && key.first == DB_TRACKER_OUTPUTS_UNDO) {
            std::vector<std::pair<ColdRewardTracker::AddressType, CAmount>> outputs;
            if (pcursor->GetValue(outputs)) {
                rewardUndo.outputs[key.second] = std::move(outputs);
            }
            pcursor->Next();
        } else {
            break;
        }
    }
    return true;
}

bool CBlockTreeDB::WriteRewardTrackerUndo(const ColdRewardUndo& rewardUndo)
{
    CDBBatch batch(*this);

    for (const auto& inputs: rewardUndo.inputs) {
        batch.Write(std::make_pair<uint8_t, int64_t>(DB_TRACKER_INPUTS_UNDO, inputs.first), inputs.second);
    }

    for (const auto& outputs: rewardUndo.outputs) {
        batch.Write(std::make_pair<uint8_t, int64_t>(DB_TRACKER_OUTPUTS_UNDO, outputs.first), outputs.second);
    }

    return WriteBatch(batch);
}