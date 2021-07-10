// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/coincontrol.h>

#include <util/system.h>

CCoinControl::CCoinControl()
{
    m_avoid_partial_spends = gArgs.GetBoolArg("-avoidpartialspends", DEFAULT_AVOIDPARTIALSPENDS);
}

bool CCoinControl::SetKeyFromInputData(const CKeyID &idk, CKey &key) const
{
    for (const auto &im : m_inputData) {
        if (idk == im.second.pubkey.GetID()) {
            key = im.second.privkey;
            if (key.IsValid()) {
                return true;
            }
        }
    }
    return false;
}
