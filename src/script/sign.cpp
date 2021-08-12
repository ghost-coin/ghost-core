// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/sign.h>

#include <key.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <uint256.h>
#include <util/vector.h>

extern bool fParticlMode;
typedef std::vector<unsigned char> valtype;

MutableTransactionSignatureCreator::MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const std::vector<uint8_t>& amountIn, int nHashTypeIn)
    : txTo(txToIn), nIn(nInIn), nHashType(nHashTypeIn), amount(amountIn), checker(txTo, nIn, amountIn, MissingDataBehavior::FAIL),
      m_txdata(nullptr)
{
}

MutableTransactionSignatureCreator::MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const std::vector<uint8_t>& amountIn, const PrecomputedTransactionData* txdata, int nHashTypeIn)
    : txTo(txToIn), nIn(nInIn), nHashType(nHashTypeIn), amount(amountIn),
      checker(txdata ? MutableTransactionSignatureChecker(txTo, nIn, amountIn, *txdata, MissingDataBehavior::FAIL) :
          MutableTransactionSignatureChecker(txTo, nIn, amountIn, MissingDataBehavior::FAIL)),
      m_txdata(txdata)
{
}

MutableTransactionSignatureCreator::MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, int nHashTypeIn)
    : txTo(txToIn), nIn(nInIn), nHashType(nHashTypeIn), amount(part::VectorFromAmount(amountIn)), checker(txTo, nIn, amount, MissingDataBehavior::FAIL),
      m_txdata(nullptr)
{
}

MutableTransactionSignatureCreator::MutableTransactionSignatureCreator(const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, const PrecomputedTransactionData* txdata, int nHashTypeIn)
    : txTo(txToIn), nIn(nInIn), nHashType(nHashTypeIn), amount(part::VectorFromAmount(amountIn)),
      checker(txdata ? MutableTransactionSignatureChecker(txTo, nIn, amount, *txdata, MissingDataBehavior::FAIL) :
          MutableTransactionSignatureChecker(txTo, nIn, amount, MissingDataBehavior::FAIL)),
      m_txdata(txdata)
{
}

bool MutableTransactionSignatureCreator::CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& address, const CScript& scriptCode, SigVersion sigversion) const
{
    assert(sigversion == SigVersion::BASE || sigversion == SigVersion::WITNESS_V0);

    CKey key;
    if (!provider.GetKey(address, key))
        return false;

    // Signing with uncompressed keys is disabled in witness scripts
    if (sigversion == SigVersion::WITNESS_V0 && !key.IsCompressed())
        return false;

    // Signing for witness scripts needs the amount.
    if (sigversion == SigVersion::WITNESS_V0 && amount.size() < 8) return false;
    // BASE/WITNESS_V0 signatures don't support explicit SIGHASH_DEFAULT, use SIGHASH_ALL instead.
    const int hashtype = nHashType == SIGHASH_DEFAULT ? SIGHASH_ALL : nHashType;

    uint256 hash = SignatureHash(scriptCode, *txTo, nIn, hashtype, amount, sigversion, m_txdata);
    if (!key.Sign(hash, vchSig))
        return false;
    vchSig.push_back((unsigned char)hashtype);
    return true;
}

bool MutableTransactionSignatureCreator::CreateSchnorrSig(const SigningProvider& provider, std::vector<unsigned char>& sig, const XOnlyPubKey& pubkey, const uint256* leaf_hash, const uint256* merkle_root, SigVersion sigversion) const
{
    assert(sigversion == SigVersion::TAPROOT || sigversion == SigVersion::TAPSCRIPT);

    CKey key;
    {
        // For now, use the old full pubkey-based key derivation logic. As it indexed by
        // Hash160(full pubkey), we need to try both a version prefixed with 0x02, and one
        // with 0x03.
        unsigned char b[33] = {0x02};
        std::copy(pubkey.begin(), pubkey.end(), b + 1);
        CPubKey fullpubkey;
        fullpubkey.Set(b, b + 33);
        CKeyID keyid = fullpubkey.GetID();
        if (!provider.GetKey(keyid, key)) {
            b[0] = 0x03;
            fullpubkey.Set(b, b + 33);
            CKeyID keyid = fullpubkey.GetID();
            if (!provider.GetKey(keyid, key)) return false;
        }
    }

    // BIP341/BIP342 signing needs lots of precomputed transaction data. While some
    // (non-SIGHASH_DEFAULT) sighash modes exist that can work with just some subset
    // of data present, for now, only support signing when everything is provided.
    if (!m_txdata || !m_txdata->m_bip341_taproot_ready || !m_txdata->m_spent_outputs_ready) return false;

    ScriptExecutionData execdata;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false; // Only support annex-less signing for now.
    if (sigversion == SigVersion::TAPSCRIPT) {
        execdata.m_codeseparator_pos_init = true;
        execdata.m_codeseparator_pos = 0xFFFFFFFF; // Only support non-OP_CODESEPARATOR BIP342 signing for now.
        if (!leaf_hash) return false; // BIP342 signing needs leaf hash.
        execdata.m_tapleaf_hash_init = true;
        execdata.m_tapleaf_hash = *leaf_hash;
    }
    uint256 hash;
    if (!SignatureHashSchnorr(hash, execdata, *txTo, nIn, nHashType, sigversion, *m_txdata, MissingDataBehavior::FAIL)) return false;
    sig.resize(64);
    if (!key.SignSchnorr(hash, sig, merkle_root, nullptr)) return false;
    if (nHashType) sig.push_back(nHashType);
    return true;
}

