// Copyright (c) 2017-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/txindex.h>

#include <index/disktxpos.h>
#include <node/blockstorage.h>
#include <util/system.h>
#include <validation.h>

#include <insight/csindex.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <wallet/ismine.h>
#include <key_io.h>

constexpr uint8_t DB_BEST_BLOCK{'B'};
constexpr uint8_t DB_TXINDEX{'t'};

/* csindex.h
constexpr uint8_t DB_TXINDEX_CSOUTPUT{'O'};
constexpr uint8_t DB_TXINDEX_CSLINK{'L'};
constexpr uint8_t DB_TXINDEX_CSBESTBLOCK{'C'};
*/

std::unique_ptr<TxIndex> g_txindex;


/** Access to the txindex database (indexes/txindex/) */
class TxIndex::DB : public BaseIndex::DB
{
public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    /// Read the disk location of the transaction data with the given hash. Returns false if the
    /// transaction hash is not indexed.
    bool ReadTxPos(const uint256& txid, CDiskTxPos& pos) const;

    /// Write a batch of transaction positions to the DB.
    bool WriteTxs(const std::vector<std::pair<uint256, CDiskTxPos>>& v_pos);
};

TxIndex::DB::DB(size_t n_cache_size, bool f_memory, bool f_wipe) :
    BaseIndex::DB(gArgs.GetDataDirNet() / "indexes" / "txindex", n_cache_size, f_memory, f_wipe)
{}

bool TxIndex::DB::ReadTxPos(const uint256 &txid, CDiskTxPos& pos) const
{
    return Read(std::make_pair(DB_TXINDEX, txid), pos);
}

bool TxIndex::DB::WriteTxs(const std::vector<std::pair<uint256, CDiskTxPos>>& v_pos)
{
    CDBBatch batch(*this);
    for (const auto& tuple : v_pos) {
        batch.Write(std::make_pair(DB_TXINDEX, tuple.first), tuple.second);
    }
    return WriteBatch(batch);
}

TxIndex::TxIndex(size_t n_cache_size, bool f_memory, bool f_wipe)
    : m_db(std::make_unique<TxIndex::DB>(n_cache_size, f_memory, f_wipe))
{}

TxIndex::~TxIndex() {}

bool TxIndex::WriteBlock(const CBlock& block, const CBlockIndex* pindex)
{
    if (m_cs_index) {
        IndexCSOutputs(block, pindex);
    }
    // Exclude genesis block transaction because outputs are not spendable.
    if (!block.IsParticlVersion() && pindex->nHeight == 0) return true;

    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos>> vPos;
    vPos.reserve(block.vtx.size());
    for (const auto& tx : block.vtx) {
        vPos.emplace_back(tx->GetHash(), pos);
        pos.nTxOffset += ::GetSerializeSize(*tx, CLIENT_VERSION);
    }
    return m_db->WriteTxs(vPos);
}

bool TxIndex::DisconnectBlock(const CBlock& block)
{
    if (!m_cs_index) {
        return true;
    }

    std::set<COutPoint> erasedCSOuts;
    CDBBatch batch(*m_db);
    for (const auto &tx : block.vtx) {
        int n = -1;
        for (const auto &o : tx->vpout) {
            n++;
            if (!o->IsType(OUTPUT_STANDARD)) {
                continue;
            }
            const CScript *ps = o->GetPScriptPubKey();
            if (!ps->StartsWithICS()) {
                continue;
            }

            ColdStakeIndexOutputKey ok(tx->GetHash(), n);
            batch.Erase(std::make_pair(DB_TXINDEX_CSOUTPUT, ok));
            erasedCSOuts.insert(COutPoint(ok.m_txnid, ok.m_n));
        }
        for (const auto &in : tx->vin) {
            ColdStakeIndexOutputKey ok(in.prevout.hash, in.prevout.n);
            ColdStakeIndexOutputValue ov;

            if (erasedCSOuts.count(in.prevout)) {
                continue;
            }
            if (m_db->Read(std::make_pair(DB_TXINDEX_CSOUTPUT, ok), ov)) {
                ov.m_spend_height = -1;
                ov.m_spend_txid.SetNull();
                batch.Write(std::make_pair(DB_TXINDEX_CSOUTPUT, ok), ov);
            }
        }
    }

    if (!m_db->WriteBatch(batch)) {
        return error("%s: WriteBatch failed.", __func__);
    }

    return true;
}

