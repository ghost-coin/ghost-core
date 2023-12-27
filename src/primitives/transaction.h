// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include <consensus/amount.h>
#include <prevector.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>
#include <pubkey.h>
#include <consensus/consensus.h>

#include <secp256k1_rangeproof.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

/**
 * A flag that is ORed into the protocol version to designate that a transaction
 * should be (un)serialized without witness data.
 * Make sure that this does not collide with any of the values in `version.h`
 * or with `ADDRV2_FORMAT`.
 */
static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;

static const uint8_t GHOST_BLOCK_VERSION = 0xA0;
static const uint8_t GHOST_TXN_VERSION = 0xA0;
static const uint8_t MAX_GHOST_TXN_VERSION = 0xBF;
static const uint8_t BTC_TXN_VERSION = 0x02;

enum OutputTypes
{
    OUTPUT_NULL             = 0, // Marker for CCoinsView (0.14)
    OUTPUT_STANDARD         = 1,
    OUTPUT_CT               = 2,
    OUTPUT_RINGCT           = 3,
    OUTPUT_DATA             = 4,
};

enum TransactionTypes
{
    TXN_STANDARD            = 0,
    TXN_COINBASE            = 1,
    TXN_COINSTAKE           = 2,
};

enum DataOutputTypes
{
    DO_NULL                 = 0, // Reserved
    DO_NARR_PLAIN           = 1,
    DO_NARR_CRYPT           = 2,
    DO_STEALTH              = 3,
    DO_STEALTH_PREFIX       = 4,
    DO_VOTE                 = 5,
    DO_FEE                  = 6,
    DO_TREASURY_FUND_CFWD   = 7,
    DO_FUND_MSG             = 8,
    DO_SMSG_FEE             = 9,
    DO_SMSG_DIFFICULTY      = 10,
    DO_MASK                 = 11,
    DO_GVR_FUND_CFWD        = 12,
};

inline const char* GetOutputTypeName(uint8_t type)
{
    switch (type) {
        case OUTPUT_STANDARD:
            return "plain";
        case OUTPUT_RINGCT:
            return "anon";
        case OUTPUT_CT:
            return "blind";
        default:
            return "unknown";
    }
}

bool ExtractCoinStakeInt64(const std::vector<uint8_t> &vData, DataOutputTypes get_type, CAmount &out);
bool ExtractCoinStakeUint32(const std::vector<uint8_t> &vData, DataOutputTypes get_type, uint32_t &out);

