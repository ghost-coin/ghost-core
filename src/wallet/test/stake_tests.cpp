// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/hdwallet.h>
#include <wallet/coincontrol.h>
#include <interfaces/chain.h>

#include <wallet/test/hdwallet_test_fixture.h>
#include <chainparams.h>
#include <coins.h>
#include <net.h>
#include <validation.h>
#include <blind.h>
#include <rpc/rpcutil.h>
#include <util/string.h>
#include <util/translation.h>
#include <node/blockstorage.h>
#include <consensus/validation.h>

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(stake_tests, StakeTestingSetup)


static void AddTxn(CHDWallet *pwallet, CTxDestination &dest, OutputTypes output_type, CAmount amount)
{
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
    BOOST_CHECK(0 == pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError));

    BOOST_REQUIRE(wtx.SubmitMemoryPoolAndRelay(sError, true));
    }
    SyncWithValidationInterfaceQueue();
}

static void TryAddBadTxn(CHDWallet *pwallet, CTxDestination &dest, OutputTypes output_type, CAmount amount)
{
    BOOST_REQUIRE(IsValidDestination(dest));
    {
    LOCK(pwallet->cs_wallet);

    std::vector<CTempRecipient> vecSend;
    std::string sError;
    CTempRecipient r;
    r.nType = output_type;
    r.SetAmount(amount);
    r.address = dest;
    vecSend.push_back(r);

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee, nFeeCheck = 0;
    CCoinControl coinControl;
    BOOST_CHECK(0 == pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError));

    BOOST_REQUIRE(wtx.tx->vpout[0]->nVersion == OUTPUT_DATA);
    BOOST_REQUIRE(wtx.tx->vpout[0]->GetCTFee(nFeeCheck));
    BOOST_REQUIRE(nFee == nFeeCheck);
    nFee -= 1;
    BOOST_REQUIRE(wtx.tx->vpout[0]->SetCTFee(nFee));

    BOOST_REQUIRE(!wtx.SubmitMemoryPoolAndRelay(sError, true));
    BOOST_REQUIRE(sError == "bad-commitment-sum");
    }
    SyncWithValidationInterfaceQueue();
}

static void DisconnectTip(CChainState &chainstate_active, CTxMemPool* mempool, CBlock &block, CBlockIndex *pindexDelete, CCoinsViewCache &view, const CChainParams &chainparams) EXCLUSIVE_LOCKS_REQUIRED(cs_main, mempool->cs)
{
    BlockValidationState state;
    BOOST_REQUIRE(DISCONNECT_OK == chainstate_active.DisconnectBlock(block, pindexDelete, view));
    BOOST_REQUIRE(FlushView(&view, state, chainstate_active, true));
    BOOST_REQUIRE(chainstate_active.FlushStateToDisk(state, FlushStateMode::IF_NEEDED));
    chainstate_active.m_chain.SetTip(pindexDelete->pprev);
    chainstate_active.UpdateTip(pindexDelete->pprev);
}

