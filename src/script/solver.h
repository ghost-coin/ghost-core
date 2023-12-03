// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The Solver functions are used by policy and the wallet, but not consensus.

#ifndef BITCOIN_SCRIPT_SOLVER_H
#define BITCOIN_SCRIPT_SOLVER_H

#include <attributes.h>
#include <script/script.h>

#include <string>
#include <optional>
#include <utility>
#include <vector>

class CKeyID;
class CScript;
class CStealthAddress;
struct CExtPubKey;
struct ScriptHash;

class CPubKey;
template <typename C> class Span;

enum class TxoutType {
    NONSTANDARD,
    // 'standard' transaction types:
    PUBKEY,
    PUBKEYHASH,
    SCRIPTHASH,
    MULTISIG,
    NULL_DATA, //!< unspendable OP_RETURN script that carries data
    WITNESS_V0_SCRIPTHASH,
    WITNESS_V0_KEYHASH,
    WITNESS_V1_TAPROOT,
    WITNESS_UNKNOWN, //!< Only for Witness versions not already defined above

    SCRIPTHASH256,
    PUBKEYHASH256,

    TIMELOCKED_SCRIPTHASH,
    TIMELOCKED_SCRIPTHASH256,
    TIMELOCKED_PUBKEYHASH,
    TIMELOCKED_PUBKEYHASH256,
    TIMELOCKED_MULTISIG,
};

/** Get the name of a TxoutType as a string */
std::string GetTxnOutputType(TxoutType t);

constexpr bool IsPushdataOp(opcodetype opcode)
{
    return opcode > OP_FALSE && opcode <= OP_PUSHDATA4;
}

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on
 * the type. For example, for a P2SH script, vSolutionsRet will contain the
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     The script type. TxoutType::NONSTANDARD represents a failed solve.
 */
TxoutType Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet);

/** Generate a P2PK script for the given pubkey. */
CScript GetScriptForRawPubKey(const CPubKey& pubkey);

/** Determine if script is a "multi_a" script. Returns (threshold, keyspans) if so, and nullopt otherwise.
 *  The keyspans refer to bytes in the passed script. */
std::optional<std::pair<int, std::vector<Span<const unsigned char>>>> MatchMultiA(const CScript& script LIFETIMEBOUND);

/** Generate a multisig script. */
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

namespace particl {
TxoutType ToTxoutType(uint8_t type_byte);
uint8_t FromTxoutType(TxoutType type_class);
bool ExtractStakingKeyID(const CScript &scriptPubKey, CKeyID &keyID);
} // namespace particl

#endif // BITCOIN_SCRIPT_SOLVER_H
