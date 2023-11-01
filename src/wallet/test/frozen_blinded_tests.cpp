// Copyright (c) 2021-2023 tecnovert
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
#include <coins.h>
#include <net.h>
#include <validation.h>
#include <blind.h>
#include <insight/insight.h>
#include <rpc/rpcutil.h>
#include <rpc/util.h>
#include <util/string.h>
#include <util/translation.h>
#include <util/moneystr.h>

#include <consensus/validation.h>
#include <consensus/tx_verify.h>

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(frozen_blinded_tests, StakeTestingSetup)


std::vector<COutputR> GetAvailable(CHDWallet *pwallet, OutputTypes output_type, bool spend_frozen_blinded=false, bool include_tainted_frozen=false)
{
    LOCK(pwallet->cs_wallet);
    CCoinControl cctl;
    cctl.m_spend_frozen_blinded = spend_frozen_blinded;
    cctl.m_include_tainted_frozen = include_tainted_frozen;
    std::vector<COutputR> vAvailableCoins;
    if (output_type == OUTPUT_CT) {
        pwallet->AvailableBlindedCoins(vAvailableCoins, &cctl);
    } else
    if (output_type == OUTPUT_RINGCT) {
        pwallet->AvailableAnonCoins(vAvailableCoins, &cctl);
    } else {
        // unknown type
        BOOST_REQUIRE(false);
    }
    return vAvailableCoins;
}