inline bool IsParticlTxVersion(int nVersion)
{
    return (nVersion & 0xFF) >= GHOST_TXN_VERSION;
}

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    static constexpr uint32_t ANON_MARKER = 0xffffffa0;
    static constexpr uint32_t NULL_INDEX = std::numeric_limits<uint32_t>::max();

    COutPoint(): n(NULL_INDEX) { }
    COutPoint(const uint256& hashIn, uint32_t nIn): hash(hashIn), n(nIn) { }

    SERIALIZE_METHODS(COutPoint, obj) { READWRITE(obj.hash, obj.n); }

    void SetNull() { hash.SetNull(); n = NULL_INDEX; }
    bool IsNull() const { return (hash.IsNull() && n == NULL_INDEX); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        int cmp = a.hash.Compare(b.hash);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    bool IsAnonInput() const
    {
        return n == ANON_MARKER;
    }

    std::string ToString() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;
    CScriptWitness scriptData; //!< Non prunable, holds key images when input is anon. TODO: refactor to use scriptWitness
    CScriptWitness scriptWitness; //!< Only serialized through CTransaction

    /**
     * Setting nSequence to this value for every input in a transaction
     * disables nLockTime/IsFinalTx().
     * It fails OP_CHECKLOCKTIMEVERIFY/CheckLockTime() for any input that has
     * it set (BIP 65).
     * It has SEQUENCE_LOCKTIME_DISABLE_FLAG set (BIP 68/112).
     */
    static const uint32_t SEQUENCE_FINAL = 0xffffffff;
    /**
     * This is the maximum sequence number that enables both nLockTime and
     * OP_CHECKLOCKTIMEVERIFY (BIP 65).
     * It has SEQUENCE_LOCKTIME_DISABLE_FLAG set (BIP 68/112).
     */
    static const uint32_t MAX_SEQUENCE_NONFINAL{SEQUENCE_FINAL - 1};

    // Below flags apply in the context of BIP 68. BIP 68 requires the tx
    // version to be set to 2, or higher.
    /**
     * If this flag is set, CTxIn::nSequence is NOT interpreted as a
     * relative lock-time.
     * It skips SequenceLocks() for any input that has it set (BIP 68).
     * It fails OP_CHECKSEQUENCEVERIFY/CheckSequence() for any input that has
     * it set (BIP 112).
     */
    static const uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1U << 31);

    /**
     * If CTxIn::nSequence encodes a relative lock-time and this flag
     * is set, the relative lock-time has units of 512 seconds,
     * otherwise it specifies blocks with a granularity of 1. */
    static const uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /**
     * If CTxIn::nSequence encodes a relative lock-time, this mask is
     * applied to extract that lock-time from the sequence field. */
    static const uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /**
     * In order to use the same number of bits to encode roughly the
     * same wall-clock duration, and because blocks are naturally
     * limited to occur every 600s on average, the minimum granularity
     * for time-based relative lock-time is fixed at 512 seconds.
     * Converting from CTxIn::nSequence to seconds is performed by
     * multiplying by 512 = 2^9, or equivalently shifting up by
     * 9 bits. */
    static const int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    CTxIn()
    {
        nSequence = SEQUENCE_FINAL;
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);

    SERIALIZE_METHODS(CTxIn, obj) {
        READWRITE(obj.prevout, obj.scriptSig, obj.nSequence);

        if (obj.IsAnonInput()) {
            READWRITE(obj.scriptData.stack);
        }
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    bool IsAnonInput() const
    {
        return prevout.IsAnonInput();
    }

    bool SetAnonInfo(uint32_t nInputs, uint32_t nRingSize)
    {
        nInputs = htole32(nInputs);
        nRingSize = htole32(nRingSize);
        memcpy(prevout.hash.begin(), &nInputs, 4);
        memcpy(prevout.hash.begin() + 4, &nRingSize, 4);
        return true;
    }

    bool GetAnonInfo(uint32_t &nInputs, uint32_t &nRingSize) const
    {
        memcpy(&nInputs, prevout.hash.begin(), 4);
        memcpy(&nRingSize, prevout.hash.begin() + 4, 4);
        nInputs = le32toh(nInputs);
        nRingSize = le32toh(nRingSize);
        return true;
    }

    std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;

    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    SERIALIZE_METHODS(CTxOut, obj) { READWRITE(obj.nValue, obj.scriptPubKey); }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    bool IsEmpty() const
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

class CTxOutStandard;
class CTxOutCT;
class CTxOutRingCT;
class CTxOutData;

class CTxOutBase
{
public:
    explicit CTxOutBase(uint8_t v) : nVersion(v) {};
    virtual ~CTxOutBase() {};
    uint8_t nVersion;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        switch (nVersion) {
            case OUTPUT_STANDARD:
                s << *((CTxOutStandard*) this);
                break;
            case OUTPUT_CT:
                s << *((CTxOutCT*) this);
                break;
            case OUTPUT_RINGCT:
                s << *((CTxOutRingCT*) this);
                break;
            case OUTPUT_DATA:
                s << *((CTxOutData*) this);
                break;
            default:
                assert(false);
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        switch (nVersion) {
            case OUTPUT_STANDARD:
                s >> *((CTxOutStandard*) this);
                break;
            case OUTPUT_CT:
                s >> *((CTxOutCT*) this);
                break;
            case OUTPUT_RINGCT:
                s >> *((CTxOutRingCT*) this);
                break;
            case OUTPUT_DATA:
                s >> *((CTxOutData*) this);
                break;
            default:
                assert(false);
        }
    }

    uint8_t GetType() const
    {
        return nVersion;
    }

    bool IsType(uint8_t nType) const
    {
        return nVersion == nType;
    }

    bool IsStandardOutput() const
    {
        return nVersion == OUTPUT_STANDARD;
    }

    const CTxOutStandard *GetStandardOutput() const
    {
        assert(nVersion == OUTPUT_STANDARD);
        return (CTxOutStandard*)this;
    }

    const CTxOut GetCTxOut() const
    {
        assert(nVersion == OUTPUT_STANDARD);
        return CTxOut(GetValue(), *GetPScriptPubKey());
    }

    bool setTxout(CTxOut &txout) const
    {
        if (nVersion != OUTPUT_STANDARD) {
            return false;
        }
        txout.nValue = GetValue();
        return GetScriptPubKey(txout.scriptPubKey);
    }

    virtual bool IsEmpty() const { return false;}

    void SetValue(CAmount value);

    virtual CAmount GetValue() const;

    virtual bool PutValue(std::vector<uint8_t> &vchAmount) const { return false; };

    virtual bool GetScriptPubKey(CScript &scriptPubKey_) const { return false; };
    virtual const CScript *GetPScriptPubKey() const { return nullptr; };

    virtual secp256k1_pedersen_commitment *GetPCommitment() { return nullptr; };
    virtual std::vector<uint8_t> *GetPRangeproof() { return nullptr; };
    virtual std::vector<uint8_t> *GetPData() { return nullptr; };
    virtual const std::vector<uint8_t> *GetPRangeproof() const { return nullptr; };
    virtual const std::vector<uint8_t> *GetPData() const { return nullptr; };
    virtual bool GetPubKey(CCmpPubKey &pk) const { return false; };

    virtual bool GetCTFee(CAmount &nFee) const { return false; };
    virtual bool SetCTFee(CAmount &nFee) { return false; };
    virtual bool GetTreasuryFundCfwd(CAmount &nCfwd) const { return false; };
    virtual bool GetSmsgFeeRate(CAmount &fee_rate) const { return false; };
    virtual bool GetSmsgDifficulty(uint32_t &compact) const { return false; };
    virtual bool GetGvrFundCfwd(CAmount& nCfwd) const { return false; };

    std::string ToString() const;
};

#define OUTPUT_PTR std::shared_ptr
typedef OUTPUT_PTR<CTxOutBase> CTxOutBaseRef;
#define MAKE_OUTPUT std::make_shared

class CTxOutStandard : public CTxOutBase
{
public:
    CTxOutStandard() : CTxOutBase(OUTPUT_STANDARD) {};
    CTxOutStandard(const CAmount& nValueIn, CScript scriptPubKeyIn);

    CAmount nValue;
    CScript scriptPubKey;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s << nValue;
        s << *(CScriptBase*)(&scriptPubKey);
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s >> nValue;
        s >> *(CScriptBase*)(&scriptPubKey);
    }

    bool IsEmpty() const override
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        part::SetAmount(vchAmount, nValue);
        return true;
    }

    CAmount GetValue() const override
    {
        return nValue;
    }

    bool GetScriptPubKey(CScript &scriptPubKey_) const override
    {
        scriptPubKey_ = scriptPubKey;
        return true;
    }

    virtual const CScript *GetPScriptPubKey() const override
    {
        return &scriptPubKey;
    }
};

