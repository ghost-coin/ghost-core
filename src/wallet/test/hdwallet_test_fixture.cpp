// Copyright (c) 2017-2023 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/test/hdwallet_test_fixture.h>

#include <rpc/server.h>
#include <wallet/db.h>
#include <wallet/hdwallet.h>
#include <wallet/rpc/wallet.h>
#include <wallet/coincontrol.h>
#include <validation.h>
#include <util/system.h>
#include <blind.h>
#include <node/miner.h>
#include <pos/miner.h>
#include <timedata.h>

#include <boost/test/unit_test.hpp>


HDWalletTestingSetup::HDWalletTestingSetup(const std::string &chainName):
    TestingSetup(chainName, { "-balancesindex" }, true, true, true /* fParticlMode */),
    m_wallet_loader{interfaces::MakeWalletLoader(*m_node.chain, *Assert(m_node.args))}
{
    pwalletMain = std::make_shared<CHDWallet>(m_node.chain.get(), "", CreateMockWalletDatabaseBDB());
    WalletContext& wallet_context = *m_wallet_loader->context();
    AddWallet(wallet_context, pwalletMain);
    pwalletMain->LoadWallet();
    m_chain_notifications_handler = m_node.chain->handleNotifications({ pwalletMain.get(), [](CHDWallet*) {} });
    m_wallet_loader->registerRpcs();
}

HDWalletTestingSetup::~HDWalletTestingSetup()
{
    WalletContext& wallet_context = *m_wallet_loader->context();
    RemoveWallet(wallet_context, pwalletMain, std::nullopt);
    pwalletMain->Finalise();
    pwalletMain.reset();

    particl::mapStakeSeen.clear();
    particl::listStakeSeen.clear();
}

void StakeNBlocks(CHDWallet *pwallet, size_t nBlocks)
{
    ChainstateManager *pchainman{nullptr};
    if (pwallet->HaveChain()) {
        pchainman = pwallet->chain().getChainman();
    }
    if (!pchainman) {
        LogPrintf("Error: Chainstate manager not found.\n");
        return;
    }

    size_t nStaked = 0;
    size_t k, nTries = 10000;
    for (k = 0; k < nTries; ++k) {
        int nBestHeight = WITH_LOCK(cs_main, return pchainman->ActiveChain().Height());

        int64_t nSearchTime = GetAdjustedTimeInt() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        std::unique_ptr<node::CBlockTemplate> pblocktemplate = pwallet->CreateNewBlock();
        BOOST_REQUIRE(pblocktemplate.get());

        if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime)) {
            CBlock *pblock = &pblocktemplate->block;

            if (CheckStake(*pchainman, pblock)) {
                nStaked++;
            }
        }

        if (nStaked >= nBlocks) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    BOOST_REQUIRE(k < nTries);
    SyncWithValidationInterfaceQueue();
}

bool CreateValidBlock(CHDWallet *pwallet, CBlock &block_out)
{
    ChainstateManager *pchainman{nullptr};
    if (pwallet->HaveChain()) {
        pchainman = pwallet->chain().getChainman();
    }
    if (!pchainman) {
        LogPrintf("Error: Chainstate manager not found.\n");
        return false;
    }

    size_t k, nTries = 10000;
    for (k = 0; k < nTries; ++k) {
        int nBestHeight = WITH_LOCK(cs_main, return pchainman->ActiveChain().Height());

        int64_t nSearchTime = GetAdjustedTimeInt() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        std::unique_ptr<node::CBlockTemplate> pblocktemplate = pwallet->CreateNewBlock();
        BOOST_REQUIRE(pblocktemplate.get());

        if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight + 1, nSearchTime)) {
            block_out = pblocktemplate->block;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

uint256 AddTxn(CHDWallet *pwallet, CTxDestination &dest, OutputTypes input_type, OutputTypes output_type, CAmount amount, CAmount exploit_amount, std::string expect_error)
{
    uint256 txid;
    BOOST_REQUIRE(IsValidDestination(dest));
    {
    LOCK(pwallet->cs_wallet);

    std::string sError;
    std::vector<CTempRecipient> vecSend;
    vecSend.emplace_back(output_type, amount, dest);

    CTransactionRef tx_new;
    CWalletTx wtx(tx_new, TxStateInactive{});
    CTransactionRecord rtx;
    CAmount nFee;
    CCoinControl coinControl;
    coinControl.m_debug_exploit_anon = exploit_amount;
    int rv = input_type == OUTPUT_RINGCT ?
        pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &coinControl, sError) :
        input_type == OUTPUT_CT ?
        pwallet->AddBlindedInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError) :
        pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError);
    BOOST_REQUIRE(rv == 0);

    rv = pwallet->SubmitTxMemoryPoolAndRelay(wtx, sError, true);
    if (expect_error.empty()) {
        BOOST_REQUIRE(rv == 1);
    } else {
        BOOST_CHECK(sError == expect_error);
        BOOST_REQUIRE(rv == 0);
    }

    txid = wtx.GetHash();
    }
    SyncWithValidationInterfaceQueue();

    return txid;
}

void StakeNBlocks(CHDWallet *pwallet, size_t nBlocks)
{
    size_t nStaked = 0;
    size_t k, nTries = 10000;
    for (k = 0; k < nTries; ++k) {
        int nBestHeight = WITH_LOCK(cs_main, return ::ChainActive().Height());

        int64_t nSearchTime = GetAdjustedTime() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        std::unique_ptr<CBlockTemplate> pblocktemplate = pwallet->CreateNewBlock();
        BOOST_REQUIRE(pblocktemplate.get());

        if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime)) {
            CBlock *pblock = &pblocktemplate->block;

            if (CheckStake(pblock)) {
                nStaked++;
            }
        }

        if (nStaked >= nBlocks) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    BOOST_REQUIRE(k < nTries);
    SyncWithValidationInterfaceQueue();
}

uint256 AddTxn(CHDWallet *pwallet, CTxDestination &dest, OutputTypes input_type, OutputTypes output_type, CAmount amount, CAmount exploit_amount, std::string expect_error)
{
    uint256 txid;
    BOOST_REQUIRE(IsValidDestination(dest));
    {
    LOCK(pwallet->cs_wallet);

    std::string sError;
    std::vector<CTempRecipient> vecSend;
    vecSend.emplace_back(output_type, amount, dest);

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    CCoinControl coinControl;
    coinControl.m_debug_exploit_anon = exploit_amount;
    int rv = input_type == OUTPUT_RINGCT ?
        pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &coinControl, sError) :
        input_type == OUTPUT_CT ?
        pwallet->AddBlindedInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError) :
        pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError);
    BOOST_REQUIRE(rv == 0);

    rv = wtx.SubmitMemoryPoolAndRelay(sError, true);
    if (expect_error.empty()) {
        BOOST_REQUIRE(rv == 1);
    } else {
        BOOST_CHECK(sError == expect_error);
        BOOST_REQUIRE(rv == 0);
    }

    txid = wtx.GetHash();
    }
    SyncWithValidationInterfaceQueue();

    return txid;
}
