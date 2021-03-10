// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_BLIND_H
#define PARTICL_BLIND_H

#include <secp256k1.h>
#include <secp256k1_bulletproofs.h>
#include <stdint.h>
#include <vector>

#include <amount.h>

// Maximum value of tainted blinded output that can be spent without being whitelisted
const static CAmount MAX_TAINTED_VALUE_OUT = 500 * COIN;

class uint256;

extern secp256k1_context *secp256k1_ctx_blind;
extern secp256k1_scratch_space *blind_scratch;
extern secp256k1_bulletproof_generators *blind_gens;

int SelectRangeProofParameters(uint64_t nValueIn, uint64_t &minValue, int &exponent, int &nBits);

int GetRangeProofInfo(const std::vector<uint8_t> &vRangeproof, int &rexp, int &rmantissa, CAmount &min_value, CAmount &max_value);

void InitBlinding();
bool IsTaintedBlindOutput(const uint256 &txid);

void ECC_Start_Blinding();
void ECC_Stop_Blinding();

#endif  // PARTICL_BLIND_H
