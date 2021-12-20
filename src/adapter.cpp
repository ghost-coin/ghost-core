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

        //! 1 - Check of the output size
        const unsigned int outSize = tx->vpout.size();
        if (outSize > Params().GetAnonMaxOutputSize()) {
            LogPrintf("%s - transaction %s has more than 3 outputs total %d\n", __func__, txHash.ToString(), outSize);
            return false;
        }

        //! 2 - Check the number of standard outputs
        auto numOfStandardTx = std::count_if(tx->vpout.begin(), tx->vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
            return tx->IsStandardOutput();
        });

        if (numOfStandardTx == 0 || numOfStandardTx > 1) {
            LogPrintf("%s - transaction %s has more than 1 standard numOfStandardTx=%d\n", __func__, txHash.ToString(), numOfStandardTx);
            return false;
        }

        //! 3 - Double check and get the standard output
       auto stdOutputIt = std::find_if(tx->vpout.begin(), tx->vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
            return tx->IsStandardOutput();
        });

        if (stdOutputIt == tx->vpout.end()){
            LogPrintf("%s - transaction %s has no standard output\n", __func__, txHash.ToString());
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

    
        //! recovery address must receive 100% of the output amount
        const std::string destTest = HexStr( (*stdOutputIt)->GetStandardOutput()->scriptPubKey);
        if (is_output_recovery_address(destTest)) {
            if ( (*stdOutputIt)->GetStandardOutput()->nValue >= GetAllowedValueFraction(totalValue)) {
                LogPrintf("Found recovery amount at vout.n #%d\n");
                return true;
            }else{
                LogPrintf("%s Sending only #%d\n this will fail out of %d", __func__,  (*stdOutputIt)->GetStandardOutput()->nValue, totalValue);
            }
        }
        // auto total = totalValue - stdOutput->GetStandardOutput()->nValue - fee;
        // LogPrintf("%s The difference equals %d", __func__, totalValue);

    }
    return false;
}
