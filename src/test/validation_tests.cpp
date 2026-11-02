// Copyright (c) 2014-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <signet.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <rctindex.h>
#include <txdb.h>

#include <array>
#include <chrono>
#include <cstring>
#include <set>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)


BOOST_AUTO_TEST_CASE(rct_read_with_status_valid_value_test)
{
    LOCK(cs_main);

    std::vector<unsigned char> bytes(33, 0);
    bytes[0] = 2;
    bytes[32] = 111;
    CCmpPubKey key(bytes);

    const int64_t expected = 123456789;
    BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(key, expected));

    int64_t actual = 0;

    DBReadResult result = pblocktree->ReadWithStatus(
        std::make_pair(DB_RCTOUTPUT_LINK, key), actual);

    BOOST_CHECK(result == DBReadResult::FOUND);
    BOOST_CHECK_EQUAL(actual, expected);
}


BOOST_AUTO_TEST_CASE(rct_read_with_status_not_found_test)
{
    LOCK(cs_main);

    std::vector<unsigned char> bytes(33, 0);
    bytes[0] = 2;
    bytes[32] = 112;
    CCmpPubKey key(bytes);

    int64_t actual = 0;

    DBReadResult result = pblocktree->ReadWithStatus(
        std::make_pair(DB_RCTOUTPUT_LINK, key), actual);

    BOOST_CHECK(result == DBReadResult::NOT_FOUND);
}