static bool GetCScript(const SigningProvider& provider, const SignatureData& sigdata, const CScriptID& scriptid, CScript& script)
{
    if (provider.GetCScript(scriptid, script)) {
        return true;
    }
    // Look for scripts in SignatureData
    if (CScriptID(sigdata.redeem_script) == scriptid) {
        script = sigdata.redeem_script;
        return true;
    } else if (CScriptID(sigdata.witness_script) == scriptid) {
        script = sigdata.witness_script;
        return true;
    }
    return false;
}

static bool GetPubKey(const SigningProvider& provider, const SignatureData& sigdata, const CKeyID& address, CPubKey& pubkey)
{
    // Look for pubkey in all partial sigs
    const auto it = sigdata.signatures.find(address);
    if (it != sigdata.signatures.end()) {
        pubkey = it->second.first;
        return true;
    }
    // Look for pubkey in pubkey list
    const auto& pk_it = sigdata.misc_pubkeys.find(address);
    if (pk_it != sigdata.misc_pubkeys.end()) {
        pubkey = pk_it->second.first;
        return true;
    }
    // Query the underlying provider
    return provider.GetPubKey(address, pubkey);
}

static bool CreateSig(const BaseSignatureCreator& creator, SignatureData& sigdata, const SigningProvider& provider, std::vector<unsigned char>& sig_out, const CPubKey& pubkey, const CScript& scriptcode, SigVersion sigversion)
{
    CKeyID keyid = pubkey.GetID();
    const auto it = sigdata.signatures.find(keyid);
    if (it != sigdata.signatures.end()) {
        sig_out = it->second.second;
        return true;
    }
    KeyOriginInfo info;
    if (provider.GetKeyOrigin(keyid, info)) {
        sigdata.misc_pubkeys.emplace(keyid, std::make_pair(pubkey, std::move(info)));
    }
    if (creator.CreateSig(provider, sig_out, keyid, scriptcode, sigversion)) {
        auto i = sigdata.signatures.emplace(keyid, SigPair(pubkey, sig_out));
        assert(i.second);
        return true;
    }
    // Could not make signature or signature not found, add keyid to missing
    sigdata.missing_sigs.push_back(keyid);
    return false;
}

static bool CreateTaprootScriptSig(const BaseSignatureCreator& creator, SignatureData& sigdata, const SigningProvider& provider, std::vector<unsigned char>& sig_out, const XOnlyPubKey& pubkey, const uint256& leaf_hash, SigVersion sigversion)
{
    auto lookup_key = std::make_pair(pubkey, leaf_hash);
    auto it = sigdata.taproot_script_sigs.find(lookup_key);
    if (it != sigdata.taproot_script_sigs.end()) {
        sig_out = it->second;
    }
    if (creator.CreateSchnorrSig(provider, sig_out, pubkey, &leaf_hash, nullptr, sigversion)) {
        sigdata.taproot_script_sigs[lookup_key] = sig_out;
        return true;
    }
    return false;
}