class CTxOutCT : public CTxOutBase
{
public:
    CTxOutCT() : CTxOutBase(OUTPUT_CT)
    {
        memset(commitment.data, 0, 33);
    }
    secp256k1_pedersen_commitment commitment;
    std::vector<uint8_t> vData; // First 33 bytes is always ephemeral pubkey, can contain token for stealth prefix matching
    CScript scriptPubKey;
    std::vector<uint8_t> vRangeproof;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);
        s.write(AsBytes(Span{(char*)commitment.data, 33}));
        s << vData;
        s << *(CScriptBase*)(&scriptPubKey);

        if (fAllowWitness) {
            s << vRangeproof;
        } else {
            WriteCompactSize(s, 0);
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s.read(AsWritableBytes(Span{(char*)commitment.data, 33}));
        s >> vData;
        s >> *(CScriptBase*)(&scriptPubKey);

        s >> vRangeproof;
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        vchAmount.resize(33);
        memcpy(&vchAmount[0], commitment.data, 33);
        return true;
    }

    bool GetScriptPubKey(CScript &scriptPubKey_) const override
    {
        scriptPubKey_ = scriptPubKey;
        return true;
    }

    virtual const CScript *GetPScriptPubKey() const override
    {
        return &scriptPubKey;
    }

    secp256k1_pedersen_commitment *GetPCommitment() override
    {
        return &commitment;
    }

    std::vector<uint8_t> *GetPRangeproof() override
    {
        return &vRangeproof;
    }
    const std::vector<uint8_t> *GetPRangeproof() const override
    {
        return &vRangeproof;
    }

    std::vector<uint8_t> *GetPData() override
    {
        return &vData;
    }
    const std::vector<uint8_t> *GetPData() const override
    {
        return &vData;
    }
};