bool TxIndex::IndexCSOutputs(const CBlock& block, const CBlockIndex* pindex)
{
    CDBBatch batch(*m_db);
    std::map<ColdStakeIndexOutputKey, ColdStakeIndexOutputValue> newCSOuts;
    std::map<ColdStakeIndexLinkKey, std::vector<ColdStakeIndexOutputKey> > newCSLinks;

    for (const auto &tx : block.vtx) {
        int n = -1;
        for (const auto &o : tx->vpout) {
            n++;
            if (!o->IsType(OUTPUT_STANDARD)) {
                continue;
            }
            const CScript *ps = o->GetPScriptPubKey();
            if (!ps->StartsWithICS()) {
                continue;
            }

            CScript scriptStake, scriptSpend;
            if (!SplitConditionalCoinstakeScript(*ps, scriptStake, scriptSpend)) {
                continue;
            }

            std::vector<valtype> vSolutions;

            ColdStakeIndexOutputKey ok;
            ColdStakeIndexOutputValue ov;
            ColdStakeIndexLinkKey lk;
            lk.m_height = pindex->nHeight;
            lk.m_stake_type = Solver(scriptStake, vSolutions);

            if (m_cs_index_whitelist.size() > 0
                && !m_cs_index_whitelist.count(vSolutions[0])) {
                continue;
            }

            if (lk.m_stake_type == TxoutType::PUBKEYHASH) {
                memcpy(lk.m_stake_id.begin(), vSolutions[0].data(), 20);
            } else
            if (lk.m_stake_type == TxoutType::PUBKEYHASH256) {
                lk.m_stake_id = CKeyID256(uint256(vSolutions[0]));
            } else {
                LogPrint(BCLog::COINDB, "%s: Ignoring unexpected stakescript type=%d.\n", __func__, particl::FromTxoutType(lk.m_stake_type));
                continue;
            }


            lk.m_spend_type = Solver(scriptSpend, vSolutions);

            if (lk.m_spend_type == TxoutType::PUBKEYHASH || lk.m_spend_type == TxoutType::SCRIPTHASH) {
                memcpy(lk.m_spend_id.begin(), vSolutions[0].data(), 20);
            } else
            if (lk.m_spend_type == TxoutType::PUBKEYHASH256 || lk.m_spend_type == TxoutType::SCRIPTHASH256) {
                lk.m_spend_id = CKeyID256(uint256(vSolutions[0]));
            } else {
                LogPrint(BCLog::COINDB, "%s: Ignoring unexpected spendscript type=%d.\n", __func__, particl::FromTxoutType(lk.m_spend_type));
                continue;
            }

            ok.m_txnid = tx->GetHash();
            ok.m_n = n;
            ov.m_value = o->GetValue();

            if (tx->IsCoinStake()) {
                ov.m_flags |= CSI_FROM_STAKE;
            }

            newCSOuts[ok] = ov;
            newCSLinks[lk].push_back(ok);
        }

        for (const auto &in : tx->vin) {
            if (in.IsAnonInput()) {
                continue;
            }
            ColdStakeIndexOutputKey ok(in.prevout.hash, (int)in.prevout.n);
            ColdStakeIndexOutputValue ov;

            auto it = newCSOuts.find(ok);
            if (it != newCSOuts.end()) {
                it->second.m_spend_height = pindex->nHeight;
                it->second.m_spend_txid = tx->GetHash();
            } else
            if (m_db->Read(std::make_pair(DB_TXINDEX_CSOUTPUT, ok), ov)) {
                ov.m_spend_height = pindex->nHeight;
                ov.m_spend_txid = tx->GetHash();
                batch.Write(std::make_pair(DB_TXINDEX_CSOUTPUT, ok), ov);
            }
        }
    }

    for (const auto &it : newCSOuts) {
        batch.Write(std::make_pair(DB_TXINDEX_CSOUTPUT, it.first), it.second);
    }
    for (const auto &it : newCSLinks) {
        batch.Write(std::make_pair(DB_TXINDEX_CSLINK, it.first), it.second);
    }

    CChain &active_chain = m_chainstate->m_chain;
    batch.Write(DB_TXINDEX_CSBESTBLOCK, active_chain.GetLocator(pindex));

    if (!m_db->WriteBatch(batch)) {
        return error("%s: WriteBatch failed.", __func__);
    }

    return true;
}

BaseIndex::DB& TxIndex::GetDB() const { return *m_db; }

bool TxIndex::FindTx(const uint256& tx_hash, uint256& block_hash, CTransactionRef& tx) const
{
    CDiskTxPos postx;
    if (!m_db->ReadTxPos(tx_hash, postx)) {
        return false;
    }

    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        return error("%s: OpenBlockFile failed", __func__);
    }
    CBlockHeader header;
    try {
        file >> header;
        if (fseek(file.Get(), postx.nTxOffset, SEEK_CUR)) {
            return error("%s: fseek(...) failed", __func__);
        }
        file >> tx;
    } catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    if (tx->GetHash() != tx_hash) {
        return error("%s: txid mismatch", __func__);
    }
    block_hash = header.GetHash();
    return true;
}

bool TxIndex::FindTx(const uint256& tx_hash, CBlockHeader& header, CTransactionRef& tx) const
{
    CDiskTxPos postx;
    if (!m_db->ReadTxPos(tx_hash, postx)) {
        return false;
    }

    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        return error("%s: OpenBlockFile failed", __func__);
    }
    try {
        file >> header;
        if (fseek(file.Get(), postx.nTxOffset, SEEK_CUR)) {
            return error("%s: fseek(...) failed", __func__);
        }
        file >> tx;
    } catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    if (tx->GetHash() != tx_hash) {
        return error("%s: txid mismatch", __func__);
    }
    return true;
}

bool TxIndex::AppendCSAddress(std::string addr)
{
    CTxDestination dest = DecodeDestination(addr);

    if (dest.index() == DI::_PKHash) {
        PKHash id = std::get<PKHash>(dest);
        valtype vSolution;
        vSolution.resize(20);
        memcpy(vSolution.data(), id.begin(), 20);
        m_cs_index_whitelist.insert(vSolution);
        return true;
    }

    if (dest.index() == DI::_CKeyID256) {
        CKeyID256 id256 = std::get<CKeyID256>(dest);
        valtype vSolution;
        vSolution.resize(32);
        memcpy(vSolution.data(), id256.begin(), 32);
        m_cs_index_whitelist.insert(vSolution);
        return true;
    }

    return error("%s: Failed to parse address %s.", __func__, addr);
}
