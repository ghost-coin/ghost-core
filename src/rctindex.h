// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_RCTINDEX_H
#define PARTICL_RCTINDEX_H

#include <primitives/transaction.h>

class CAnonOutput
{
// Stored in txdb, key is 64bit index
public:
    CAnonOutput() : nBlockHeight(0), nCompromised(0) {};
    CAnonOutput(CCmpPubKey pubkey_, secp256k1_pedersen_commitment commitment_, COutPoint &outpoint_, int nBlockHeight_, uint8_t nCompromised_)
        : pubkey(pubkey_), commitment(commitment_), outpoint(outpoint_), nBlockHeight(nBlockHeight_), nCompromised(nCompromised_) {};

    CCmpPubKey pubkey;
    secp256k1_pedersen_commitment commitment;
    COutPoint outpoint;
    int nBlockHeight;
    uint8_t nCompromised;

    SERIALIZE_METHODS(CAnonOutput, obj)
    {
        READWRITE(obj.pubkey);
        if (ser_action.ForRead()) {
            s.read((char*)&obj.commitment.data[0], 33);
        } else {
            s.write((char*)&obj.commitment.data[0], 33);
        }

        READWRITE(obj.outpoint);
        READWRITE(obj.nBlockHeight);
        READWRITE(obj.nCompromised);
    }
};

class CAnonKeyImageInfo
{
public:
    CAnonKeyImageInfo() {};
    CAnonKeyImageInfo(const uint256 &txid_, int height_) : txid(txid_), height(height_) {};
    uint256 txid;
    int height = 0;

    SERIALIZE_METHODS(CAnonKeyImageInfo, obj)
    {
        READWRITE(obj.txid);
        READWRITE(obj.height);
    }
};


#endif // PARTICL_RCTINDEX_H