class CTxOutRingCT : public CTxOutBase
{
public:
    CTxOutRingCT() : CTxOutBase(OUTPUT_RINGCT) {};
    CCmpPubKey pk;
    std::vector<uint8_t> vData; // First 33 bytes is always ephemeral pubkey, can contain token for stealth prefix matching
    secp256k1_pedersen_commitment commitment;
    std::vector<uint8_t> vRangeproof;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);
        s.write(AsBytes(Span{(char*)pk.begin(), 33}));
        s.write(AsBytes(Span{(char*)commitment.data, 33}));
        s << vData;

        if (fAllowWitness) {
            s << vRangeproof;
        } else {
            WriteCompactSize(s, 0);
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s.read(AsWritableBytes(Span{(char*)pk.ncbegin(), 33}));
        s.read(AsWritableBytes(Span{(char*)commitment.data, 33}));
        s >> vData;
        s >> vRangeproof;
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        vchAmount.resize(33);
        memcpy(&vchAmount[0], commitment.data, 33);
        return true;
    }

    secp256k1_pedersen_commitment *GetPCommitment() override
    {
        return &commitment;
    }

    std::vector<uint8_t> *GetPRangeproof() override
    {
        return &vRangeproof;
    }
    const std::vector<uint8_t> *GetPRangeproof() const override
    {
        return &vRangeproof;
    }

    std::vector<uint8_t> *GetPData() override
    {
        return &vData;
    }
    const std::vector<uint8_t> *GetPData() const override
    {
        return &vData;
    }
    bool GetPubKey(CCmpPubKey &pk_) const override
    {
        pk_ = pk;
        return true;
    }
};

class CTxOutData : public CTxOutBase
{
public:
    CTxOutData() : CTxOutBase(OUTPUT_DATA) {};
    explicit CTxOutData(const std::vector<uint8_t> &vData_) : CTxOutBase(OUTPUT_DATA), vData(vData_) {};

    std::vector<uint8_t> vData;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s << vData;
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s >> vData;
    }

    bool GetCTFee(CAmount &nFee) const override
    {
        if (vData.size() < 2 || vData[0] != DO_FEE) {
            return false;
        }
        size_t nb;
        return (0 == part::GetVarInt(vData, 1, (uint64_t&)nFee, nb));
    }

    bool SetCTFee(CAmount &nFee) override
    {
        vData.clear();
        vData.push_back(DO_FEE);
        return (0 == part::PutVarInt(vData, nFee));
    }

    bool GetTreasuryFundCfwd(CAmount &nCfwd) const override
    {
        return ExtractCoinStakeInt64(vData, DO_TREASURY_FUND_CFWD, nCfwd);
    }


    bool GetGvrFundCfwd(CAmount& nCfwd) const override
    {
        return ExtractCoinStakeInt64(vData, DO_GVR_FUND_CFWD, nCfwd);
    }

    bool GetSmsgFeeRate(CAmount &fee_rate) const override
    {
        return ExtractCoinStakeInt64(vData, DO_SMSG_FEE, fee_rate);
    }

    bool GetSmsgDifficulty(uint32_t &compact) const override
    {
        return ExtractCoinStakeUint32(vData, DO_SMSG_DIFFICULTY, compact);
    }

    std::vector<uint8_t> *GetPData() override
    {
        return &vData;
    }
    const std::vector<uint8_t> *GetPData() const override
    {
        return &vData;
    }
};

