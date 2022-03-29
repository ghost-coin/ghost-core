// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <hash.h> // for signet block challenge hash
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <util/moneystr.h>
#include <key/keyutil.h>
#include <versionbitsinfo.h>
#include <chain/chainparamsimport.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include "chain/tx_blacklist.h"

int64_t CChainParams::GetCoinYearReward(int64_t nTime) const
{
    static const int64_t nSecondsInYear = 365 * 24 * 60 * 60;

    if (strNetworkID != "regtest") {
        // After HF2: 8%, 8%, 7%, 7%, 6%
        // if (nTime >= consensus.exploit_fix_2_time) {
        //     int64_t nPeriodsSinceHF2 = (nTime - consensus.exploit_fix_2_time) / (nSecondsInYear * 2);
        //     if (nPeriodsSinceHF2 >= 0 && nPeriodsSinceHF2 < 2) {
        //         return (8 - nPeriodsSinceHF2) * CENT;
        //     }
        //     return 6 * CENT;
        // }

        // Y1 5%, Y2 4%, Y3 3%, Y4 2%, ... YN 2%
        int64_t nYearsSinceGenesis = (nTime - genesis.nTime) / nSecondsInYear;
        if (nYearsSinceGenesis >= 0 && nYearsSinceGenesis < 3) {
            return (5 - nYearsSinceGenesis) * CENT;
        }
    }

    return nCoinYearReward;
};

bool CChainParams::PushTreasuryFundSettings(int64_t time_from, TreasuryFundSettings &settings)
{
    if (settings.nMinTreasuryStakePercent < 0 or settings.nMinTreasuryStakePercent > 100) {
        throw std::runtime_error("minstakepercent must be in range [0, 100].");
    }

    vTreasuryFundSettings.emplace_back(time_from, settings);

    return true;
};

// @notes(me) This one is from Particl Commenting it to use the version from legacy
// int64_t CChainParams::GetProofOfStakeReward(const CBlockIndex *pindexPrev, int64_t nFees) const
// {
//     int64_t nSubsidy;

//     nSubsidy = (pindexPrev->nMoneySupply / COIN) * GetCoinYearReward(pindexPrev->nTime) / (365 * 24 * (60 * 60 / nTargetSpacing));

//     return nSubsidy + nFees;
// };

int CChainParams::GetCoinYearPercent(int year) const
{
    if(static_cast<std::size_t>(year) < nBlockPerc.size()) {
        return nBlockPerc[year];
    } else {
        return 10;
    }
}

int64_t CChainParams::GetMaxSmsgFeeRateDelta(int64_t smsg_fee_prev, int64_t time) const
{
    int64_t max_delta = (smsg_fee_prev * consensus.smsg_fee_max_delta_percent) / 1000000;
    if (time >= consensus.smsg_fee_rate_fix_time) {
        return std::max((int64_t)1, max_delta);
    }
    return max_delta;
}

// bool CChainParams::CheckImportCoinbase(int nHeight, uint256 &hash) const
// {
//     // for (auto &cth : Params().vImportedCoinbaseTxns) {
//     //     if (cth.nHeight != (uint32_t)nHeight) {
//     //         continue;
//     //     }
//     //     if (hash == cth.hash) {
//     //         return true;
//     //     }
//     //     return error("%s - Hash mismatch at height %d: %s, expect %s.", __func__, nHeight, hash.ToString(), cth.hash.ToString());
//     // }

//     // return error("%s - Unknown height.", __func__);
// }

CAmount CChainParams::GetBaseBlockReward() const
{
    return nBlockReward;
};

CAmount CChainParams::GetProofOfStakeRewardAtYear(const int year) const
{
    auto x = (GetBaseBlockReward() * GetCoinYearPercent(year)) / 100;
    return x;
};

CAmount CChainParams::GetProofOfStakeRewardAtHeight(const int nHeight) const
{
    const CAmount nBlocksInAYear = (365 * 24 * 60 * 60) / GetTargetSpacing();
    const int currYear = nHeight / nBlocksInAYear;
    CAmount nSubsidy = GetProofOfStakeRewardAtYear(currYear);
    if(nHeight >= consensus.nBlockRewardIncreaseHeight)
        nSubsidy *= nBlockRewardIncrease;

    return nSubsidy;
}

int64_t CChainParams::GetProofOfStakeReward(const CBlockIndex *pindexPrev, const int64_t nFees) const
{
    int nHeight = pindexPrev ? pindexPrev->nHeight + 1 : 0;
    return GetProofOfStakeRewardAtHeight(nHeight) + nFees;
}

const TreasuryFundSettings *CChainParams::GetTreasuryFundSettings(int nHeight) const
{

    for (auto i = vTreasuryFundSettings.begin(); i != vTreasuryFundSettings.end(); ++i) {
         if (nHeight >= i->first) {
             return &i->second;
         }
    }

    return nullptr;
}

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn) const
{
    for (auto &hrp : bech32Prefixes)  {
        if (vchPrefixIn == hrp) {
            return true;
        }
    }

    return false;
}

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        auto &hrp = bech32Prefixes[k];
        if (vchPrefixIn == hrp) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
}