static bool SignTaprootScript(const SigningProvider& provider, const BaseSignatureCreator& creator, SignatureData& sigdata, int leaf_version, const CScript& script, std::vector<valtype>& result)
{
    // Only BIP342 tapscript signing is supported for now.
    if (leaf_version != TAPROOT_LEAF_TAPSCRIPT) return false;
    SigVersion sigversion = SigVersion::TAPSCRIPT;

    uint256 leaf_hash = (CHashWriter(HASHER_TAPLEAF) << uint8_t(leaf_version) << script).GetSHA256();

    // <xonly pubkey> OP_CHECKSIG
    if (script.size() == 34 && script[33] == OP_CHECKSIG && script[0] == 0x20) {
        XOnlyPubKey pubkey(MakeSpan(script).subspan(1, 32));
        std::vector<unsigned char> sig;
        if (CreateTaprootScriptSig(creator, sigdata, provider, sig, pubkey, leaf_hash, sigversion)) {
            result = Vector(std::move(sig));
            return true;
        }
    }

    return false;
}

static bool SignTaproot(const SigningProvider& provider, const BaseSignatureCreator& creator, const WitnessV1Taproot& output, SignatureData& sigdata, std::vector<valtype>& result)
{
    TaprootSpendData spenddata;

    // Gather information about this output.
    if (provider.GetTaprootSpendData(output, spenddata)) {
        sigdata.tr_spenddata.Merge(spenddata);
    }

    // Try key path spending.
    {
        std::vector<unsigned char> sig;
        if (sigdata.taproot_key_path_sig.size() == 0) {
            if (creator.CreateSchnorrSig(provider, sig, spenddata.internal_key, nullptr, &spenddata.merkle_root, SigVersion::TAPROOT)) {
                sigdata.taproot_key_path_sig = sig;
            }
        }
        if (sigdata.taproot_key_path_sig.size()) {
            result = Vector(sigdata.taproot_key_path_sig);
            return true;
        }
    }

    // Try script path spending.
    std::vector<std::vector<unsigned char>> smallest_result_stack;
    for (const auto& [key, control_blocks] : sigdata.tr_spenddata.scripts) {
        const auto& [script, leaf_ver] = key;
        std::vector<std::vector<unsigned char>> result_stack;
        if (SignTaprootScript(provider, creator, sigdata, leaf_ver, script, result_stack)) {
            result_stack.emplace_back(std::begin(script), std::end(script)); // Push the script
            result_stack.push_back(*control_blocks.begin()); // Push the smallest control block
            if (smallest_result_stack.size() == 0 ||
                GetSerializeSize(result_stack, PROTOCOL_VERSION) < GetSerializeSize(smallest_result_stack, PROTOCOL_VERSION)) {
                smallest_result_stack = std::move(result_stack);
            }
        }
    }
    if (smallest_result_stack.size() != 0) {
        result = std::move(smallest_result_stack);
        return true;
    }

    return false;
}

/**
 * Sign scriptPubKey using signature made with creator.
 * Signatures are returned in scriptSigRet (or returns false if scriptPubKey can't be signed),
 * unless whichTypeRet is TxoutType::SCRIPTHASH, in which case scriptSigRet is the redemption script.
 * Returns false if scriptPubKey could not be completely satisfied.
 */
