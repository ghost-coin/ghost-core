// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <kernel/chainparams.h> // IWYU pragma: export

#include <memory>

class ArgsManager;

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 */
std::unique_ptr<CChainParams> CreateChainParams(const ArgsManager& args, const ChainType chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();
const CChainParams *pParams();
bool HaveParams();

/**
 * Sets the params returned by Params() to those for the given chain type.
 */
void SelectParams(const ChainType chain);

/**
 * Toggle old parameters for unit tests
 */
void SetOldParams(std::unique_ptr<CChainParams> &params);
void ResetParams(const ChainType chain, bool fParticlModeIn);

/**
 * mutable handle to regtest params
 */
CChainParams &RegtestParams();

#endif // BITCOIN_CHAINPARAMS_H
