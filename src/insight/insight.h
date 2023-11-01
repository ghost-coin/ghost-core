// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2017-2023 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INSIGHT_INSIGHT_H
#define BITCOIN_INSIGHT_INSIGHT_H

#include <threadsafety.h>

#include <consensus/amount.h>
#include <sync.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <utility>
#include <kernel/cs_main.h>

extern bool fAddressIndex;
extern bool fSpentIndex;
extern bool fTimestampIndex;
extern bool fBalancesIndex;

class ChainstateManager;
class CTxOutBase;
class CScript;
class uint256;
class CTxMemPool;
class BlockBalances;
struct CAddressIndexKey;
struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CSpentIndexKey;
struct CSpentIndexValue;

bool ExtractIndexInfo(const CScript *pScript, int &scriptType, std::vector<uint8_t> &hashBytes);
bool ExtractIndexInfo(const CTxOutBase *out, int &scriptType, std::vector<uint8_t> &hashBytes, CAmount &nValue, const CScript *&pScript);

/** Functions for insight block explorer */
bool GetTimestampIndex(ChainstateManager &chainman, const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool GetSpentIndex(ChainstateManager &chainman, const CSpentIndexKey &key, CSpentIndexValue &value, const CTxMemPool *pmempool);
bool GetAddressIndex(ChainstateManager &chainman, const uint256 &addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                     int start = 0, int end = 0);
bool GetAddressUnspent(ChainstateManager &chainman, const uint256 &addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs);
bool GetBlockBalances(ChainstateManager &chainman, const uint256 &block_hash, BlockBalances &balances);

bool getAddressFromIndex(const int &type, const uint256 &hash, std::string &address);

#endif // BITCOIN_INSIGHT_INSIGHT_H
