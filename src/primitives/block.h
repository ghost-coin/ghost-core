// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <util/time.h>

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint256 hashWitnessMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CBlockHeader, obj) {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot);
        if (obj.IsParticlVersion()) {
            READWRITE(obj.hashWitnessMerkleRoot);
        }
        READWRITE(obj.nTime, obj.nBits, obj.nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        hashWitnessMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    NodeSeconds Time() const
    {
<<<<<<< HEAD
        return nVersion == GHOST_BLOCK_VERSION;
=======
        return NodeSeconds{std::chrono::seconds{nTime}};
>>>>>>> particl/25.x
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    bool IsParticlVersion() const
    {
        return nVersion == PARTICL_BLOCK_VERSION;
    }
};

/**
    see GETHEADERS message, vtx collapses to a single 0 byte
*/
class CBlockGetHeader : public CBlockHeader
{
    public:
        CBlockGetHeader() {};
        CBlockGetHeader(const CBlockHeader &header) { *((CBlockHeader*)this) = header; };
        std::vector<CTransactionRef> vtx;
        SERIALIZE_METHODS(CBlockGetHeader, obj)
        {
            READWRITEAS(CBlockHeader, obj);
            READWRITE(obj.vtx);
        }
};

class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // pos block signature - signed by one of the coin stake txout[N]'s owner
    std::vector<uint8_t> vchBlockSig;

    // memory only
    mutable bool fChecked;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    bool IsProofOfStake() const
    {
        return (vtx.size() > 0 && vtx[0]->IsCoinStake());
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITEAS(CBlockHeader, obj);
        READWRITE(obj.vtx);
        if (obj.nVersion == GHOST_BLOCK_VERSION) {
            READWRITE(obj.vchBlockSig);
        }
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion              = nVersion;
        block.hashPrevBlock         = hashPrevBlock;
        block.hashMerkleRoot        = hashMerkleRoot;
        block.hashWitnessMerkleRoot = hashWitnessMerkleRoot;
        block.nTime                 = nTime;
        block.nBits                 = nBits;
        block.nNonce                = nNonce;
        return block;
    }

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(std::vector<uint256>&& have) : vHave(std::move(have)) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