class CTxOutSign
{
public:
    CTxOutSign(const std::vector<uint8_t>& valueIn, const CScript &scriptPubKeyIn)
        : m_is_anon_input(false), amount(valueIn), scriptPubKey(scriptPubKeyIn) {};
    CTxOutSign()
        : m_is_anon_input(true) {};

    bool m_is_anon_input;
    std::vector<uint8_t> amount;
    CScript scriptPubKey;
    SERIALIZE_METHODS(CTxOutSign, obj)
    {
        if (ser_action.ForRead()) {
            assert(false);
        }
        s.write(AsBytes(Span{(const char*)obj.amount.data(), obj.amount.size()}));
        READWRITE(obj.scriptPubKey);
    }
};


struct CMutableTransaction;

/**
 * Basic transaction serialization format:
 * - int32_t nVersion
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - uint32_t nLockTime
 *
 * Extended transaction serialization format:
 * - int32_t nVersion
 * - unsigned char dummy = 0x00
 * - unsigned char flags (!= 0)
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - if (flags & 1):
 *   - CScriptWitness scriptWitness; (deserialized into CTxIn)
 * - uint32_t nLockTime
 */
template<typename Stream, typename TxType>
inline void UnserializeTransaction(TxType& tx, Stream& s) {
    const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);

    uint8_t bv;
    tx.nVersion = 0;
    s >> bv;

    if (bv >= GHOST_TXN_VERSION) {
        tx.nVersion = bv;

        s >> bv;
        tx.nVersion |= bv<<8; // TransactionTypes

        s >> tx.nLockTime;

        tx.vin.clear();
        s >> tx.vin;

        size_t nOutputs = ReadCompactSize(s);
        tx.vpout.clear();
        tx.vpout.reserve(nOutputs);
        for (size_t k = 0; k < nOutputs; ++k) {
            s >> bv;
            switch (bv) {
                case OUTPUT_STANDARD:
                    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>());
                    break;
                case OUTPUT_CT:
                    tx.vpout.push_back(MAKE_OUTPUT<CTxOutCT>());
                    break;
                case OUTPUT_RINGCT:
                    tx.vpout.push_back(MAKE_OUTPUT<CTxOutRingCT>());
                    break;
                case OUTPUT_DATA:
                    tx.vpout.push_back(MAKE_OUTPUT<CTxOutData>());
                    break;
                default:
                    throw std::ios_base::failure("Unknown transaction output type");
            }
            tx.vpout[k]->nVersion = bv;
            s >> *tx.vpout[k];
        }

        if (fAllowWitness) {
            for (auto &txin : tx.vin)
                s >> txin.scriptWitness.stack;
        }
        return;
    }

    tx.nVersion |= bv;
    s >> bv;
    tx.nVersion |= bv<<8;
    s >> bv;
    tx.nVersion |= bv<<16;
    s >> bv;
    tx.nVersion |= bv<<24;

    unsigned char flags = 0;
    tx.vin.clear();
    tx.vout.clear();
    /* Try to read the vin. In case the dummy is there, this will be read as an empty vector. */
    s >> tx.vin;
    if (tx.vin.size() == 0 && fAllowWitness) {
        /* We read a dummy or an empty vin. */
        s >> flags;
        if (flags != 0) {
            s >> tx.vin;
            s >> tx.vout;
        }
    } else {
        /* We read a non-empty vin. Assume a normal vout follows. */
        s >> tx.vout;
    }
    if ((flags & 1) && fAllowWitness) {
        /* The witness flag is present, and we support witnesses. */
        flags ^= 1;
        for (size_t i = 0; i < tx.vin.size(); i++) {
            s >> tx.vin[i].scriptWitness.stack;
        }
        if (!tx.HasWitness()) {
            /* It's illegal to encode witnesses when all witness stacks are empty. */
            throw std::ios_base::failure("Superfluous witness record");
        }
    }
    if (flags) {
        /* Unknown flag in the serialization */
        throw std::ios_base::failure("Unknown transaction optional data");
    }
    s >> tx.nLockTime;
}