BOOST_AUTO_TEST_CASE(stake_test)
{
    SeedInsecureRand();
    auto &chain_active = m_node.chainman->ActiveChain();
    auto &chainstate_active = m_node.chainman->ActiveChainstate();
    CHDWallet *pwallet = pwalletMain.get();
    const auto context = util::AnyPtr<NodeContext>(&m_node);
    {
        int last_height = WITH_LOCK(cs_main, return chain_active.Height());
        uint256 last_hash = WITH_LOCK(cs_main, return chain_active.Tip()->GetBlockHash());
        WITH_LOCK(pwallet->cs_wallet, pwallet->SetLastBlockProcessed(last_height, last_hash));
    }
    UniValue rv;

    std::unique_ptr<CChainParams> regtestChainParams = CreateChainParams(gArgs, CBaseChainParams::REGTEST);
    const CChainParams &chainparams = *regtestChainParams;

    BOOST_REQUIRE(chainparams.GenesisBlock().GetHash() == chain_active.Tip()->GetBlockHash());

    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster tprv8ZgxMBicQKsPeK5mCpvMsd1cwyT1JZsrBN82XkoYuZY1EVK7EwDaiL9sDfqUU5SntTfbRfnRedFWjg5xkDG5i3iwd3yP7neX5F2dtdCojk4", context));

    // Import the key to the last 5 outputs in the regtest genesis coinbase
    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster tprv8ZgxMBicQKsPe3x7bUzkHAJZzCuGqN6y28zFFyg5i7Yqxqm897VCnmMJz6QScsftHDqsyWW5djx6FzrbkF9HSD3ET163z1SzRhfcWxvwL4G", context));
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewextaddress lblHDKey", context));

    {
        LOCK(pwallet->cs_wallet);
        CTxDestination addr = DecodeDestination("pdtYqn1fBVpgRa6Am6VRRLH8fkrFr1TuDq");
        CKeyID idk = ToKeyID(std::get<PKHash>(addr));
        BOOST_CHECK(pwallet->IsMine(idk) == ISMINE_SPENDABLE);

        const CEKAKey *pak = nullptr;
        const CEKASCKey *pasc = nullptr;
        CExtKeyAccount *pa = nullptr;
        BOOST_CHECK(pwallet->HaveKey(idk, pak, pasc, pa));
        BOOST_REQUIRE(pa);
        BOOST_REQUIRE(pak);
        BOOST_CHECK(pak->nParent == 1);
        BOOST_CHECK(pak->nKey == 1);
        BOOST_CHECK(!pasc);

        CEKAKey ak;
        CKey key;
        CKeyID idStealth;
        BOOST_CHECK(pwallet->GetKey(idk, key, pa, ak, idStealth));
        BOOST_CHECK(idk == key.GetPubKey().GetID());
    }

    CAmount base_supply = 12500000000000;
    {
        LOCK(pwallet->cs_wallet);
        const auto bal = pwallet->GetBalance();
        BOOST_REQUIRE(bal.m_mine_trusted == base_supply);
    }
    BOOST_REQUIRE(chain_active.Tip()->nMoneySupply == base_supply);
    CAmount stake_reward = Params().GetProofOfStakeReward(chain_active.Tip(), 0);

    StakeNBlocks(pwallet, 2);
    BOOST_REQUIRE(chain_active.Tip()->nMoneySupply == 12500000079274);
    BOOST_REQUIRE(chain_active.Tip()->nMoneySupply == base_supply + stake_reward * 2);

    CBlockIndex *pindexDelete = chain_active.Tip();
    BOOST_REQUIRE(pindexDelete);

    CBlock block;
    BOOST_REQUIRE(ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()));

    const CTxIn &txin = block.vtx[0]->vin[0];

    // Test that disconnecting a block abandons the coinstake
    {
    pwallet->BlockUntilSyncedToCurrentChain();
    LOCK(pwallet->cs_wallet);
    BOOST_REQUIRE(pwallet->IsSpent(txin.prevout.hash, txin.prevout.n));
    }

    {
    LOCK(cs_main);
    CCoinsViewCache &view = chainstate_active.CoinsTip();
    const Coin &coin = view.AccessCoin(txin.prevout);
    BOOST_REQUIRE(coin.IsSpent());
    LOCK(m_node.mempool->cs);
    DisconnectTip(chainstate_active, m_node.mempool.get(), block, pindexDelete, view, chainparams);
    }
    {
    // Normally sent through GetMainSignals().BlockDisconnected
    pwallet->blockDisconnected(block, pindexDelete->nHeight);
    pwallet->BlockUntilSyncedToCurrentChain();
    LOCK(pwallet->cs_wallet);
    BOOST_REQUIRE(!pwallet->IsSpent(txin.prevout.hash, txin.prevout.n));
    }

    {
    LOCK(cs_main);
    BOOST_REQUIRE(pindexDelete->pprev->GetBlockHash() == chain_active.Tip()->GetBlockHash());

    CCoinsViewCache &view = chainstate_active.CoinsTip();
    const Coin &coin2 = view.AccessCoin(txin.prevout);
    BOOST_REQUIRE(!coin2.IsSpent());
    }

    BOOST_CHECK(chain_active.Height() == pindexDelete->nHeight - 1);
    BOOST_CHECK(chain_active.Tip()->GetBlockHash() == pindexDelete->pprev->GetBlockHash());
    BOOST_REQUIRE(chain_active.Tip()->nMoneySupply == base_supply + stake_reward * 1);


    // Reconnect block
    {
        BlockValidationState state;
        state.m_chainman = m_node.chainman.get();
        std::shared_ptr<const CBlock> pblock = std::make_shared<const CBlock>(block);
        BOOST_REQUIRE(chainstate_active.ActivateBestChain(state, pblock));

        LOCK(cs_main);
        CCoinsViewCache &view = chainstate_active.CoinsTip();
        const Coin &coin = view.AccessCoin(txin.prevout);
        BOOST_REQUIRE(coin.IsSpent());
        BOOST_REQUIRE(chain_active.Tip()->nMoneySupply == base_supply + stake_reward * 2);
    }
    BOOST_REQUIRE(block.GetHash() == chain_active.Tip()->GetBlockHash());
    {
    pwallet->BlockUntilSyncedToCurrentChain();
    LOCK(pwallet->cs_wallet);
    BOOST_REQUIRE(pwallet->IsSpent(txin.prevout.hash, txin.prevout.n));
    }

    CKey kRecv;
    InsecureNewKey(kRecv, true);

    bool fSubtractFeeFromAmount = false;
    CAmount nAmountSendAway = 10000;
    CTransactionRef tx_new;

    // Parse Bitcoin address
    CScript scriptPubKey = GetScriptForDestination(PKHash(kRecv.GetPubKey()));

    // Create and send the transaction
    CAmount nFeeRequired;
    bilingual_str bl_error;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nAmountSendAway, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);

    CCoinControl coinControl;
    {
        FeeCalculation fee_calc;
        BOOST_CHECK(pwallet->CreateTransaction(vecSend, tx_new, nFeeRequired, nChangePosRet, bl_error, coinControl, fee_calc));
    }
    {
        pwallet->SetBroadcastTransactions(true);
        mapValue_t mapValue;
        pwallet->CommitTransaction(tx_new, std::move(mapValue), {} /* orderForm */);
    }

    StakeNBlocks(pwallet, 1);

    CBlock blockLast;
    BOOST_REQUIRE(ReadBlockFromDisk(blockLast, chain_active.Tip(), chainparams.GetConsensus()));

    BOOST_REQUIRE(blockLast.vtx.size() == 2);
    BOOST_REQUIRE(blockLast.vtx[1]->GetHash() == tx_new->GetHash());

    {
        uint256 tipHash = chain_active.Tip()->GetBlockHash();
        uint256 prevTipHash = chain_active.Tip()->pprev->GetBlockHash();

        // Disconnect last block
        CBlockIndex *pindexDelete = chain_active.Tip();
        BOOST_REQUIRE(pindexDelete);

        CBlock block;
        BOOST_REQUIRE(ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()));

        {
        LOCK(cs_main);
        CCoinsViewCache &view = chainstate_active.CoinsTip();
        LOCK(m_node.mempool->cs);
        DisconnectTip(chainstate_active, m_node.mempool.get(), block, pindexDelete, view, chainparams);
        }


        BOOST_CHECK(prevTipHash == chain_active.Tip()->GetBlockHash());


        {
            LOCK(cs_main);

            // Reduce the reward
            RegtestParams().SetCoinYearReward(1 * CENT);
            BOOST_CHECK(Params().GetCoinYearReward(0) == 1 * CENT);

            BlockValidationState state;
            state.m_chainman = m_node.chainman.get();
            CCoinsViewCache view(&chainstate_active.CoinsTip());
            BOOST_REQUIRE(false == chainstate_active.ConnectBlock(block, state, pindexDelete, view, false));

            BOOST_CHECK(state.IsInvalid());
            BOOST_CHECK(state.GetRejectReason() == "bad-cs-amount");
            BOOST_CHECK(prevTipHash == chain_active.Tip()->GetBlockHash());

            // Restore the reward
            RegtestParams().SetCoinYearReward(2 * CENT);
            BOOST_CHECK(Params().GetCoinYearReward(0) == 2 * CENT);

            // Block should connect now
            BlockValidationState clearstate;
            clearstate.m_chainman = m_node.chainman.get();
            CCoinsViewCache &clearview = chainstate_active.CoinsTip();
            BOOST_REQUIRE(chainstate_active.ConnectBlock(block, clearstate, pindexDelete, clearview, false));

            BOOST_CHECK(!clearstate.IsInvalid());
            BOOST_REQUIRE(FlushView(&clearview, state, chainstate_active, false));
            BOOST_REQUIRE(chainstate_active.FlushStateToDisk(clearstate, FlushStateMode::IF_NEEDED));
            chain_active.SetTip(pindexDelete);
            chainstate_active.UpdateTip(pindexDelete);

            BOOST_CHECK(tipHash == chain_active.Tip()->GetBlockHash());
            BOOST_CHECK(chain_active.Tip()->nMoneySupply == 12500000118911);
        }
    }

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewextaddress testLbl", context));
    std::string extaddr = part::StripQuotes(rv.write());

    BOOST_CHECK(pwallet->GetBalance().m_mine_trusted + pwallet->GetStaked() == 12500000108911);
    BOOST_CHECK(chain_active.Tip()->nMoneySupply - nAmountSendAway == 12500000108911);


    {
        BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress", context));
        CTxDestination address = DecodeDestination(part::StripQuotes(rv.write()));

        AddTxn(pwallet, address, OUTPUT_RINGCT, 10 * COIN);
        AddTxn(pwallet, address, OUTPUT_RINGCT, 20 * COIN);
        AddTxn(pwallet, address, OUTPUT_CT, 30 * COIN);

        TryAddBadTxn(pwallet, address, OUTPUT_CT, 11 * COIN);
        TryAddBadTxn(pwallet, address, OUTPUT_RINGCT, 12 * COIN);

        StakeNBlocks(pwallet, 2);
        CCoinControl coinControl;
        BOOST_CHECK(30 * COIN == pwallet->GetAvailableAnonBalance(&coinControl));
        BOOST_CHECK(30 * COIN == pwallet->GetAvailableBlindBalance(&coinControl));

        BOOST_CHECK(chain_active.Tip()->nAnonOutputs == 4);
        BOOST_CHECK(chain_active.Tip()->nMoneySupply == base_supply + stake_reward * 5);

        for (size_t i = 0; i < 2; ++i) {
            LOCK(cs_main);
            // Disconnect last block
            uint256 prevTipHash = chain_active.Tip()->pprev->GetBlockHash();
            CBlockIndex *pindexDelete = chain_active.Tip();
            BOOST_REQUIRE(pindexDelete);

            CBlock block;
            BOOST_REQUIRE(ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()));

            CCoinsViewCache &view = chainstate_active.CoinsTip();
            LOCK(m_node.mempool->cs);
            DisconnectTip(chainstate_active, m_node.mempool.get(), block, pindexDelete, view, chainparams);

            BOOST_CHECK(prevTipHash == chain_active.Tip()->GetBlockHash());
        }

        BOOST_CHECK(chain_active.Tip()->nAnonOutputs == 0);
        BOOST_CHECK(chain_active.Tip()->nMoneySupply == 12500000118911);
    }
}

BOOST_AUTO_TEST_SUITE_END()
