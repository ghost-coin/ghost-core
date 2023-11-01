// Copyright (c) 2017-2022 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_WALLET_HDWALLETDB_H
#define PARTICL_WALLET_HDWALLETDB_H

#include <primitives/transaction.h>
#include <wallet/bdb.h>
#include <wallet/walletdb.h>
#include <key/types.h>

#include <string>
#include <vector>

namespace wallet {
struct CAddressBookData;
} // namespace wallet

using namespace wallet;

class CEKAKeyPack;
class CEKASCKeyPack;
class CEKAStealthKeyPack;
class CExtKeyAccount;
class CStealthAddress;
class CStoredExtKey;
class CEKLKey;
class uint160;
class uint256;

/*
prefixes
    name

    abe                 - address book entry
    acc
    acentry

    aki                 - anon key image: CPubKey - COutpoint

    bestblock
    bestblockheader

    ckey
    cscript

    defaultkey

    eacc                - extended account
    ecpk                - extended account stealth child key pack
    ek32                - bip32 extended keypair
    eknm                - named extended key
    epak                - extended account key pack
    espk                - extended account stealth key pack
    elck                - extended loose key child key

    flag                - named integer flag

    ine                 - extkey index
    ins                 - stealth index, key: uint32_t, value: raw stealth address bytes

    keymeta
    key

    lao                 - locked anon/blind output: COutpoint
    lastfilteredheight
    lns                 - stealth link, key: keyid, value uint32_t (stealth index)
    lne                 - extkey link key: keyid, value uint32_t (stealth index)
    luo                 - locked unspent output

    mkey                - CMasterKey
    minversion

    orderposnext

    pool

    ris                 - reverse stealth index key: hashed raw stealth address bytes, value: uint32_t
    rtx                 - CTransactionRecord

    stx                 - CStoredTransaction
    sxad                - loose stealth address
    sxkm                - key meta data for keys received on stealth while wallet locked

    tx

    version
    votes               - vector of vote tokens added by time added asc

    wkey
    wset                - wallet setting
*/

class CTransactionRecord;
class CStoredTransaction;


class CStealthKeyMetadata
{
// Used to get secret for keys created by stealth transaction with wallet locked
public:
    CStealthKeyMetadata() {}

    CStealthKeyMetadata(CPubKey pkEphem_, CPubKey pkScan_)
    {
        pkEphem = pkEphem_;
        pkScan = pkScan_;
    }

    CPubKey pkEphem;
    CPubKey pkScan;

    SERIALIZE_METHODS(CStealthKeyMetadata, obj)
    {
        READWRITE(obj.pkEphem);
        READWRITE(obj.pkScan);
    }
};

class CLockedAnonOutput
{
// expand key for anon output received with wallet locked
// stored in walletdb, key is pubkey hash160
public:
    CLockedAnonOutput() {}

    CLockedAnonOutput(CPubKey pkEphem_, CPubKey pkScan_, COutPoint outpoint_)
    {
        pkEphem = pkEphem_;
        pkScan = pkScan_;
        outpoint = outpoint_;
    }

    CPubKey   pkEphem;
    CPubKey   pkScan;
    COutPoint outpoint;

    SERIALIZE_METHODS(CLockedAnonOutput, obj)
    {
        READWRITE(obj.pkEphem);
        READWRITE(obj.pkScan);
        READWRITE(obj.outpoint);
    }
};

class COwnedAnonOutput
{
// stored in walletdb, key is keyimage
// TODO: store nValue?
public:
    COwnedAnonOutput() {};

    COwnedAnonOutput(COutPoint outpoint_, bool fSpent_)
    {
        outpoint = outpoint_;
        fSpent   = fSpent_;
    }

    ec_point vchImage;
    int64_t nValue;

    COutPoint outpoint;
    bool fSpent;

    SERIALIZE_METHODS(COwnedAnonOutput, obj)
    {
        READWRITE(obj.outpoint);
        READWRITE(obj.fSpent);
    }
};

class CStealthAddressIndexed
{
public:
    CStealthAddressIndexed() {};

    CStealthAddressIndexed(std::vector<uint8_t> &addrRaw_) : addrRaw(addrRaw_) {};
    std::vector<uint8_t> addrRaw;

    SERIALIZE_METHODS(CStealthAddressIndexed, obj)
    {
        READWRITE(obj.addrRaw);
    }
};

class CVoteToken
{
public:
    CVoteToken() {};
    CVoteToken(uint32_t nToken_, int nStart_, int nEnd_, int64_t nTimeAdded_) :
        nToken(nToken_), nStart(nStart_), nEnd(nEnd_), nTimeAdded(nTimeAdded_) {};

