// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/coincontrol.h>

#include <util/system.h>

void CCoinControl::SetNull()
{
    destChange = CNoDestination();
    m_change_type.reset();
    m_add_inputs = true;
    fAllowOtherInputs = false;
    fAllowWatchOnly = false;
    m_avoid_partial_spends = gArgs.GetBoolArg("-avoidpartialspends", DEFAULT_AVOIDPARTIALSPENDS);
    m_avoid_address_reuse = false;
    setSelected.clear();
    m_feerate.reset();
    fOverrideFeeRate = false;
    m_confirm_target.reset();
    m_signal_bip125_rbf.reset();
    m_fee_mode = FeeEstimateMode::UNSET;
    m_min_depth = DEFAULT_MIN_DEPTH;
    m_max_depth = DEFAULT_MAX_DEPTH;

    nCoinType = OUTPUT_STANDARD;
    fHaveAnonOutputs = false;
    fNeedHardwareKey = false;
    m_extrafee = 0;
    m_inputData.clear();
    fAllowLocked = false;
    nChangePos = -1;
    m_addChangeOutput = true;
    m_include_immature = false;
    m_blind_watchonly_visible = false;
    m_extra_data0.clear();
    m_spend_frozen_blinded = false;
    m_include_tainted_frozen = false;
    m_debug_exploit_anon = 0;
    m_use_mixins.clear();
    m_mixin_selection_mode = 1;
    vSplitCommitBlindingKeys.clear();
    scriptChange = CScript();
    m_changepubkey = CPubKey();
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