BOOST_AUTO_TEST_CASE(rct_read_with_status_trailing_data_test)
{
    LOCK(cs_main);

    std::vector<unsigned char> bytes(33, 0);
    bytes[0] = 2;
    bytes[32] = 113;
    CCmpPubKey key(bytes);

    // Serialize a valid int64_t followed by one extra byte.
    const std::pair<int64_t, unsigned char> corrupt_value(
        123456789, 0xAA);

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTOUTPUT_LINK, key), corrupt_value));

    int64_t actual = 0;

    DBReadResult result = pblocktree->ReadWithStatus(
        std::make_pair(DB_RCTOUTPUT_LINK, key), actual);

    BOOST_CHECK(result == DBReadResult::CORRUPT);
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_success_test)
{
    LOCK(cs_main);

    const int64_t base = 100;
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 1);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 10;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(base + i, output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], base + i));
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_REQUIRE(pblocktree->RollbackRCTIndexAtomic(
        base, base + 2, 10, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    BOOST_CHECK(pblocktree->ReadRCTOutput(base, output));
    BOOST_CHECK(pblocktree->ReadRCTOutputLink(keys[0], linked_index));
    BOOST_CHECK_EQUAL(linked_index, base);

    for (int64_t i = base + 1; i <= base + 2; ++i) {
        BOOST_CHECK(!pblocktree->ReadRCTOutput(i, output));
        BOOST_CHECK(!pblocktree->ReadRCTOutputLink(keys[i - base], linked_index));
    }
}




BOOST_AUTO_TEST_CASE(rct_atomic_rollback_missing_link_test)
{
    LOCK(cs_main);

    const int64_t base = 200;
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 10);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 20;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(base + i, output));

        if (i != 2) {
            BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], base + i));
        }
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        base, base + 2, 20, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (int64_t i = base; i <= base + 2; ++i) {
        BOOST_CHECK(pblocktree->ReadRCTOutput(i, output));
    }

    BOOST_CHECK(pblocktree->ReadRCTOutputLink(keys[0], linked_index));
    BOOST_CHECK_EQUAL(linked_index, base);

    BOOST_CHECK(pblocktree->ReadRCTOutputLink(keys[1], linked_index));
    BOOST_CHECK_EQUAL(linked_index, base + 1);

    BOOST_CHECK(!pblocktree->ReadRCTOutputLink(keys[2], linked_index));
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_link_mismatch_test)
{
    LOCK(cs_main);

    const int64_t base = 300;
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 20);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 30;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(base + i, output));

        const int64_t link_index = (i == 2) ? base + 99 : base + i;
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], link_index));
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        base, base + 2, 30, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (int64_t i = base; i <= base + 2; ++i) {
        BOOST_CHECK(pblocktree->ReadRCTOutput(i, output));
        BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(
            keys[i - base], linked_index));

        const int64_t expected = (i == base + 2) ? base + 99 : i;
        BOOST_CHECK_EQUAL(linked_index, expected);
    }
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_corrupt_link_test)
{
    LOCK(cs_main);

    const int64_t base = 400;
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 30);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 40;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(base + i, output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], base + i));
    }

    // Replace the final link value with an invalid serialization.
    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTOUTPUT_LINK, keys[2]),
        std::vector<unsigned char>{0x01}));

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        base, base + 2, 40, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (int64_t i = base; i <= base + 2; ++i) {
        BOOST_CHECK(pblocktree->ReadRCTOutput(i, output));
    }

    BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(keys[0], linked_index));
    BOOST_CHECK_EQUAL(linked_index, base);

    BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(keys[1], linked_index));
    BOOST_CHECK_EQUAL(linked_index, base + 1);

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTOUTPUT_LINK, keys[2])));
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_corrupt_output_test)
{
    LOCK(cs_main);

    const int64_t base = 500;
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < keys.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 40);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 50;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(base + i, output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], base + i));
    }

    // Replace output 502 with an invalid serialized value.
    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTOUTPUT, base + 2),
        std::vector<unsigned char>{0x01}));

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        base, base + 2, 50, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    BOOST_CHECK(pblocktree->ReadRCTOutput(base, output));
    BOOST_CHECK(pblocktree->ReadRCTOutput(base + 1, output));

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTOUTPUT, base + 2)));

    for (int64_t i = base; i <= base + 2; ++i) {
        BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(
            keys[i - base], linked_index));
        BOOST_CHECK_EQUAL(linked_index, i);
    }
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_gapped_outputs_test)
{
    LOCK(cs_main);

    const int64_t base = 600;
    const std::array<int64_t, 3> indices{{601, 603, 605}};
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < indices.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 60);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 60;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(indices[i], output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], indices[i]));
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_REQUIRE(pblocktree->RollbackRCTIndexAtomic(
        base, 605, 60, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (size_t i = 0; i < indices.size(); ++i) {
        BOOST_CHECK(!pblocktree->ReadRCTOutput(indices[i], output));
        BOOST_CHECK(!pblocktree->ReadRCTOutputLink(keys[i], linked_index));
    }
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_above_expected_test)
{
    LOCK(cs_main);

    const int64_t base = 700;
    const std::array<int64_t, 3> indices{{701, 703, 705}};
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < indices.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 70);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 70;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(indices[i], output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], indices[i]));
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_REQUIRE(pblocktree->RollbackRCTIndexAtomic(
        base, 703, 70, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (size_t i = 0; i < indices.size(); ++i) {
        BOOST_CHECK(!pblocktree->ReadRCTOutput(indices[i], output));
        BOOST_CHECK(!pblocktree->ReadRCTOutputLink(keys[i], linked_index));
    }
}



BOOST_AUTO_TEST_CASE(rct_atomic_rollback_corrupt_key_image_test)
{
    LOCK(cs_main);

    const int64_t index = 900;

    std::vector<unsigned char> output_bytes(33, 0);
    output_bytes[0] = 2;
    output_bytes[32] = 91;
    CCmpPubKey output_key(output_bytes);

    CAnonOutput output;
    output.pubkey = output_key;
    std::memset(output.commitment.data, 0, 33);
    output.nBlockHeight = 90;

    BOOST_REQUIRE(pblocktree->WriteRCTOutput(index, output));
    BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(output_key, index));

    std::vector<unsigned char> ki_bytes(33, 0);
    ki_bytes[0] = 2;
    ki_bytes[32] = 92;
    CCmpPubKey corrupt_ki(ki_bytes);

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, corrupt_ki),
        std::vector<unsigned char>{0x01}));

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        index - 1, index, 90, key_images));

    CAnonOutput preserved_output;
    int64_t linked_index = 0;

    BOOST_CHECK(pblocktree->ReadRCTOutput(index, preserved_output));
    BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(
        output_key, linked_index));
    BOOST_CHECK_EQUAL(linked_index, index);

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, corrupt_ki)));
}