    uint32_t nToken;
    int nStart;
    int nEnd;
    int64_t nTimeAdded;

    SERIALIZE_METHODS(CVoteToken, obj)
    {
        READWRITE(obj.nToken);
        READWRITE(obj.nStart);
        READWRITE(obj.nEnd);
        READWRITE(obj.nTimeAdded);
    }
};

/** Access to the wallet database */
class CHDWalletDB : public WalletBatch
{
public:
    CHDWalletDB(WalletDatabase& dbw, bool _fFlushOnClose = true) : WalletBatch(dbw, _fFlushOnClose)
    {
    };

    bool InTxn()
    {
        BerkeleyBatch *bb = static_cast<BerkeleyBatch*>(m_batch.get());
        return bb && bb->pdb && bb->activeTxn;
    }

    Dbc *GetTxnCursor()
    {
        BerkeleyBatch *bb = static_cast<BerkeleyBatch*>(m_batch.get());
        if (!bb || !bb->pdb || !bb->activeTxn) {
            return nullptr;
        }

        DbTxn *ptxnid = bb->activeTxn; // call TxnBegin first

        Dbc *pcursor = nullptr;
        int ret = bb->pdb->cursor(ptxnid, &pcursor, 0);
        if (ret != 0) {
            return nullptr;
        }
        return pcursor;
    }

    Dbc *GetCursor()
    {
        BerkeleyBatch *bb = static_cast<BerkeleyBatch*>(m_batch.get());
        if (!bb || !bb->pdb) {
            return nullptr;
        }
        Dbc* pcursor = nullptr;
        int ret = bb->pdb->cursor(nullptr, &pcursor, 0);
        if (ret != 0)
            return nullptr;
        return pcursor;
    }

    template< typename T>
    bool Replace(Dbc *pcursor, const T &value)
    {
        BerkeleyBatch *bb = static_cast<BerkeleyBatch*>(m_batch.get());
        if (!pcursor) {
            return false;
        }

        if (bb->fReadOnly) {
            assert(!"Replace called on database in read-only mode");
        }

        // Value
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;
        Dbt datValue(&ssValue[0], ssValue.size());

        // Write
        int ret = pcursor->put(nullptr, &datValue, DB_CURRENT);

        if (ret != 0) {
            LogPrintf("CursorPut ret %d - %s\n", ret, DbEnv::strerror(ret));
        }
        // Clear memory in case it was a private key
        memset(datValue.get_data(), 0, datValue.get_size());

        return (ret == 0);
    }

    int ReadAtCursor(Dbc *pcursor, DataStream &ssKey, DataStream &ssValue, unsigned int fFlags=DB_NEXT)
    {
        // Read at cursor
        SafeDbt datKey, datValue;
        if (fFlags == DB_SET || fFlags == DB_SET_RANGE || fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            datKey.set_data(ssKey.data(), ssKey.size());
        }
        if (fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            datValue.set_data(ssValue.data(), ssValue.size());
        }
        int ret = pcursor->get(datKey, datValue, fFlags);
        if (ret != 0) {
            if (datKey.get_data() == ssKey.data()) {
                datKey.set_data(nullptr, 0); // Avoid free in ~SafeDbt
            }
            if (datValue.get_data() == ssValue.data()) {
                datValue.set_data(nullptr, 0); // Avoid free in ~SafeDbt
            }
            return ret;
        } else
        if (datKey.get_data() == nullptr || datValue.get_data() == nullptr) {
            return 99999;
        }

        // Convert to streams
        ssKey.clear();
        ssKey.write(AsBytes(Span{(char*)datKey.get_data(), datKey.get_size()}));

        ssValue.clear();
        ssValue.write(AsBytes(Span{(char*)datValue.get_data(), datValue.get_size()}));
        return 0;
    }

    int ReadKeyAtCursor(Dbc *pcursor, DataStream &ssKey, unsigned int fFlags=DB_NEXT)
    {
        // Read key at cursor
        SafeDbt datKey;
        if (fFlags == DB_SET || fFlags == DB_SET_RANGE) {
            datKey.set_data(&ssKey[0], ssKey.size());
        }
        Dbt datValue;
        datValue.set_flags(DB_DBT_PARTIAL); // don't read data, dlen and doff are 0 after memset

        int ret = pcursor->get(datKey, &datValue, fFlags);
        if (ret != 0) {
            return ret;
        }
        if (datKey.get_data() == nullptr) {
            return 99999;
        }

        // Convert to streams
        ssKey.clear();
        ssKey.write(AsBytes(Span{(char*)datKey.get_data(), datKey.get_size()}));
        return 0;
    }


