// Copyright (c) 2017-2020 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/client.h>
#include <rpc/rpcutil.h>

#include <base58.h>
#include <validation.h>
#include <wallet/hdwallet.h>

#include <policy/policy.h>

#include <wallet/test/hdwallet_test_fixture.h>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>
#include <util/string.h>


using namespace std;

extern UniValue createArgs(int nRequired, const char* address1 = NULL, const char* address2 = nullptr);

void RewindHdSxChain(CHDWallet *pwallet)
{
    // Rewind the chain to get the same key next request
    ExtKeyAccountMap::iterator mi = pwallet->mapExtAccounts.find(pwallet->idDefaultAccount);
    BOOST_REQUIRE(mi != pwallet->mapExtAccounts.end());
    CExtKeyAccount *sea = mi->second;
    BOOST_REQUIRE(sea->nActiveStealth < sea->vExtKeys.size());
    CStoredExtKey *sek = sea->vExtKeys[sea->nActiveStealth];
    sek->nHGenerated -= 2;
};

BOOST_FIXTURE_TEST_SUITE(rpc_hdwallet_tests, HDWalletTestingSetup)


BOOST_AUTO_TEST_CASE(rpc_hdwallet)
{
    UniValue rv;
    util::Ref context{m_node};

    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster xprv9s21ZrQH143K3VrEYG4rhyPddr2o53qqqpCufLP6Rb3XSta2FZsqCanRJVfpTi4UX28pRaAfVGfiGpYDczv8tzTM6Qm5TRvUA9HDStbNUbQ", context));

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress", context));
    auto res = part::StripQuotes(rv.write());
    BOOST_CHECK(part::StripQuotes(rv.write()) == "SPGwTU7DibFptJSGsghe5yrTgPRBtJzMh2kV5DyzpHGmY2NdyGe3bLssyeszuunZKaVr62E6VJtekcDC8DcgBGobzfck2AXgkKPx5a");

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 1", context));
    BOOST_CHECK(part::StripQuotes(rv.write()) == "2w3KncdXMxuSS9C7JAMxw7PYzakeergWkn7xnje6KTmTAdhB9Nkm4dQVjDHZn3bXFn1RgbXrsYCWjC1o2Cwu9Zw8wG6wjbCnRStgURc9");

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 1 0b1", context));
    std::string sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "2w3KggENVCJZnT6FGh7z4Vhe1uUke3L5QrgH7oon3X9fTWiMXWigB4mDTULUuXszjZzKLBt4XAUhJfky1RadbmXP2Hjf1YtsTa8gB6Fy");

    CStealthAddress s1;
    s1.SetEncoded(sResult);
    BOOST_CHECK(s1.Encoded() == sResult);
    BOOST_CHECK(s1.prefix.number_bits == 1);
    BOOST_CHECK(s1.prefix.bitfield == 0b1);


    RewindHdSxChain(pwalletMain.get());

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 32", context));
    sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "3s74EnSY8ep39a1AwTxuxxeQJp2G1vRWdN3V9nGzF1DZ4SCP4YTn5zanDXm1iDJCMqh4yKd8xW7SJQqJ5A3scg7VQGLughUdK9Lm1feZFXM2");

    CStealthAddress s2;
    s2.SetEncoded(sResult);
    BOOST_CHECK(s2.prefix.number_bits == 32);
    BOOST_CHECK(s2.prefix.bitfield == 2047437459);

    // Check the key is the same
    BOOST_CHECK(s2.scan_pubkey == s1.scan_pubkey);
    BOOST_CHECK(s2.spend_pubkey == s1.spend_pubkey);

    RewindHdSxChain(pwalletMain.get());

    // Check the same prefix is generated
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 32", context));
    BOOST_CHECK(part::StripQuotes(rv.write()) == "3s74EnSY8ep39a1AwTxuxxeQJp2G1vRWdN3V9nGzF1DZ4SCP4YTn5zanDXm1iDJCMqh4yKd8xW7SJQqJ5A3scg7VQGLughUdK9Lm1feZFXM2");


    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress t1bin 10 0b1010101111", context));
    sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "9XXFK7HHbN6UG8YwxPPRJfHGVVSbP3TJLii6bMi2Qu6CGGAjxWSy55i6xk91afTrsBPG4utj6Fq7C4VfE2rC9kA3JH7vcQvA4Eq8kNNBU");

    RewindHdSxChain(pwalletMain.get());
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress t2hex 10 0x2AF", context));
    BOOST_CHECK(sResult == part::StripQuotes(rv.write()));

    RewindHdSxChain(pwalletMain.get());
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress t3dec 10 687", context));
    BOOST_CHECK(sResult == part::StripQuotes(rv.write()));


    BOOST_CHECK_THROW(rv = CallRPC("getnewstealthaddress mustfail 33", context), runtime_error);
    BOOST_CHECK_THROW(rv = CallRPC("getnewstealthaddress mustfail 5 wrong", context), runtime_error);


    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewextaddress", context));
    sResult = part::StripQuotes(rv.write());
}