BOOST_AUTO_TEST_CASE(frozen_blinded_test)
{
    SeedInsecureRand();
    CHDWallet *pwallet = pwalletMain.get();
    CTxDestination recovery_addr = DecodeDestination("pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it");

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
    CTxDestination non_recovery_addr;
    CAmount base_supply = 12500000000000;
    {
        LOCK(pwallet->cs_wallet);
        pwallet->SetBroadcastTransactions(true);
        const auto bal = pwallet->GetBalance();
        BOOST_REQUIRE(bal.m_mine_trusted == base_supply);

        BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress", context));
        stealth_address = DecodeDestination(part::StripQuotes(rv.write()));

        BOOST_CHECK_NO_THROW(rv = CallRPC("getnewaddress", context));
        non_recovery_addr = DecodeDestination(part::StripQuotes(rv.write()));
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
    uint32_t nTime = chain_active.Tip()->nTime;

    StakeNBlocks(pwallet, 2);

    BlockBalances blockbalances;
    BOOST_CHECK(blockbalances.plain() == 0);
    BOOST_CHECK(blockbalances.blind() == 0);
    BOOST_CHECK(blockbalances.anon() == 0);
    uint256 tip_hash = chain_active.Tip()->GetBlockHash();
    BOOST_CHECK(GetBlockBalances(*m_node.chainman, tip_hash, blockbalances));
    BOOST_CHECK(blockbalances.plain() == particl::GetUTXOSum(chainstate_active));
    BOOST_CHECK(blockbalances.blind() == 1111 * COIN);
    BOOST_CHECK(blockbalances.anon() < -49770 * COIN);

    // Enable fix
    RegtestParams().GetConsensus_nc().exploit_fix_1_time = nTime + 1;
    while (GetTime() < nTime + 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    RegtestParams().SetAnonRestricted(true);
    RegtestParams().GetConsensus_nc().anonRestrictionStartHeight = 0;
    BOOST_REQUIRE(gArgs.GetBoolArg("-acceptanontxn", false)); // Was set in AppInitParameterInteraction

    // Exploit should fail
    AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 9000000 * COIN, "bad-commitment-sum");

    gArgs.ClearForced("-acceptanontxn");
    gArgs.ClearForced("-acceptblindtxn");
    BOOST_REQUIRE(!gArgs.GetBoolArg("-acceptanontxn", false));
    BOOST_REQUIRE(!gArgs.GetBoolArg("-acceptblindtxn", false));

    // CT and RCT should fail
    // AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_RINGCT, 10 * COIN, 9000000 * COIN, "bad-txns-anon-disabled");
    // AddTxn(pwallet, stealth_address, OUTPUT_STANDARD, OUTPUT_CT, 10 * COIN, 0, "bad-txns-blind-disabled");

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentanon", context));
    BOOST_REQUIRE(rv.size() > 0);
    size_t num_prefork_anon = rv.size();

    BOOST_CHECK_NO_THROW(rv = CallRPC("listunspentblind", context));
    BOOST_REQUIRE(rv.size() > 0);
    size_t num_prefork_blind = rv.size();


    // Set frozen blinded markers
    const CBlockIndex *tip = ::ChainActive().Tip();
    // @todo(Sonkeng): When back to normal restore normal tests
    RegtestParams().GetConsensus_nc().m_frozen_anon_index = tip->nAnonOutputs;
    RegtestParams().GetConsensus_nc().anonRestrictionStartHeight = tip->nHeight;
    std::set<std::uint64_t> aoi_blacklist{1, 2, 3, 4, 5};
    RegtestParams().SetBlacklistedAnonOutput(aoi_blacklist);


    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    size_t num_spendable = rv["num_spendable"].get_int();
    size_t num_unspendable = rv["num_unspendable"].get_int();
    BOOST_CHECK(num_spendable > 0);
    BOOST_CHECK(num_unspendable > 0);
    BOOST_CHECK(AmountFromValue(rv["frozen_outputs"][0]["amount"]) > AmountFromValue(rv["frozen_outputs"][num_spendable + num_unspendable - 1]["amount"]));
    // RegtestParams().GetConsensus_nc().exploit_fix_2_time is set to true, we will restore this after the recovery process is complete
    // BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"spend_frozen_output\":true}", context));
    // BOOST_CHECK(rv["error"].get_str() == "Exploit repair fork is not active yet.");

    // Enable HF2
    RegtestParams().GetConsensus_nc().exploit_fix_2_time = tip->nTime + 1;
    RegtestParams().GetConsensus_nc().exploit_fix_2_height = tip->nHeight + 1;
    
    RegtestParams().GetConsensus_nc().anonRestrictionStartHeight = 0;
    RegtestParams().GetConsensus_nc().m_frozen_anon_index = tip->nAnonOutputs;

    CAmount moneysupply_before_fork = tip->nMoneySupply;

    while (GetTime() < tip->nTime + 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Test spend_frozen_output with num_spendable == 0
    RegtestParams().GetConsensus_nc().m_max_tainted_value_out = 100;
    auto r = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context);

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    BOOST_CHECK(rv["num_spendable"].get_int() == 0);
    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"spend_frozen_output\":true}", context));
    BOOST_CHECK(rv["error"].get_str() == "No spendable outputs.");

    RegtestParams().GetConsensus_nc().m_max_tainted_value_out = 500 * COIN;
    RegtestParams().GetConsensus_nc().anonRestrictionStartHeight = tip->nHeight;
    RegtestParams().GetConsensus_nc().m_frozen_anon_index = tip->nAnonOutputs;


    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    BOOST_CHECK(rv["num_spendable"].get_int() > 0);
    CAmount unspendable_value = AmountFromValue(rv["total_unspendable"]);
    // Find a spendable prevout
    COutPoint prevout_spendable;
    for (size_t i = 0; i < rv["frozen_outputs"].size(); ++i) {
        const UniValue &uvo = rv["frozen_outputs"][i];
        if (uvo["spendable"].get_bool() == true) {
            prevout_spendable = COutPoint(ParseHashO(uvo, "txid"), uvo["n"].get_int());
            break;
        }
    }
    BOOST_CHECK(!prevout_spendable.IsNull());

    // Test trace_frozen_outputs
    // BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"trace_frozen_outputs\":true}", context));
    // BOOST_CHECK(rv["total_traced"].get_int64() == unspendable_value);
    // int last_num_traced = rv["num_traced"].get_int();

    // BOOST_CHECK_NO_THROW(rv = CallRPC(strprintf("debugwallet {\"trace_frozen_outputs\":true,\"trace_frozen_extra\":[{\"tx\":\"%s\",\"n\":%d}]}",
    //     prevout_spendable.hash.ToString(), prevout_spendable.n), context));
    // BOOST_CHECK(rv["num_traced"].get_int() == last_num_traced + 1);
    // std::string str_rv_check = rv.write();
    // BOOST_CHECK(str_rv_check.find("anon_spend_key") == std::string::npos);

    // BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"trace_frozen_outputs\":true,\"trace_frozen_dump_privkeys\":true}", context));
    // str_rv_check = rv.write();
    // BOOST_CHECK(str_rv_check.find("anon_spend_key") != std::string::npos);


    // Build and install ct tainted bloom filter
    CBloomFilter tainted_filter(160, 0.004, 0, BLOOM_UPDATE_NONE);
    tainted_filter.insert(txid_ct_anon_small);
    tainted_filter.insert(txid_ct_anon_large);
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tainted_filter;
    LoadCTTaintedFilter((const unsigned char*) stream.data(), stream.size());
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

        str_cmd = strprintf("sendtypeto blind ghost [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 5 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        // CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        // BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
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

        RegtestParams().SetAnonRestricted(true);
        RegtestParams().SetAnonMaxOutputSize(2);

        str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 5 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-commitment-sum");
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
            RegtestParams().SetAnonRestricted(true);
            RegtestParams().SetAnonMaxOutputSize(2);

            // Test blacklist, should override whitelist
            std::set<std::uint64_t> aoi_blacklist{1, 2, 3, 4};
            RegtestParams().SetBlacklistedAnonOutput(aoi_blacklist);

            str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 2 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"mixin_selection_mode\":99,\"use_mixins\":[1,2,3,4]}",
                                EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);
            BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
            BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-frozen-ringsize");
        }

        RegtestParams().SetAnonRestricted(true);
        RegtestParams().SetAnonMaxOutputSize(2);

        str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(recovery_addr), FormatMoney(extract_value), spend_txid.ToString(), output_n);
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

        RegtestParams().SetAnonRestricted(true);
        RegtestParams().SetAnonMaxOutputSize(2);

        str_cmd = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(non_recovery_addr), FormatMoney(extract_value), spend_txid.ToString(), output_n);
        // Thi was normally expected to fail but since we changed the way re restrict speding
        // it won't fail anymore and will cause "bad-anonin-dup-ki" to be thrown in subsequent tests
        // BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
       // BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-anonin-extract-i");

        // Get prevout anon index
        BOOST_CHECK_NO_THROW(rv = CallRPC(strprintf("gettransaction %s true true", spend_txid.ToString()), context));
        std::string str_ao_pubkey = rv["decoded"]["vout"][output_n]["pubkey"].get_str();
        BOOST_CHECK_NO_THROW(rv = CallRPC(strprintf("anonoutput %s", str_ao_pubkey), context));

        int64_t ao_index = rv["index"].get_int64();

        // // Whitelist index
        // int64_t aoi_whitelist[] = {
        //     ao_index,
        // };
        // LoadRCTWhitelist(aoi_whitelist, 1, 1);

        // Test blacklist, should override whitelist
        std::set<std::uint64_t> aoi_blacklist{static_cast<std::uint64_t>(ao_index)};

        RegtestParams().SetBlacklistedAnonOutput(aoi_blacklist);
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        // The anon index is blacklisted but it's not spending to the recovery addr
        BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "bad-frozen-spend-to-non-recovery");

        RegtestParams().SetBlacklistedAnonOutput(aoi_blacklist);
        std::string str_cmd2 = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(recovery_addr), FormatMoney(extract_value), spend_txid.ToString(), output_n);

        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd2, context));
        BOOST_REQUIRE(rv["txid"].isStr());
        balance_before += extract_value - rv["fee"].get_int64();

        int64_t aoi_reset[] = {
            0,
        };
        LoadRCTBlacklist(aoi_reset, 1);

        // Transaction should send
        RegtestParams().SetAnonRestricted(true);
        RegtestParams().SetAnonMaxOutputSize(2);
        RegtestParams().GetConsensus_nc().m_min_ringsize = 1;

        // Extract new value for spending
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
        // end extraction
        std::string str_cmd3 = strprintf("sendtypeto anon part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true,\"debug\":true}",
                            EncodeDestination(stealth_address), FormatMoney(extract_value), spend_txid.ToString(), output_n);

        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd3, context));
        BOOST_REQUIRE(rv["txid"].isStr());
        CAmount txFee = rv["fee"].get_int64();
        pwallet->GetBalances(balances);
        BOOST_CHECK(balance_before + extract_value - txFee == balances.nPart + balances.nPartStaked);
        balance_before = balances.nPart + balances.nPartStaked;
    }

    StakeNBlocks(pwallet, 1);

    pwallet->GetBalances(balances);
    // CAmount moneysupply_before_post_fork_to_blinded = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    // BOOST_REQUIRE(moneysupply_before_post_fork_to_blinded == balances.nPart + balances.nPartStaked);
    // BOOST_REQUIRE(GetUTXOSum() == moneysupply_before_post_fork_to_blinded);

    // BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));
    // BOOST_CHECK(blockbalances.plain() == moneysupply_before_post_fork_to_blinded);
    // BOOST_CHECK(blockbalances.blind() == 0);
    // BOOST_CHECK(blockbalances.anon() == 0);

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
    // CAmount moneysupply_after_post_fork_to_blinded = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    // CAmount utxosum = GetUTXOSum();
    // @todo(Sonkeng) : Take this back when we're removing anon restrictions

    // BOOST_REQUIRE(utxosum + 2100 * COIN == moneysupply_after_post_fork_to_blinded);
    // BOOST_REQUIRE(balances.nPart + balances.nPartStaked + 2100 * COIN == moneysupply_after_post_fork_to_blinded);

    // BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));
    // BOOST_CHECK(blockbalances.plain() == utxosum);
    // BOOST_CHECK(blockbalances.blind() == 1000 * COIN);
    // BOOST_CHECK(blockbalances.anon() == 1100 * COIN);

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
        // str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d},{\"tx\":\"%s\",\"n\":%d}],\"spend_frozen_blinded\":true,\"test_mempool_accept\":true,\"show_fee\":true}",
        //                     EncodeDestination(stealth_address), FormatMoney(send_value), op_pre.hash.ToString(), op_pre.n, op_post.hash.ToString(), op_post.n);
        // BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        // BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "mixed-frozen-blinded");

        // // Should fail without spend_frozen_blinded
        // str_cmd = strprintf("sendtypeto blind part [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 1 1 false {\"inputs\":[{\"tx\":\"%s\",\"n\":%d},{\"tx\":\"%s\",\"n\":%d}],\"test_mempool_accept\":true,\"show_fee\":true}",
        //                     EncodeDestination(stealth_address), FormatMoney(send_value), op_pre.hash.ToString(), op_pre.n, op_post.hash.ToString(), op_post.n);
        // BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        // BOOST_REQUIRE(rv["mempool-reject-reason"].get_str() == "mixed-frozen-blinded");


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

        RegtestParams().SetAnonRestricted(true);
        RegtestParams().SetAnonMaxOutputSize(2);
        CAmount send_value = 1 * COIN;
        // Nothing is blacklisted. We have no restrictions of ringsize
        str_cmd = strprintf("sendtypeto anon anon [{\"address\":\"%s\",\"amount\":%s,\"subfee\":true}] \"\" \"\" 3 1 false {\"test_mempool_accept\":true,\"show_fee\":true}",
                            EncodeDestination(stealth_address), FormatMoney(send_value));
        BOOST_CHECK_NO_THROW(rv = CallRPC(str_cmd, context));
        BOOST_REQUIRE(rv["mempool-allowed"].get_bool() == true);

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
    // CAmount moneysupply_after_post_fork_blind_spends = WITH_LOCK(cs_main, return ::ChainActive().Tip()->nMoneySupply);
    // BOOST_REQUIRE(moneysupply_after_post_fork_to_blinded + stake_reward * 2 ==  moneysupply_after_post_fork_blind_spends);

    // Test debugwallet spend_frozen_output
    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    num_spendable = rv["num_spendable"].get_int();
    BOOST_CHECK(num_spendable > 0);

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    // BOOST_CHECK(rv["num_spendable"].get_int() == 0);
    BOOST_CHECK(rv["num_unspendable"].get_int() > 0);

    // balancesindex tracks the amount of plain coin sent to and from blind to anon.
    // Coins can move between anon and blind but the sums should match
    BOOST_CHECK(GetBlockBalances(::ChainActive().Tip()->GetBlockHash(), blockbalances));

    BOOST_CHECK_NO_THROW(rv = CallRPC("getbalances", context));
    CAmount blind_trusted = AmountFromValue(rv["mine"]["blind_trusted"]);
    // CAmount anon_trusted = AmountFromValue(rv["mine"]["anon_trusted"]);
    BOOST_CHECK(blind_trusted > blockbalances.blind()); // anon -> blind

    BOOST_CHECK_NO_THROW(rv = CallRPC("debugwallet {\"list_frozen_outputs\":true}", context));
    // CAmount anon_spendable = anon_trusted - AmountFromValue(rv["total_unspendable"]);
    // BOOST_CHECK(anon_spendable < blockbalances.anon()); // anon -> blind
    // BOOST_CHECK(anon_spendable + blind_trusted == blockbalances.blind() + blockbalances.anon());

    SetNumBlocksOfPeers(peer_blocks);
}

BOOST_AUTO_TEST_SUITE_END()
