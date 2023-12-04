// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/solver.h>
#include <span.h>

#include <algorithm>
#include <cassert>
#include <string>

typedef std::vector<unsigned char> valtype;

std::string GetTxnOutputType(TxoutType t)
{
    switch (t) {
    case TxoutType::NONSTANDARD: return "nonstandard";
    case TxoutType::PUBKEY: return "pubkey";
    case TxoutType::PUBKEYHASH: return "pubkeyhash";
    case TxoutType::SCRIPTHASH: return "scripthash";
    case TxoutType::MULTISIG: return "multisig";
    case TxoutType::NULL_DATA: return "nulldata";
    case TxoutType::WITNESS_V0_KEYHASH: return "witness_v0_keyhash";
    case TxoutType::WITNESS_V0_SCRIPTHASH: return "witness_v0_scripthash";
    case TxoutType::WITNESS_V1_TAPROOT: return "witness_v1_taproot";
    case TxoutType::WITNESS_UNKNOWN: return "witness_unknown";

    case TxoutType::SCRIPTHASH256: return "scripthash256";
    case TxoutType::PUBKEYHASH256: return "pubkeyhash256";
    case TxoutType::TIMELOCKED_SCRIPTHASH: return "timelocked_scripthash";
    case TxoutType::TIMELOCKED_SCRIPTHASH256: return "timelocked_scripthash256";
    case TxoutType::TIMELOCKED_PUBKEYHASH: return "timelocked_pubkeyhash";
    case TxoutType::TIMELOCKED_PUBKEYHASH256: return "timelocked_pubkeyhash256";
    case TxoutType::TIMELOCKED_MULTISIG: return "timelocked_multisig";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

static bool MatchPayToPubkey(const CScript& script, valtype& pubkey)
{
    if (script.size() == CPubKey::SIZE + 2 && script[0] == CPubKey::SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    if (script.size() == CPubKey::COMPRESSED_SIZE + 2 && script[0] == CPubKey::COMPRESSED_SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::COMPRESSED_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    return false;
}

static bool MatchPayToPubkeyHash(const CScript& script, valtype& pubkeyhash)
{
    if (script.size() == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        pubkeyhash = valtype(script.begin () + 3, script.begin() + 23);
        return true;
    }
    return false;
}

static bool MatchPayToPubkeyHash256(const CScript& script, valtype& pubkeyhash)
{
    if (!script.IsPayToPublicKeyHash256()) {
        return false;
    }
    pubkeyhash = valtype(script.begin () + 3, script.begin() + 35);
    return true;
}

/** Test for "small positive integer" script opcodes - OP_1 through OP_16. */
static constexpr bool IsSmallInteger(opcodetype opcode)
{
    return opcode >= OP_1 && opcode <= OP_16;
}

/** Retrieve a minimally-encoded number in range [min,max] from an (opcode, data) pair,
 *  whether it's OP_n or through a push. */
static std::optional<int> GetScriptNumber(opcodetype opcode, valtype data, int min, int max)
{
    int count;
    if (IsSmallInteger(opcode)) {
        count = CScript::DecodeOP_N(opcode);
    } else if (IsPushdataOp(opcode)) {
        if (!CheckMinimalPush(data, opcode)) return {};
        try {
            count = CScriptNum(data, /* fRequireMinimal = */ true).getint();
        } catch (const scriptnum_error&) {
            return {};
        }
    } else {
        return {};
    }
    if (count < min || count > max) return {};
    return count;
}

static bool MatchMultisig(const CScript& script, int& required_sigs, std::vector<valtype>& pubkeys)
{
    opcodetype opcode;
    valtype data;

    CScript::const_iterator it = script.begin();
    if (script.size() < 1 || script.back() != OP_CHECKMULTISIG) return false;

    if (!script.GetOp(it, opcode, data)) return false;
    auto req_sigs = GetScriptNumber(opcode, data, 1, MAX_PUBKEYS_PER_MULTISIG);
    if (!req_sigs) return false;
    required_sigs = *req_sigs;
    while (script.GetOp(it, opcode, data) && CPubKey::ValidSize(data)) {
        pubkeys.emplace_back(std::move(data));
    }
    auto num_keys = GetScriptNumber(opcode, data, required_sigs, MAX_PUBKEYS_PER_MULTISIG);
    if (!num_keys) return false;
    if (pubkeys.size() != static_cast<unsigned long>(*num_keys)) return false;

    return (it + 1 == script.end());
}

std::optional<std::pair<int, std::vector<Span<const unsigned char>>>> MatchMultiA(const CScript& script)
{
    std::vector<Span<const unsigned char>> keyspans;

    // Redundant, but very fast and selective test.
    if (script.size() == 0 || script[0] != 32 || script.back() != OP_NUMEQUAL) return {};

    // Parse keys
    auto it = script.begin();
    while (script.end() - it >= 34) {
        if (*it != 32) return {};
        ++it;
        keyspans.emplace_back(&*it, 32);
        it += 32;
        if (*it != (keyspans.size() == 1 ? OP_CHECKSIG : OP_CHECKSIGADD)) return {};
        ++it;
    }
    if (keyspans.size() == 0 || keyspans.size() > MAX_PUBKEYS_PER_MULTI_A) return {};

    // Parse threshold.
    opcodetype opcode;
    std::vector<unsigned char> data;
    if (!script.GetOp(it, opcode, data)) return {};
    if (it == script.end()) return {};
    if (*it != OP_NUMEQUAL) return {};
    ++it;
    if (it != script.end()) return {};
    auto threshold = GetScriptNumber(opcode, data, 1, (int)keyspans.size());
    if (!threshold) return {};

    // Construct result.
    return std::pair{*threshold, std::move(keyspans)};
}

TxoutType Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet)
{
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash()) {
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return TxoutType::SCRIPTHASH;
    }

    if (scriptPubKey.IsPayToScriptHash256()) {
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+34);
        vSolutionsRet.push_back(hashBytes);
        return TxoutType::SCRIPTHASH256;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_KEYHASH_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TxoutType::WITNESS_V0_KEYHASH;
        }
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_SCRIPTHASH_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TxoutType::WITNESS_V0_SCRIPTHASH;
        }
        if (witnessversion == 1 && witnessprogram.size() == WITNESS_V1_TAPROOT_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TxoutType::WITNESS_V1_TAPROOT;
        }
        if (witnessversion != 0) {
            vSolutionsRet.push_back(std::vector<unsigned char>{(unsigned char)witnessversion});
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TxoutType::WITNESS_UNKNOWN;
        }
        return TxoutType::NONSTANDARD;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        return TxoutType::NULL_DATA;
    }

    std::vector<unsigned char> data;
    if (MatchPayToPubkey(scriptPubKey, data)) {
        vSolutionsRet.push_back(std::move(data));
        return TxoutType::PUBKEY;
    }

    if (MatchPayToPubkeyHash(scriptPubKey, data)) {
        vSolutionsRet.push_back(std::move(data));
        return TxoutType::PUBKEYHASH;
    }

    if (MatchPayToPubkeyHash256(scriptPubKey, data)) {
        vSolutionsRet.push_back(std::move(data));
        return TxoutType::PUBKEYHASH256;
    }

    int required;
    std::vector<std::vector<unsigned char>> keys;
    if (MatchMultisig(scriptPubKey, required, keys)) {
        vSolutionsRet.push_back({static_cast<unsigned char>(required)}); // safe as required is in range 1..20
        vSolutionsRet.insert(vSolutionsRet.end(), keys.begin(), keys.end());
        vSolutionsRet.push_back({static_cast<unsigned char>(keys.size())}); // safe as size is in range 1..20
        return TxoutType::MULTISIG;
    }

    vSolutionsRet.clear();
    return TxoutType::NONSTANDARD;
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << nRequired;
    for (const CPubKey& key : keys)
        script << ToByteVector(key);
    script << keys.size() << OP_CHECKMULTISIG;

    return script;
}

