// Copyright (c) 2021 tecnovert
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/*
    Test spending frozen blinded outputs (rct and ct)
    Can be removed after reintegration period ends.
*/

#include <wallet/hdwallet.h>
#include <wallet/coincontrol.h>
#include <interfaces/chain.h>

#include <wallet/test/hdwallet_test_fixture.h>
#include <chainparams.h>
#include <miner.h>
#include <pos/miner.h>
#include <timedata.h>
#include <coins.h>
#include <net.h>
#include <validation.h>
#include <anon.h>
#include <blind.h>
#include <insight/insight.h>
#include <rpc/rpcutil.h>
#include <rpc/util.h>
#include <util/string.h>
#include <util/translation.h>
#include <util/moneystr.h>

#include <consensus/validation.h>
#include <consensus/tx_verify.h>

#include <secp256k1_mlsag.h>

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>


struct FBTestingSetup: public HDWalletTestingSetup {
    FBTestingSetup(const std::string& chainName = CBaseChainParams::REGTEST):
        HDWalletTestingSetup(chainName)
    {
        SetMockTime(0);
    }
};

BOOST_FIXTURE_TEST_SUITE(frozen_blinded_tests, FBTestingSetup)


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

static uint256 AddTxn(CHDWallet *pwallet, CTxDestination &dest, OutputTypes input_type, OutputTypes output_type, CAmount amount, CAmount exploit_amount=0, std::string expect_error="")
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

std::vector<COutputR> GetAvailable(CHDWallet *pwallet, OutputTypes output_type, bool spend_frozen_blinded=false, bool include_tainted_frozen=false)
{
    LOCK(pwallet->cs_wallet);
    CCoinControl cctl;
    cctl.m_spend_frozen_blinded = spend_frozen_blinded;
    cctl.m_include_tainted_frozen = include_tainted_frozen;
    std::vector<COutputR> vAvailableCoins;
    if (output_type == OUTPUT_CT) {
        pwallet->AvailableBlindedCoins(vAvailableCoins, true, &cctl);
    } else
    if (output_type == OUTPUT_RINGCT) {
        pwallet->AvailableAnonCoins(vAvailableCoins, true, &cctl);
    } else {
        // unknown type
        BOOST_REQUIRE(false);
    }
    return vAvailableCoins;
}

bool ProcessNewBlock(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, BlockValidationState &state)
{
    // Copy of ChainstateManager::ProcessNewBlock with state passthrough
    CBlockIndex *pindex = nullptr;
    bool fForceProcessing = true;
    {
        LOCK(cs_main);
        bool ret = CheckBlock(*pblock, state, chainparams.GetConsensus());
        if (ret) {
            ret = ::ChainstateActive().AcceptBlock(pblock, state, chainparams, &pindex, fForceProcessing, nullptr, nullptr);
        }
        if (!ret) {
            return error("%s: AcceptBlock FAILED (%s)", __func__, state.ToString());
        }
    }
    state.m_preserve_state = true; // else would be cleared
    if (!::ChainstateActive().ActivateBestChain(state, chainparams, pblock) || !state.IsValid()) {
        return error("%s: ActivateBestChain failed (%s)", __func__, state.ToString());
    }
    return true;
}