BOOST_AUTO_TEST_CASE(rct_atomic_rollback_key_image_trailing_data_test)
{
    LOCK(cs_main);

    const int64_t index = 950;

    std::vector<unsigned char> output_bytes(33, 0);
    output_bytes[0] = 2;
    output_bytes[32] = 115;
    CCmpPubKey output_key(output_bytes);

    CAnonOutput output;
    output.pubkey = output_key;
    std::memset(output.commitment.data, 0, 33);
    output.nBlockHeight = 95;

    BOOST_REQUIRE(pblocktree->WriteRCTOutput(index, output));
    BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(output_key, index));

    std::vector<unsigned char> ki_bytes(33, 0);
    ki_bytes[0] = 2;
    ki_bytes[32] = 116;
    CCmpPubKey corrupt_ki(ki_bytes);

    // A valid 36-byte key image record plus one trailing byte.
    std::vector<unsigned char> corrupt_value(37, 0);
    corrupt_value[36] = 0xAA;

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, corrupt_ki),
        corrupt_value));

    const std::set<CCmpPubKey> key_images;

    BOOST_CHECK(!pblocktree->RollbackRCTIndexAtomic(
        index - 1, index, 95, key_images));

    // The failed rollback must not commit staged output deletions.
    CAnonOutput preserved_output;
    int64_t linked_index = 0;

    BOOST_CHECK(pblocktree->ReadRCTOutput(index, preserved_output));
    BOOST_REQUIRE(pblocktree->ReadRCTOutputLink(
        output_key, linked_index));
    BOOST_CHECK_EQUAL(linked_index, index);

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, corrupt_ki)));
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_key_image_compatibility_test)
{
    LOCK(cs_main);

    auto make_key = [](unsigned char suffix) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = suffix;
        return CCmpPubKey(bytes);
    };

    const CCmpPubKey old_key = make_key(101);
    const CCmpPubKey current_key = make_key(102);
    const CCmpPubKey future_key = make_key(103);
    const CCmpPubKey legacy_key = make_key(104);
    const CCmpPubKey explicit_key = make_key(105);

    const uint256 txid;

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, old_key),
        CAnonKeyImageInfo(txid, 90)));

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, current_key),
        CAnonKeyImageInfo(txid, 100)));

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, future_key),
        CAnonKeyImageInfo(txid, 110)));

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, legacy_key),
        txid));

    BOOST_REQUIRE(pblocktree->Write(
        std::make_pair(DB_RCTKEYIMAGE, explicit_key),
        txid));

    const std::set<CCmpPubKey> key_images{explicit_key};

    BOOST_REQUIRE(pblocktree->RollbackRCTIndexAtomic(
        0, 0, 100, key_images));

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, old_key)));

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, current_key)));

    BOOST_CHECK(!pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, future_key)));

    BOOST_CHECK(pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, legacy_key)));

    BOOST_CHECK(!pblocktree->Exists(
        std::make_pair(DB_RCTKEYIMAGE, explicit_key)));
}