bool CChainParams::IsBech32Prefix(const char *ps, size_t slen, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        const auto &hrp = bech32Prefixes[k];
        size_t hrplen = hrp.size();
        if (hrplen > 0
            && slen > hrplen
            && strncmp(ps, (const char*)&hrp[0], hrplen) == 0) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

const std::pair<const char*, CAmount> regTestOutputs[] = {
    std::make_pair("585c2b3914d9ee51f8e710304e386531c3abcc82", 10000 * COIN),
    std::make_pair("c33f3603ce7c46b423536f0434155dad8ee2aa1f", 10000 * COIN),
    std::make_pair("72d83540ed1dcf28bfaca3fa2ed77100c2808825", 10000 * COIN),
    std::make_pair("69e4cc4c219d8971a253cd5db69a0c99c4a5659d", 10000 * COIN),
    std::make_pair("eab5ed88d97e50c87615a015771e220ab0a0991a", 10000 * COIN),
    std::make_pair("119668a93761a34a4ba1c065794b26733975904f", 10000 * COIN),
    std::make_pair("6da49762a4402d199d41d5778fcb69de19abbe9f", 10000 * COIN),
    std::make_pair("27974d10ff5ba65052be7461d89ef2185acbe411", 10000 * COIN),
    std::make_pair("89ea3129b8dbf1238b20a50211d50d462a988f61", 10000 * COIN),
    std::make_pair("3baab5b42a409b7c6848a95dfd06ff792511d561", 10000 * COIN),

    std::make_pair("649b801848cc0c32993fb39927654969a5af27b0", 5000 * COIN),
    std::make_pair("d669de30fa30c3e64a0303cb13df12391a2f7256", 5000 * COIN),
    std::make_pair("f0c0e3ebe4a1334ed6a5e9c1e069ef425c529934", 5000 * COIN),
    std::make_pair("27189afe71ca423856de5f17538a069f22385422", 5000 * COIN),
    std::make_pair("0e7f6fe0c4a5a6a9bfd18f7effdd5898b1f40b80", 5000 * COIN),
};
const size_t nGenesisOutputsRegtest = sizeof(regTestOutputs) / sizeof(regTestOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputs[] = {
    std::make_pair("ac91d9def79121740404da83c600d187e89f8aff", 685150.75 * COIN),//GZaPN2m7hRUDumJ7qZd65u3tT362khT3LF
    std::make_pair("4d43e963865032057ef616caec9e086ff6120ac2", 685150.75 * COIN),//GQtToV2LnHGhHy4LRVapLDMaukdDgzZZZV
    std::make_pair("b2671804429dc27f109da9c587487d0144567643", 685150.75 * COIN),//Ga7ECMeX8QUJTTvf9VUnYgTQUFxPChDqqU
    std::make_pair("f5a9f6f57a007a029e836a848eb4876dfa8e3a03", 685150.75 * COIN),//GgEsaUiyMA8j67pw9SkRiWn3sNcXdFiKb6
    std::make_pair("8837a52768d79e080d79b09cf4a116968ceef725", 685150.75 * COIN),//GWGAyWQ3zKBUdUq5zFhe96zhzqR7TeryjM
    std::make_pair("c7d1847cb9fb340415fa8baf45fca6f197f43321", 685150.75 * COIN),//Gc4TsqMNxNy5N2AewSbmX5Uf2gpccx9gve
    std::make_pair("dcd6e461bdad602cc1aa58a5d52e1e5967efa48e", 685150.75 * COIN),//GdycWJ7SwaptNxr4naQ3ybQu7fA1pZxPSN
    std::make_pair("9e322d2934db522f13a9a7c86226e4fa100aec6b", 685150.75 * COIN),//GYGPHkiAPvU7nN8tCWDsrJBrvyXVWq5cJ3
    std::make_pair("8d4dafe7bcf2d7572d39e3493dacbbc4c67278e1", 685150.75 * COIN),//GWj4kyJqTQGAhCFHDUer7CoXCu6AvGcoaU
    std::make_pair("f859e9757a493aadf12e60896bbe8b9b39eb26d2", 685150.75 * COIN),//GgV5htjf6WdNfYywRvYGcTTEv2e98RAc1y
    std::make_pair("81093899c94b6f86650ef57a8a4bcd724488bc21", 685150.75 * COIN),//GVcCdZoEvr52S46ug5G5BvoHKXpt9ZKV79
    std::make_pair("a00c672cf0ae25d9d42c2350bbb08fb6df344786", 685150.75 * COIN),//GYSBPbcb4n8ncSdGn9BSJt2rRWMjjJhbz9
    std::make_pair("59ca3ae2f992dc6a73ec668ac747a327a99adec0", 685150.75 * COIN),//GS2gpPVRhNdXP4mMEopMmPy8Y2txfQTdDR
    std::make_pair("a43f74d1d773ff485dc157714e6ed8772c88e523", 685150.75 * COIN),//GYpPFcM2XkPFd36SuhUgF5Tii9HKov5ZwL
    std::make_pair("451d033e99f26e254e118ced3b6d6e709e80429d", 685150.75 * COIN),//GQ9MoCWvDxEH1em3jdXEbjgag9kryk4FZ7
    std::make_pair("686c7590c3418d0dc49f16cbbcfe6528905dd9b1", 685150.75 * COIN),//GTN4cxVh4PryFGAnYJDhudrD1UamPofmGw
    std::make_pair("d755c6410c5008f88771bba9879336a01208d88f", 685150.75 * COIN),//GdUWT5jz8Jk61dP9fVUTWBsCPYNDNDq8WZ
    std::make_pair("0c59e6e59b1fe7cd0361a193356c39d4202bf5ca", 685150.75 * COIN),//GJyEDvdYg4RntmA5zZsveiEadQn12KVjGH
    std::make_pair("b9539acc18027f45f451c3567d47136e4aac6817", 685150.75 * COIN),//Gajqaa3ZU9VoWbtQtdA2qba7wYzYAKJ98m
    std::make_pair("d09288f9150d32166573cbeb0e7f34ef43403d20", 685150.75 * COIN),//GcrkWTjM8nbseKCv7sBdeBhVwMx3PGgwSe
};
const size_t nGenesisOutputs = sizeof(genesisOutputs) / sizeof(genesisOutputs[0]);

const std::pair<const char*, CAmount> genesisOutputsTestnet[] = {
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("6bae970439f44cfaf2b415af69863b0bfc0eef3b", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("9853372eacf2c949e6e2e4ead30ea63e5fb08f56", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("116dd7d52bbfe27a792d8c240da6bd9a73b1a356", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),
    std::make_pair("4ecbde8c1ada7ea8a47f536963ee3714c8e08638", 800000 * COIN),

};
const size_t nGenesisOutputsTestnet = sizeof(genesisOutputsTestnet) / sizeof(genesisOutputsTestnet[0]);


static CBlock CreateGenesisBlockRegTest(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";

    CMutableTransaction txNew;
    txNew.nVersion = GHOST_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsRegtest);
    for (size_t k = 0; k < nGenesisOutputsRegtest; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = regTestOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(regTestOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = GHOST_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockTestNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";

    CMutableTransaction txNew;
    txNew.nVersion = GHOST_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);
    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputsTestnet);
    for (size_t k = 0; k < nGenesisOutputsTestnet; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputsTestnet[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputsTestnet[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = GHOST_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}

static CBlock CreateGenesisBlockMainNet(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    const char *pszTimestamp = "BTC 000000000000000000c679bc2209676d05129834627c7b1c02d1018b224c6f37";

    CMutableTransaction txNew;
    txNew.nVersion = GHOST_TXN_VERSION;
    txNew.SetType(TXN_COINBASE);

    txNew.vin.resize(1);
    uint32_t nHeight = 0;  // bip34
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp)) << OP_RETURN << nHeight;

    txNew.vpout.resize(nGenesisOutputs);
    for (size_t k = 0; k < nGenesisOutputs; ++k) {
        OUTPUT_PTR<CTxOutStandard> out = MAKE_OUTPUT<CTxOutStandard>();
        out->nValue = genesisOutputs[k].second;
        out->scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex(genesisOutputs[k].first) << OP_EQUALVERIFY << OP_CHECKSIG;
        txNew.vpout[k] = out;
    }

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = GHOST_BLOCK_VERSION;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);

    return genesis;
}


/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;

        consensus.OpIsCoinstakeTime = 0x5A04EC00;       // 2017-11-10 00:00:00 UTC
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0x5C791EC0;           // 2019-03-01 12:00:00 UTC
        consensus.smsg_fee_time = 0x5D2DBC40;           // 2019-07-16 12:00:00 UTC
        consensus.bulletproof_time = 0x5D2DBC40;        // 2019-07-16 12:00:00 UTC
        consensus.rct_time = 0x5D2DBC40;                // 2019-07-16 12:00:00 UTC
        consensus.smsg_difficulty_time = 0x5D2DBC40;    // 2019-07-16 12:00:00 UTC
        // consensus.exploit_fix_1_time = 1614992554;      // 2021-03-06 01:00:00 GMT+8
        // Removed to make used of the default 0xffffffff
        // consensus.exploit_fix_2_time = 1626109200;      // 2021-07-12 17:00:00 UTC

        consensus.clamp_tx_version_time = 1646150400;   // 2022-03-01 17:00:00 UTC
        // consensus.exploit_fix_3_time = 1643734800;      // 2022-02-01 17:00:00 UTC
        consensus.smsg_fee_rate_fix_time = 1646150400;  // 2022-03-01 17:00:00 UTC
        // consensus.m_taproot_time = 1643734800;          // 2022-02-01 17:00:00 UTC

        consensus.m_frozen_anon_index = 2382; // Called LAST_ANONINDEX = 2379 by Barry
        consensus.m_frozen_blinded_height = 884433;

        consensus.smsg_fee_period = 5040;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 43;
        consensus.smsg_min_difficulty = 0x1effffff;
        consensus.smsg_difficulty_max_delta = 0xffff;

        // @note We will instead use this in place of m_frozen_blinded_height. They have the same meaning here
        // We use this to avoid a fork. Also this is already parameterize and testable
        // It could be better named anonRestrictionEndHeight since the height 300'000 denotes the limit
        consensus.anonRestrictionStartHeight = 300000;

        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1619222400; // April 24th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = 1628640000; // August 11th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 709632; // Approximately November 12th, 2021

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000af273924ccacbf60");//Chainwork at Block 2913

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xeccad59c62c2b669a746297d1f3ffb49c4de8620d6ad69c240079386130b2343k"); //Blockhash of Block 2913

        consensus.nMinRCTOutputDepth = 12;

        anonRecoveryAddress = "GeF4crGDi56ri72HtREoBuJQgiJLspJfAW";

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf2;
        pchMessageStart[1] = 0xf3;
        pchMessageStart[2] = 0xe1;
        pchMessageStart[3] = 0xb4;
        nDefaultPort = 51728;
        nBIP44IDLegacy = 0x8000002C;
        nBIP44IDCurrent = 0x80000213;

        nModifierInterval = 10 * 60;    // 10 minutes
        nStakeMinConfirmations = 225;   // 225 * 2 minutes
        nTargetSpacing = 120;           // 2 minutes
        nTargetTimespan = 24 * 60;      // 24 mins
        consensus.nLWMADiffUpgradeHeight = 40863;
        consensus.nZawyLwmaAveragingWindow = 45;
        nBlockReward = 6 * COIN;
        consensus.nBlockRewardIncreaseHeight = 40862;
        consensus.nGVRPayOnetimeAmt = 129000 * COIN;
        consensus.nOneTimeGVRPayHeight = 42308;
        consensus.nGVRTreasuryFundAdjustment = 458743;
        nBlockRewardIncrease = 2;
        nBlockPerc = {100, 100, 95, 90, 86, 81, 77, 74, 70, 66, 63, 60, 57, 54, 51, 49, 46, 44, 42, 40, 38, 36, 34, 32, 31, 29, 28, 26, 25, 24, 23, 21, 20, 19, 18, 17, 17, 16, 15, 14, 14, 13, 12, 12, 11, 10, 10};

        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlockMainNet(1592430039, 96427, 0x1f00ffff); // 2017-07-17 13:00:00
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00001e92daa9a7c945afdf3ce2736862b128f95c8966d3cda112caea98dd95f0"));
        assert(genesis.hashMerkleRoot == uint256S("0x3365ed8b8758ef69f7edeae23c1ec4bc7a893df9b7d3ff49e4846a1c29a2121f"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x9b4cee449a778b349408c8d3200c1e45dbf097926a69276240d2b767305bfac3"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("ghostseeder.ghostbyjohnmcafee.com");

        vTreasuryFundSettings.emplace_back(458743, TreasuryFundSettings("GgtiuDqVxAzg47yW7oSMmophe3tU8qoE1f", 66.67, 5040));
        vTreasuryFundSettings.emplace_back(140536, TreasuryFundSettings("GQJ4unJi6hAzd881YM17rEzPNWaWZ4AR3f", 66.67, 5040));
        vTreasuryFundSettings.emplace_back(40862,  TreasuryFundSettings("Ga7ECMeX8QUJTTvf9VUnYgTQUFxPChDqqU", 66.67, 5040)); //Approx each week to GVR Funds addr
        vTreasuryFundSettings.emplace_back(0,      TreasuryFundSettings("GQtToV2LnHGhHy4LRVapLDMaukdDgzZZZV", 33.00, 360));  //Approx each 12 hr payment to dev fund

        base58Prefixes[PUBKEY_ADDRESS]     = {0x26}; // G
        base58Prefixes[SCRIPT_ADDRESS]     = {0x61}; // g
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x39};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x3d};
        base58Prefixes[SECRET_KEY]         = {0xA6}; //PUBKEY_ADDRESS Prefix in int + 128 converted to hexadecimal
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x68, 0xDF, 0x7C, 0xBD}; // PGHST
        base58Prefixes[EXT_SECRET_KEY]     = {0x8E, 0x8E, 0xA8, 0xEA}; // XGHST
        base58Prefixes[STEALTH_ADDRESS]    = {0x14};
        base58Prefixes[EXT_KEY_HASH]       = {0x4b}; // X
        base58Prefixes[EXT_ACC_HASH]       = {0x17}; // A
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x88, 0xB2, 0x1E}; // xpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x88, 0xAD, 0xE4}; // xprv

        {
            std::map<int, std::string> bech32PrefixesMap{
                {PUBKEY_ADDRESS, "gp"},
                {SCRIPT_ADDRESS,"gw"},
                {PUBKEY_ADDRESS_256,"gl"},
                {SCRIPT_ADDRESS_256,"gj"},
                {SECRET_KEY,"gtx"},
                {EXT_PUBLIC_KEY,"gep"},
                {EXT_SECRET_KEY,"gex"},
                {STEALTH_ADDRESS,"gx"},
                {EXT_KEY_HASH,"gek"},
                {EXT_ACC_HASH,"gea"},
                {STAKE_ONLY_PKADDR,"gcs"},
            };

            for(auto&& p: bech32PrefixesMap)
            {
                bech32Prefixes[p.first].assign(p.second.begin(), p.second.end());
            }
        }

        bech32_hrp = "gw";
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                { 0, genesis.GetHash()},
                {10000,  uint256S("930135028fc99b99548621d76b6bb90604a45041aec1d1bd02117275cfdb4c53")},
                {20000,  uint256S("ae42938922053252fda2397f2a2fa13b8db7a710bfd1273c57677b72b6c52dc1")},
                {30000,  uint256S("28505249e831f1bc3c70b8c178b6049b089dfa139564ec23345093448d51d023")},
                {40000,  uint256S("5f137e0861dc1b0453ce8e0aa7b88f5f9fa5e2d2dd7715fc244e10341ff223e3")},
                {50000,  uint256S("2b1a546f606070c743bba1eb41f3e3d02e90e882943f8aa0344bdbe9e766a83d")},
                {60000,  uint256S("aca8d354e52e1fef6e9a2e616c79dd9ba5ca8e8e707dc6ffdd740c10bad6da80")},
                {70000,  uint256S("611e5450478e8d450df3232d079a284375141fc9367b2cd30f793281e3725e12")},
                {80000,  uint256S("ba8554481f68ec364aadd66436742890111fdb92f5760136b3224673000b2bc1")},
                {90000,  uint256S("b32fa9d575224bf6a96f3fc073c066a6eceea0391372dc2fbd752605823198d2")},
                {100000, uint256S("ccf886de73b02cf0ff48a0665c4d56ecc0e577c3f617708f0a7b9a366e13cd13")},
                {110000, uint256S("4f587d45e17588472cae4b53bd8a6f8ee043c1ab5ef373234179cd86eee71014")},
                {120000, uint256S("7bea9e48552a00765b5f29e2a490c92941d2047b14cbc93ba5550718cecc0b85")},
                {130000, uint256S("0fbf16425f05b19639194bd33830b8a263f005bc949a27ec8e08233ba059a768")},
                {140000, uint256S("087ff906072b35f2a198c16701bd60f4826ea0caee4eb212f635c3169e898c59")},
                {150000, uint256S("5ff55586701c4d426e3d7fb4a444036ced7e458d180d9fffa08cf84ff22766a0")},
                {160000, uint256S("d5212cf8d922a219246fe8d4ec4420ddf268ed0e5065c2c5804b35d203e25d75")},
                {170000, uint256S("5205a42512ae9f9ee6548a7ee1ee424a6d52fee262d89f7cb10319da21724b65")},
                {180000, uint256S("4d0dc61cb3b66ad6b3a67fb794328f47befe5d0b2f175535396c184b31e6edf1")},
                {190000, uint256S("98ceccbf4cf61aa3f0a8d9bcf93d47f92f7ac07fcaf8f94daa85340e9df04567")},
                {200000, uint256S("3bcb537e5df3784fca64c557a7f7166d37e5cd224c92d4beba6c9d12470c4e86")},
                {210000, uint256S("eb6014b6d22b807f484ce6afb056a11b1c21d68975faf24f9ecc28f0e80a0993")},
                {220000, uint256S("635a7cace1e72e1f7b85ddfa567811750c47da48e75a796c392e9d07ac37ce7e")},
                {230000, uint256S("84a3066c2cd035cb222a35d7a3a5cc46ec63016a6fc449eab67cbd298c05a7f0")},
                {240000, uint256S("33b9cabb5d8bfdc502c59384d907b4deb64dfd3bc54f8c8809ef0a911a2909d0")},
                {250000, uint256S("4cb92f5ae1d986230c5ab4ba4e3e62640e47ed20c89a76458afb65617ceda742")},
                {260000, uint256S("c43be53b56ee576dafd112bc733a6f56a4a2e4a9222e4305cc6b893c7d968dae")},
                {270000, uint256S("27609adb02319a8c3583ef5c564499ffc7ed4796091a0b58da0a82e862b4cfa2")},
                {280000, uint256S("8be64d38cf598a5d0dc57c84fbf377d5e3178f4abedd6b9ee00f5db4600ab8fa")},
                {290000, uint256S("19b24cc079a3869f8803ae8c7602f39ff473b17488f705520a4cfdddd4495e98")},
                {300000, uint256S("9251bf689bc354084370385cca94fca0939b6cce42d49430fa01cb8f9f3cc07c")}
            }
        };

        chainTxData = ChainTxData {
            1628232496,
            343652,
            0.0081
        };

       anonRestricted = DEFAULT_ANON_RESTRICTED;

       blacklistedAnonTxs.insert(anon_index_blacklist.begin(), anon_index_blacklist.end());
    }

    void SetOld()
    {
        consensus.BIP16Exception = uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.CSVHeight = 419328; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
        consensus.SegwitHeight = 481824; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
        consensus.MinBIP9WarningHeight = consensus.SegwitHeight + consensus.nMinerConfirmationWindow;
        consensus.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        genesis = CreateGenesisBlock(1231006505, 2083236893, 0x1d00ffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 0;
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;

        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0;
        consensus.smsg_fee_time = 0x5C67FB40;           // 2019-02-16 12:00:00
        consensus.bulletproof_time = 0x5C67FB40;        // 2019-02-16 12:00:00
        consensus.rct_time = 0;
        consensus.smsg_difficulty_time = 0x5D19F5C0;    // 2019-07-01 12:00:00
        // consensus.exploit_fix_1_time = 1614268800;      // 2021-02-25 16:00:00

        consensus.clamp_tx_version_time = 1646150400;   // 2022-03-01 17:00:00
        consensus.smsg_fee_rate_fix_time = 1646150400;  // 2022-03-01 17:00:00
        // consensus.m_taproot_time = 1641056400;          // 2022-01-01 17:00:00 UTC

        consensus.smsg_fee_period = 5040;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 43;
        consensus.smsg_min_difficulty = 0x1effffff;
        consensus.smsg_difficulty_max_delta = 0xffff;

        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true; // No retargeting for now in testnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = 1619222400; // April 24th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = 1628640000; // August 11th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        consensus.nMinRCTOutputDepth = 2;
        consensus.m_frozen_anon_index = 20;
        consensus.anonRestrictionStartHeight = 50;

        pchMessageStart[0] = 0x08;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x05;
        pchMessageStart[3] = 0x0b;
        nDefaultPort = 51928;
        nBIP44IDCurrent = 0x80000213;
        nBIP44IDLegacy = nBIP44IDCurrent;

        nModifierInterval = 2 * 60;    
        nStakeMinConfirmations = 12;   
        nTargetSpacing = 5;           
        nTargetTimespan = 16 * 60;

        consensus.nLWMADiffUpgradeHeight = 40863;
        consensus.nZawyLwmaAveragingWindow = 45;
        nBlockReward = 6 * COIN;
        consensus.nBlockRewardIncreaseHeight = 40862;
        consensus.nGVRPayOnetimeAmt = 129000 * COIN;
        consensus.nOneTimeGVRPayHeight = 42308;
        consensus.nGVRTreasuryFundAdjustment = 140536;
        consensus.m_frozen_blinded_height = 884433;

        nBlockRewardIncrease = 2;
        nBlockPerc = {100, 100, 95, 90, 86, 81, 77, 74, 70, 66, 63, 60, 57, 54, 51, 49, 46, 44, 42, 40, 38, 36, 34, 32, 31, 29, 28, 26, 25, 24, 23, 21, 20, 19, 18, 17, 17, 16, 15, 14, 14, 13, 12, 12, 11, 10, 10};

        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlockTestNet(1645041600, 39904, 0x1f00ffff);

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00009f9cb2deb45406db7b8e34961eb1afdb6c4f2908d58bddf3312e8c15a43a"));
        assert(genesis.hashMerkleRoot == uint256S("0xc088a85a1e2aa0a55900f079078075af187600d5d242c09d5139fc3bbb23f1f8"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x5e35a3292cbf2e112a65236817519565a3c50544dd24d602ceba985dba4e806c"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top

        base58Prefixes[PUBKEY_ADDRESS]     = {0x4B}; // X
        base58Prefixes[SCRIPT_ADDRESS]     = {0x89}; // x
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("tph",(const char*)"tph"+3);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr",(const char*)"tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl",(const char*)"tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj",(const char*)"tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx",(const char*)"tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep",(const char*)"tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex",(const char*)"tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps",(const char*)"tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek",(const char*)"tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea",(const char*)"tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs",(const char*)"tpcs"+4);

        bech32_hrp = "tpw";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        anonRestricted = gArgs.GetBoolArg("-anonrestricted", DEFAULT_ANON_RESTRICTED);
        // Full Script pubkey of the recovery addr: 76a91418cf988c85fdff42269cf1d39c526aa3530c778d88ac
        anonRecoveryAddress = "XXZL34hbjru176j3q3f1EkofGCSprn5Hbq";
        checkpointData = {
            {
                { 0, genesis.GetHash()},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 12e6a081d1874b3dfff99e120b8e22599e15730c23c88805740c507c11c91809
            /* nTime    */ 0,
            /* nTxCount */ 0,
            /* dTxRate  */ 0
        };
        blacklistedAnonTxs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    }
};

/**
 * Signet
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const ArgsManager& args) {
        std::vector<uint8_t> bin;
        vSeeds.clear();

        if (!args.IsArgSet("-signetchallenge")) {
            bin = ParseHex("512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae");
            vSeeds.emplace_back("178.128.221.177");
            vSeeds.emplace_back("2a01:7c8:d005:390::5");
            vSeeds.emplace_back("v7ajjeirttkbnt32wpy3c6w3emwnfr3fkla7hpxcfokr3ysd3kqtzmqd.onion:38333");

            consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000019fd16269a");
            consensus.defaultAssumeValid = uint256S("0x0000002a1de0f46379358c1fd09906f7ac59adf3712323ed90eb59e4c183c020"); // 9434
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 0000002a1de0f46379358c1fd09906f7ac59adf3712323ed90eb59e4c183c020
                /* nTime    */ 1603986000,
                /* nTxCount */ 9582,
                /* dTxRate  */ 0.00159272030651341,
            };
        } else {
            const auto signet_challenge = args.GetArgs("-signetchallenge");
            if (signet_challenge.size() != 1) {
                throw std::runtime_error(strprintf("%s: -signetchallenge cannot be multiple values.", __func__));
            }
            bin = ParseHex(signet_challenge[0]);

            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", signet_challenge[0]);
        }

        if (args.IsArgSet("-signetseednode")) {
            vSeeds = args.GetArgs("-signetseednode");
        }

        strNetworkID = CBaseChainParams::TESTNET;// TODO(me): Reactivate signet
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Exception = uint256{};
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1815; // 90% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256S("00000377ae000000000000000000000000000000000000000000000000000000");
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay
        consensus.anonRestrictionStartHeight = gArgs.GetArg("-anonrestrictionstartheight", DEFAULT_ANON_RESTRICTION_START_HEIGHT);


        // message start is defined as the first 4 bytes of the sha256d of the block script
        CHashWriter h(SER_DISK, 0);
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        memcpy(pchMessageStart, hash.begin(), 4);

        nDefaultPort = 38333;
        nPruneAfterHeight = 1000;
        // Full Script pubkey of the recovery addr: 76a91418cf988c85fdff42269cf1d39c526aa3530c778d88ac
        anonRecoveryAddress = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it";

        genesis = CreateGenesisBlock(1598918400, 52613770, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6"));
        assert(genesis.hashMerkleRoot == uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

        vFixedSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
        anonRestricted = gArgs.GetBoolArg("-anonrestricted", DEFAULT_ANON_RESTRICTED);

        std::string blacklisted = gArgs.GetArg("-blacklistedanon", "");
        blacklistedAnonTxs = GetAnonIndexFromString(blacklisted);
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID =  CBaseChainParams::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 500; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        consensus.CSVHeight = 432; // CSV activated on regtest (Used in rpc activation tests)
        consensus.SegwitHeight = 0; // SEGWIT is always activated on regtest unless overridden
        consensus.MinBIP9WarningHeight = 0;

        consensus.OpIsCoinstakeTime = 0;
        consensus.fAllowOpIsCoinstakeWithP2PKH = false;
        consensus.nPaidSmsgTime = 0;
        consensus.smsg_fee_time = 0;
        consensus.bulletproof_time = 0;
        consensus.rct_time = 0;
        consensus.smsg_difficulty_time = 0;

        consensus.clamp_tx_version_time = 0;

        consensus.smsg_fee_period = 50;
        consensus.smsg_fee_funding_tx_per_k = 200000;
        consensus.smsg_fee_msg_per_day_per_k = 50000;
        consensus.smsg_fee_max_delta_percent = 4300;
        consensus.smsg_min_difficulty = 0x1f0fffff;
        consensus.smsg_difficulty_max_delta = 0xffff;
        consensus.smsg_fee_rate_fix_time = 0;
        // consensus.m_taproot_time = 0;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        consensus.nMinRCTOutputDepth = 2;

        consensus.anonRestrictionStartHeight = gArgs.GetArg("-anonrestrictionstartheight", DEFAULT_ANON_RESTRICTION_START_HEIGHT);

        pchMessageStart[0] = 0x09;
        pchMessageStart[1] = 0x12;
        pchMessageStart[2] = 0x06;
        pchMessageStart[3] = 0x0c;
        nDefaultPort = 11928;
        nBIP44IDCurrent = 0x80000001;
        nBIP44IDLegacy = nBIP44IDCurrent;

        nModifierInterval = 2 * 60;    // 10 minutes
        nStakeMinConfirmations = 12;   // 12 * 2 minutes
        nTargetSpacing = 5;           // 5 seconds
        nTargetTimespan = 16 * 60;      // 16 mins
        consensus.nLWMADiffUpgradeHeight = 40863;
        consensus.nZawyLwmaAveragingWindow = 45;
        nBlockReward = 6 * COIN;
        consensus.nBlockRewardIncreaseHeight = 40862;
        consensus.nGVRPayOnetimeAmt = 129000 * COIN;
        consensus.nOneTimeGVRPayHeight = 42308;
        consensus.nGVRTreasuryFundAdjustment = 140536;
        nBlockRewardIncrease = 2;
        nStakeTimestampMask = 0;
        nBlockPerc = {100, 100, 95, 90, 86, 81, 77, 74, 70, 66, 63, 60, 57, 54, 51, 49, 46, 44, 42, 40, 38, 36, 34, 32, 31, 29, 28, 26, 25, 24, 23, 21, 20, 19, 18, 17, 17, 16, 15, 14, 14, 13, 12, 12, 11, 10, 10};

        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        genesis = CreateGenesisBlockRegTest(1543578342, 1, 0x207fffff);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x0df42459b6ced4f7c9ec8c7d4c4efe1a9ca89441f17e8c2485a80c247d0544b2"));
        assert(genesis.hashMerkleRoot == uint256S("0xf89653c7208af2c76a3070d436229fb782acbd065bd5810307995b9982423ce7"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0x36b66a1aff91f34ab794da710d007777ef5e612a320e1979ac96e5f292399639"));


        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;

        std::string blacklisted = gArgs.GetArg("-blacklistedanon", "");
        blacklistedAnonTxs = GetAnonIndexFromString(blacklisted);

        checkpointData = {
            {
                {0, uint256S("0x0df42459b6ced4f7c9ec8c7d4c4efe1a9ca89441f17e8c2485a80c247d0544b2")},
            }
        };

        base58Prefixes[PUBKEY_ADDRESS]     = {0x76}; // p
        base58Prefixes[SCRIPT_ADDRESS]     = {0x7a};
        base58Prefixes[PUBKEY_ADDRESS_256] = {0x77};
        base58Prefixes[SCRIPT_ADDRESS_256] = {0x7b};
        base58Prefixes[SECRET_KEY]         = {0x2e};
        base58Prefixes[EXT_PUBLIC_KEY]     = {0xe1, 0x42, 0x78, 0x00}; // ppar
        base58Prefixes[EXT_SECRET_KEY]     = {0x04, 0x88, 0x94, 0x78}; // xpar
        base58Prefixes[STEALTH_ADDRESS]    = {0x15}; // T
        base58Prefixes[EXT_KEY_HASH]       = {0x89}; // x
        base58Prefixes[EXT_ACC_HASH]       = {0x53}; // a
        base58Prefixes[EXT_PUBLIC_KEY_BTC] = {0x04, 0x35, 0x87, 0xCF}; // tpub
        base58Prefixes[EXT_SECRET_KEY_BTC] = {0x04, 0x35, 0x83, 0x94}; // tprv

        bech32Prefixes[PUBKEY_ADDRESS].assign       ("rghost",(const char*)"rghost"+6);
        bech32Prefixes[SCRIPT_ADDRESS].assign       ("tpr",(const char*)"tpr"+3);
        bech32Prefixes[PUBKEY_ADDRESS_256].assign   ("tpl",(const char*)"tpl"+3);
        bech32Prefixes[SCRIPT_ADDRESS_256].assign   ("tpj",(const char*)"tpj"+3);
        bech32Prefixes[SECRET_KEY].assign           ("tpx",(const char*)"tpx"+3);
        bech32Prefixes[EXT_PUBLIC_KEY].assign       ("tpep",(const char*)"tpep"+4);
        bech32Prefixes[EXT_SECRET_KEY].assign       ("tpex",(const char*)"tpex"+4);
        bech32Prefixes[STEALTH_ADDRESS].assign      ("tps",(const char*)"tps"+3);
        bech32Prefixes[EXT_KEY_HASH].assign         ("tpek",(const char*)"tpek"+4);
        bech32Prefixes[EXT_ACC_HASH].assign         ("tpea",(const char*)"tpea"+4);
        bech32Prefixes[STAKE_ONLY_PKADDR].assign    ("tpcs",(const char*)"tpcs"+4);

        bech32_hrp = "rtpw";

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        anonRestricted = gArgs.GetBoolArg("-anonrestricted", DEFAULT_ANON_RESTRICTED);
        consensus.m_frozen_anon_index = gArgs.GetArg("-lastanonindex", DEFAULT_LAST_ANON_INDEX);

        // Full Script pubkey of the recovery addr: 76a91418cf988c85fdff42269cf1d39c526aa3530c778d88ac
        anonRecoveryAddress = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it";
        // Pubkey:  pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it
        // PrivKey: 7shnesmjFcQZoxXCsNV55v7hrbQMtBfMNscuBkYrLa1mcJNPbXhU
    }

    void SetOld()
    {
        genesis = CreateGenesisBlock(1296688602, 2, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        /*
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        */

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int min_activation_height)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        consensus.vDeployments[d].min_activation_height = min_activation_height;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (args.IsArgSet("-segwitheight")) {
        int64_t height = args.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() < 3 || 4 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:min_activation_height]");
        }
        int64_t nStartTime, nTimeout;
        int min_activation_height = 0;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() >= 4 && !ParseInt32(vDeploymentParams[3], &min_activation_height)) {
            throw std::runtime_error(strprintf("Invalid min_activation_height (%s)", vDeploymentParams[3]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, min_activation_height);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, min_activation_height=%d\n", vDeploymentParams[0], nStartTime, nTimeout, min_activation_height);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

const CChainParams *pParams() {
    return globalChainParams.get();
}

std::unique_ptr<CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    }
    /* else if (chain == CBaseChainParams::SIGNET) {
        return std::unique_ptr<CChainParams>(new SigNetParams(args));
    }*/
    else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}


void SetOldParams(std::unique_ptr<CChainParams> &params)
{
    if (params->NetworkID() == CBaseChainParams::MAIN) {
        return ((CMainParams*)params.get())->SetOld();
    }
    if (params->NetworkID() == CBaseChainParams::REGTEST) {
        return ((CRegTestParams*)params.get())->SetOld();
    }
};

void ResetParams(std::string sNetworkId, bool fParticlModeIn)
{
    // Hack to pass old unit tests
    globalChainParams = CreateChainParams(gArgs, sNetworkId);
    if (!fParticlModeIn) {
        SetOldParams(globalChainParams);
    }
};

/**
 * Mutable handle to regtest params
 */
CChainParams &RegtestParams()
{
    return *globalChainParams.get();
};

std::set<std::uint64_t> GetAnonIndexFromString(const std::string& str) {
    std::set<std::uint64_t> internal;
    std::stringstream ss(str);
    std::string tok;
    while (getline(ss, tok, ',')) {
        internal.insert( std::stoll(tok) );
    }
    return internal;
}
