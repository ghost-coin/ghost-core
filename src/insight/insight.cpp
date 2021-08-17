// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <insight/insight.h>
#include <insight/addressindex.h>
#include <insight/spentindex.h>
#include <insight/timestampindex.h>
#include <validation.h>
#include <txdb.h>
#include <txmempool.h>
#include <uint256.h>
#include <script/script.h>
#include <script/standard.h>
#include <key_io.h>

#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <util/system.h>

bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;
bool fBalancesIndex = false;

bool ExtractIndexInfo(const CScript *pScript, int &scriptType, std::vector<uint8_t> &hashBytes)
{
    CScript tmpScript;
    if (HasIsCoinstakeOp(*pScript)
        && GetNonCoinstakeScriptPath(*pScript, tmpScript)) {
        return ExtractIndexInfo(&tmpScript, scriptType, hashBytes);
    }

    int witnessversion = 0;
    std::vector<unsigned char> witnessprogram;
    scriptType = ADDR_INDT_UNKNOWN;
    if (pScript->IsPayToPublicKeyHash()) {
        hashBytes.assign(pScript->begin()+3, pScript->begin()+23);
        scriptType = ADDR_INDT_PUBKEY_ADDRESS;
    } else
    if (pScript->IsPayToScriptHash()) {
        hashBytes.assign(pScript->begin()+2, pScript->begin()+22);
        scriptType = ADDR_INDT_SCRIPT_ADDRESS;
    } else
    if (pScript->IsPayToPublicKeyHash256()) {
        hashBytes.assign(pScript->begin()+3, pScript->begin()+35);
        scriptType = ADDR_INDT_PUBKEY_ADDRESS_256;
    } else
    if (pScript->IsPayToScriptHash256()) {
        hashBytes.assign(pScript->begin()+2, pScript->begin()+34);
        scriptType = ADDR_INDT_SCRIPT_ADDRESS_256;
    } else
    if (pScript->IsPayToWitnessScriptHash()) {
        hashBytes.assign(pScript->begin()+2, pScript->begin()+34);
        scriptType = ADDR_INDT_WITNESS_V0_SCRIPTHASH;
    } else
    if (pScript->IsWitnessProgram(witnessversion, witnessprogram)) {
        hashBytes.assign(witnessprogram.begin(), witnessprogram.begin() + witnessprogram.size());
        scriptType = ADDR_INDT_WITNESS_V0_KEYHASH;
    }

    return true;
};

bool ExtractIndexInfo(const CTxOutBase *out, int &scriptType, std::vector<uint8_t> &hashBytes, CAmount &nValue, const CScript *&pScript)
{
    if (!(pScript = out->GetPScriptPubKey())) {
        return error("%s: Expected script pointer.", __func__);
    }

    nValue = out->IsType(OUTPUT_STANDARD) ? out->GetValue() : 0;

    ExtractIndexInfo(pScript, scriptType, hashBytes);

    return true;
};

static bool HashOnchainActive(ChainstateManager &chainman, const uint256 &hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    CBlockIndex* pblockindex = chainman.BlockIndex()[hash];

    if (!chainman.ActiveChain().Contains(pblockindex)) {
        return false;
    }

    return true;
};

bool GetTimestampIndex(ChainstateManager &chainman, const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes)
{
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};
    if (!fTimestampIndex) {
        return error("Timestamp index not enabled");
    }
    if (!pblocktree->ReadTimestampIndex(high, low, hashes)) {
        return error("Unable to get hashes for timestamps");
    }

    if (fActiveOnly) {
        for (auto it = hashes.begin(); it != hashes.end(); ) {
            if (!HashOnchainActive(chainman, it->first)) {
                it = hashes.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
};

bool GetSpentIndex(ChainstateManager &chainman, const CSpentIndexKey &key, CSpentIndexValue &value, const CTxMemPool *pmempool)
{
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};
    if (!fSpentIndex) {
        return false;
    }
    if (pmempool && pmempool->getSpentIndex(key, value)) {
        return true;
    }
    if (!pblocktree->ReadSpentIndex(key, value)) {
        return false;
    }

    return true;
};

bool GetAddressIndex(ChainstateManager &chainman, const uint256 &addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};
    if (!fAddressIndex) {
        return error("Address index not enabled");
    }
    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end)) {
        return error("Unable to get txids for address");
    }

    return true;
};

bool GetAddressUnspent(ChainstateManager &chainman, const uint256 &addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};
    if (!fAddressIndex) {
        return error("Address index not enabled");
    }
    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs)) {
        return error("Unable to get txids for address");
    }

    return true;
};

bool GetBlockBalances(ChainstateManager &chainman, const uint256 &block_hash, BlockBalances &balances)
{
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};
    if (!fBalancesIndex) {
        return error("Balances index not enabled");
    }
    if (!pblocktree->ReadBlockBalancesIndex(block_hash, balances)) {
        return error("Unable to get balances for block %s", block_hash.ToString());
    }

    return true;
};

bool getAddressFromIndex(const int &type, const uint256 &hash, std::string &address)
{
    if (type == ADDR_INDT_SCRIPT_ADDRESS) {
        address = EncodeDestination(ScriptHash(uint160(hash.begin(), 20)));
    } else if (type == ADDR_INDT_PUBKEY_ADDRESS) {
        address = EncodeDestination(PKHash(uint160(hash.begin(), 20)));
    } else if (type == ADDR_INDT_SCRIPT_ADDRESS_256) {
        address = EncodeDestination(CScriptID256(hash));
    } else if (type == ADDR_INDT_PUBKEY_ADDRESS_256) {
        address = EncodeDestination(CKeyID256(hash));
    } else if (type == ADDR_INDT_WITNESS_V0_KEYHASH) {
        address = EncodeDestination(WitnessV0KeyHash(uint160(hash.begin(), 20)));
    } else if (type == ADDR_INDT_WITNESS_V0_SCRIPTHASH) {
        address = EncodeDestination(WitnessV0ScriptHash(hash));
    } else {
        return false;
    }
    return true;
}
