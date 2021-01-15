// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <net.h>
#include <sync.h>
#include <validationinterface.h>

class CChainParams;
class CTxMemPool;
class ChainstateManager;

extern RecursiveMutex cs_main;
extern RecursiveMutex g_cs_orphans;

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
static const bool DEFAULT_PEERBLOOMFILTERS = false;
static const bool DEFAULT_PEERBLOCKFILTERS = false;
/** Threshold for marking a node to be discouraged, e.g. disconnected and added to the discouragement filter. */
static const int DISCOURAGEMENT_THRESHOLD{100};

struct CNodeStateStats {
    int m_misbehavior_score = 0;
    int nSyncHeight = -1;
    int nCommonHeight = -1;

    int m_starting_height = -1;
    int m_chain_height = -1;
    std::vector<int> vHeightInFlight;
    int nDuplicateCount = 0;
    int nLooseHeadersCount = 0;
};

class PeerManager : public CValidationInterface, public NetEventsInterface
{
public:
    static std::unique_ptr<PeerManager> make(const CChainParams& chainparams, CConnman& connman, BanMan* banman,
                                             CScheduler& scheduler, ChainstateManager& chainman, CTxMemPool& pool,
                                             bool ignore_incoming_txs);
    virtual ~PeerManager() { }

    /** Get statistics from node state */
    virtual bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) = 0;

    /** Whether this node ignores txs received over p2p. */
    virtual bool IgnoresIncomingTxs() = 0;

    /** Set the best height */
    virtual void SetBestHeight(int height) = 0;

    /**
     * Increment peer's misbehavior score. If the new value >= DISCOURAGEMENT_THRESHOLD, mark the node
     * to be discouraged, meaning the peer might be disconnected and added to the discouragement filter.
     * Public for unit testing.
     */
    virtual void Misbehaving(const NodeId pnode, const int howmuch, const std::string& message) = 0;

    /**
     * Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound.
     * Public for unit testing.
     */
    virtual void CheckForStaleTipAndEvictPeers() = 0;

    /** Process a single message from a peer. Public for fuzz testing */
    virtual void ProcessMessage(CNode& pfrom, const std::string& msg_type, CDataStream& vRecv,
                                const std::chrono::microseconds time_received, const std::atomic<bool>& interruptMsgProc) = 0;

    /** Particl */
    virtual void IncPersistentMisbehaviour(NodeId node_id, const CService &node_address, int howmuch) = 0;
    virtual void DecMisbehaving(NodeId nodeid, int howmuch) = 0;
    virtual void MisbehavingByAddr(CNetAddr addr, int misbehavior_cfwd, int howmuch, const std::string& message="") = 0;
    virtual bool IncDuplicateHeaders(NodeId node_id, const CService &node_address) = 0;
};

extern PeerManager *g_peerman;

/** Decrease a node's misbehavior score. */
void DecMisbehaving(NodeId nodeid, int howmuch);

NodeId GetBlockSource(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

void IncPersistentMisbehaviour(NodeId node_id, int howmuch) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
int GetNumDOSStates() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
void ClearDOSStates() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Relay transaction to every node */
void RelayTransaction(const uint256& txid, const uint256& wtxid, const CConnman& connman) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

#endif // BITCOIN_NET_PROCESSING_H
