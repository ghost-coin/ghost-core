// Copyright (c) 2023 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_SMSG_SECUREMESSAGE_H
#define PARTICL_SMSG_SECUREMESSAGE_H

#include <string.h>
#include <inttypes.h>


namespace smsg {

class SecureMessage
{
public:
    SecureMessage() {};
    SecureMessage(const unsigned char *bytes) { set(bytes); };
    SecureMessage(bool fPaid, uint32_t ttl)
    {
        if (fPaid) {
            version[0] = 3;
            version[1] = 0;
        }
        m_ttl = ttl;
    }
    ~SecureMessage()
    {
        if (pPayload) {
            delete[] pPayload;
        }
        pPayload = nullptr;
    }

    void SetNull()
    {
        memset(iv, 0, 16);
        memset(cpkR, 0, 33);
        memset(mac, 0, 32);
    }

    bool IsPaidVersion() const
    {
        return version[0] == 3;
    }

    void set(const uint8_t *data)
    {
        size_t ofs = 0;
        uint64_t tmp64;
        uint32_t tmp32;
        memcpy(hash, data + ofs, 4); ofs += 4;
        memcpy(nonce, data + ofs, 4); ofs += 4;
        memcpy(version, data + ofs, 2); ofs += 2;
        flags = *(data + ofs);  ofs += 1;
        memcpy(&tmp64, data + ofs, 8); ofs += 8;
        timestamp = le64toh(tmp64);
        memcpy(&tmp32, data + ofs, 4); ofs += 4;
        m_ttl = le32toh(tmp32);
        memcpy(iv, data + ofs, 16); ofs += 16;
        memcpy(cpkR, data + ofs, 33); ofs += 33;
        memcpy(mac, data + ofs, 32); ofs += 32;
        memcpy(&tmp32, data + ofs, 4); ofs += 4;
        nPayload = le32toh(tmp32);
        pPayload = nullptr;
    }

    void WriteHeader(uint8_t *data) const {
        size_t ofs = 0;
        uint64_t tmp64;
        uint32_t tmp32;
        memcpy(data + ofs, hash, 4); ofs += 4;
        memcpy(data + ofs, nonce, 4); ofs += 4;
        memcpy(data + ofs, version, 2); ofs += 2;
        *(data + ofs) = flags;  ofs += 1;
        tmp64 = htole64(timestamp);
        memcpy(data + ofs, &tmp64, 8); ofs += 8;
        tmp32 = htole32(m_ttl);
        memcpy(data + ofs, &tmp32, 4); ofs += 4;
        memcpy(data + ofs, iv, 16); ofs += 16;
        memcpy(data + ofs, cpkR, 33); ofs += 33;
        memcpy(data + ofs, mac, 32); ofs += 32;
        tmp32 = htole32(nPayload);
        memcpy(data + ofs, &tmp32, 4); ofs += 4;
    }

    uint8_t hash[4] = {0, 0, 0, 0};
    uint8_t nonce[4] = {0, 0, 0, 0};
    uint8_t version[2] = {2, 1};
    uint8_t flags = 0;
    int64_t timestamp = 0;
    uint32_t m_ttl = 0;
    uint8_t iv[16];
    uint8_t cpkR[33];
    uint8_t mac[32];
    uint32_t nPayload = 0;
    uint8_t *pPayload = nullptr;
};

} // namespace smsg

#endif // PARTICL_SMSG_SECUREMESSAGE_H
