// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>

#include <common/system.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/solver.h>
#include <serialize.h>
#include <streams.h>
#include <undo.h>
#include <univalue.h>
#include <util/check.h>
#include <util/strencodings.h>

#include <map>
#include <string>
#include <vector>

// Particl
#include <insight/spentindex.h>
#include <blind.h>

UniValue ValueFromAmount(const CAmount amount)
{
    static_assert(COIN > 1);
    int64_t quotient = amount / COIN;
    int64_t remainder = amount % COIN;
    if (amount < 0) {
        quotient = -quotient;
        remainder = -remainder;
    }
    return UniValue(UniValue::VNUM,
            strprintf("%s%d.%08d", amount < 0 ? "-" : "", quotient, remainder));
}

std::string FormatScript(const CScript& script)
{
    std::string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        std::vector<unsigned char> vch;
        if (script.GetOp(it, op, vch)) {
            if (op == OP_0) {
                ret += "0 ";
                continue;
            } else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE) {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            } else if (op >= OP_NOP && op <= OP_NOP10) {
                std::string str(GetOpName(op));
                if (str.substr(0, 3) == std::string("OP_")) {
                    ret += str.substr(3, std::string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0) {
                ret += strprintf("0x%x 0x%x ", HexStr(std::vector<uint8_t>(it2, it - vch.size())),
                                               HexStr(std::vector<uint8_t>(it - vch.size(), it)));
            } else {
                ret += strprintf("0x%x ", HexStr(std::vector<uint8_t>(it2, it)));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(std::vector<uint8_t>(it2, script.end())));
        break;
    }
    return ret.substr(0, ret.empty() ? ret.npos : ret.size() - 1);
}

const std::map<unsigned char, std::string> mapSigHashTypes = {
    {static_cast<unsigned char>(SIGHASH_ALL), std::string("ALL")},
    {static_cast<unsigned char>(SIGHASH_ALL|SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_NONE), std::string("NONE")},
    {static_cast<unsigned char>(SIGHASH_NONE|SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_SINGLE), std::string("SINGLE")},
    {static_cast<unsigned char>(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY")},
};

std::string SighashToStr(unsigned char sighash_type)
{
    const auto& it = mapSigHashTypes.find(sighash_type);
    if (it == mapSigHashTypes.end()) return "";
    return it->second;
}

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<std::vector<unsigned char>::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    std::string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, nullptr)) {
                        const unsigned char chSigHashType = vch.back();
                        const auto it = mapSigHashTypes.find(chSigHashType);
                        if (it != mapSigHashTypes.end()) {
                            strSigHashDecode = "[" + it->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | serializeFlags);
    ssTx << tx;
    return HexStr(ssTx);
}

void ScriptToUniv(const CScript& script, UniValue& out, bool include_hex, bool include_address, const SigningProvider* provider)
{
    CTxDestination address;

    out.pushKV("asm", ScriptToAsmStr(script));
    if (include_address) {
        out.pushKV("desc", InferDescriptor(script, provider ? *provider : DUMMY_SIGNING_PROVIDER)->ToString());
    }
    if (include_hex) {
        out.pushKV("hex", HexStr(script));
    }

    std::vector<std::vector<unsigned char>> solns;
    const TxoutType type{Solver(script, solns)};

    if (include_address && ExtractDestination(script, address) && type != TxoutType::PUBKEY) {
        out.pushKV("address", EncodeDestination(address));
    }
    if (include_address && HasIsCoinstakeOp(script)) {
        CScript scriptCS;
        if (GetCoinstakeScriptPath(script, scriptCS) &&
            ExtractDestination(scriptCS, address)) {
            out.pushKV("stakeaddress", EncodeDestination(address));
        }
    }
    out.pushKV("type", GetTxnOutputType(type));
}

void AddRangeproof(const std::vector<uint8_t> &vRangeproof, UniValue &entry)
{
    entry.pushKV("rangeproof", HexStr(vRangeproof));

    if (vRangeproof.size() > 0) {
        int exponent, mantissa;
        CAmount min_value, max_value;
        if (0 == GetRangeProofInfo(vRangeproof, exponent, mantissa, min_value, max_value)) {
            entry.pushKV("rp_exponent", exponent);
            entry.pushKV("rp_mantissa", mantissa);
            entry.pushKV("rp_min_value", ValueFromAmount(min_value));
            entry.pushKV("rp_max_value", ValueFromAmount(max_value));
        }
    }
}

void OutputToJSON(uint256 &txid, int i,
    const CTxOutBase *baseOut, UniValue &entry)
{
    switch (baseOut->GetType()) {
        case OUTPUT_STANDARD:
            {
            entry.pushKV("type", "standard");
            CTxOutStandard *s = (CTxOutStandard*) baseOut;
            entry.pushKV("value", ValueFromAmount(s->nValue));
            entry.pushKV("valueSat", s->nValue);
            UniValue o(UniValue::VOBJ);
            ScriptToUniv(s->scriptPubKey, o, true, true);
            entry.pushKV("scriptPubKey", o);
            }
            break;
        case OUTPUT_DATA:
            {
            CTxOutData *s = (CTxOutData*) baseOut;
            entry.pushKV("type", "data");
            entry.pushKV("data_hex", HexStr(s->vData));
            CAmount nValue;
            if (s->GetCTFee(nValue)) {
                entry.pushKV("ct_fee", ValueFromAmount(nValue));
            }
            if (s->GetTreasuryFundCfwd(nValue)) {
                entry.pushKV("treasury_fund_cfwd", ValueFromAmount(nValue));
            }
            if (s->GetSmsgFeeRate(nValue)) {
                entry.pushKV("smsgfeerate", ValueFromAmount(nValue));
            }
            uint32_t difficulty;
            if (s->GetSmsgDifficulty(difficulty)) {
                entry.pushKV("smsgdifficulty", strprintf("%08x", difficulty));
            }
            if (s->vData.size() >= 9 && s->vData[4] == DO_VOTE) {
                uint32_t voteToken;
                memcpy(&voteToken, &s->vData[5], 4);
                voteToken = le32toh(voteToken);
                int issue = (int) (voteToken & 0xFFFF);
                int option = (int) (voteToken >> 16) & 0xFFFF;
                entry.pushKV("vote", strprintf("%d, %d", issue, option));
            }
            }
            break;
        case OUTPUT_CT:
            {
            CTxOutCT *s = (CTxOutCT*) baseOut;
            entry.pushKV("type", "blind");
            entry.pushKV("valueCommitment", HexStr(Span<const unsigned char>(s->commitment.data, 33)));
            UniValue o(UniValue::VOBJ);
            ScriptToUniv(s->scriptPubKey, o, true, true);
            entry.pushKV("scriptPubKey", o);
            entry.pushKV("data_hex", HexStr(s->vData));

            AddRangeproof(s->vRangeproof, entry);
            }
            break;
        case OUTPUT_RINGCT:
            {
            CTxOutRingCT *s = (CTxOutRingCT*) baseOut;
            entry.pushKV("type", "anon");
            entry.pushKV("pubkey", HexStr(s->pk));
            entry.pushKV("valueCommitment", HexStr(Span<const unsigned char>(s->commitment.data, 33)));
            entry.pushKV("data_hex", HexStr(s->vData));

            AddRangeproof(s->vRangeproof, entry);
            }
            break;
        default:
            entry.pushKV("type", "unknown");
            break;
    }
}

void TxToUniv(const CTransaction& tx, const uint256& block_hash, UniValue& entry, bool include_hex, int serialize_flags, const CTxUndo* txundo, TxVerbosity verbosity)
{
    CHECK_NONFATAL(verbosity >= TxVerbosity::SHOW_DETAILS);

    uint256 txid = tx.GetHash();
    entry.pushKV("txid", txid.GetHex());
    entry.pushKV("hash", tx.GetWitnessHash().GetHex());
    // Transaction version is actually unsigned in consensus checks, just signed in memory,
    // so cast to unsigned before giving it to the user.
    entry.pushKV("version", static_cast<int64_t>(static_cast<uint32_t>(tx.nVersion)));
    entry.pushKV("size", (int)::GetSerializeSize(tx, PROTOCOL_VERSION));
    entry.pushKV("vsize", (GetTransactionWeight(tx) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR);
    entry.pushKV("weight", GetTransactionWeight(tx));
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin{UniValue::VARR};

    // If available, use Undo data to calculate the fee. Note that txundo == nullptr
    // for coinbase transactions and for transactions where undo data is unavailable.
    const bool have_undo = txundo != nullptr;
    CAmount amt_total_in = 0;
    CAmount amt_total_out = 0;

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase()) {
            in.pushKV("coinbase", HexStr(txin.scriptSig));
        }
        if (txin.IsAnonInput()) {
            in.pushKV("type", "anon");
            uint32_t nSigInputs, nSigRingSize;
            txin.GetAnonInfo(nSigInputs, nSigRingSize);
            in.pushKV("num_inputs", (int)nSigInputs);
            in.pushKV("ring_size", (int)nSigRingSize);

            if (verbosity == TxVerbosity::SHOW_DETAILS_AND_PREVOUT &&
                tx.HasWitness() &&
                !txin.scriptWitness.IsNull() &&
                txin.scriptWitness.stack.size() > 0) {
                const std::vector<uint8_t> &vMI = txin.scriptWitness.stack[0];

                UniValue ring_member_rows(UniValue::VOBJ);
                size_t ofs = 0, nb = 0;
                for (size_t k = 0; k < nSigInputs; ++k) {
                    std::string row_out;
                    for (size_t i = 0; i < nSigRingSize; ++i) {
                        int64_t anon_index;
                        if (0 != part::GetVarInt(vMI, ofs, (uint64_t&)anon_index, nb)) {
                            // throw JSONRPCError(RPC_MISC_ERROR, "Decode anon index failed.");
                            break;
                        }
                        ofs += nb;
                        row_out += row_out.size() == 0 ? strprintf("%lu", anon_index) : strprintf(", %lu", anon_index); // linter fails ? "%lu" : ", %lu"
                    }
                    ring_member_rows.pushKV(strprintf("%d", k), row_out);
                }
                in.pushKV("ring_member_rows", ring_member_rows);
            }
        } else {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig));
            in.pushKV("scriptSig", o);
        }
        if (!txin.scriptData.IsNull()) {
            UniValue scriptdata(UniValue::VARR);
            for (unsigned int j = 0; j < txin.scriptData.stack.size(); j++) {
                std::vector<unsigned char> item = txin.scriptData.stack[j];
                scriptdata.push_back(HexStr(item));
            }
            in.pushKV("scriptdata", scriptdata);
        }
        if (!tx.vin[i].scriptWitness.IsNull()) {
            UniValue txinwitness(UniValue::VARR);
            for (const auto& item : tx.vin[i].scriptWitness.stack) {
                txinwitness.push_back(HexStr(item));
            }
            in.pushKV("txinwitness", txinwitness);
        }
        if (have_undo) {
            const Coin& prev_coin = txundo->vprevout[i];
            const CTxOut& prev_txout = prev_coin.out;

            amt_total_in += prev_txout.nValue;

            if (verbosity == TxVerbosity::SHOW_DETAILS_AND_PREVOUT) {
                UniValue o_script_pub_key(UniValue::VOBJ);
                ScriptToUniv(prev_txout.scriptPubKey, /*out=*/o_script_pub_key, /*include_hex=*/true, /*include_address=*/true);

                UniValue p(UniValue::VOBJ);
                p.pushKV("generated", bool(prev_coin.fCoinBase));
                p.pushKV("height", uint64_t(prev_coin.nHeight));
                p.pushKV("value", ValueFromAmount(prev_txout.nValue));
                p.pushKV("scriptPubKey", o_script_pub_key);
                in.pushKV("prevout", p);
            }
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vpout.size(); i++) {
        UniValue out(UniValue::VOBJ);
        out.pushKV("n", (int64_t)i);
        OutputToJSON(txid, i, tx.vpout[i].get(), out);
        vout.push_back(out);
    }

    if (!tx.IsParticlVersion())
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        out.pushKV("value", ValueFromAmount(txout.nValue));
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptToUniv(txout.scriptPubKey, /*out=*/o, /*include_hex=*/true, /*include_address=*/true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);

        if (have_undo) {
            amt_total_out += txout.nValue;
        }
    }

    entry.pushKV("vout", vout);

    if (have_undo) {
        const CAmount fee = amt_total_in - amt_total_out;
        CHECK_NONFATAL(MoneyRange(fee));
        entry.pushKV("fee", ValueFromAmount(fee));
    }

    if (!block_hash.IsNull()) {
        entry.pushKV("blockhash", block_hash.GetHex());
    }

    if (include_hex) {
        entry.pushKV("hex", EncodeHexTx(tx, serialize_flags)); // The hex-encoded transaction. Used the name "hex" to be consistent with the verbose output of "getrawtransaction".
    }
}

