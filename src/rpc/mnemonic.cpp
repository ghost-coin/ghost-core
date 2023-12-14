// Copyright (c) 2015 The ShadowCoin developers
// Copyright (c) 2017-2022 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>

#include <util/strencodings.h>
#include <rpc/util.h>
#include <key_io.h>
#include <key/extkey.h>
#include <random.h>
#include <chainparams.h>
#include <support/cleanse.h>
#include <key/mnemonic.h>

#include <string>
#include <univalue.h>

//typedef std::basic_string<char, std::char_traits<char>, secure_allocator<char> > SecureString;

static RPCHelpMan mnemonicrpc()
{
    return RPCHelpMan{"mnemonic",
            "\nGenerate mnemonic phrases.\n"
        "mnemonic new ( \"password\" language nBytesEntropy bip44 )\n"
        "    Generate a new extended key and mnemonic\n"
        "    password, can be blank "", default blank\n"
        "    language, " + mnemonic::ListEnabledLanguages("|") + ", default english\n"
        "    nBytesEntropy, 16 -> 64, default 32\n"
        "    bip44, true|false, default true\n"
        "mnemonic decode \"password\" \"mnemonic\" ( bip44 )\n"
        "    Decode mnemonic\n"
        "    bip44, true|false, default true\n"
        "mnemonic addchecksum \"mnemonic\"\n"
        "    Add checksum words to mnemonic.\n"
        "    Final no of words in mnemonic must be divisible by three.\n"
        "mnemonic dumpwords ( \"language\" )\n"
        "    Print list of words.\n"
        "    language, default english\n"
        "mnemonic listlanguages\n"
        "    Print list of supported languages.\n",
    {
        {"mode", RPCArg::Type::STR, RPCArg::Optional::NO, "One of: new, decode, addchecksum, dumpwords, listlanguages"},
        {"arg0", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "", RPCArgOptions{.skip_type_check = true}},
        {"arg1", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "", RPCArgOptions{.skip_type_check = true}},
        {"arg2", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "", RPCArgOptions{.skip_type_check = true}},
        {"arg3", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "", RPCArgOptions{.skip_type_check = true}},
    },
    RPCResult{RPCResult::Type::ANY, "", ""},
    RPCExamples{
        HelpExampleCli("mnemonic", "\"new\" \"my pass phrase\" french 64 true") +
        HelpExampleRpc("smsgpurge", "\"new\", \"my pass phrase\", french, 64, true")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string mode;

    if (request.params.size() > 0) {
        std::string s = request.params[0].get_str();
        std::string st = " " + s + " "; // Note the spaces
        st = ToLower(st);
        static const char *pmodes = " new decode addchecksum dumpwords listlanguages ";
        if (strstr(pmodes, st.c_str()) != nullptr) {
            st.erase(std::remove(st.begin(), st.end(), ' '), st.end());
            mode = st;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown mode.");
        }
    }

    UniValue result(UniValue::VOBJ);

    if (mode == "new") {
        int nLanguage = mnemonic::WLL_ENGLISH;
        int nBytesEntropy = 32;
        std::string sMnemonic, sPassword, sError;
        CExtKey ekMaster;

        if (request.params.size() > 1) {
            sPassword = request.params[1].get_str();
        }
        if (request.params.size() > 2) {
            nLanguage = mnemonic::GetLanguageOffset(request.params[2].get_str());
        }

        if (request.params.size() > 3) {
            if (request.params[3].isNum()) {
                nBytesEntropy = request.params[3].getInt<int>();
            } else
            if (!ParseInt32(request.params[3].get_str(), &nBytesEntropy)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid num bytes entropy");
            }
            if (nBytesEntropy < 16 || nBytesEntropy > 64) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Num bytes entropy out of range [16,64].");
            }
        }

        bool fBip44 = request.params.size() > 4 ? GetBool(request.params[4]) : true;

        if (request.params.size() > 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");
        }

        std::vector<uint8_t> vEntropy(nBytesEntropy), vSeed;
        for (uint32_t i = 0; i < MAX_DERIVE_TRIES; ++i) {
            GetStrongRandBytes2(&vEntropy[0], nBytesEntropy);

            if (0 != mnemonic::Encode(nLanguage, vEntropy, sMnemonic, sError)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Encode failed %s.", sError.c_str()).c_str());
            }
            if (0 != mnemonic::ToSeed(sMnemonic, sPassword, vSeed)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "mnemonic::ToSeed failed.");
            }

            ekMaster.SetSeed(&vSeed[0], vSeed.size());
            if (!ekMaster.IsValid()) {
                continue;
            }
            break;
        }

        CExtKey58 eKey58;
        result.pushKV("mnemonic", sMnemonic);

        if (fBip44) {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY_BTC);
            result.pushKV("master", eKey58.ToString());

            // m / purpose' / coin_type' / account' / change / address_index
            // path "44' Params().BIP44ID()
        } else {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY);
            result.pushKV("master", eKey58.ToString());
        }

        // In c++11 strings are definitely contiguous, and before they're very unlikely not to be
        if (sMnemonic.size() > 0) {
            memory_cleanse(&sMnemonic[0], sMnemonic.size());
        }
        if (sPassword.size() > 0) {
            memory_cleanse(&sPassword[0], sPassword.size());
        }
    } else
    if (mode == "decode") {
        std::string sPassword, sMnemonic, sError;

        if (request.params.size() > 1) {
            sPassword = request.params[1].get_str();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must specify password.");
        }
        if (request.params.size() > 2) {
            sMnemonic = request.params[2].get_str();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must specify mnemonic.");
        }

        bool fBip44 = request.params.size() > 3 ? GetBool(request.params[3]) : true;
        bool fLegacy = request.params.size() > 4 ? GetBool(request.params[4]) : false;
        
        if (request.params.size() > 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");
        }
        if (sMnemonic.empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Mnemonic can't be blank.");
        }

        // Decode to determine validity of mnemonic
        std::vector<uint8_t> vEntropy, vSeed;
        int nLanguage = -1;
        if (0 != mnemonic::Decode(nLanguage, sMnemonic, vEntropy, sError)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Decode failed %s.", sError.c_str()).c_str());
        }
        if (0 != mnemonic::ToSeed(sMnemonic, sPassword, vSeed)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "mnemonic::ToSeed failed.");
        }

        CExtKey ekMaster;
        CExtKey58 eKey58;
        ekMaster.SetSeed(&vSeed[0], vSeed.size());

        if (!ekMaster.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid key.");
        }

        if (fBip44) {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY_BTC);
            result.pushKV("master", eKey58.ToString());

            // m / purpose' / coin_type' / account' / change / address_index
            CExtKey ekDerived;
            if (!ekMaster.Derive(ekDerived, BIP44_PURPOSE)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to derive master key");
            }
            if (!ekDerived.Derive(ekDerived, (uint32_t)Params().BIP44ID(fLegacy))) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to derive bip44 key");
            }

            eKey58.SetKey(CExtKeyPair(ekDerived), CChainParams::EXT_SECRET_KEY);
            result.pushKV("derived", eKey58.ToString());
        } else {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY);
            result.pushKV("master", eKey58.ToString());
        }

        result.pushKV("language", mnemonic::GetLanguage(nLanguage));

        if (sMnemonic.size() > 0) {
            memory_cleanse(&sMnemonic[0], sMnemonic.size());
        }
        if (sPassword.size() > 0) {
            memory_cleanse(&sPassword[0], sPassword.size());
        }
    } else
    if (mode == "addchecksum") {
        std::string sMnemonicIn, sMnemonicOut, sError;
        if (request.params.size() != 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must provide input mnemonic.");
        }

        sMnemonicIn = request.params[1].get_str();

        if (0 != mnemonic::AddChecksum(-1, sMnemonicIn, sMnemonicOut, sError)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::AddChecksum failed %s", sError.c_str()).c_str());
        }
        result.pushKV("result", sMnemonicOut);
    } else
    if (mode == "dumpwords") {
        int nLanguage = mnemonic::WLL_ENGLISH;

        if (request.params.size() > 1) {
            nLanguage = mnemonic::GetLanguageOffset(request.params[1].get_str());
        }

        int nWords = 0;
        UniValue arrayWords(UniValue::VARR);

        std::string sWord, sError;
        while (0 == mnemonic::GetWord(nLanguage, nWords, sWord, sError)) {
            arrayWords.push_back(sWord);
            nWords++;
        }

        result.pushKV("words", arrayWords);
        result.pushKV("num_words", nWords);
    } else
    if (mode == "listlanguages") {
        for (size_t k = 1; k < mnemonic::WLL_MAX; ++k) {
            if (!mnemonic::HaveLanguage(k)) {
                continue;
            }
            std::string sName(mnemonic::mnLanguagesTag[k]);
            std::string sDesc(mnemonic::mnLanguagesDesc[k]);
            result.pushKV(sName, sDesc);
        }
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown mode.");
    }

    return result;
},
    };
};

