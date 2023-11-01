// Copyright (c) 2017-2023 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_ANON_H
#define PARTICL_ANON_H

#include <sync.h>
#include <pubkey.h>
#include <consensus/amount.h>
#include <set>
#include <kernel/cs_main.h>


class uint256;
class CTxIn;
class CKey;
class CTransaction;
class CTxMemPool;
class TxValidationState;
class ChainstateManager;
class Chainstate;

const size_t MIN_RINGSIZE = 1;
const size_t MAX_RINGSIZE = 32;
// const size_t MIN_RINGSIZE_AFTER_FORK = 3; // Moved to consensusParams to avoid circular dependency

const size_t MAX_ANON_INPUTS = 32; // To raise see MLSAG_MAX_ROWS also

const size_t ANON_FEE_MULTIPLIER = 2;

const size_t DEFAULT_RING_SIZE = 12;
const size_t DEFAULT_INPUTS_PER_SIG = 1;

bool CheckAnonInputMempoolConflicts(const CTxIn &txin, const uint256 txhash, CTxMemPool *pmempool, TxValidationState &state);

bool VerifyMLSAG(const CTransaction &tx, TxValidationState &state) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

int GetKeyImage(CCmpPubKey &ki, const CCmpPubKey &pubkey, const CKey &key);
bool AddKeyImagesToMempool(const CTransaction &tx, CTxMemPool &pool);
bool RemoveKeyImagesFromMempool(const uint256 &hash, const CTxIn &txin, CTxMemPool &pool);

bool AllAnonOutputsUnknown(Chainstate &active_chainstate, const CTransaction &tx, TxValidationState &state);

bool RollBackRCTIndex(ChainstateManager &chainman, int64_t nLastValidRCTOutput, int64_t nExpectErase, int chain_height, std::set<CCmpPubKey> &setKi) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

bool RewindToHeight(ChainstateManager &chainman, CTxMemPool &mempool, int nToHeight, int &nBlocks, std::string &sError) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

bool RewindRangeProof(const std::vector<uint8_t> &rangeproof, const std::vector<uint8_t> &commitment, const uint256 &nonce,
                      std::vector<uint8_t> &blind_out, CAmount &value_out);

#endif // PARTICL_ANON_H
