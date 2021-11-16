// Copyright (c) 2021 Ghost Core Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADAPTER_H
#define ADAPTER_H

#include <chainparams.h>
#include <consensus/params.h>
#include <util/system.h>
#include <validation.h>

bool is_ghost_debug();
bool exploit_fixtime_passed(uint32_t nTime);

const uint256 TEST_TX = uint256S("c22280de808fdc24e1831a0daa91f34d01b93186d8f02e780788ed9f2c93aa24");

bool is_output_recovery_address(const CPubKey& pubkey);
bool is_anonblind_transaction_ok(const CTransactionRef& tx, const size_t totalRing);
#endif // ADAPTER_H
