// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_TXINDEX_H
#define BITCOIN_INDEX_TXINDEX_H

#include <index/base.h>

static constexpr bool DEFAULT_TXINDEX{false};
class CBlockHeader;

namespace particl {
static constexpr bool DEFAULT_CSINDEX{false};
static constexpr bool DEFAULT_ADDRESSINDEX{false};
static constexpr bool DEFAULT_TIMESTAMPINDEX{false};
static constexpr bool DEFAULT_SPENTINDEX{false};
static constexpr bool DEFAULT_BALANCESINDEX{false};
} // particl


/**
 * TxIndex is used to look up transactions included in the blockchain by hash.
 * The index is written to a LevelDB database and records the filesystem
 * location of each transaction by transaction hash.
 */
class TxIndex final : public BaseIndex
{
protected:
    class DB;

private:
    const std::unique_ptr<DB> m_db;

    bool AllowPrune() const override { return false; }

protected:
    bool CustomInit(const std::optional<interfaces::BlockKey>& block) override;
    bool CustomAppend(const interfaces::BlockInfo& block) override;
    bool DisconnectBlock(const CBlock& block) override;

    bool IndexCSOutputs(const interfaces::BlockInfo& block);

public:
    BaseIndex::DB& GetDB() const override;

    /// Constructs the index, which becomes available to be queried.
    explicit TxIndex(std::unique_ptr<interfaces::Chain> chain, size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    // Destructor is declared because this class contains a unique_ptr to an incomplete type.
    virtual ~TxIndex() override;

    /// Look up a transaction by hash.
    ///
    /// @param[in]   tx_hash  The hash of the transaction to be returned.
    /// @param[out]  block_hash  The hash of the block the transaction is found in.
    /// @param[out]  tx  The transaction itself.
    /// @return  true if transaction is found, false otherwise
    bool FindTx(const uint256& tx_hash, uint256& block_hash, CTransactionRef& tx) const;
    bool FindTx(const uint256& tx_hash, CBlockHeader& header, CTransactionRef& tx) const;

    bool AppendCSAddress(std::string addr);

    bool m_cs_index = false;
    std::set<std::vector<uint8_t> > m_cs_index_whitelist;
};

/// The global transaction index, used in GetTransaction. May be null.
extern std::unique_ptr<TxIndex> g_txindex;

#endif // BITCOIN_INDEX_TXINDEX_H
