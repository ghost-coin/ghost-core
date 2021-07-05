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
    const auto context = util::AnyPtr<NodeContext>(&m_node);

    BOOST_CHECK_NO_THROW(rv = CallRPC("extkeyimportmaster xprv9s21ZrQH143K3VrEYG4rhyPddr2o53qqqpCufLP6Rb3XSta2FZsqCanRJVfpTi4UX28pRaAfVGfiGpYDczv8tzTM6Qm5TRvUA9HDStbNUbQ", context));

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress", context));
    BOOST_CHECK(part::StripQuotes(rv.write()) == "SPGxiYZ1Q5dhAJxJNMk56ZbxcsUBYqTCsdEPPHsJJ96Vcns889gHTqSrTZoyrCd5E9NSe9XxLivK6izETniNp1Gu1DtrhVwv3VuZ3e");

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 1", context));
    BOOST_CHECK(part::StripQuotes(rv.write()) == "2w3KaKNNRkWvgxNVymgTwxVd95hDTKRwa98eh5fUpyZfQ17XCRDsxQ3tTARJYz2pNnCekEFni7ukDwvgdbVDgbTy449DNcJYrevkyzPL");

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 1 0b1", context));
    std::string sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "2w3KJzSkeDxiZDFQ5cQdMGKyEuM2zhKHX7TWCTVFobXXcWxWS5zs3aaoF3LPWfcwKd3m65CHx7j8F9CbESmi53GqmHJmnwKggRiXQoac");

    CStealthAddress s1;
    s1.SetEncoded(sResult);
    BOOST_CHECK(s1.Encoded() == sResult);
    BOOST_CHECK(s1.prefix.number_bits == 1);
    BOOST_CHECK(s1.prefix.bitfield == 0b1);


    RewindHdSxChain(pwalletMain.get());

    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 32", context));
    sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "3s73gdiUKMVi4tHMTdker9YzHAS2r6F2CJvC12GfimDdTTn9CLEnEeWW8vdXXkeZouWLgxFGqzbPsnSShNRMsW3j3yL6ssEtjc3gwNSkbBfy");

    CStealthAddress s2;
    s2.SetEncoded(sResult);
    BOOST_CHECK(s2.prefix.number_bits == 32);
    BOOST_CHECK(s2.prefix.bitfield == 4215576597);

    // Check the key is the same
    BOOST_CHECK(s2.scan_pubkey == s1.scan_pubkey);
    BOOST_CHECK(s2.spend_pubkey == s1.spend_pubkey);

    RewindHdSxChain(pwalletMain.get());

    // Check the same prefix is generated
    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress onebit 32", context));
    BOOST_CHECK(part::StripQuotes(rv.write()) == "3s73gdiUKMVi4tHMTdker9YzHAS2r6F2CJvC12GfimDdTTn9CLEnEeWW8vdXXkeZouWLgxFGqzbPsnSShNRMsW3j3yL6ssEtjc3gwNSkbBfy");


    BOOST_CHECK_NO_THROW(rv = CallRPC("getnewstealthaddress t1bin 10 0b1010101111", context));
    sResult = part::StripQuotes(rv.write());
    BOOST_CHECK(sResult == "9XXDiTExjRZsi1ZrvptyJr8AVpMpS5hPsi9uQ3EHgkhicC4EP5MzTg7BkLkSjbgeE69V3wRyuvuoR8WdRPCK6aTcNFKcRYJopwy7BinU3");

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

BOOST_AUTO_TEST_SUITE_END()
