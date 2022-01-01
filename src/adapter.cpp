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

bool is_output_recovery_address(const CTxOutStandard* standardOutput) {
    const std::string& recoveryAddress = Params().GetRecoveryAddress();
    std::vector<std::vector<unsigned char> > solutions;
    const auto& recoveryAddr = CBitcoinAddress{recoveryAddress};

    const TxoutType txoutType = Solver(standardOutput->GetStandardOutput()->scriptPubKey, solutions);

    if (txoutType != TxoutType::PUBKEYHASH || solutions.size() != 1U) {
        return false;
    }

    CKeyID keyId;

    if (!recoveryAddr.GetKeyID(keyId)) {
        LogPrintf("%s - Failed to get the keyId of the recovery address\n", __func__);
        return false;
    }

    return solutions[0] == ToByteVector(keyId);
}

CAmount GetAllowedValueFraction(const CAmount value)
{
    return value;
}


bool is_anonblind_transaction_ok(const CTransactionRef& tx, const size_t totalRing)
{
    static_assert(std::is_unsigned<decltype(totalRing)>::value, "totalRing is only treated for unsigned cases");
    if (totalRing == 0)
    {
        return true;
    }

    if (totalRing > 0) {
        const uint256& txHash = tx->GetHash();

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
        const std::size_t standardTxCount = std::count_if(tx->vpout.begin(), tx->vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
            return tx->IsStandardOutput();
        });

        if (standardTxCount != 1) {
            LogPrintf("%s - transaction %s has more than 1 standard numOfStandardTx=%d\n", __func__, txHash.ToString(), standardTxCount);
            return false;
        }

        //! 3 - Double check and get the standard output
        std::optional<std::size_t> stdOutputIndex = standardOutputIndex(tx->vpout);

        if (!stdOutputIndex || !tx->vpout[*stdOutputIndex]->IsStandardOutput()) {
            LogPrintf("%s - transaction %s has no standard output\n", __func__, txHash.ToString());
            return false;
        }

        //! 4 - Making sure the type of the other output is DATA
        //! If the size is not one then it means there is another output to check
        if (tx->vpout.size() != 1) {
            const std::size_t dataTxCount = std::count_if(tx->vpout.begin(), tx->vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
                return tx->nVersion == OUTPUT_DATA;
            });

            if (dataTxCount != 1) {
                LogPrintf("%s - transaction %s has no data output\n", __func__, txHash.ToString());
                return false;
            }
        }

        // Make the check of fees here
        CAmount fee;
        for (unsigned int i=0; i < outSize; ++i) {
            if (tx->vpout[i]->GetCTFee(fee)) {
                if (fee >= 10 * COIN ) {
                    LogPrintf("%s - Fee greater than 10*COIN value %s %d\n", __func__, txHash.ToString(), fee);
                    return false;
                }
            }
        }

        //! recovery address must receive 100% of the output amount
        const auto& standardOutput = tx->vpout[*stdOutputIndex]->GetStandardOutput();

        if (is_output_recovery_address(standardOutput)) {
            if (standardOutput->GetStandardOutput()->nValue >= GetAllowedValueFraction(totalValue)) {
                LogPrintf("Found recovery amount at vout.n #%d\n");
                return true;
            } else {
                LogPrintf("%s Sending only #%d\n this will fail out of %d", __func__,  standardOutput->GetStandardOutput()->nValue, totalValue);
            }
        }
    }
    return false;
}


std::optional<std::size_t> standardOutputIndex(const std::vector<CTxOutBaseRef>& vpout) {
    auto stdOutputIt = std::find_if(vpout.begin(), vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
        return tx->IsStandardOutput();
    });

    if (stdOutputIt == vpout.end()) {
        return std::nullopt;
    }

    return std::distance(vpout.begin(), stdOutputIt);
}

bool ignoreTx(const CTransaction &tx) {
    return tx_to_allow.count(tx.GetHash());
}