namespace ghost {
TxoutType ToTxoutType(uint8_t type_byte)
{
    switch (type_byte) {
        case 0: return TxoutType::NONSTANDARD;
        case 1: return TxoutType::PUBKEY;
        case 2: return TxoutType::PUBKEYHASH;
        case 3: return TxoutType::SCRIPTHASH;
        case 4: return TxoutType::MULTISIG;
        case 5: return TxoutType::NULL_DATA;
        case 6: return TxoutType::WITNESS_V0_SCRIPTHASH;
        case 7: return TxoutType::WITNESS_V0_KEYHASH;
        case 8: return TxoutType::WITNESS_UNKNOWN;
        case 9: return TxoutType::SCRIPTHASH256;
        case 10: return TxoutType::PUBKEYHASH256;
        case 11: return TxoutType::TIMELOCKED_SCRIPTHASH;
        case 12: return TxoutType::TIMELOCKED_SCRIPTHASH256;
        case 13: return TxoutType::TIMELOCKED_PUBKEYHASH;
        case 14: return TxoutType::TIMELOCKED_PUBKEYHASH256;
        case 15: return TxoutType::TIMELOCKED_MULTISIG;
        case 16: return TxoutType::WITNESS_V1_TAPROOT;
        default: return TxoutType::NONSTANDARD;
    }
}

uint8_t FromTxoutType(TxoutType type_class)
{
    switch (type_class) {
        case TxoutType::NONSTANDARD: return 0;
        case TxoutType::PUBKEY: return 1;
        case TxoutType::PUBKEYHASH: return 2;
        case TxoutType::SCRIPTHASH: return 3;
        case TxoutType::MULTISIG: return 4;
        case TxoutType::NULL_DATA: return 5;
        case TxoutType::WITNESS_V0_SCRIPTHASH: return 6;
        case TxoutType::WITNESS_V0_KEYHASH: return 7;
        case TxoutType::WITNESS_UNKNOWN: return 8;
        case TxoutType::SCRIPTHASH256: return 9;
        case TxoutType::PUBKEYHASH256: return 10;
        case TxoutType::TIMELOCKED_SCRIPTHASH: return 11;
        case TxoutType::TIMELOCKED_SCRIPTHASH256: return 12;
        case TxoutType::TIMELOCKED_PUBKEYHASH: return 13;
        case TxoutType::TIMELOCKED_PUBKEYHASH256: return 14;
        case TxoutType::TIMELOCKED_MULTISIG: return 15;
        case TxoutType::WITNESS_V1_TAPROOT: return 16;
    }
    return 0;
}

bool ExtractStakingKeyID(const CScript &scriptPubKey, CKeyID &keyID)
{
    if (scriptPubKey.IsPayToPublicKeyHash()) {
        keyID = CKeyID(uint160(&scriptPubKey[3], 20));
        return true;
    }
    if (scriptPubKey.IsPayToPublicKeyHash256()) {
        keyID = CKeyID(uint256(&scriptPubKey[3], 32));
        return true;
    }
    if (scriptPubKey.IsPayToPublicKeyHash256_CS() ||
        scriptPubKey.IsPayToScriptHash256_CS() ||
        scriptPubKey.IsPayToScriptHash_CS()) {
        keyID = CKeyID(uint160(&scriptPubKey[5], 20));
        return true;
    }
    return false;
}

} // namespace ghost