BOOST_AUTO_TEST_CASE(rpc_hdwallet_timelocks)
{
    UniValue rv;
    util::Ref context{m_node};

    std::string sResult, sTxn, sCmd;
    std::vector<std::string> vAddresses;

    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster xprv9s21ZrQH143K3VrEYG4rhyPddr2o53qqqpCufLP6Rb3XSta2FZsqCanRJVfpTi4UX28pRaAfVGfiGpYDczv8tzTM6Qm5TRvUA9HDStbNUbQ", context));

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewaddress", context));
    sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "GKzUA846vxCULLhZDesMxshgtoxYQB1zSC");


    CKeyID id;
    BOOST_CHECK(CBitcoinAddress(sResult).GetKeyID(id));

    CScript script = CScript() << 1487406900 << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(id) << OP_EQUALVERIFY << OP_CHECKSIG;

    CMutableTransaction txn;
    txn.nVersion = GHOST_TXN_VERSION;
    txn.SetType(TXN_COINBASE);
    txn.nLockTime = 0;
    OUTPUT_PTR<CTxOutStandard> out0 = MAKE_OUTPUT<CTxOutStandard>();
    out0->nValue = 100;
    out0->scriptPubKey = script;
    txn.vpout.push_back(out0);


    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewaddress", context));
    sResult = part::StripQuotes(rv.write());
    vAddresses.push_back(sResult);
    BOOST_CHECK(sResult == "GJgrkH1syivmdCCZ79QaXSPjGbCSU45GgQ");
    BOOST_CHECK(CBitcoinAddress(sResult).GetKeyID(id));


    sTxn = "[{\"txid\":\""
        + txn.GetHash().ToString() + "\","
        + "\"vout\":0,"
        + "\"scriptPubKey\":\"" + HexStr(script) + "\","
        + "\"amount\":100}]";
    sCmd = "createrawtransaction " + sTxn + " {\"" + EncodeDestination(PKHash(id)) + "\":99.99}";


    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    sResult = part::StripQuotes(rv.write());


    sCmd = "signrawtransactionwithwallet " + sResult + " " + sTxn;

    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    BOOST_CHECK(rv["errors"][0]["error"].getValStr() == "Locktime requirement not satisfied");

    sTxn = "[{\"txid\":\""
        + txn.GetHash().ToString() + "\","
        + "\"vout\":0,"
        + "\"scriptPubKey\":\"" + HexStr(script) + "\","
        + "\"amount\":100}]";
    sCmd = "createrawtransaction " + sTxn + " {\"" + EncodeDestination(PKHash(id)) + "\":99.99}" + " 1487500000";

    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    sResult = part::StripQuotes(rv.write());

    sCmd = "signrawtransactionwithwallet " + sResult + " " + sTxn;
    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    BOOST_CHECK(rv["complete"].getBool() == true);
    sResult = rv["hex"].getValStr();


    uint32_t nSequence = 0;

    int32_t nTime = 2880 / (1 << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY); // 48 minutes
    nSequence |= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    nSequence |= nTime;

    script = CScript() << nSequence << OP_CHECKSEQUENCEVERIFY << OP_DROP << OP_DUP << OP_HASH160 << ToByteVector(id) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript *ps = &((CTxOutStandard*)txn.vpout[0].get())->scriptPubKey;
    BOOST_REQUIRE(ps);

    *ps = script;


    sTxn = "[{\"txid\":\""
        + txn.GetHash().ToString() + "\","
        + "\"vout\":0,"
        + "\"scriptPubKey\":\"" + HexStr(script) + "\","
        + "\"amount\":100}]";
    sCmd = "createrawtransaction " + sTxn + " {\"" + CBitcoinAddress(vAddresses[0]).ToString() + "\":99.99}";


    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    sResult = part::StripQuotes(rv.write());


    sCmd = "signrawtransactionwithwallet " + sResult + " " + sTxn;
    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    BOOST_CHECK(rv["errors"][0]["error"].getValStr() == "Locktime requirement not satisfied");

    sTxn = "[{\"txid\":\""
        + txn.GetHash().ToString() + "\","
        + "\"vout\":0,"
        + "\"scriptPubKey\":\"" + HexStr(script) + "\","
        + "\"amount\":100,"
        +"\"sequence\":" + strprintf("%d", nSequence) + "}]";
    sCmd = "createrawtransaction " + sTxn + " {\"" + CBitcoinAddress(vAddresses[0]).ToString() + "\":99.99}";


    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    sResult = part::StripQuotes(rv.write());


    sCmd = "signrawtransactionwithwallet " + sResult + " " + sTxn;
    BOOST_REQUIRE_NO_THROW(rv = CallRPC(sCmd, context));
    BOOST_CHECK(rv["complete"].getBool() == true);
    sResult = rv["hex"].getValStr();


    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewaddress", context));
    std::string sAddr = part::StripQuotes(rv.write());
    BOOST_CHECK(sAddr == "GdcMkDg6Kkvf2gVgYZMfMVmaWoniWoxR4S");

    // 2147483648 is > 32bit signed
    BOOST_CHECK_NO_THROW(rv = CallRPC("buildscript {\"recipe\":\"abslocktime\",\"time\":2147483648,\"addr\":\"" + sAddr + "\"}", context));
    BOOST_REQUIRE(rv["hex"].isStr());

    std::vector<uint8_t> vScript = ParseHex(rv["hex"].get_str());
    script = CScript(vScript.begin(), vScript.end());

    TxoutType whichType;
    BOOST_CHECK(IsStandard(script, whichType));

    opcodetype opcode;
    valtype vchPushValue;
    CScript::const_iterator pc = script.begin();
    BOOST_REQUIRE(script.GetOp(pc, opcode, vchPushValue));
    CScriptNum nTest(vchPushValue, false, 5);
    BOOST_CHECK(nTest == 2147483648);


    BOOST_CHECK_NO_THROW(rv = CallRPC("buildscript {\"recipe\":\"rellocktime\",\"time\":1447483648,\"addr\":\"" + sAddr + "\"}", context));
    BOOST_REQUIRE(rv["hex"].isStr());

    vScript = ParseHex(rv["hex"].get_str());
    script = CScript(vScript.begin(), vScript.end());

    BOOST_CHECK(IsStandard(script, whichType));

    pc = script.begin();
    BOOST_REQUIRE(script.GetOp(pc, opcode, vchPushValue));
    nTest = CScriptNum(vchPushValue, false, 5);
    BOOST_CHECK(nTest == 1447483648);
}

BOOST_AUTO_TEST_SUITE_END()