template<typename Stream, typename TxType>
inline void SerializeTransaction(const TxType& tx, Stream& s) {
    const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);

    if (IsParticlTxVersion(tx.nVersion)) {
        uint8_t bv = tx.nVersion & 0xFF;
        s << bv;

        bv = (tx.nVersion>>8) & 0xFF;
        s << bv; // TransactionType

        s << tx.nLockTime;
        s << tx.vin;

        WriteCompactSize(s, tx.vpout.size());
        for (size_t k = 0; k < tx.vpout.size(); ++k) {
            s << tx.vpout[k]->nVersion;
            s << *tx.vpout[k];
        }

        if (fAllowWitness) {
            for (auto &txin : tx.vin) {
                s << txin.scriptWitness.stack;
            }
        }
        return;
    }

    s << tx.nVersion;

    unsigned char flags = 0;
    // Consistency check
    if (fAllowWitness) {
        /* Check whether witnesses need to be serialized. */
        if (tx.HasWitness()) {
            flags |= 1;
        }
    }
    if (flags) {
        /* Use extended format in case witnesses are to be serialized. */
        std::vector<CTxIn> vinDummy;
        s << vinDummy;
        s << flags;
    }
    s << tx.vin;
    s << tx.vout;
    if (flags & 1) {
        for (size_t i = 0; i < tx.vin.size(); i++) {
            s << tx.vin[i].scriptWitness.stack;
        }
    }
    s << tx.nLockTime;
}

template<typename TxType>
inline CAmount CalculateOutputValue(const TxType& tx)
{
    return std::accumulate(tx.vout.cbegin(), tx.vout.cend(), CAmount{0}, [](CAmount sum, const auto& txout) { return sum + txout.nValue; });
}


/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
public:
    // Default transaction version.
    static const int32_t CURRENT_VERSION=2;
    static const int32_t CURRENT_PARTICL_VERSION=0xA0;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const std::vector<CTxOutBaseRef> vpout;
    const int32_t nVersion;
    const uint32_t nLockTime;

private:
    /** Memory only. */
    const uint256 hash;
    const uint256 m_witness_hash;

    uint256 ComputeHash() const;
    uint256 ComputeWitnessHash() const;

