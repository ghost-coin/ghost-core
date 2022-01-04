// Copyright (c) 2021 Ghost Core Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADAPTER_H
#define ADAPTER_H

#include <chainparams.h>
#include <consensus/params.h>
#include <util/system.h>
#include <validation.h>
#include <script/standard.h>
#include <key_io.h>
#include "chain/tx_whitelist.h"


bool is_output_recovery_address(const CTxOutStandard*);
bool is_anonblind_transaction_ok(const CTransactionRef& tx, const size_t totalRing, std::string& errorMsg);

inline boost::optional<std::size_t> standardOutputIndex(const std::vector<CTxOutBaseRef>& vpout) {
    auto stdOutputIt = std::find_if(vpout.begin(), vpout.end(), [](const std::shared_ptr<CTxOutBase>& tx){
        return tx->IsStandardOutput();
    });

    if (stdOutputIt == vpout.end()) {
        return boost::none;
    }

    return std::distance(vpout.begin(), stdOutputIt);
}

bool ignoreTx(const CTransaction &tx);

#endif // ADAPTER_H