    bool WriteStealthKeyMeta(const CKeyID &keyId, const CStealthKeyMetadata &sxKeyMeta);
    bool EraseStealthKeyMeta(const CKeyID &keyId);

    bool WriteStealthAddress(const CStealthAddress &sxAddr);
    bool ReadStealthAddress(CStealthAddress &sxAddr);
    bool EraseStealthAddress(const CStealthAddress &sxAddr);


    bool ReadNamedExtKeyId(const std::string &name, CKeyID &identifier, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteNamedExtKeyId(const std::string &name, const CKeyID &identifier);

    bool ReadExtKey(const CKeyID &identifier, CStoredExtKey &ek32, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtKey(const CKeyID &identifier, const CStoredExtKey &ek32);

    bool ReadExtAccount(const CKeyID &identifier, CExtKeyAccount &ekAcc, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtAccount(const CKeyID &identifier, const CExtKeyAccount &ekAcc);

    bool ReadExtKeyPack(const CKeyID &identifier, const uint32_t nPack, std::vector<CEKAKeyPack> &ekPak, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtKeyPack(const CKeyID &identifier, const uint32_t nPack, const std::vector<CEKAKeyPack> &ekPak);

    bool ReadExtStealthKeyPack(const CKeyID &identifier, const uint32_t nPack, std::vector<CEKAStealthKeyPack> &aksPak, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtStealthKeyPack(const CKeyID &identifier, const uint32_t nPack, const std::vector<CEKAStealthKeyPack> &aksPak);

    bool ReadExtStealthKeyChildPack(const CKeyID &identifier, const uint32_t nPack, std::vector<CEKASCKeyPack> &asckPak, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtStealthKeyChildPack(const CKeyID &identifier, const uint32_t nPack, const std::vector<CEKASCKeyPack> &asckPak);

    bool ReadFlag(const std::string &name, int32_t &nValue, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteFlag(const std::string &name, int32_t nValue);
    bool EraseFlag(const std::string &name);
    bool WriteWalletFlags(const uint64_t flags);


    bool ReadExtKeyIndex(uint32_t id, CKeyID &identifier, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteExtKeyIndex(uint32_t id, const CKeyID &identifier);


    bool ReadStealthAddressIndex(uint32_t id, CStealthAddressIndexed &sxi, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteStealthAddressIndex(uint32_t id, const CStealthAddressIndexed &sxi);

    bool ReadStealthAddressIndexReverse(const uint160 &hash, uint32_t &id, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteStealthAddressIndexReverse(const uint160 &hash, uint32_t id);

    //bool GetStealthAddressIndex(const CStealthAddressIndexed &sxi, uint32_t &id); // Get stealth index or create new index if none found

    bool ReadStealthAddressLink(const CKeyID &keyId, uint32_t &id, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteStealthAddressLink(const CKeyID &keyId, uint32_t id);

    bool WriteAddressBookEntry(const std::string &sKey, const wallet::CAddressBookData &data);
    bool EraseAddressBookEntry(const std::string &sKey);

    bool ReadVoteTokens(std::vector<CVoteToken> &vVoteTokens, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteVoteTokens(const std::vector<CVoteToken> &vVoteTokens);
    bool EraseVoteTokens();

    bool WriteTxRecord(const uint256 &hash, const CTransactionRecord &rtx);
    bool EraseTxRecord(const uint256 &hash);


    bool ReadStoredTx(const uint256 &hash, CStoredTransaction &stx, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteStoredTx(const uint256 &hash, const CStoredTransaction &stx);
    bool EraseStoredTx(const uint256 &hash);

    bool ReadAnonKeyImage(const CCmpPubKey &ki, COutPoint &op, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteAnonKeyImage(const CCmpPubKey &ki, const COutPoint &op);
    bool EraseAnonKeyImage(const CCmpPubKey &ki);


    bool HaveLockedAnonOut(const COutPoint &op, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteLockedAnonOut(const COutPoint &op);
    bool EraseLockedAnonOut(const COutPoint &op);


    bool ReadWalletSetting(const std::string &setting, std::string &json, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteWalletSetting(const std::string &setting, const std::string &json);
    bool EraseWalletSetting(const std::string &setting);

    /** extkey chain loose child keys */
    bool ReadEKLKey(const CKeyID &id, CEKLKey &c, uint32_t nFlags=DB_READ_UNCOMMITTED);
    bool WriteEKLKey(const CKeyID &id, const CEKLKey &c);
};

#endif // PARTICL_WALLET_HDWALLETDB_H