public:
    /** Convert a CMutableTransaction into a CTransaction. */
    explicit CTransaction(const CMutableTransaction& tx);
    explicit CTransaction(CMutableTransaction&& tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const {
        return vin.empty() && vout.empty() && vpout.empty();
    }

    bool IsParticlVersion() const {
        return IsParticlTxVersion(nVersion);
    }

    int GetParticlVersion() const {
        return nVersion & 0xFF;
    }

    int GetType() const {
        return (nVersion >> 8) & 0xFF;
    }

    size_t GetNumVOuts() const
    {
        return IsParticlTxVersion(nVersion) ? vpout.size() : vout.size();
    }

    const uint256& GetHash() const { return hash; }
    const uint256& GetWitnessHash() const { return m_witness_hash; };

    // Return sum of txouts.
    CAmount GetValueOut() const;

    // Return sum of standard txouts and counts of output types
    CAmount GetPlainValueOut(size_t &nStandard, size_t &nCT, size_t &nRingCT) const;

    // Return sum of standard txouts with unspendable scripts
    CAmount GetPlainValueBurned() const;

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const
    {
        if (IsParticlVersion()) {
            return (GetType() == TXN_COINBASE &&
                    vin.size() == 1 && vin[0].prevout.IsNull());
        }
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    bool IsCoinStake() const
    {
        return (GetType() == TXN_COINSTAKE &&
                vin.size() > 0 && vpout.size() > 1 &&
                vpout[0]->nVersion == OUTPUT_DATA &&
                vpout[1]->nVersion == OUTPUT_STANDARD);
    }

    bool GetCoinStakeHeight(int &height) const
    {
        if (vpout.size() < 2 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }

        std::vector<uint8_t> &vData = *vpout[0]->GetPData();
        if (vData.size() < 4) {
            return false;
        }
        memcpy(&height, &vData[0], 4);
        height = le32toh(height);
        return true;
    }

    bool GetCTFee(CAmount &nFee) const
    {
        if (vpout.size() < 2 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }
        return vpout[0]->GetCTFee(nFee);
    }

    bool GetTreasuryFundCfwd(CAmount &nCfwd) const
    {
        if (vpout.size() < 1 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }
        return vpout[0]->GetTreasuryFundCfwd(nCfwd);
    }

    bool GetGvrFundCfwd(CAmount& nCfwd) const
    {
        if (vpout.size() < 1 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }
        return vpout[0]->GetGvrFundCfwd(nCfwd);
    }

    bool GetSmsgFeeRate(CAmount &fee_rate) const
    {
        if (vpout.size() < 1 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }
        return vpout[0]->GetSmsgFeeRate(fee_rate);
    }

    bool GetSmsgDifficulty(uint32_t &compact) const
    {
        if (vpout.size() < 1 || vpout[0]->nVersion != OUTPUT_DATA) {
            return false;
        }
        return vpout[0]->GetSmsgDifficulty(compact);
    }

    CAmount GetTotalSMSGFees() const;

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    std::vector<CTxOutBaseRef> vpout;
    int32_t nVersion;
    uint32_t nLockTime;

    explicit CMutableTransaction();
    explicit CMutableTransaction(const CTransaction& tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        UnserializeTransaction(*this, s);
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    void SetType(int type) {
        nVersion |= (type & 0xFF) << 8;
    }

    bool IsParticlVersion() const {
        return IsParticlTxVersion(nVersion);
    }

    int GetParticlVersion() const {
        return nVersion & 0xFF;
    }

    int GetType() const {
        return (nVersion >> 8) & 0xFF;
    }

    bool IsCoinStake() const
    {
        return GetType() == TXN_COINSTAKE
            && vin.size() > 0 && vpout.size() > 1
            && vpout[0]->nVersion == OUTPUT_DATA
            && vpout[1]->nVersion == OUTPUT_STANDARD;
    }

    size_t GetNumVOuts() const
    {
        return IsParticlTxVersion(nVersion) ? vpout.size() : vout.size();
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
};

typedef std::shared_ptr<const CTransaction> CTransactionRef;
template <typename Tx> static inline CTransactionRef MakeTransactionRef(Tx&& txIn) { return std::make_shared<const CTransaction>(std::forward<Tx>(txIn)); }

/** A generic txid reference (txid or wtxid). */
class GenTxid
{
    bool m_is_wtxid;
    uint256 m_hash;
    GenTxid(bool is_wtxid, const uint256& hash) : m_is_wtxid(is_wtxid), m_hash(hash) {}

public:
    static GenTxid Txid(const uint256& hash) { return GenTxid{false, hash}; }
    static GenTxid Wtxid(const uint256& hash) { return GenTxid{true, hash}; }
    bool IsWtxid() const { return m_is_wtxid; }
    const uint256& GetHash() const { return m_hash; }
    friend bool operator==(const GenTxid& a, const GenTxid& b) { return a.m_is_wtxid == b.m_is_wtxid && a.m_hash == b.m_hash; }
    friend bool operator<(const GenTxid& a, const GenTxid& b) { return std::tie(a.m_is_wtxid, a.m_hash) < std::tie(b.m_is_wtxid, b.m_hash); }
};

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
