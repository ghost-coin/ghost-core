// Copyright (c) 2018 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_INSIGHT_CSINDEX_H
#define PARTICL_INSIGHT_CSINDEX_H

#include <script/solver.h>

constexpr uint8_t DB_TXINDEX_CSOUTPUT{'O'};
constexpr uint8_t DB_TXINDEX_CSLINK{'L'};
constexpr uint8_t DB_TXINDEX_CSBESTBLOCK{'C'};

enum CSIndexFlags
{
    CSI_FROM_STAKE = (1 << 0),
};

class ColdStakeIndexOutputKey
{
public:
    uint256 m_txnid;
    int m_n;

    ColdStakeIndexOutputKey() {};
    ColdStakeIndexOutputKey(uint256 txnid, int n) : m_txnid(txnid), m_n(n) {};

    template<typename Stream>
    void Serialize(Stream& s) const {
        m_txnid.Serialize(s);
        ser_writedata32be(s, (uint32_t)m_n);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        m_txnid.Unserialize(s);
        m_n = int(ser_readdata32be(s));
    }

    friend bool operator<(const ColdStakeIndexOutputKey& a, const ColdStakeIndexOutputKey& b) {
        int cmp = a.m_txnid.Compare(b.m_txnid);
        return cmp < 0 || (cmp == 0 && a.m_n < b.m_n);
    }
};

class ColdStakeIndexOutputValue
{
public:
    CAmount m_value = 0;
    uint8_t m_flags = 0; // Mark outputs resulting from coldstaking
    int m_spend_height = -1;
    uint256 m_spend_txid;

    SERIALIZE_METHODS(ColdStakeIndexOutputValue, obj)
    {
        READWRITE(obj.m_value);
        READWRITE(obj.m_flags);
        READWRITE(obj.m_spend_height);
        READWRITE(obj.m_spend_txid);
    }
};

class ColdStakeIndexLinkKey
{
public:
    TxoutType m_stake_type = TxoutType::NONSTANDARD, m_spend_type = TxoutType::NONSTANDARD;
    CKeyID256 m_stake_id, m_spend_id;
    unsigned int m_height = 0;

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata8(s, ghost::FromTxoutType(m_stake_type));
        s.write(AsBytes(Span{(char*)m_stake_id.begin(), size_t((m_stake_type == TxoutType::PUBKEYHASH256) ? 32 : 20)}));
        ser_writedata32be(s, m_height);
        ser_writedata8(s, ghost::FromTxoutType(m_spend_type));
        s.write(AsBytes(Span{(char*)m_spend_id.begin(), size_t((m_spend_type == TxoutType::PUBKEYHASH256 || m_spend_type == TxoutType::SCRIPTHASH256) ? 32 : 20)}));
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        uint8_t stake_type = ser_readdata8(s);
        m_stake_type = ghost::ToTxoutType(stake_type);
        m_stake_id.SetNull();
        s.read(AsWritableBytes(Span{m_stake_id.begin(), size_t((m_stake_type == TxoutType::PUBKEYHASH256) ? 32 : 20)}));
        m_height = ser_readdata32be(s);
        uint8_t spend_type = ser_readdata8(s);
        m_spend_type = ghost::ToTxoutType(spend_type);
        m_spend_id.SetNull();
        s.read(AsWritableBytes(Span{m_spend_id.begin(), size_t((m_spend_type == TxoutType::PUBKEYHASH256 || m_spend_type == TxoutType::SCRIPTHASH256) ? 32 : 20)}));
    }

    friend bool operator<(const ColdStakeIndexLinkKey& a, const ColdStakeIndexLinkKey& b) {
        int cmp = a.m_stake_id.Compare(b.m_stake_id);
        if (cmp < 0) return true;
        if (cmp > 0) return false;
        cmp = a.m_spend_id.Compare(b.m_spend_id);
        if (cmp < 0) return true;
        if (cmp > 0) return false;
        return a.m_height < b.m_height;
    }
};

#endif // PARTICL_INSIGHT_CSINDEX_H
