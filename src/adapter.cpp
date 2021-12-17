// Copyright (c) 2021 Ghost Core Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "adapter.h"

bool is_ghost_debug() {
    return gArgs.GetBoolArg("-ghostdebug", DEFAULT_GHOSTDEBUG);
}

bool exploit_fixtime_passed(uint32_t nTime)
{
    uint32_t testTime = Params().GetConsensus().exploit_fix_2_time;
    if (nTime > testTime) {
        if (is_ghost_debug())
            LogPrintf("%s - returning true\n", __func__);
        return true;
    }
    if (is_ghost_debug())
        LogPrintf("%s - returning false\n", __func__);
    return false;
}

bool is_output_recovery_address(const std::string& dest) {
    const std::string recoveryAddress = Params().GetRecoveryAddress();
    return dest.find(recoveryAddress) != std::string::npos;
}

CAmount GetAllowedValueFraction(const CAmount value)
{
    return (value / 1000) * 995;
}

bool HasRestrictionHeightStarted() {
    return ::ChainActive().Tip()->nHeight >= ::Params().GetConsensus().anonRestrictionStartHeight;
}

bool is_anonblind_transaction_ok(const CTransactionRef& tx, const size_t totalRing)
{
    static_assert(std::is_unsigned<decltype(totalRing)>::value, "totalRing is only treated for unsigned cases");
    if(totalRing == 0)
    {
        return true;
    }

    if (totalRing > 0) {

        const uint256& txHash = tx->GetHash();
        if (txHash == TEST_TX) {
            return true;
        }

        //! for restricted anon/blind spends
        //! no mixed component stakes allowed
        if (tx->IsCoinStake()) {
            LogPrintf("%s - transaction %s is a coinstake with anon/blind components\n", __func__, txHash.ToString());
            return false;
        }

        //! total value out must be greater than 5 coins
        const CAmount totalValue = tx->GetValueOut();
        if (totalValue < 5 * COIN) {
            LogPrintf("%s - transaction %s has output of less than 5 coins total\n", __func__, txHash.ToString());
            return false;
        }

        //! split among no more than three outputs
        const unsigned int outSize = tx->vpout.size();
        if (outSize > Params().GetAnonMaxOutputSize()) {
            LogPrintf("%s - transaction %s has more than 3 outputs total %d\n", __func__, txHash.ToString(), outSize);
            return false;
        }

        // Make the check of fees here
        CAmount fee;
        for (unsigned int i=0; i < outSize; ++i) {
            if (tx->vpout[i]->GetCTFee(fee)){
                if (fee >= 10 * COIN ) {
                    LogPrintf("%s - Fee greater than 10*COIN value %s %d\n", __func__, txHash.ToString(), fee);
                    return false;
                }
            }
        }
        int standardOutputIndex = 0;

        //! recovery address must receive 100% of the output amount
        for (unsigned int i=0; i < outSize; ++i) {
            if (tx->vpout[i]->IsStandardOutput()) {
                standardOutputIndex = i;
                const std::string destTest = HexStr(tx->vpout[i]->GetStandardOutput()->scriptPubKey);
                if (is_output_recovery_address(destTest)) {
                    if (tx->vpout[i]->GetStandardOutput()->nValue >= GetAllowedValueFraction(totalValue)) {
                        LogPrintf("Found recovery amount at vout.n #%d\n", i);
                        return true;
                    }else{
                        LogPrintf("%s Sending only #%d\n this will fail out of %d", __func__, tx->vpout[i]->GetStandardOutput()->nValue, totalValue);
                    }
                }
            }
        }
        auto total = totalValue - tx->vpout[standardOutputIndex]->GetStandardOutput()->nValue - fee;
        LogPrintf("%s The difference equals", __func__, totalValue);

    }
    return false;
}