static RPCHelpMan splitmnemonic()
{
    return RPCHelpMan{"splitmnemonic",
        "\nSplit a mnemonic according to shamir39.\n"
        "Should be compatible with: https://iancoleman.io/shamir39\n"
        "WARNING: This feature is experimental.\n",
        {
            {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Parameters",
                {
                    {"mnemonic", RPCArg::Type::STR, RPCArg::Optional::NO, "The mnemonic to be split"},
                    {"numshares", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of shares to output"},
                    {"threshold", RPCArg::Type::NUM, RPCArg::Optional::NO, "Minimum number of shares to recreate the mnemonic"},
                    {"language", RPCArg::Type::STR, RPCArg::Default{"autodetect"}, "The language to use"},
                    // {"specification", RPCArg::Type::STR, RPCArg::Default{"shamir39"}, "shamir39/slip39"}, // TODO
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "shares", "",
            {
                {RPCResult::Type::STR, "share", "Mnemonic share"},
            }
        },
        RPCExamples{
            HelpExampleCli("splitmnemonic", "'{ \"mnemonic\": \"mnemonic words\", \"numsplits\": 2, \"threshold\": 2 }'")
            + HelpExampleRpc("splitmnemonic", "'{ \"mnemonic\": \"mnemonic words\", \"numsplits\": 2, \"threshold\": 2 }'")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const UniValue &parameters = request.params[0].get_obj();
    RPCTypeCheckObj(parameters,
    {
        {"mnemonic",  UniValueType(UniValue::VSTR)},
        {"numshares", UniValueType(UniValue::VNUM)},
        {"threshold", UniValueType(UniValue::VNUM)},
    }, /* allow null */ false, /* strict */ false);

    std::string error_str, mnemonic_string = parameters["mnemonic"].get_str();
    int num_splits = parameters["numshares"].getInt<int>();
    int actual_threshold = parameters["threshold"].getInt<int>();
    std::vector<std::string> shares_out;
    int language_ind = -1;
    if (parameters.exists("language")) {
        std::string language_str = parameters["language"].get_str();
        if (language_str != "autodetect") {
            language_ind = mnemonic::GetLanguageOffset(language_str);
        }
    }

    if (0 != shamir39::splitmnemonic(mnemonic_string, language_ind, num_splits, actual_threshold, shares_out, error_str)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("splitmnemonic failed %s", error_str.c_str()).c_str());
    }

    UniValue rv(UniValue::VARR);
    for (const auto &share : shares_out) {
        rv.push_back(share);
    }
    return rv;
},
    };
}

static RPCHelpMan combinemnemonic()
{
    return RPCHelpMan{"combinemnemonic",
        "\nCombine shamir39 shares into a mnemonic.\n"
        "WARNING: This feature is experimental.\n",
        {
            {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Parameters",
                {
                    {"shares", RPCArg::Type::ARR, RPCArg::Optional::NO, "The mnemonics to be joined",
                        {
                            {"mnemonic", RPCArg::Type::STR, RPCArg::Optional::NO, "mnemonic"},
                        },
                    },
                    {"language", RPCArg::Type::STR, RPCArg::Default{"autodetect"}, "The language to use"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::STR, "mnemonic", "The joined mnemonic"
        },
        RPCExamples{
            HelpExampleCli("combinemnemonic", "'{ \"shares\": [ \"mnemonic words 1\", \"mnemonic words 2\" ] }'")
            + HelpExampleRpc("combinemnemonic", "'{ \"shares\": [ \"mnemonic words 1\", \"mnemonic words 2\" ] }'")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const UniValue &parameters = request.params[0].get_obj();
    RPCTypeCheckObj(parameters,
    {
        {"shares", UniValueType(UniValue::VARR)},
    }, /* allow null */ false, /* strict */ false);

    std::vector<std::string> shares;
    const UniValue &uv_shares = parameters["shares"];
    shares.reserve(uv_shares.size());
    for (size_t i = 0; i < uv_shares.size(); ++i) {
        shares.push_back(uv_shares[i].get_str());
    }
    std::string mnemonic_out, error_str;
    int language_ind = -1;
    if (parameters.exists("language")) {
        std::string language_str = parameters["language"].get_str();
        if (language_str != "autodetect") {
            language_ind = mnemonic::GetLanguageOffset(language_str);
        }
    }

    if (0 != shamir39::combinemnemonic(shares, language_ind, mnemonic_out, error_str)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("combinemnemonic failed %s", error_str.c_str()).c_str());
    }

    return mnemonic_out;
},
    };
}

static RPCHelpMan mnemonictoentropy()
{
    return RPCHelpMan{"mnemonictoentropy",
        "\nConvert mnemonic words to entropy hex.\n",
        {
            {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Parameters",
                {
                    {"mnemonic", RPCArg::Type::STR, RPCArg::Optional::NO, "The mnemonic to decode"},
                    {"language", RPCArg::Type::STR, RPCArg::Default{"autodetect"}, "The language to use"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "entropy", "The entropy bytes"
        },
        RPCExamples{
            HelpExampleCli("mnemonictoentropy", "'{ \"mnemonic\": \"mnemonic words\" }'")
            + HelpExampleRpc("mnemonictoentropy", "'{ \"mnemonic\": \"mnemonic words\" }'")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const UniValue &parameters = request.params[0].get_obj();
    RPCTypeCheckObj(parameters,
    {
        {"mnemonic", UniValueType(UniValue::VSTR)},
    }, /* allow null */ false, /* strict */ false);

    int language_ind = -1;
    if (parameters.exists("language")) {
        std::string language_str = parameters["language"].get_str();
        if (language_str != "autodetect") {
            language_ind = mnemonic::GetLanguageOffset(language_str);
        }
    }

    std::string error_str, mnemonic_string = parameters["mnemonic"].get_str();
    std::vector<uint8_t> entropy;
    if (0 != mnemonic::Decode(language_ind, mnemonic_string, entropy, error_str)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Decode failed %s.", error_str.c_str()).c_str());
    }

    return HexStr(entropy);
},
    };
}

static RPCHelpMan mnemonicfromentropy()
{
    return RPCHelpMan{"mnemonicfromentropy",
        "\nConvert entropy hex to mnemonic words.\n",
        {
            {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Parameters",
                {
                    {"entropy", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The entropy bytes to be encoded"},
                    {"language", RPCArg::Type::STR, RPCArg::Default{"english"}, "The language to use"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::STR, "mnemonic", "The mnemonic words"
        },
        RPCExamples{
            HelpExampleCli("mnemonicfromentropy", "'{ \"entropy\": \"entropyhex\" }'")
            + HelpExampleRpc("mnemonicfromentropy", "'{ \"entropy\": \"entropyhex\" }'")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const UniValue &parameters = request.params[0].get_obj();
    RPCTypeCheckObj(parameters,
    {
        {"entropy", UniValueType(UniValue::VSTR)},
    }, /* allow null */ false, /* strict */ false);

    int language_ind = mnemonic::WLL_ENGLISH;
    if (parameters.exists("language")) {
        std::string language_str = parameters["language"].get_str();
        language_ind = mnemonic::GetLanguageOffset(language_str);
    }

    std::vector<uint8_t> entropy = ParseHex(parameters["entropy"].get_str());
    std::string error_str, mnemonic_string;
    if (0 != mnemonic::Encode(language_ind, entropy, mnemonic_string, error_str)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Encode failed %s.", error_str.c_str()).c_str());
    }

    return mnemonic_string;
},
    };
}

void RegisterMnemonicRPCCommands(CRPCTable &t)
{
    static const CRPCCommand commands[]{
        {"mnemonic", &mnemonicrpc},
        {"mnemonic", &splitmnemonic},
        {"mnemonic", &combinemnemonic},
        {"mnemonic", &mnemonictoentropy},
        {"mnemonic", &mnemonicfromentropy},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