BOOST_AUTO_TEST_CASE(frozen_blinded_test)
{
    SeedInsecureRand();
    CHDWallet *pwallet = pwalletMain.get();
    util::Ref context{m_node};
    {
        int last_height = WITH_LOCK(cs_main, return ::ChainActive().Height());
        uint256 last_hash = WITH_LOCK(cs_main, return ::ChainActive().Tip()->GetBlockHash());
        WITH_LOCK(pwallet->cs_wallet, pwallet->SetLastBlockProcessed(last_height, last_hash));
    }
    UniValue rv;

    int peer_blocks = GetNumBlocksOfPeers();
    SetNumBlocksOfPeers(0);

    // Disable rct mint fix
    RegtestParams().GetConsensus_nc().exploit_fix_1_time = 0xffffffff;
    BOOST_REQUIRE(RegtestParams().GenesisBlock().GetHash() == ::ChainActive().Tip()->GetBlockHash());

    // Import the regtest genesis coinbase keys
    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster tprv8ZgxMBicQKsPeK5mCpvMsd1cwyT1JZsrBN82XkoYuZY1EVK7EwDaiL9sDfqUU5SntTfbRfnRedFWjg5xkDG5i3iwd3yP7neX5F2dtdCojk4", context));
    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster tprv8ZgxMBicQKsPe3x7bUzkHAJZzCuGqN6y28zFFyg5i7Yqxqm897VCnmMJz6QScsftHDqsyWW5djx6FzrbkF9HSD3ET163z1SzRhfcWxvwL4G", context));
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewextaddress lblHDKey", context));

    CTxDestination stealth_address;
    CAmount base_supply = 12500000000000;
    {
        LOCK(pwallet->cs_wallet);
        pwallet->SetBroadcastTransactions(true);
        const auto bal = pwallet->GetBalance();
        BOOST_REQUIRE(bal.m_mine_trusted == base_supply);

        BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress", context));
        stealth_address = DecodeDestination(part::StripQuotes(rv.write()));
    }
    BOOST_REQUIRE(::ChainActive().Tip()->nMoneySupply == base_supply);

    std::vector<uint256> txids_unexploited;
    for (size_t i = 0; i < 10; ++i) {
        txids_unexploited.push_back(AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 20 * COIN));
    }

    // Do exploit
    CHDWalletBalances balances;
    pwallet->GetBalances(balances);
    CAmount plain_balance_before_expolit = balances.nPart + balances.nPartStaked;
    uint256 txid_exploited1 = AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 3000000 * COIN);
    uint256 txid_exploited2 = AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 3000000 * COIN);
    uint256 txid_exploited3 = AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 3000000 * COIN);
    BOOST_CHECK(!txid_exploited1.IsNull());
    BOOST_CHECK(!txid_exploited2.IsNull());
    BOOST_CHECK(!txid_exploited3.IsNull());

    StakeNBlocks(pwallet, 2);

    BOOST_REQUIRE(pwallet->GetAnonBalance() == 18000230 * COIN);
    AddTxn(pwallet, stealth_address, OUTPUT_RINGCT, OUTPUT_STANDARD, 50000 * COIN);
    StakeNBlocks(pwallet, 1);

    pwallet->GetBalances(balances);
    BOOST_REQUIRE(plain_balance_before_expolit + (50000-30) * COIN <= balances.nPart + balances.nPartStaked);

    // Add some blinded txns
    uint256 txid_ct_plain_small = AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_CT, 11 * COIN);
    uint256 txid_ct_plain_large = AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_CT, 1100 * COIN);
    uint256 txid_ct_anon_small = AddTxn(pwallet, stealth_address, OUTPUT_RINGCT, OUTPUT_CT, 12 * COIN);
    uint256 txid_ct_anon_large = AddTxn(pwallet, stealth_address, OUTPUT_RINGCT, OUTPUT_CT, 1100 * COIN);

    uint256 txid_anon_large = AddTxn(pwallet, stealth_address, OUTPUT_RINGCT, OUTPUT_RINGCT, 1100 * COIN);
    BOOST_CHECK(!txid_anon_large.IsNull());
    uint32_t nTime = ::ChainActive().Tip()->nTime;

    StakeNBlocks(pwallet, 2);

    BlockBalances blockbalances;
    BOOST_CHECK(blockbalances.plain() == 0);
    BOOST_CHECK(blockbalances.blind() == 0);
    BOOST_CHECK(blockbalances.anon() == 0);
    uint256 tip_hash = ::ChainActive().Tip()->GetBlockHash();
    BOOST_CHECK(GetBlockBalances(tip_hash, blockbalances));
    BOOST_CHECK(blockbalances.plain() == GetUTXOSum());
    BOOST_CHECK(blockbalances.blind() == 1111 * COIN);
    BOOST_CHECK(blockbalances.anon() < -49770 * COIN);

    // Enable fix
    RegtestParams().GetConsensus_nc().exploit_fix_1_time = nTime + 1;
    while (GetTime() < nTime + 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    BOOST_REQUIRE(gArgs.GetBoolArg("-acceptanontxn", false)); // Was set in AppInitParameterInteraction

    // Exploit should fail
    AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 9000000 * COIN, "bad-commitment-sum");

    gArgs.ClearForced("-acceptanontxn");
    gArgs.ClearForced("-acceptblindtxn");
    BOOST_REQUIRE(!gArgs.GetBoolArg("-acceptanontxn", false));
    BOOST_REQUIRE(!gArgs.GetBoolArg("-acceptblindtxn", false));

    // CT and RCT should fail
    AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 9000000 * COIN, "bad-txns-anon-disabled");
    AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_CT, 10 * COIN, 0, "bad-txns-blind-disabled");

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentanon", context));
    BOOST_REQUIRE(rv.size() > 0);
    size_t num_prefork_anon = rv.size();

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentblind", context));
    BOOST_REQUIRE(rv.size() > 0);
    size_t num_prefork_blind = rv.size();


    // Set frozen blinded markers
    const CBlockIndex *tip = ::ChainActive().Tip();
    RegtestParams().GetConsensus_nc().m_frozen_anon_index = tip->nAnonOutputs;
    RegtestParams().GetConsensus_nc().m_frozen_blinded_height = tip->nHeight;

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    size_t num_spendable = rv["num_spendable"].get_int();
    size_t num_unspendable = rv["num_unspendable"].get_int();
    BOOST_CHECK(num_spendable > 0);
    BOOST_CHECK(num_unspendable > 0);
    BOOST_CHECK(AmountFromValue(rv["frozen_outputs"][0]["amount"]) > AmountFromValue(rv["frozen_outputs"][num_spendable + num_unspendable - 1]["amount"]));

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"spend_frozen_output\":true}", context));
    BOOST_CHECK(rv["error"].get_str() == "Exploit repair fork is not active yet.");

    // Enable HF2
    RegtestParams().GetConsensus_nc().exploit_fix_2_time = tip->nTime + 1;
    CAmount moneysupply_before_fork = tip->nMoneySupply;

    while (GetTime() < tip->nTime + 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Test spend_frozen_output with num_spendable == 0
    RegtestParams().GetConsensus_nc().m_max_tainted_value_out = 100;
    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    BOOST_CHECK(rv["num_spendable"].get_int() == 0);
    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"spend_frozen_output\":true}", context));
    BOOST_CHECK(rv["error"].get_str() == "No spendable outputs.");
    RegtestParams().GetConsensus_nc().m_max_tainted_value_out = 500 * COIN;

    // Build and install ct tainted bloom filter
    CBloomFilter tainted_filter(160, 0.004, 0, BLOOM_UPDATE_NONE);
    tainted_filter.insert(txid_ct_anon_small);
    tainted_filter.insert(txid_ct_anon_large);
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tainted_filter;
    LoadCTTaintedFilter(stream.data(), stream.size());
    BOOST_REQUIRE(IsFrozenBlindOutput(txid_ct_anon_small));
    BOOST_REQUIRE(IsFrozenBlindOutput(txid_ct_anon_large));
    BOOST_REQUIRE(!IsFrozenBlindOutput(txid_ct_plain_small));
    BOOST_REQUIRE(!IsFrozenBlindOutput(txid_ct_plain_large));

    // Test available coins, should be all frozen
    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentanon", context));
    BOOST_REQUIRE(rv.size() == 0);

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentblind", context));
    BOOST_REQUIRE(rv.size() == 0);


    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentanon 1 9999999 [] true {\"frozen\":true}", context));
    BOOST_REQUIRE(rv.size() < num_prefork_anon);
    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentanon 1 9999999 [] true {\"frozen\":true,\"include_tainted_frozen\":true}", context));
    BOOST_REQUIRE(rv.size() == num_prefork_anon);

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentblind 1 9999999 [] true {\"frozen\":true}", context));
    BOOST_REQUIRE(rv.size() < num_prefork_blind);
    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentblind 1 9999999 [] true {\"frozen\":true,\"include_tainted_frozen\":true}", context));
    BOOST_REQUIRE(rv.size() == num_prefork_blind);

    CAmount utxo_sum_before_fork = GetUTXOSum();
    BOOST_REQUIRE(utxo_sum_before_fork > moneysupply_before_fork + 48000 * COIN);

    // Test that moneysupply is updated
    CAmount stake_reward = Params().GetProofOfStakeReward(::ChainActive().Tip(), 0);
    StakeNBlocks(pwallet, 1);

    CAmount moneysupply_post_fork = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    pwallet->GetBalances(balances);
    CAmount balance_before = balances.nPart + balances.nPartStaked;
    CAmount utxo_sum_after_fork = GetUTXOSum();
    BOOST_REQUIRE(moneysupply_post_fork == balance_before);
    BOOST_REQUIRE(moneysupply_post_fork == utxo_sum_after_fork);
    BOOST_REQUIRE(utxo_sum_before_fork + stake_reward == utxo_sum_after_fork);

    // Test that the balanceindex is reset
    BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));
    BOOST_CHECK(blockbalances.plain() == utxo_sum_after_fork);
    BOOST_CHECK(blockbalances.blind() == 0);
    BOOST_CHECK(blockbalances.anon() == 0);

    // Spend a large non tainted ct output
    std::string str_cmd;
    {
        uint256 spend_txid = txid_ct_plain_large;
        int output_n = -1;
        CAmount extract_value = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_CT, true);
            for (const auto &c : vAvailableCoins) {
                if (c.txhash == spend_txid) {
                    const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                    if (oR && oR->nFlags & ORF_OWNED && oR->nValue > 500 * COIN) {
                        output_n = c.i;
                        extract_value = oR->nValue;
                        break;
                    }
                }
            }
        }
        BOOST_REQUIRE(output_n > -1);

        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 5 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
        balance_before = balances.nPart + balances.nPartStaked;
    }

    // Try spend a large non tainted ct output
    {
        uint256 spend_txid = txid_ct_anon_large;
        int output_n = -1;
        CAmount extract_value = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_CT, true, true);

            for (const auto &c : vAvailableCoins) {
                if (c.txhash == spend_txid) {
                    const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                    if (oR && oR->nFlags & ORF_OWNED && oR->nValue > 500 * COIN) {
                        output_n = c.i;
                        extract_value = oR->nValue;
                        break;
                    }
                }
            }
        }
        BOOST_REQUIRE(output_n > -1);

        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 5 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-txns-frozen-blinded-too-large");

        // Update whitelist
        std::vector<uint8_t> vct_whitelist;
        vct_whitelist.resize(32);
        memcpy(vct_whitelist.data(), txid_ct_anon_large.data(), 32);
        LoadCTWhitelist(vct_whitelist.data(), vct_whitelist.size());

        // Txn should pass now
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());
        CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
        balance_before = balances.nPart + balances.nPartStaked;
    }

    // Spend a small rct output
    {
        uint256 spend_txid;
        int output_n = -1;
        CAmount extract_value = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_RINGCT, true, true);

            for (const auto &c : vAvailableCoins) {
                const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                if (oR && oR->nFlags & ORF_OWNED && oR->nFlags & ORF_OWNED && oR->nValue < 500 * COIN) {
                    spend_txid = c.txhash;
                    output_n = c.i;
                    extract_value = oR->nValue;
                    break;
                }
            }
        }
        BOOST_REQUIRE(output_n > -1);

        // Check that ringsize > 1 fails, set mixin_selection_mode to avoid failure when setting ranges
        {
            str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 2 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"mixin_selection_mode\":2,\"use_mixins\":[1,2,3,4]}",
                                EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
            BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
            BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-frozen-ringsize");
        }

        str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
        balance_before = balances.nPart + balances.nPartStaked;
    }

    // Try spend a large rct output
    {
        uint256 spend_txid;
        int output_n = -1;
        CAmount extract_value = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_RINGCT, true, true);

            for (const auto &c : vAvailableCoins) {
                const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                if (oR && oR->nFlags & ORF_OWNED && oR->nValue > 500 * COIN && oR->nValue < 2000 * COIN) {
                    spend_txid = c.txhash;
                    output_n = c.i;
                    extract_value = oR->nValue;
                    break;
                }
            }
        }
        BOOST_REQUIRE(output_n > -1);

        str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-txns-frozen-blinded-too-large");


        // Whitelist index
        BOOST_CHECK_NO_THROW(rv = CallRPC(strprintf("gettransaction %s true true", spend_txid.ToString()), context));
        std::string str_ao_pubkey = rv["decoded"]["vout"][output_n]["pubkey"].get_str();
        BOOST_CHECK_NO_THROW(rv = CallRPC(strprintf("anonoutput %s", str_ao_pubkey), context));
        int64_t ao_index = rv["index"].get_int64();

        int64_t aoi_whitelist[] = {
            ao_index,
        };
        LoadRCTWhitelist(aoi_whitelist, 1);

        // Transaction should send
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());
        CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
        balance_before = balances.nPart + balances.nPartStaked;
    }

    StakeNBlocks(pwallet, 1);

    pwallet->GetBalances(balances);
    CAmount moneysupply_before_post_fork_to_blinded = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    BOOST_REQUIRE(moneysupply_before_post_fork_to_blinded == balances.nPart + balances.nPartStaked);
    BOOST_REQUIRE(GetUTXOSum() == moneysupply_before_post_fork_to_blinded);

    BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));
    BOOST_CHECK(blockbalances.plain() == moneysupply_before_post_fork_to_blinded);
    BOOST_CHECK(blockbalances.blind() == 0);
    BOOST_CHECK(blockbalances.anon() == 0);

    // Send some post-fork blinded txns
    str_cmd = strprintf("sendtypeto part blind [{\"address\":\"%s\",\"amount\":1000}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                        EncodeDestination(stealth_address));
    BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
    BOOST_REQUIRE(rv["txid"].isStr());

    for (size_t i = 0; i < 10; ++i) {
        str_cmd = strprintf("sendtypeto part anon [{\"address\":\"%s\",\"amount\":10}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());
    }
    str_cmd = strprintf("sendtypeto part anon [{\"address\":\"%s\",\"amount\":1000}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                        EncodeDestination(stealth_address));
    BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
    BOOST_REQUIRE(rv["txid"].isStr());


    StakeNBlocks(pwallet, 2);


    pwallet->GetBalances(balances);
    CAmount moneysupply_after_post_fork_to_blinded = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    CAmount utxosum = GetUTXOSum();
    BOOST_REQUIRE(utxosum + 2100 * COIN == moneysupply_after_post_fork_to_blinded);
    BOOST_REQUIRE(balances.nPart + balances.nPartStaked + 2100 * COIN == moneysupply_after_post_fork_to_blinded);

    BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));
    BOOST_CHECK(blockbalances.plain() == utxosum);
    BOOST_CHECK(blockbalances.blind() == 1000 * COIN);
    BOOST_CHECK(blockbalances.anon() == 1100 * COIN);

    // Check that mixing pre and post fork CT fails
    {
        COutPoint op_pre;
        CAmount extract_value_pre = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_CT, true, true);

            for (const auto &c : vAvailableCoins) {
                const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                if (oR && oR->nFlags & ORF_OWNED && oR->nValue < 500 * COIN) {
                    op_pre.hash = c.txhash;
                    op_pre.n = c.i;
                    extract_value_pre = oR->nValue;
                    break;
                }
            }
        }
        BOOST_REQUIRE(op_pre.n < 5000);

        COutPoint op_post;
        CAmount extract_value_post = 0;
        {
            std::vector<COutputR> vAvailableCoins = GetAvailable(pwallet, OUTPUT_CT);

            for (const auto &c : vAvailableCoins) {
                const COutputRecord *oR = c.rtx->second.GetOutput(c.i);
                if (oR && oR->nFlags & ORF_OWNED && oR->nValue < 500 * COIN) {
                    op_post.hash = c.txhash;
                    op_post.n = c.i;
                    extract_value_post = oR->nValue;
                    break;
                }
            }
        }
        BOOST_REQUIRE(op_post.n < 5000);

        CAmount send_value = extract_value_pre + extract_value_post;
        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d},{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value), op_pre.hash.ToString(), op_pre.n, op_post.hash.ToString(), op_post.n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "mixed-frozen-blinded");

        // Should fail without spend_frozen_blinded
        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d},{\"tx\":\"%s\",\"n\":%d}],\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value), op_pre.hash.ToString(), op_pre.n, op_post.hash.ToString(), op_post.n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "mixed-frozen-blinded");


        // Should pass without op_pre
        send_value = extract_value_post + 1; // require 2 inputs
        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());

        // b->b
        send_value = 1 * COIN;
        str_cmd = strprintf("sendtypeto blind blind [{\"address\":\"%s\",\"amount\":%s}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());

        // b->a
        send_value = 1 * COIN;
        str_cmd = strprintf("sendtypeto blind blind [{\"address\":\"%s\",\"amount\":%s}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());
    }

    {
        // Try send with small ringsize
        CAmount send_value = 1 * COIN;
        str_cmd = strprintf("sendtypeto anon anon [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-anon-ringsize");

        // Otherwise should work
        str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 3 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());

        // a->a
        str_cmd = strprintf("sendtypeto anon anon [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 3 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());

        // a->b
        str_cmd = strprintf("sendtypeto anon blind [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 3 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["txid"].isStr());
    }

    StakeNBlocks(pwallet, 2);

    // Check moneysupply didn't climb more than stakes
    stake_reward = Params().GetProofOfStakeReward(::ChainActive().Tip(), 0);
    CAmount moneysupply_after_post_fork_blind_spends = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    BOOST_REQUIRE(moneysupply_after_post_fork_to_blinded + stake_reward * 2 ==  moneysupply_after_post_fork_blind_spends);

    // Test debugwallet spend_frozen_output
    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    num_spendable = rv["num_spendable"].get_int();
    BOOST_CHECK(num_spendable > 0);

    for (size_t i = 0; i < num_spendable; i++) {
        BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"spend_frozen_output\":true}", context));
        BOOST_REQUIRE(rv["txid"].isStr());
    }

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    BOOST_CHECK(rv["num_spendable"].get_int() == 0);
    BOOST_CHECK(rv["num_unspendable"].get_int() > 0);

    // balancesindex tracks the amount of plain coin sent to and from blind to anon.
    // Coins can move between anon and blind but the sums should match
    BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));

    BOOST_CHECK_NO_THROW(rv = CallRPC("getbalances", context));
    CAmount blind_trusted = AmountFromValue(rv["mine"]["blind_trusted"]);
    CAmount anon_trusted = AmountFromValue(rv["mine"]["anon_trusted"]);
    BOOST_CHECK(blind_trusted > blockbalances.blind()); // anon -> blind

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    CAmount anon_spendable = anon_trusted - AmountFromValue(rv["total_unspendable"]);
    BOOST_CHECK(anon_spendable < blockbalances.anon()); // anon -> blind
    BOOST_CHECK(anon_spendable + blind_trusted == blockbalances.blind() + blockbalances.anon());


    // TODO: Move RCT tests to new file
    std::string sError;

    // Verify duplicate input fails
    {
    LOCK(pwallet->cs_wallet);
    CPubKey pk_to;
    BOOST_REQUIRE(0 == pwallet->NewKeyFromAccount(pk_to));

    std::vector<CTempRecipient> vecSend;
    CTxDestination dest = PKHash(pk_to);
    vecSend.emplace_back(OUTPUT_STANDARD, 1 * COIN, dest);

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    CCoinControl cctl;
    BOOST_REQUIRE(0 == pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &cctl, sError));

    // Validate
    {
    LOCK(cs_main);
    int nSpendHeight = ::ChainActive().Tip()->nHeight;
    TxValidationState state;
    state.m_exploit_fix_1 = true;
    state.m_exploit_fix_2 = true;
    state.m_spend_height = nSpendHeight;
    CAmount txfee = 0;
    CCoinsViewCache &view = ::ChainstateActive().CoinsTip();
    BOOST_REQUIRE(Consensus::CheckTxInputs(*wtx.tx, state, view, nSpendHeight, txfee));
    BOOST_REQUIRE(VerifyMLSAG(*wtx.tx, state));

    // Rewrite input matrix to add duplicate index
    CMutableTransaction mtx(*wtx.tx);
    CTxIn &txin = mtx.vin[0];

    std::vector<uint8_t> &vMI = txin.scriptWitness.stack[0];
    std::vector<int64_t> indices;
    indices.reserve(vMI.size());

    uint32_t nSigInputs, nSigRingSize;
    txin.GetAnonInfo(nSigInputs, nSigRingSize);
    size_t ofs = 0, nb = 0;
    for (size_t k = 0; k < nSigInputs; ++k) {
        for (size_t i = 0; i < nSigRingSize; ++i) {
            int64_t anon_index;
            BOOST_REQUIRE(0 == part::GetVarInt(vMI, ofs, (uint64_t&)anon_index, nb));
            ofs += nb;
            indices.push_back(anon_index);
        }
    }
    vMI.clear();
    for (size_t i = 0; i < indices.size(); ++i) {
        size_t use_i = i == 1 ? 0 : i; // Make duplicate
        BOOST_REQUIRE(0 == part::PutVarInt(vMI, indices[use_i]));
    }

    // Should fail verification
    CTransaction fail_tx(mtx);
    BOOST_REQUIRE(Consensus::CheckTxInputs(fail_tx, state, view, nSpendHeight, txfee));
    BOOST_REQUIRE(!VerifyMLSAG(fail_tx, state));
    BOOST_REQUIRE(state.GetRejectReason() == "bad-anonin-dup-i");
    }
    }


    // Verify duplicate keyimage fails
    {
    LOCK(pwallet->cs_wallet);
    CPubKey pk_to;
    BOOST_REQUIRE(0 == pwallet->NewKeyFromAccount(pk_to));

    // Pick inputs so two are used
    CCoinControl cctl;
    std::vector<COutputR> vAvailableCoins;
    pwallet->AvailableAnonCoins(vAvailableCoins, true, &cctl, 100000);
    BOOST_REQUIRE(vAvailableCoins.size() > 2);
    CAmount prevouts_sum = 0;
    for (const auto &output : vAvailableCoins) {
        const COutputRecord *pout = output.rtx->second.GetOutput(output.i);
        prevouts_sum += pout->nValue;
        cctl.Select(COutPoint(output.txhash, output.i));
        if (cctl.NumSelected() >= 2) {
            break;
        }
    }

    std::vector<CTempRecipient> vecSend;
    CTxDestination dest = PKHash(pk_to);
    vecSend.emplace_back(OUTPUT_STANDARD, prevouts_sum, dest);
    vecSend.back().fSubtractFeeFromAmount = true;

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    BOOST_REQUIRE(0 == pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &cctl, sError));
    BOOST_REQUIRE(wtx.tx->vin.size() == 2);

    // Validate
    {
    LOCK(cs_main);
    int nSpendHeight = ::ChainActive().Tip()->nHeight;
    TxValidationState state;
    state.m_exploit_fix_1 = true;
    state.m_exploit_fix_2 = true;
    state.m_spend_height = nSpendHeight;
    CAmount txfee = 0;
    CCoinsViewCache &view = ::ChainstateActive().CoinsTip();
    BOOST_REQUIRE(Consensus::CheckTxInputs(*wtx.tx, state, view, nSpendHeight, txfee));
    BOOST_REQUIRE(VerifyMLSAG(*wtx.tx, state));

    // Rewrite scriptData to add duplicate keyimage
    CMutableTransaction mtx(*wtx.tx);
    std::vector<uint8_t> &vKeyImages0 = mtx.vin[0].scriptData.stack[0];
    std::vector<uint8_t> &vKeyImages1 = mtx.vin[1].scriptData.stack[0];
    memcpy(vKeyImages1.data(), vKeyImages0.data(), 33);

    // Changing the keyimage changes the txid, resign the first sig
    auto &txin = mtx.vin[0];
    uint256 blinding_factor_prevout;
    uint8_t rand_seed[32];
    GetStrongRandBytes(rand_seed, 32);

    uint32_t nInputs, nRingSize;
    txin.GetAnonInfo(nInputs, nRingSize);
    size_t nCols = nRingSize;
    size_t nRows = nInputs + 1;

    std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
    std::vector<uint8_t> &vMI = txin.scriptWitness.stack[0];
    std::vector<uint8_t> &vDL = txin.scriptWitness.stack[1];
    std::vector<const uint8_t*> vpsk(nRows), vpBlinds;

    std::vector<secp256k1_pedersen_commitment> vCommitments;
    vCommitments.reserve(nCols * nInputs);
    std::vector<const uint8_t*> vpOutCommits;
    std::vector<const uint8_t*> vpInCommits(nCols * nInputs);
    std::vector<uint8_t> vM(nCols * nRows * 33);

    CKey key;
    size_t real_column = 5000;
    size_t ofs = 0, nB = 0;
    for (size_t k = 0; k < nInputs; ++k)
    for (size_t i = 0; i < nCols; ++i) {
        int64_t nIndex;
        BOOST_REQUIRE(0 == part::GetVarInt(vMI, ofs, (uint64_t&)nIndex, nB));
        ofs += nB;

        CAnonOutput ao;
        BOOST_REQUIRE(pblocktree->ReadRCTOutput(nIndex, ao));
        memcpy(&vM[(i+k*nCols)*33], ao.pubkey.begin(), 33);
        vCommitments.push_back(ao.commitment);
        vpInCommits[i+k*nCols] = vCommitments.back().data;

        // Find real input
        if (real_column < 5000) {
            continue; // skip if found, so as not to overwrite key
        }
        CKeyID idk = ao.pubkey.GetID();
        if (pwallet->GetKey(idk, key)) {
            CCmpPubKey test_keyimage;
            BOOST_REQUIRE(0 == GetKeyImage(test_keyimage, ao.pubkey, key));
            const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);
            if (test_keyimage == ki) {
                real_column = i;
                vpsk[0] = key.begin();

                // Get blinding factor
                CHDWalletDB wdb(pwallet->GetDatabase());
                CStoredTransaction stx;
                BOOST_REQUIRE(wdb.ReadStoredTx(ao.outpoint.hash, stx));
                BOOST_REQUIRE(stx.GetBlind(ao.outpoint.n, blinding_factor_prevout.data()));
            }
        }
    }
    BOOST_REQUIRE(real_column < 5000);
    vpBlinds.push_back(blinding_factor_prevout.data());

    // Get blinding factor for change output commitment
    CAmount changeAmount = -1;
    uint8_t changeBlindOut[32] = {0};

    for (const auto &tmp_out : vecSend) {
        if (!tmp_out.fChange) {
            continue;
        }
        changeAmount = tmp_out.nAmount;
        memcpy(changeBlindOut, tmp_out.vBlind.data(), 32);
        vpOutCommits.push_back(tmp_out.commitment.data);
        break;
    }
    BOOST_REQUIRE(changeAmount > -1);

    uint8_t blindSum[32] = {0}; // Set by secp256k1_prepare_mlsag
    vpsk[nRows-1] = blindSum;
    vpBlinds.push_back(cctl.vSplitCommitBlindingKeys[0].begin());

    const uint8_t *pSplitCommit = &vDL[(1 + (nInputs+1) * nRingSize) * 32];
    BOOST_REQUIRE(0 == secp256k1_prepare_mlsag(&vM[0], blindSum,
        1, 1, nCols, nRows,
        &vpInCommits[0], &pSplitCommit, &vpBlinds[0]));

    uint256 txhash = mtx.GetHash();
    BOOST_REQUIRE(0 == secp256k1_generate_mlsag(secp256k1_ctx_blind, vKeyImages.data(), &vDL[0], &vDL[32],
        rand_seed, txhash.begin(), nCols, nRows, real_column, &vpsk[0], &vM[0]));

    // Should fail verification
    CTransaction ctx(mtx);
    BOOST_REQUIRE(Consensus::CheckTxInputs(ctx, state, view, nSpendHeight, txfee));
    BOOST_REQUIRE(!VerifyMLSAG(ctx, state));
    BOOST_REQUIRE(state.GetRejectReason() == "bad-anonin-dup-ki");
    }
    }


    // Verify duplicate keyimage in block fails
    CMutableTransaction mtx1, mtx2;
    {
    {
    LOCK(pwallet->cs_wallet);
    CCoinControl cctl;
    std::vector<COutputR> vAvailableCoins;
    pwallet->AvailableAnonCoins(vAvailableCoins, true, &cctl, 100000);
    BOOST_REQUIRE(vAvailableCoins.size() > 1);
    CAmount prevouts_sum = 0;
    for (const auto &output : vAvailableCoins) {
        const COutputRecord *pout = output.rtx->second.GetOutput(output.i);
        prevouts_sum += pout->nValue;
        cctl.Select(COutPoint(output.txhash, output.i));
        if (cctl.NumSelected() >= 1) {
            break;
        }
    }

    {
    CPubKey pk_toA;
    BOOST_REQUIRE(0 == pwallet->NewKeyFromAccount(pk_toA));

    std::vector<CTempRecipient> vecSend;
    CTxDestination dest = PKHash(pk_toA);
    vecSend.emplace_back(OUTPUT_STANDARD, prevouts_sum, dest);
    vecSend.back().fSubtractFeeFromAmount = true;

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    BOOST_REQUIRE(0 == pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &cctl, sError));
    BOOST_REQUIRE(wtx.tx->vin.size() == 1);

    mtx1 = CMutableTransaction(*wtx.tx);
    }
    {
    CPubKey pk_toB;
    BOOST_REQUIRE(0 == pwallet->NewKeyFromAccount(pk_toB));

    std::vector<CTempRecipient> vecSend;
    CTxDestination dest = PKHash(pk_toB);
    vecSend.emplace_back(OUTPUT_STANDARD, prevouts_sum, dest);
    vecSend.back().fSubtractFeeFromAmount = true;

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    BOOST_REQUIRE(0 == pwallet->AddAnonInputs(wtx, rtx, vecSend, true, 3, 1, nFee, &cctl, sError));
    BOOST_REQUIRE(wtx.tx->vin.size() == 1);

    mtx2 = CMutableTransaction(*wtx.tx);
    }
    }

    // Txns should be valid individually
    CTransaction tx1(mtx1), tx2(mtx2);
    int nSpendHeight = ::ChainActive().Tip()->nHeight;
    TxValidationState tx_state;
    tx_state.m_exploit_fix_1 = true;
    tx_state.m_exploit_fix_2 = true;
    tx_state.m_spend_height = nSpendHeight;
    CAmount txfee = 0;
    {
    LOCK(cs_main);
    CCoinsViewCache &tx_view = ::ChainstateActive().CoinsTip();
    BOOST_REQUIRE(Consensus::CheckTxInputs(tx1, tx_state, tx_view, nSpendHeight, txfee));
    BOOST_REQUIRE(VerifyMLSAG(tx1, tx_state));
    BOOST_REQUIRE(Consensus::CheckTxInputs(tx2, tx_state, tx_view, nSpendHeight, txfee));
    BOOST_REQUIRE(VerifyMLSAG(tx2, tx_state));
    }

    // Add to block
    std::unique_ptr<CBlockTemplate> pblocktemplate = pwallet->CreateNewBlock();
    BOOST_REQUIRE(pblocktemplate.get());
    pblocktemplate->block.vtx.push_back(MakeTransactionRef(mtx1));
    pblocktemplate->block.vtx.push_back(MakeTransactionRef(mtx2));

    size_t k, nTries = 10000;
    for (k = 0; k < nTries; ++k) {
        int nBestHeight = WITH_LOCK(cs_main, return ::ChainActive().Height());
        int64_t nSearchTime = GetAdjustedTime() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime > pwallet->nLastCoinStakeSearchTime &&
            pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    {
    CBlock *pblock = &pblocktemplate->block;
    BlockValidationState state;
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    BOOST_REQUIRE(!ProcessNewBlock(Params(), shared_pblock, state));
    BOOST_REQUIRE(state.GetRejectReason() == "bad-anonin-dup-ki");
    }

    // Should connect without bad tx
    pblocktemplate->block.vtx.pop_back();
    for (k = 0; k < nTries; ++k) {
        int nBestHeight = WITH_LOCK(cs_main, return ::ChainActive().Height());
        int64_t nSearchTime = GetAdjustedTime() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime > pwallet->nLastCoinStakeSearchTime &&
            pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    {
    CBlock *pblock = &pblocktemplate->block;
    BlockValidationState state;
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    BOOST_REQUIRE(ProcessNewBlock(Params(), shared_pblock, state));
    }

    // Verify duplicate keyimage in chain fails
    {
    LOCK(cs_main);
    CCoinsViewCache &tx_view = ::ChainstateActive().CoinsTip();
    nSpendHeight = ::ChainActive().Tip()->nHeight;
    BOOST_REQUIRE(Consensus::CheckTxInputs(tx2, tx_state, tx_view, nSpendHeight, txfee));
    BOOST_REQUIRE(!VerifyMLSAG(tx2, tx_state));
    BOOST_REQUIRE(tx_state.GetRejectReason() == "bad-anonin-dup-ki");
    }
    }

    // Wait to add time for db flushes to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1250));

    SetNumBlocksOfPeers(peer_blocks);
}

BOOST_AUTO_TEST_SUITE_END()