static bool SignStep(const SigningProvider& provider, const BaseSignatureCreator& creator, const CScript& scriptPubKey,
                     std::vector<valtype>& ret, TxoutType& whichTypeRet, SigVersion sigversion, SignatureData& sigdata)
{
    CScript scriptRet;
    uint160 h160;
    ret.clear();
    std::vector<unsigned char> sig;

    std::vector<valtype> vSolutions;

    if (HasIsCoinstakeOp(scriptPubKey)) {
        CScript scriptPath;
        if (!GetColdStakeScriptPath(creator.IsCoinStake(), scriptPubKey, scriptPath)) {
            return false;
        }
        whichTypeRet = Solver(scriptPath, vSolutions);
    } else {
        whichTypeRet = Solver(scriptPubKey, vSolutions);
    }

    switch (whichTypeRet) {
    case TxoutType::NONSTANDARD:
    case TxoutType::NULL_DATA:
    case TxoutType::WITNESS_UNKNOWN:
    case TxoutType::TIMELOCKED_SCRIPTHASH:
    case TxoutType::TIMELOCKED_SCRIPTHASH256:
    case TxoutType::TIMELOCKED_PUBKEYHASH:
    case TxoutType::TIMELOCKED_PUBKEYHASH256:
    case TxoutType::TIMELOCKED_MULTISIG:
        return false;
    case TxoutType::PUBKEY:
        if (!CreateSig(creator, sigdata, provider, sig, CPubKey(vSolutions[0]), scriptPubKey, sigversion)) return false;
        ret.push_back(std::move(sig));
        return true;
    case TxoutType::PUBKEYHASH:
    case TxoutType::PUBKEYHASH256: {
        CKeyID keyID = vSolutions[0].size() == 32 ? CKeyID(uint256(vSolutions[0])) : CKeyID(uint160(vSolutions[0]));
        CPubKey pubkey;
        if (!GetPubKey(provider, sigdata, keyID, pubkey)) {
            // Pubkey could not be found, add to missing
            sigdata.missing_pubkeys.push_back(keyID);
            return false;
        }
        if (!CreateSig(creator, sigdata, provider, sig, pubkey, scriptPubKey, sigversion)) return false;
        ret.push_back(std::move(sig));
        ret.push_back(ToByteVector(pubkey));
        return true;
    }
    case TxoutType::SCRIPTHASH:
    case TxoutType::SCRIPTHASH256: {
        CScriptID idScript;
        if (vSolutions[0].size() == 20) {
            idScript = CScriptID(uint160(vSolutions[0]));
        } else
        if (vSolutions[0].size() == 32) {
            idScript.Set(uint256(vSolutions[0]));
        } else {
            return false;
        }
        if (GetCScript(provider, sigdata, idScript, scriptRet)) {
            ret.push_back(std::vector<unsigned char>(scriptRet.begin(), scriptRet.end()));
            return true;
        }
        // Could not find redeemScript, add to missing
        sigdata.missing_redeem_script = h160;
        }
        return false;

    case TxoutType::MULTISIG: {
        size_t required = vSolutions.front()[0];
        ret.push_back(valtype()); // workaround CHECKMULTISIG bug
        for (size_t i = 1; i < vSolutions.size() - 1; ++i) {
            CPubKey pubkey = CPubKey(vSolutions[i]);
            // We need to always call CreateSig in order to fill sigdata with all
            // possible signatures that we can create. This will allow further PSBT
            // processing to work as it needs all possible signature and pubkey pairs
            if (CreateSig(creator, sigdata, provider, sig, pubkey, scriptPubKey, sigversion)) {
                if (ret.size() < required + 1) {
                    ret.push_back(std::move(sig));
                }
            }
        }
        bool ok = ret.size() == required + 1;
        for (size_t i = 0; i + ret.size() < required + 1; ++i) {
            ret.push_back(valtype());
        }
        return ok;
    }
    case TxoutType::WITNESS_V0_KEYHASH:
        ret.push_back(vSolutions[0]);
        return true;

    case TxoutType::WITNESS_V0_SCRIPTHASH:
        CRIPEMD160().Write(vSolutions[0].data(), vSolutions[0].size()).Finalize(h160.begin());
        if (GetCScript(provider, sigdata, CScriptID{h160}, scriptRet)) {
            ret.push_back(std::vector<unsigned char>(scriptRet.begin(), scriptRet.end()));
            return true;
        }
        // Could not find witnessScript, add to missing
        sigdata.missing_witness_script = uint256(vSolutions[0]);
        return false;

    case TxoutType::WITNESS_V1_TAPROOT:
        return SignTaproot(provider, creator, WitnessV1Taproot(XOnlyPubKey{vSolutions[0]}), sigdata, ret);
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

static CScript PushAll(const std::vector<valtype>& values)
{
    CScript result;
    for (const valtype& v : values) {
        if (v.size() == 0) {
            result << OP_0;
        } else if (v.size() == 1 && v[0] >= 1 && v[0] <= 16) {
            result << CScript::EncodeOP_N(v[0]);
        } else if (v.size() == 1 && v[0] == 0x81) {
            result << OP_1NEGATE;
        } else {
            result << v;
        }
    }
    return result;
}

bool ProduceSignature(const SigningProvider& provider, const BaseSignatureCreator& creator, const CScript& fromPubKey, SignatureData& sigdata)
{
    if (sigdata.complete) return true;

    std::vector<valtype> result;
    TxoutType whichType;
    bool solved = SignStep(provider, creator, fromPubKey, result, whichType, SigVersion::BASE, sigdata);
    bool P2SH = false;
    CScript subscript;

    bool fIsP2SH = creator.IsParticlVersion()
        ? (whichType == TxoutType::SCRIPTHASH || whichType == TxoutType::SCRIPTHASH256)
        : whichType == TxoutType::SCRIPTHASH;
    if (solved && fIsP2SH)
    {
        // Solver returns the subscript that needs to be evaluated;
        // the final scriptSig is the signatures from that
        // and then the serialized subscript:
        subscript = CScript(result[0].begin(), result[0].end());
        sigdata.redeem_script = subscript;
        solved = solved && SignStep(provider, creator, subscript, result, whichType, SigVersion::BASE, sigdata) && whichType != TxoutType::SCRIPTHASH
            && whichType != TxoutType::SCRIPTHASH256;
        P2SH = true;
    }

    if (solved && whichType == TxoutType::WITNESS_V0_KEYHASH)
    {
        CScript witnessscript;
        witnessscript << OP_DUP << OP_HASH160 << ToByteVector(result[0]) << OP_EQUALVERIFY << OP_CHECKSIG;
        TxoutType subType;
        solved = solved && SignStep(provider, creator, witnessscript, result, subType, SigVersion::WITNESS_V0, sigdata);
        sigdata.scriptWitness.stack = result;
        sigdata.witness = true;
        result.clear();
    }
    else if (solved && whichType == TxoutType::WITNESS_V0_SCRIPTHASH)
    {
        CScript witnessscript(result[0].begin(), result[0].end());
        sigdata.witness_script = witnessscript;
        TxoutType subType;
        solved = solved && SignStep(provider, creator, witnessscript, result, subType, SigVersion::WITNESS_V0, sigdata) && subType != TxoutType::SCRIPTHASH && subType != TxoutType::WITNESS_V0_SCRIPTHASH && subType != TxoutType::WITNESS_V0_KEYHASH;
        result.push_back(std::vector<unsigned char>(witnessscript.begin(), witnessscript.end()));
        sigdata.scriptWitness.stack = result;
        sigdata.witness = true;
        result.clear();
    } else if (whichType == TxoutType::WITNESS_V1_TAPROOT && !P2SH) {
        sigdata.witness = true;
        if (solved) {
            sigdata.scriptWitness.stack = std::move(result);
        }
        result.clear();
    } else if (solved && whichType == TxoutType::WITNESS_UNKNOWN) {
        sigdata.witness = true;
    }

    if (!sigdata.witness) sigdata.scriptWitness.stack.clear();
    if (P2SH) {
        result.push_back(std::vector<unsigned char>(subscript.begin(), subscript.end()));
    }

    if (creator.IsParticlVersion()) {
        if (!sigdata.witness) {
            sigdata.scriptWitness.stack = result;
        }
    } else  {
        sigdata.scriptSig = PushAll(result);
    }

    // Test solution
    sigdata.complete = solved && VerifyScript(sigdata.scriptSig, fromPubKey, &sigdata.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, creator.Checker());
    return sigdata.complete;
}

namespace {
class SignatureExtractorChecker final : public DeferringSignatureChecker
{
private:
    SignatureData& sigdata;

public:
    SignatureExtractorChecker(SignatureData& sigdata, BaseSignatureChecker& checker) : DeferringSignatureChecker(checker), sigdata(sigdata) {}

    bool CheckECDSASignature(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const override
    {
        if (m_checker.CheckECDSASignature(scriptSig, vchPubKey, scriptCode, sigversion)) {
            CPubKey pubkey(vchPubKey);
            sigdata.signatures.emplace(pubkey.GetID(), SigPair(pubkey, scriptSig));
            return true;
        }
        return false;
    }

    bool is_particl_tx = false;
    bool IsParticlVersion() const override { return is_particl_tx; }
};

struct Stacks
{
    std::vector<valtype> script;
    std::vector<valtype> witness;

    Stacks() = delete;
    Stacks(const Stacks&) = delete;
    explicit Stacks(const SignatureData& data) : witness(data.scriptWitness.stack) {
        EvalScript(script, data.scriptSig, SCRIPT_VERIFY_STRICTENC, BaseSignatureChecker(), SigVersion::BASE);
    }
};
}

// Extracts signatures and scripts from incomplete scriptSigs. Please do not extend this, use PSBT instead
SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn, const CTxOut& txout)
{
    std::vector<uint8_t> amount(8);
    part::SetAmount(amount, txout.nValue);
    return DataFromTransaction(tx, nIn, amount,  txout.scriptPubKey);
};
SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn, const std::vector<uint8_t> &amount, const CScript &scriptPubKey)
{
    SignatureData data;
    assert(tx.vin.size() > nIn);
    data.scriptSig = tx.vin[nIn].scriptSig;
    data.scriptWitness = tx.vin[nIn].scriptWitness;
    Stacks stack(data);

    // Get signatures
    MutableTransactionSignatureChecker tx_checker(&tx, nIn, amount, MissingDataBehavior::FAIL);
    SignatureExtractorChecker extractor_checker(data, tx_checker);
    extractor_checker.is_particl_tx = tx.IsParticlVersion();
    if (VerifyScript(data.scriptSig, scriptPubKey, &data.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, extractor_checker)) {
        data.complete = true;
        return data;
    }

    // Get scripts
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType script_type;
    if (HasIsCoinstakeOp(scriptPubKey)) {
        CScript script;
        GetColdStakeScriptPath(tx_checker.IsCoinStake(), scriptPubKey, script);
        script_type = Solver(script, solutions);
    } else
    script_type = Solver(scriptPubKey, solutions);
    SigVersion sigversion = SigVersion::BASE;
    CScript next_script = scriptPubKey;

    if (tx.IsParticlVersion()) {
        if (script_type == TxoutType::PUBKEY || script_type == TxoutType::PUBKEYHASH || script_type == TxoutType::PUBKEYHASH256)
            script_type = TxoutType::WITNESS_V0_KEYHASH;
        else
        if (script_type == TxoutType::SCRIPTHASH || script_type == TxoutType::SCRIPTHASH256)
            script_type = TxoutType::WITNESS_V0_SCRIPTHASH;
    }

    if (script_type == TxoutType::SCRIPTHASH && !stack.script.empty() && !stack.script.back().empty()) {
        // Get the redeemScript
        CScript redeem_script(stack.script.back().begin(), stack.script.back().end());
        data.redeem_script = redeem_script;
        next_script = std::move(redeem_script);

        // Get redeemScript type
        script_type = Solver(next_script, solutions);
        stack.script.pop_back();
    }
    if (script_type == TxoutType::WITNESS_V0_SCRIPTHASH && !stack.witness.empty() && !stack.witness.back().empty()) {
        // Get the witnessScript
        CScript witness_script(stack.witness.back().begin(), stack.witness.back().end());
        data.witness_script = witness_script;
        next_script = std::move(witness_script);

        // Get witnessScript type
        script_type = Solver(next_script, solutions);
        stack.witness.pop_back();
        stack.script = std::move(stack.witness);
        stack.witness.clear();
        sigversion = SigVersion::WITNESS_V0;
    }
    if (script_type == TxoutType::MULTISIG && !stack.script.empty()) {
        // Build a map of pubkey -> signature by matching sigs to pubkeys:
        assert(solutions.size() > 1);
        unsigned int num_pubkeys = solutions.size()-2;
        unsigned int last_success_key = 0;
        for (const valtype& sig : stack.script) {
            for (unsigned int i = last_success_key; i < num_pubkeys; ++i) {
                const valtype& pubkey = solutions[i+1];
                // We either have a signature for this pubkey, or we have found a signature and it is valid
                if (data.signatures.count(CPubKey(pubkey).GetID()) || extractor_checker.CheckECDSASignature(sig, pubkey, next_script, sigversion)) {
                    last_success_key = i + 1;
                    break;
                }
            }
        }
    }

    return data;
}

void UpdateInput(CTxIn& input, const SignatureData& data)
{
    input.scriptSig = data.scriptSig;
    input.scriptWitness = data.scriptWitness;
}

void SignatureData::MergeSignatureData(SignatureData sigdata)
{
    if (complete) return;
    if (sigdata.complete) {
        *this = std::move(sigdata);
        return;
    }
    if (redeem_script.empty() && !sigdata.redeem_script.empty()) {
        redeem_script = sigdata.redeem_script;
    }
    if (witness_script.empty() && !sigdata.witness_script.empty()) {
        witness_script = sigdata.witness_script;
    }
    signatures.insert(std::make_move_iterator(sigdata.signatures.begin()), std::make_move_iterator(sigdata.signatures.end()));
}

bool SignSignature(const SigningProvider &provider, const CScript& fromPubKey, CMutableTransaction& txTo, unsigned int nIn, const std::vector<uint8_t>& amount, int nHashType)
{
    assert(nIn < txTo.vin.size());

    MutableTransactionSignatureCreator creator(&txTo, nIn, amount, nHashType);

    SignatureData sigdata;
    bool ret = ProduceSignature(provider, creator, fromPubKey, sigdata);
    UpdateInput(txTo.vin.at(nIn), sigdata);
    return ret;
}

bool SignSignature(const SigningProvider &provider, const CScript& fromPubKey, CMutableTransaction& txTo, unsigned int nIn, const CAmount amount, int nHashType)
{
    std::vector<uint8_t> vamount(8);
    part::SetAmount(vamount, amount);
    return SignSignature(provider, fromPubKey, txTo, nIn, vamount, nHashType);
}

bool SignSignature(const SigningProvider &provider, const CTransaction& txFrom, CMutableTransaction& txTo, unsigned int nIn, int nHashType)
{
    assert(nIn < txTo.vin.size());
    const CTxIn& txin = txTo.vin[nIn];

    if (txTo.IsParticlVersion()) {
        assert(txin.prevout.n < txFrom.vpout.size());
        CScript scriptPubKey;
        std::vector<uint8_t> vamount;
        if (!txFrom.vpout[txin.prevout.n]->PutValue(vamount)
            || !txFrom.vpout[txin.prevout.n]->GetScriptPubKey(scriptPubKey))
            return false;
        return SignSignature(provider, scriptPubKey, txTo, nIn, vamount, nHashType);
    }

    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    return SignSignature(provider, txout.scriptPubKey, txTo, nIn, txout.nValue, nHashType);
}

namespace {
/** Dummy signature checker which accepts all signatures. */
class DummySignatureChecker : public BaseSignatureChecker
{
public:
    DummySignatureChecker() {}
    bool CheckECDSASignature(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const override { return true; }
    bool CheckSchnorrSignature(Span<const unsigned char> sig, Span<const unsigned char> pubkey, SigVersion sigversion, const ScriptExecutionData& execdata, ScriptError* serror) const override { return true; }
};
const DummySignatureChecker DUMMY_CHECKER;

class DummySignatureCreator : public BaseSignatureCreator {
private:
    char m_r_len = 32;
    char m_s_len = 32;
public:
    DummySignatureCreator(char r_len, char s_len) : m_r_len(r_len), m_s_len(s_len) {}
    const BaseSignatureChecker& Checker() const override { return DUMMY_CHECKER; }
    bool CreateSig(const SigningProvider& provider, std::vector<unsigned char>& vchSig, const CKeyID& keyid, const CScript& scriptCode, SigVersion sigversion) const override
    {
        // Create a dummy signature that is a valid DER-encoding
        vchSig.assign(m_r_len + m_s_len + 7, '\000');
        vchSig[0] = 0x30;
        vchSig[1] = m_r_len + m_s_len + 4;
        vchSig[2] = 0x02;
        vchSig[3] = m_r_len;
        vchSig[4] = 0x01;
        vchSig[4 + m_r_len] = 0x02;
        vchSig[5 + m_r_len] = m_s_len;
        vchSig[6 + m_r_len] = 0x01;
        vchSig[6 + m_r_len + m_s_len] = SIGHASH_ALL;
        return true;
    }
    bool CreateSchnorrSig(const SigningProvider& provider, std::vector<unsigned char>& sig, const XOnlyPubKey& pubkey, const uint256* leaf_hash, const uint256* tweak, SigVersion sigversion) const override
    {
        sig.assign(64, '\000');
        return true;
    }
};

class DummySignatureCheckerParticl : public DummySignatureChecker
{
// IsParticlVersion() must return true to skip stack evaluation
public:
    DummySignatureCheckerParticl() : DummySignatureChecker() {}
    bool IsParticlVersion() const override { return true; }
};
const DummySignatureCheckerParticl DUMMY_CHECKER_PARTICL;

class DummySignatureCreatorParticl : public DummySignatureCreator {
public:
    DummySignatureCreatorParticl() : DummySignatureCreator(33, 32) {}
    const BaseSignatureChecker& Checker() const override { return DUMMY_CHECKER_PARTICL; }
    bool IsParticlVersion() const override { return true; }
};

template<typename M, typename K, typename V>
bool LookupHelper(const M& map, const K& key, V& value)
{
    auto it = map.find(key);
    if (it != map.end()) {
        value = it->second;
        return true;
    }
    return false;
}

}

const BaseSignatureCreator& DUMMY_SIGNATURE_CREATOR = DummySignatureCreator(32, 32);
const BaseSignatureCreator& DUMMY_MAXIMUM_SIGNATURE_CREATOR = DummySignatureCreator(33, 32);
const BaseSignatureCreator& DUMMY_SIGNATURE_CREATOR_PARTICL = DummySignatureCreatorParticl();

bool IsSolvable(const SigningProvider& provider, const CScript& script)
{
    // This check is to make sure that the script we created can actually be solved for and signed by us
    // if we were to have the private keys. This is just to make sure that the script is valid and that,
    // if found in a transaction, we would still accept and relay that transaction. In particular,
    // it will reject witness outputs that require signing with an uncompressed public key.
    SignatureData sigs;
    // Make sure that STANDARD_SCRIPT_VERIFY_FLAGS includes SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, the most
    // important property this function is designed to test for.
    static_assert(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, "IsSolvable requires standard script flags to include WITNESS_PUBKEYTYPE");
    if (ProduceSignature(provider, fParticlMode ? DUMMY_SIGNATURE_CREATOR_PARTICL : DUMMY_SIGNATURE_CREATOR, script, sigs)) {
        // VerifyScript check is just defensive, and should never fail.
        bool verified = VerifyScript(sigs.scriptSig, script, &sigs.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, fParticlMode ? DUMMY_CHECKER_PARTICL : DUMMY_CHECKER);
        assert(verified);
        return true;
    }
    return false;
}

bool IsSegWitOutput(const SigningProvider& provider, const CScript& script)
{
    int version;
    valtype program;
    if (script.IsWitnessProgram(version, program)) return true;
    if (script.IsPayToScriptHash()) {
        std::vector<valtype> solutions;
        auto whichtype = Solver(script, solutions);
        if (whichtype == TxoutType::SCRIPTHASH) {
            auto h160 = uint160(solutions[0]);
            CScript subscript;
            if (provider.GetCScript(CScriptID{h160}, subscript)) {
                if (subscript.IsWitnessProgram(version, program)) return true;
            }
        }
    }
    return false;
}

bool SignTransaction(CMutableTransaction& mtx, const SigningProvider* keystore, const std::map<COutPoint, Coin>& coins, int nHashType, std::map<int, std::string>& input_errors)
{
    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);

    PrecomputedTransactionData txdata;
    std::vector<CTxOut> spent_outputs;
    spent_outputs.resize(mtx.vin.size());
    bool have_all_spent_outputs = true;
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        auto coin = coins.find(txin.prevout);
        if (coin == coins.end() || coin->second.IsSpent()) {
            have_all_spent_outputs = false;
        } else {
            spent_outputs[i] = CTxOut(coin->second.out.nValue, coin->second.out.scriptPubKey);
        }
    }
    if (have_all_spent_outputs) {
        txdata.Init(txConst, std::move(spent_outputs), true);
    } else {
        txdata.Init(txConst, {}, true);
    }

    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        auto coin = coins.find(txin.prevout);
        if (coin == coins.end() || coin->second.IsSpent()) {
            input_errors[i] = "Input not found or already spent";
            continue;
        }
        const CScript& prevPubKey = coin->second.out.scriptPubKey;
        CAmount amount;
        std::vector<uint8_t> vchAmount;
        if (coin->second.nType == OUTPUT_STANDARD) {
            amount = coin->second.out.nValue;
            vchAmount.resize(8);
            part::SetAmount(vchAmount, coin->second.out.nValue);
        } else
        if (coin->second.nType == OUTPUT_CT) {
            amount = 0; // Bypass amount check
            vchAmount.resize(33);
            memcpy(vchAmount.data(), coin->second.commitment.data, 33);
        } else {
            input_errors[i] = "Bad input type";
            continue;
        }

        SignatureData sigdata = DataFromTransaction(mtx, i, vchAmount, prevPubKey);
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size())) {
            ProduceSignature(*keystore, MutableTransactionSignatureCreator(&mtx, i, vchAmount, &txdata, nHashType), prevPubKey, sigdata);
        }

        UpdateInput(txin, sigdata);

        // amount must be specified for valid segwit signature
        if (amount == MAX_MONEY && !txin.scriptWitness.IsNull()) {
            input_errors[i] = "Missing amount";
            continue;
        }

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, vchAmount, txdata, MissingDataBehavior::FAIL), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                input_errors[i] = "Unable to sign input, invalid stack size (possibly missing key)";
            } else if (serror == SCRIPT_ERR_SIG_NULLFAIL) {
                // Verification failed (possibly due to insufficient signatures).
                input_errors[i] = "CHECK(MULTI)SIG failing with non-zero signature (possibly need more signatures)";
            } else {
                input_errors[i] = ScriptErrorString(serror);
            }
        } else {
            // If this input succeeds, make sure there is no error set for it
            input_errors.erase(i);
        }
    }
    return input_errors.empty();
}