BOOST_AUTO_TEST_CASE(rct_atomic_rollback_byte_order_test)
{
    LOCK(cs_main);

    const int64_t last_valid = 254;
    const std::array<int64_t, 3> indices{{255, 256, 257}};
    std::array<CCmpPubKey, 3> keys;

    for (size_t i = 0; i < indices.size(); ++i) {
        std::vector<unsigned char> bytes(33, 0);
        bytes[0] = 2;
        bytes[32] = static_cast<unsigned char>(i + 80);
        keys[i] = CCmpPubKey(bytes);

        CAnonOutput output;
        output.pubkey = keys[i];
        std::memset(output.commitment.data, 0, 33);
        output.nBlockHeight = 80;

        BOOST_REQUIRE(pblocktree->WriteRCTOutput(indices[i], output));
        BOOST_REQUIRE(pblocktree->WriteRCTOutputLink(keys[i], indices[i]));
    }

    const std::set<CCmpPubKey> key_images;

    BOOST_REQUIRE(pblocktree->RollbackRCTIndexAtomic(
        last_valid, 257, 80, key_images));

    CAnonOutput output;
    int64_t linked_index = 0;

    for (size_t i = 0; i < indices.size(); ++i) {
        BOOST_CHECK(!pblocktree->ReadRCTOutput(indices[i], output));
        BOOST_CHECK(!pblocktree->ReadRCTOutputLink(keys[i], linked_index));
    }
}

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = 50 * COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
    TestBlockSubsidyHalvings(150); // As in regtest
    TestBlockSubsidyHalvings(1000); // Just another interval
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy * 1000;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, CAmount{2099999997690000});
}

// BOOST_AUTO_TEST_CASE(signet_parse_tests)
// {
//     ArgsManager signet_argsman;
//     signet_argsman.ForceSetArg("-signetchallenge", "51"); // set challenge to OP_TRUE
//     const auto signet_params = CreateChainParams(CBaseChainParams::SIGNET);
//     CBlock block;
//     BOOST_CHECK(signet_params->GetConsensus().signet_challenge == std::vector<uint8_t>{OP_TRUE});
//     CScript challenge{OP_TRUE};

//     // empty block is invalid
//     BOOST_CHECK(!SignetTxs::Create(block, challenge));
//     BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // no witness commitment
//     CMutableTransaction cb;
//     cb.vout.emplace_back(0, CScript{});
//     block.vtx.push_back(MakeTransactionRef(cb));
//     block.vtx.push_back(MakeTransactionRef(cb)); // Add dummy tx to excercise merkle root code
//     BOOST_CHECK(!SignetTxs::Create(block, challenge));
//     BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // no header is treated valid
//     std::vector<uint8_t> witness_commitment_section_141{0xaa, 0x21, 0xa9, 0xed};
//     for (int i = 0; i < 32; ++i) {
//         witness_commitment_section_141.push_back(0xff);
//     }
//     cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141;
//     block.vtx.at(0) = MakeTransactionRef(cb);
//     BOOST_CHECK(SignetTxs::Create(block, challenge));
//     BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // no data after header, valid
//     std::vector<uint8_t> witness_commitment_section_325{0xec, 0xc7, 0xda, 0xa2};
//     cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
//     block.vtx.at(0) = MakeTransactionRef(cb);
//     BOOST_CHECK(SignetTxs::Create(block, challenge));
//     BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // Premature end of data, invalid
//     witness_commitment_section_325.push_back(0x01);
//     witness_commitment_section_325.push_back(0x51);
//     cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
//     block.vtx.at(0) = MakeTransactionRef(cb);
//     BOOST_CHECK(!SignetTxs::Create(block, challenge));
//     BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // has data, valid
//     witness_commitment_section_325.push_back(0x00);
//     cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
//     block.vtx.at(0) = MakeTransactionRef(cb);
//     BOOST_CHECK(SignetTxs::Create(block, challenge));
//     BOOST_CHECK(CheckSignetBlockSolution(block, signet_params->GetConsensus()));

//     // Extraneous data, invalid
//     witness_commitment_section_325.push_back(0x00);
//     cb.vout.at(0).scriptPubKey = CScript{} << OP_RETURN << witness_commitment_section_141 << witness_commitment_section_325;
//     block.vtx.at(0) = MakeTransactionRef(cb);
//     BOOST_CHECK(!SignetTxs::Create(block, challenge));
//     BOOST_CHECK(!CheckSignetBlockSolution(block, signet_params->GetConsensus()));
// }

BOOST_AUTO_TEST_SUITE_END()
