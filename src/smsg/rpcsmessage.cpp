// Copyright (c) 2014-2016 The ShadowCoin developers
// Copyright (c) 2017-2022 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>

#include <algorithm>
#include <string>

#include <smsg/smessage.h>
#include <smsg/db.h>
#include <wallet/types.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/fs_helpers.h>
#include <node/blockstorage.h>
#include <consensus/consensus.h>
#include <core_io.h>
#include <base58.h>
#include <rpc/util.h>
#include <rpc/server_util.h>
#include <rpc/blockchain.h>
#include <validation.h>
#include <timedata.h>
#include <anon.h>
#include <validationinterface.h>
#include <node/context.h>
#include <interfaces/wallet.h>
#include <util/time.h>
#include <util/string.h>
#include <util/syserror.h>
#include <util/moneystr.h>

#include <leveldb/db.h>

#ifdef ENABLE_WALLET
#include <wallet/hdwallet.h>
#include <wallet/coincontrol.h>
extern void EnsureWalletIsUnlocked(const CHDWallet *pwallet);
extern void ParseCoinControlOptions(const UniValue &obj, const CHDWallet *pwallet, CCoinControl &coin_control);
#endif

#include <univalue.h>
#include <fstream>


static void EnsureSMSGIsEnabled()
{
    if (!smsg::fSecMsgEnabled) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Secure messaging is disabled.");
    }
};

static RPCHelpMan smsgenable()
{
    return RPCHelpMan{"smsgenable",
                "Enable secure messaging with the specified wallet as the active wallet.\n"
                "Uses the first smsg-enabled wallet as the active wallet if none specified.\n",
                {
                    {"walletname", RPCArg::Type::STR, RPCArg::Default{"wallet.dat"}, "Active smsg wallet."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "result", "Result."},
                        {RPCResult::Type::STR, "wallet", "The active wallet."}
                    }
                },
                RPCExamples{
            HelpExampleCli("smsgenable", "\"wallet_name\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsgenable", "\"wallet_name\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (smsg::fSecMsgEnabled) {
        throw JSONRPCError(RPC_MISC_ERROR, "Secure messaging is already enabled.");
    }

    UniValue result(UniValue::VOBJ);

    std::shared_ptr<wallet::CWallet> pwallet;
    std::string sFindWallet, wallet_name = "Not set.";
#ifdef ENABLE_WALLET
    auto vpwallets = GetWallets(*smsgModule.m_node->wallet_loader->context());

    if (!request.params[0].isNull()) {
        sFindWallet = request.params[0].get_str();
    }
    for (const auto &pw : vpwallets) {
        CHDWallet *const ppartw = GetParticlWallet(pw.get());
        if (!ppartw) {
            continue;
        }
        if (!request.params[0].isNull() && ppartw->GetName() == sFindWallet) {
            pwallet = pw;
            break;
        }
        if (ppartw->m_smsg_enabled) {
            pwallet = pw;
            break;
        }
    }
    if (!request.params[0].isNull() && !pwallet) {
        throw JSONRPCError(RPC_MISC_ERROR, "Wallet not found: \"" + sFindWallet + "\"");
    }
    if (pwallet) {
        wallet_name = pwallet->GetName();
    }
    result.pushKV("result", (smsgModule.Enable(pwallet, vpwallets) ? "Enabled secure messaging." : "Failed."));
#else
    std::vector<std::shared_ptr<wallet::CWallet>> empty;
    result.pushKV("result", (smsgModule.Enable(pwallet, empty) ? "Enabled secure messaging." : "Failed."));
#endif

    result.pushKV("wallet", wallet_name);

    return result;
},
    };
}

static RPCHelpMan smsgdisable()
{
    return RPCHelpMan{"smsgdisable",
                "\nDisable secure messaging.\n",
                {
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "result", "Result."}
                    }
                },
                RPCExamples{
                    HelpExampleCli("smsgdisable", "")
                    + HelpExampleRpc("smsgdisable", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (!smsg::fSecMsgEnabled) {
        throw JSONRPCError(RPC_MISC_ERROR, "Secure messaging is already disabled.");
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("result", (smsgModule.Disable() ? "Disabled secure messaging." : "Failed."));

    return result;
},
    };
}

static RPCHelpMan smsgsetwallet()
{
    return RPCHelpMan{"smsgsetwallet",
                "Set secure messaging to use the specified wallet.\n"
                "SMSG can only be enabled on one wallet.\n"
                "Call with no parameters to unset the active wallet.\n",
                {
                    {"walletname", RPCArg::Type::STR, RPCArg::Default{"wallet.dat"}, "Enable smsg on a specific wallet."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgsetwallet", "\"wallet_name\"")
                    + HelpExampleRpc("smsgsetwallet", "\"wallet_name\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (!smsg::fSecMsgEnabled) {
        throw JSONRPCError(RPC_MISC_ERROR, "Secure messaging must be enabled.");
    }

    UniValue result(UniValue::VOBJ);

    std::shared_ptr<wallet::CWallet> pwallet;
    std::string wallet_name = "Not set.";
#ifndef ENABLE_WALLET
    throw JSONRPCError(RPC_MISC_ERROR, "Wallet is disabled.");
#else
    auto vpwallets = GetWallets(*smsgModule.m_node->wallet_loader->context());

    if (!request.params[0].isNull()) {
        std::string sFindWallet = request.params[0].get_str();

        for (const auto &pw : vpwallets) {
            if (pw->GetName() != sFindWallet) {
                continue;
            }
            pwallet = pw;
            break;
        }
        if (!pwallet) {
            throw JSONRPCError(RPC_MISC_ERROR, "Wallet not found: \"" + sFindWallet + "\"");
        }
    }
    if (pwallet) {
        wallet_name = pwallet->GetName();
    }
#endif

    result.pushKV("result", (smsgModule.SetActiveWallet(pwallet) ? "Set active wallet." : "Failed."));
    result.pushKV("wallet", wallet_name);

    return result;
},
    };
}

static RPCHelpMan smsgoptions()
{
    return RPCHelpMan{"smsgoptions",
                "\nList and manage options.\n",
                {
                    {"mode", RPCArg::Type::STR, RPCArg::Default{"list"}, "Mode: list or set, 2nd arg is with_description in list mode."},
                    {"optname", RPCArg::Type::STR, RPCArg::Default{""}, "Option name.", RPCArgOptions{.skip_type_check = true}},
                    {"value", RPCArg::Type::STR, RPCArg::Default{""}, "New option value.", RPCArgOptions{.skip_type_check = true}},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
            "\nList possible options with descriptions.\n"
            + HelpExampleCli("smsgoptions", "list 1")
            + HelpExampleRpc("smsgoptions", "\"list\", 1")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string mode = "list";
    if (request.params.size() > 0) {
        mode = request.params[0].get_str();
    }

    UniValue result(UniValue::VOBJ);

    if (mode == "list") {
        UniValue options(UniValue::VARR);

        bool fDescriptions = false;
        if (!request.params[1].isNull()) {
            fDescriptions = GetBool(request.params[1]);
        }

        UniValue option(UniValue::VOBJ);
        option.pushKV("name", "newAddressRecv");
        option.pushKV("value", smsgModule.options.fNewAddressRecv);
        if (fDescriptions) {
            option.pushKV("description", "Enable receiving messages for newly created addresses.");
        }
        options.push_back(option);

        option = UniValue(UniValue::VOBJ);
        option.pushKV("name", "newAddressAnon");
        option.pushKV("value", smsgModule.options.fNewAddressAnon);
        if (fDescriptions) {
            option.pushKV("description", "Enable receiving anonymous messages for newly created addresses.");
        }
        options.push_back(option);

        option = UniValue(UniValue::VOBJ);
        option.pushKV("name", "scanIncoming");
        option.pushKV("value", smsgModule.options.fScanIncoming);
        if (fDescriptions) {
            option.pushKV("description", "Scan incoming blocks for public keys, -smsgscanincoming must also be set");
        }
        options.push_back(option);

        result.pushKV("options", options);
        result.pushKV("result", "Success.");
    } else
    if (mode == "set") {
        if (request.params.size() < 3) {
            result.pushKV("result", "Too few parameters.");
            result.pushKV("expected", "set <optname> <value>");
            return result;
        }

        std::string optname = request.params[1].get_str();
        bool fValue = GetBool(request.params[2]);

        optname = ToLower(optname);
        if (optname == "newaddressrecv") {
            smsgModule.options.fNewAddressRecv = fValue;
            result.pushKV("set option", std::string("newAddressRecv = ") + (smsgModule.options.fNewAddressRecv ? "true" : "false"));
        } else
        if (optname == "newaddressanon") {
            smsgModule.options.fNewAddressAnon = fValue;
            result.pushKV("set option", std::string("newAddressAnon = ") + (smsgModule.options.fNewAddressAnon ? "true" : "false"));
        } else
        if (optname == "scanincoming") {
            smsgModule.options.fScanIncoming = fValue;
            result.pushKV("set option", std::string("scanIncoming = ") + (smsgModule.options.fScanIncoming ? "true" : "false"));
        } else {
            result.pushKV("result", "Option not found.");
            return result;
        }
    } else {
        result.pushKV("result", "Unknown Mode.");
        result.pushKV("expected", "smsgoptions [list|set <optname> <value>]");
    }

    return result;
},
    };
}

static RPCHelpMan smsglocalkeys()
{
    return RPCHelpMan{"smsglocalkeys",
                "\nList and manage keys messages can be received with.\n",
                {
                    {"mode", RPCArg::Type::STR, RPCArg::Default{"whitelist"}, "whitelist|all|wallet|recv +/- \"address\"|anon +/- \"address\""},
                    {"optype", RPCArg::Type::STR, RPCArg::Default{""}, "Add or remove +/-."},
                    {"address", RPCArg::Type::STR, RPCArg::Default{""}, "Address to affect."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    "\nList local keys.\n"
                    + HelpExampleCli("smsglocalkeys", "")
                    + HelpExampleRpc("smsglocalkeys", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    UniValue result(UniValue::VOBJ);

    std::string mode = "whitelist";
    if (request.params.size() > 0) {
        mode = request.params[0].get_str();
    }

    if (mode == "whitelist"
        || mode == "all") {
        LOCK(smsgModule.cs_smsg);
        uint32_t nKeys = 0;

        UniValue keys(UniValue::VARR);
#ifdef ENABLE_WALLET
        int all = mode == "all" ? 1 : 0;
        for (auto it = smsgModule.addresses.begin(); it != smsgModule.addresses.end(); ++it) {
            if (!all
                && !it->fReceiveEnabled) {
                continue;
            }

            CKeyID &keyID = it->address;
            std::string sPublicKey;
            CPubKey pubKey;

            if (0 == smsgModule.GetLocalKey(keyID, pubKey)) {
                sPublicKey = EncodeBase58(pubKey);
            }

            UniValue objM(UniValue::VOBJ);
            std::string sInfo, sLabel;
            PKHash pkh = PKHash(keyID);
            sLabel = smsgModule.LookupLabel(pkh);
            if (all) {
                sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on,  " : "off, ");
            }
            sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
            //result.pushKV("key", it->sAddress + " - " + sPublicKey + " " + sInfo + " - " + sLabel);
            objM.pushKV("address", EncodeDestination(PKHash(keyID)));
            objM.pushKV("public_key", sPublicKey);
            objM.pushKV("receive", (it->fReceiveEnabled ? "1" : "0"));
            objM.pushKV("anon", (it->fReceiveAnon ? "1" : "0"));
            objM.pushKV("label", sLabel);
            keys.push_back(objM);

            nKeys++;
        }
        result.pushKV("wallet_keys", keys);
#endif

        keys = UniValue(UniValue::VARR);
        for (auto &p : smsgModule.keyStore.mapKeys) {
            auto &key = p.second;
            UniValue objM(UniValue::VOBJ);
            CPubKey pk = key.key.GetPubKey();
            objM.pushKV("address", EncodeDestination(PKHash(p.first)));
            objM.pushKV("public_key", EncodeBase58(pk));
            objM.pushKV("receive", (key.nFlags & smsg::SMK_RECEIVE_ON ? "1" : "0"));
            objM.pushKV("anon", (key.nFlags & smsg::SMK_RECEIVE_ANON ? "1" : "0"));
            objM.pushKV("label", key.sLabel);
            keys.push_back(objM);

            nKeys++;
        }
        result.pushKV("smsg_keys", keys);

        result.pushKV("result", strprintf("%u", nKeys));
    } else
    if (mode == "recv") {
        if (request.params.size() < 3) {
            result.pushKV("result", "Too few parameters.");
            result.pushKV("expected", "recv <+/-> <address>");
            return result;
        }

        bool fValue = GetBool(request.params[1]);
        std::string addr = request.params[2].get_str();

        CKeyID keyID;
        CBitcoinAddress coinAddress(addr);
        if (!coinAddress.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address.");
        }
        if (!coinAddress.GetKeyID(keyID)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address.");
        }

        if (!smsgModule.SetWalletAddressOption(keyID, "receive", fValue)
            && !smsgModule.SetSmsgAddressOption(keyID, "receive", fValue)) {
            result.pushKV("result", "Address not found.");
            return result;
        }

        std::string sInfo;
        sInfo = std::string("Receive ") + (fValue ? "on" : "off");
        result.pushKV("result", "Success.");
        result.pushKV("key", coinAddress.ToString() + " " + sInfo);
        return result;
    } else
    if (mode == "anon") {
        if (request.params.size() < 3) {
            result.pushKV("result", "Too few parameters.");
            result.pushKV("expected", "anon <+/-> <address>");
            return result;
        }

        bool fValue = GetBool(request.params[1]);
        std::string addr = request.params[2].get_str();

        CKeyID keyID;
        CBitcoinAddress coinAddress(addr);
        if (!coinAddress.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address.");
        }
        if (!coinAddress.GetKeyID(keyID)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address.");
        }

        if (!smsgModule.SetWalletAddressOption(keyID, "anon", fValue)
            && !smsgModule.SetSmsgAddressOption(keyID, "anon", fValue)) {
            result.pushKV("result", "Address not found.");
            return result;
        }

        std::string sInfo;
        sInfo += std::string("Anon ") + (fValue ? "on" : "off");
        result.pushKV("result", "Success.");
        result.pushKV("key", coinAddress.ToString() + " " + sInfo);

        return result;
    } else
    if (mode == "wallet") {
#ifdef ENABLE_WALLET
        uint32_t nKeys = 0;
        UniValue keys(UniValue::VOBJ);
        for (const auto &pw : smsgModule.m_vpwallets) {
            LOCK(pw->cs_wallet);

            for (const auto &entry : pw->m_address_book) {
                if (!pw->IsMine(entry.first)) {
                    continue;
                }

                CBitcoinAddress coinAddress(entry.first);
                if (!coinAddress.IsValid()) {
                    continue;
                }

                std::string address = coinAddress.ToString();
                std::string sPublicKey;

                CKeyID keyID;
                if (!coinAddress.GetKeyID(keyID)) {
                    continue;
                }

                CPubKey pubKey;
                if (!pw->GetPubKey(keyID, pubKey)) {
                    continue;
                }
                if (!pubKey.IsValid()
                    || !pubKey.IsCompressed()) {
                    continue;
                }

                sPublicKey = EncodeBase58(pubKey);
                UniValue objM(UniValue::VOBJ);

                objM.pushKV("key", address);
                objM.pushKV("publickey", sPublicKey);
                objM.pushKV("label", entry.second.GetLabel());

                keys.push_back(objM);
                nKeys++;
            }
        }
        result.pushKV("keys", keys);
        result.pushKV("result", strprintf("%u", nKeys));
#else
        throw JSONRPCError(RPC_MISC_ERROR, "No wallet.");
#endif
    } else {
        result.pushKV("result", "Unknown Mode.");
        result.pushKV("expected", "smsglocalkeys [whitelist|all|wallet|recv <+/-> <address>|anon <+/-> <address>]");
    }

    return result;
},
    };
};

static RPCHelpMan smsgscanchain()
{
    return RPCHelpMan{"smsgscanchain",
                "\nLook for public keys in the block chain.\n",
                {},
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgscanchain", "")
                    + HelpExampleRpc("smsgscanchain", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    UniValue result(UniValue::VOBJ);
    if (!smsgModule.ScanBlockChain()) {
        result.pushKV("result", "Scan Chain Failed.");
    } else {
        result.pushKV("result", "Scan Chain Completed.");
    }

    return result;
},
    };
}

static RPCHelpMan smsgscanbuckets()
{
    return RPCHelpMan{"smsgscanbuckets",
                "\nForce rescan of all messages in the bucket store.\n"
                "Wallet must be unlocked if any receiving keys are stored in the wallet.\n",
                {
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"scanexpired", RPCArg::Type::BOOL, RPCArg::Default{false}, "Scan all messages."},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgscanbuckets", "")
                    + HelpExampleRpc("smsgscanbuckets", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    bool scan_all = false;
    if (request.params[0].isObject()) {
        UniValue options = request.params[0].get_obj();
        RPCTypeCheckObj(options,
        {
            {"scanexpired",          UniValueType(UniValue::VBOOL)},
        }, true, true);
        if (options["scanexpired"].isBool()) {
            scan_all = options["scanexpired"].get_bool();
        }
    }

    UniValue result(UniValue::VOBJ);
    if (!smsgModule.ScanBuckets(scan_all)) {
        result.pushKV("result", "Scan Buckets Failed.");
    } else {
        result.pushKV("result", "Scan Buckets Completed.");
    }

    return result;
},
    };
}

static RPCHelpMan smsgaddaddress()
{
    return RPCHelpMan{"smsgaddaddress",
                "\nAdd address and matching public key to database.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to add."},
                    {"pubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "Public key for \"address\"."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgaddaddress", "\"address\" \"public_key\"")
                    + HelpExampleRpc("smsgaddaddress", "\"address\", \"public_key\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string addr = request.params[0].get_str();
    std::string pubk = request.params[1].get_str();

    UniValue result(UniValue::VOBJ);
    int rv = smsgModule.AddAddress(addr, pubk);
    if (rv != 0) {
        result.pushKV("result", "Public key not added to db.");
        result.pushKV("reason", smsg::GetString(rv));
    } else {
        result.pushKV("result", "Public key added to db.");
    }

    return result;
},
    };
}

static RPCHelpMan smsgaddlocaladdress()
{
    return RPCHelpMan{"smsgaddlocaladdress",
                "\nEnable receiving messages on <address>.\n"
                "Key for \"address\" must exist in the wallet.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to add."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgaddlocaladdress", "\"address\"")
                    + HelpExampleRpc("smsgaddlocaladdress", "\"address\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string addr = request.params[0].get_str();

    UniValue result(UniValue::VOBJ);
    int rv = smsgModule.AddLocalAddress(addr);
    if (rv != 0) {
        result.pushKV("result", "Address not added.");
        result.pushKV("reason", smsg::GetString(rv));
    } else {
        result.pushKV("result", "Receiving messages enabled for address.");
    }

    return result;
},
    };
}

static RPCHelpMan smsgimportprivkey()
{
    return RPCHelpMan{"smsgimportprivkey",
                "\nAdds a private key (as returned by dumpprivkey) to the SMSG database.\n"
                "Keys imported into SMSG will be stored unencrypted and can receive messages even if the wallet is locked.\n",
                {
                    {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The private key to import (see dumpprivkey)."},
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "An optional label."},
                },
                RPCResult{
                    RPCResult::Type::NONE, "", "None"},
                RPCExamples{
            "\nDump a private key\n"
            + HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key\n"
            + HelpExampleCli("smsgimportprivkey", "\"mykey\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsgimportprivkey", "\"mykey\", \"testing\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    CBitcoinSecret vchSecret;
    if (!request.params[0].isStr()
        || !vchSecret.SetString(request.params[0].get_str())) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");
    }

    std::string strLabel;
    if (!request.params[1].isNull()) {
        strLabel = request.params[1].get_str();
    }

    int rv = smsgModule.ImportPrivkey(vchSecret, strLabel);
    if (0 != rv) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Import failed.");
    }

    return UniValue::VNULL;
},
    };
}

static RPCHelpMan smsgdumpprivkey()
{
    return RPCHelpMan{"smsgdumpprivkey",
        "\nReveals the private key corresponding to 'address'.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The particl address for the private key"},
        },
        RPCResult{
            RPCResult::Type::STR, "key", "The private key"
        },
        RPCExamples{
            HelpExampleCli("dumpprivkey", "\"myaddress\"")
    + HelpExampleCli("smsgimportprivkey", "\"mykey\"")
    + HelpExampleRpc("smsgdumpprivkey", "\"myaddress\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string strAddress = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Particl address");
    }

    if (dest.index() != DI::_PKHash) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address not a key id");
    }
    const CKeyID &idk = ToKeyID(std::get<PKHash>(dest));

    CKey key_out;
    int rv = smsgModule.DumpPrivkey(idk, key_out);
    if (0 != rv) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Private key for address " + strAddress + " is not known");
    }

    return EncodeSecret(key_out);
},
    };
}

static RPCHelpMan smsggetpubkey()
{
    return RPCHelpMan{"smsggetpubkey",
                "\nReturn the base58 encoded compressed public key for an address.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Return the pubkey matching \"address\"."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "address", "address of public key"},
                        {RPCResult::Type::STR_HEX, "publickey", "public key of address"},
                    },
                },
                RPCExamples{
            HelpExampleCli("smsggetpubkey", "\"myaddress\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsggetpubkey", "\"myaddress\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string address = request.params[0].get_str();
    std::string publicKey;

    UniValue result(UniValue::VOBJ);
    int rv = smsgModule.GetLocalPublicKey(address, publicKey);
    switch (rv) {
        case smsg::SMSG_NO_ERROR:
            result.pushKV("address", address);
            result.pushKV("publickey", publicKey);
            return result; // success, don't check db
        case smsg::SMSG_WALLET_NO_PUBKEY:
            break; // check db
        //case 1:
        default:
            throw JSONRPCError(RPC_INTERNAL_ERROR, smsg::GetString(rv));
    }

    CBitcoinAddress coinAddress(address);
    CKeyID keyID;
    if (!coinAddress.GetKeyID(keyID)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address.");
    }

    CPubKey cpkFromDB;
    rv = smsgModule.GetStoredKey(keyID, cpkFromDB);

    switch (rv) {
        case smsg::SMSG_NO_ERROR:
            if (!cpkFromDB.IsValid()
                || !cpkFromDB.IsCompressed()) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid public key.");
            } else {
                publicKey = EncodeBase58(cpkFromDB);

                result.pushKV("address", address);
                result.pushKV("publickey", publicKey);
            }
            break;
        case smsg::SMSG_PUBKEY_NOT_EXISTS:
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Address not found in wallet or db.");
        default:
            throw JSONRPCError(RPC_INTERNAL_ERROR, smsg::GetString(rv));
    }

    return result;
},
    };
}

static RPCHelpMan smsgsend()
{
    return RPCHelpMan{"smsgsend",
                "\nSend an encrypted message from \"address_from\" to \"address_to\".\n",
                {
                    {"address_from", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of the sender."},
                    {"address_to", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of the recipient."},
                    {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message to send."},
                    {"paid_msg", RPCArg::Type::BOOL, RPCArg::Default{false}, "Send as paid message."},
                    {"days_retention", RPCArg::Type::NUM, RPCArg::Default{1}, "No. of days for which the message will be retained by network."},
                    {"testfee", RPCArg::Type::BOOL, RPCArg::Default{false}, "Don't send the message, only estimate the fee."},
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"fromfile", RPCArg::Type::BOOL, RPCArg::Default{false}, "Send file as message, path specified in \"message\"."},
                            {"decodehex", RPCArg::Type::BOOL, RPCArg::Default{false}, "Decode \"message\" from hex before sending."},
                            {"submitmsg", RPCArg::Type::BOOL, RPCArg::Default{true}, "Submit smsg to network, if false POW is not set and hex encoded smsg returned."},
                            {"savemsg", RPCArg::Type::BOOL, RPCArg::Default{true}, "Save smsg to outbox."},
                            {"ttl_is_seconds", RPCArg::Type::BOOL, RPCArg::Default{false}, "If true days_retention parameter is interpreted as seconds to live."},
                            {"fund_from_rct", RPCArg::Type::BOOL, RPCArg::Default{false}, "Fund message from anon balance."},
                            {"rct_ring_size", RPCArg::Type::NUM, RPCArg::Default{(int)DEFAULT_RING_SIZE}, "Ring size to use with fund_from_rct."},
                            {"fundmsg", RPCArg::Type::BOOL, RPCArg::Default{true}, "Fund paid message, if false message will be stashed for later funding."},
                        },
                    },
                    {"coin_control", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"changeaddress", RPCArg::Type::STR, RPCArg::Default{""}, "The particl address to receive the change"},
                            {"inputs", RPCArg::Type::ARR, RPCArg::Default{UniValue::VARR}, "A json array of json objects",
                                {
                                    {"", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                                        {
                                            {"tx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "txn id"},
                                            {"n", RPCArg::Type::NUM, RPCArg::Optional::NO, "txn vout"},
                                        },
                                    },
                                },
                            },
                            {"replaceable", RPCArg::Type::BOOL, RPCArg::Default{""}, "Marks this transaction as BIP125 replaceable.\n"
                            "                              Allows this transaction to be replaced by a transaction with higher fees"},
                            {"conf_target", RPCArg::Type::NUM, RPCArg::Default{""}, "Confirmation target (in blocks)"},
                            {"estimate_mode", RPCArg::Type::STR, RPCArg::Default{"UNSET"}, "The fee estimate mode, must be one of:\n"
                            "         \"UNSET\"\n"
                            "         \"ECONOMICAL\"\n"
                            "         \"CONSERVATIVE\""},
                            {"avoid_reuse", RPCArg::Type::BOOL, RPCArg::Default{true}, "(only available if avoid_reuse wallet flag is set) Avoid spending from dirty addresses; addresses are considered\n"
                            "                             dirty if they have previously been used in a transaction."},
                            {"feeRate", RPCArg::Type::AMOUNT, RPCArg::Default{"not set: makes wallet determine the fee"}, "Set a specific fee rate in " + CURRENCY_UNIT + "/kB"},
                            {"allow_other_inputs", RPCArg::Type::BOOL, RPCArg::Default{true}, "Allow inputs to be added if any inputs already exist."},
                            {"allow_change_output", RPCArg::Type::BOOL, RPCArg::Default{true}, "Allow change output to be added if needed (only for 'blind' input_type)."},
                            {"minimumAmount", RPCArg::Type::AMOUNT, RPCArg::Default{FormatMoney(0)}, "Minimum value of each UTXO to select in " + CURRENCY_UNIT + ""},
                            {"maximumAmount", RPCArg::Type::AMOUNT, RPCArg::DefaultHint{"unlimited"}, "Maximum value of each UTXO to select in " + CURRENCY_UNIT + ""},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "result", "\"Sent\"/\"Not Sent\""},
                        {RPCResult::Type::STR_HEX, "msgid", /*optional=*/true, "Message id, if sent"},
                        {RPCResult::Type::STR_HEX, "msg", /*optional=*/true, "Hex encoded message"},
                        {RPCResult::Type::STR_HEX, "txid", /*optional=*/true, "txnid of the funding transaction, if paid msg"},
                        {RPCResult::Type::NUM, "tx_vsize", /*optional=*/true, "Virtual size of funding transaction, if paid msg"},
                        {RPCResult::Type::STR_AMOUNT, "fee", /*optional=*/true, "fee paid, if paid msg"},
                        {RPCResult::Type::STR, "error", /*optional=*/true, "Error if failed to send"},
                }},
                RPCExamples{
             HelpExampleCli("smsgsend", "\"myaddress\" \"toaddress\" \"message\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsgsend", "\"myaddress\", \"toaddress\", \"message\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string addrFrom  = request.params[0].get_str();
    std::string addrTo    = request.params[1].get_str();
    std::string msg       = request.params[2].get_str();

    bool fPaid = request.params[3].isNull() ? false : request.params[3].get_bool();
    int nRetention = request.params[4].isNull() ? 1 : request.params[4].getInt<int>();
    bool fTestFee = request.params[5].isNull() ? false : request.params[5].get_bool();

    bool fFromFile = false;
    bool fDecodeHex = false;
    bool submit_msg = true;
    bool save_msg = true;
    bool ttl_in_seconds = false;
    bool fund_from_rct = false;
    bool fund_paid_msg = true;
    size_t rct_ring_size = DEFAULT_RING_SIZE;

    UniValue options = request.params[6];
    if (options.isObject()) {
        RPCTypeCheckObj(options,
        {
            {"fromfile",          UniValueType(UniValue::VBOOL)},
            {"decodehex",         UniValueType(UniValue::VBOOL)},
            {"submitmsg",         UniValueType(UniValue::VBOOL)},
            {"savemsg",           UniValueType(UniValue::VBOOL)},
            {"ttl_is_seconds",    UniValueType(UniValue::VBOOL)},
            {"fund_from_rct",     UniValueType(UniValue::VBOOL)},
            {"rct_ring_size",     UniValueType(UniValue::VNUM)},
            {"fundmsg",           UniValueType(UniValue::VBOOL)},
        }, true, false);
        if (!options["fromfile"].isNull()) {
            fFromFile = options["fromfile"].get_bool();
        }
        if (!options["decodehex"].isNull()) {
            fDecodeHex = options["decodehex"].get_bool();
        }
        if (!options["submitmsg"].isNull()) {
            submit_msg = options["submitmsg"].get_bool();
        }
        if (!options["savemsg"].isNull()) {
            save_msg = options["savemsg"].get_bool();
        }
        if (!options["ttl_is_seconds"].isNull()) {
            ttl_in_seconds = options["ttl_is_seconds"].get_bool();
        }
        if (!options["fund_from_rct"].isNull()) {
            fund_from_rct = options["fund_from_rct"].get_bool();
        }
        if (!options["rct_ring_size"].isNull()) {
            rct_ring_size = options["rct_ring_size"].getInt<int>();
        }
        if (!options["fundmsg"].isNull()) {
            fund_paid_msg = options["fundmsg"].get_bool();
        }
    }

    if (fFromFile && fDecodeHex) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't use decodehex with fromfile.");
    }

    if (fDecodeHex) {
        if (!IsHex(msg)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Expect hex encoded message with decodehex.");
        }
        std::vector<uint8_t> vData = ParseHex(msg);
        msg = std::string(vData.begin(), vData.end());
    }

    CAmount nFee = 0;
    size_t nTxBytes = 0;

    if (fPaid && Params().GetConsensus().nPaidSmsgTime > GetTime()) {
        throw std::runtime_error("Paid SMSG not yet active on mainnet.");
    }

    CKeyID kiFrom, kiTo;
    CBitcoinAddress coinAddress(addrFrom);
    if (!coinAddress.IsValid() || !coinAddress.GetKeyID(kiFrom)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid from address.");
    }
    coinAddress.SetString(addrTo);
    if (!coinAddress.IsValid() || !coinAddress.GetKeyID(kiTo)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid to address.");
    }

    if (!ttl_in_seconds) {
        nRetention *= smsg::SMSG_SECONDS_IN_DAY;
    }

    UniValue result(UniValue::VOBJ);
    std::string sError;
    smsg::SecureMessage smsgOut;

#ifdef ENABLE_WALLET
    CCoinControl cctl;
    if (fPaid) {
        if (!smsgModule.pactive_wallet) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Active wallet must be set to send a paid smsg.");
        }
        CHDWallet *const pw = GetParticlWallet(smsgModule.pactive_wallet.get());
        if (!fTestFee) {
            EnsureWalletIsUnlocked(pw);
        }
        UniValue uv_cctl = request.params[7];
        if (uv_cctl.isObject()) {
            ParseCoinControlOptions(uv_cctl, pw, cctl);
        }
    }
    if (smsgModule.Send(kiFrom, kiTo, msg, smsgOut, sError, fPaid, nRetention, fTestFee, &nFee, &nTxBytes,
                        fFromFile, submit_msg, save_msg, fund_from_rct, rct_ring_size, &cctl, fund_paid_msg) != 0) {
#else
    if (smsgModule.Send(kiFrom, kiTo, msg, smsgOut, sError, fPaid, nRetention, fTestFee, &nFee, &nTxBytes,
                        fFromFile, submit_msg, save_msg) != 0) {
#endif
        result.pushKV("result", "Send failed.");
        result.pushKV("error", sError);
    } else {
        result.pushKV("result", (!submit_msg || fTestFee || !fund_paid_msg) ? "Not Sent." : "Sent.");

        if (!fTestFee) {
            result.pushKV("msgid", HexStr(smsgModule.GetMsgID(smsgOut)));
        }

        if (!submit_msg) {
            unsigned char header_buffer[smsg::SMSG_HDR_LEN];
            smsgOut.WriteHeader(header_buffer);
            result.pushKV("msg", HexStr(Span<const unsigned char>(header_buffer, smsg::SMSG_HDR_LEN)) +
                                 HexStr(Span<const unsigned char>(smsgOut.pPayload, smsgOut.nPayload)));
        }

        if (fPaid) {
            if (!fTestFee && fund_paid_msg) {
                uint256 txid;
                smsgOut.GetFundingTxid(txid);
                result.pushKV("txid", txid.ToString());
            }
            result.pushKV("fee", ValueFromAmount(nFee));
            result.pushKV("tx_vsize", (int)nTxBytes);
        }
    }

    return result;
},
    };
}

static OutputTypes WordToType(std::string &s)
{
    if (s == "part" || s == "standard") {
        return OUTPUT_STANDARD;
    }
    if (s == "blind") {
        //return OUTPUT_CT;
    }
    if (s == "anon") {
        return OUTPUT_RINGCT;
    }
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid fund from type.");
};

static RPCHelpMan smsgfund()
{
    return RPCHelpMan{"smsgfund",
        "\nFund and send stashed messages.\n",
        {
            {"msgids", RPCArg::Type::ARR, RPCArg::Optional::NO, "A json array of smsg ids to fund",
                {
                    {"msgid", RPCArg::Type::STR, RPCArg::Default{""}, "smsg id"},
                },
            },
            {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                {
                    {"fundtype", RPCArg::Type::STR, RPCArg::Default{"plain"}, "Fund from \"plain\" or \"anon\" balance."},
                    {"testfee", RPCArg::Type::BOOL, RPCArg::Default{false}, "Test fee only."},
                    {"rct_ring_size", RPCArg::Type::NUM, RPCArg::Default{(int)DEFAULT_RING_SIZE}, "Ring size to use with fund_from_rct."},
                },
            },
            {"coin_control", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                {
                    {"changeaddress", RPCArg::Type::STR, RPCArg::Default{""}, "The particl address to receive the change"},
                    {"inputs", RPCArg::Type::ARR, RPCArg::Default{UniValue::VARR}, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                                {
                                    {"tx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "txn id"},
                                    {"n", RPCArg::Type::NUM, RPCArg::Optional::NO, "txn vout"},
                                },
                            },
                        },
                    },
                    {"replaceable", RPCArg::Type::BOOL, RPCArg::Default{""}, "Marks this transaction as BIP125 replaceable.\n"
                    "                              Allows this transaction to be replaced by a transaction with higher fees"},
                    {"conf_target", RPCArg::Type::NUM, RPCArg::Default{""}, "Confirmation target (in blocks)"},
                    {"estimate_mode", RPCArg::Type::STR, RPCArg::Default{"UNSET"}, "The fee estimate mode, must be one of:\n"
                    "         \"UNSET\"\n"
                    "         \"ECONOMICAL\"\n"
                    "         \"CONSERVATIVE\""},
                    {"avoid_reuse", RPCArg::Type::BOOL, RPCArg::Default{true}, "(only available if avoid_reuse wallet flag is set) Avoid spending from dirty addresses; addresses are considered\n"
                    "                             dirty if they have previously been used in a transaction."},
                    {"feeRate", RPCArg::Type::AMOUNT, RPCArg::Default{"not set: makes wallet determine the fee"}, "Set a specific fee rate in " + CURRENCY_UNIT + "/kB"},
                    {"allow_other_inputs", RPCArg::Type::BOOL, RPCArg::Default{true}, "Allow inputs to be added if any inputs already exist."},
                    {"allow_change_output", RPCArg::Type::BOOL, RPCArg::Default{true}, "Allow change output to be added if needed (only for 'blind' input_type)."},
                    {"minimumAmount", RPCArg::Type::AMOUNT, RPCArg::Default{FormatMoney(0)}, "Minimum value of each UTXO to select in " + CURRENCY_UNIT + ""},
                    {"maximumAmount", RPCArg::Type::AMOUNT, RPCArg::DefaultHint{"unlimited"}, "Maximum value of each UTXO to select in " + CURRENCY_UNIT + ""},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "", {
                {RPCResult::Type::STR_HEX, "txid", /*optional=*/true, "funding txid"},
                {RPCResult::Type::NUM, "tx_vsize", "Virtual size of funding transaction"},
                {RPCResult::Type::STR_AMOUNT, "fee", "tx fee paid"},
        }},
        RPCExamples{
    HelpExampleCli("smsgfund", "[\"msgid\"]") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("smsgfund", "[\"msgid\"]")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    UniValue uv_msgids = request.params[0].get_array();
    std::vector<smsg::SecureMessage> v_smsgs(uv_msgids.size());
    std::vector<smsg::SecureMessage*> v_psmsgs(uv_msgids.size());
    std::vector<CKeyID> v_msg_addr_to(uv_msgids.size()); // Not required
    {
        LOCK(smsg::cs_smsgDB);
        smsg::SecMsgDB dbMsg;
        if (!dbMsg.Open("cr+")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        for (unsigned int idx = 0; idx < uv_msgids.size(); idx++) {
            const UniValue &uv_msgid = uv_msgids[idx];

            const std::string &sMsgId = uv_msgid.get_str();
            if (!IsHex(sMsgId) || sMsgId.size() != 56) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "msgid must be 28 bytes in hex string.");
            }
            std::vector<uint8_t> vMsgId = ParseHex(sMsgId.c_str());

            uint8_t chKey[30];
            chKey[0] = 'T'; // stashed
            chKey[1] = 'M';
            memcpy(chKey + 2, vMsgId.data(), 28);
            smsg::SecMsgStored smsgStored;
            if (!dbMsg.ReadSmesg(chKey, smsgStored)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown message id '%s'", sMsgId));
            }
            v_smsgs[idx] = smsg::SecureMessage(smsgStored.vchMessage.data());
            auto &smsg = v_smsgs[idx];
            if (!smsg.IsPaidVersion()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Non-paid message id '%s'", sMsgId));
            }
            try { smsg.pPayload = new uint8_t[smsg.nPayload]; } catch (std::exception &e) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Could not allocate payload %s", e.what()));
            }
            memcpy(smsg.pPayload, &smsgStored.vchMessage[smsg::SMSG_HDR_LEN], smsg.nPayload);
            v_msg_addr_to[idx] = smsgStored.addrTo;
        }
    }
    for (size_t k = 0; k < v_smsgs.size(); ++k) {
        v_psmsgs[k] = &v_smsgs[k];
    }

    if (v_psmsgs.size() < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Must specify one or more message ids");
    }
    size_t max_messages = (MAX_DATA_OUTPUT_SIZE - 1) / 24;
    if (v_psmsgs.size() > max_messages) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Too many messages, max %d", max_messages));
    }

    OutputTypes fund_from = OUTPUT_STANDARD;
    bool test_fee = false;
    size_t rct_ring_size = DEFAULT_RING_SIZE;

    UniValue options = request.params[1];
    if (options.isObject()) {
        RPCTypeCheckObj(options,
        {
            {"fundtype",          UniValueType(UniValue::VSTR)},
            {"testfee",           UniValueType(UniValue::VBOOL)},
            {"rct_ring_size",     UniValueType(UniValue::VNUM)},
        }, true, false);
        if (!options["fundtype"].isNull()) {
            std::string str_fund_from = options["fundtype"].get_str();
            fund_from = WordToType(str_fund_from);
        }
        if (!options["testfee"].isNull()) {
            test_fee = options["testfee"].get_bool();
        }
        if (!options["rct_ring_size"].isNull()) {
            rct_ring_size = options["rct_ring_size"].getInt<int>();
        }
    }

    std::string sError;
    UniValue result(UniValue::VOBJ);

    CAmount nTxFee = 0;
    size_t nTxBytes = 0;

#ifdef ENABLE_WALLET
    CCoinControl cctl;
    if (!smsgModule.pactive_wallet) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Active wallet must be set.");
    }
    CHDWallet *const pw = GetParticlWallet(smsgModule.pactive_wallet.get());
    if (!test_fee) {
        EnsureWalletIsUnlocked(pw);
    }
    UniValue uv_cctl = request.params[2];
    if (uv_cctl.isObject()) {
        ParseCoinControlOptions(uv_cctl, pw, cctl);
    }

    bool fund_from_rct = (fund_from == OUTPUT_RINGCT) ? true : false;
    if (0 != smsgModule.FundMsgs(v_psmsgs, sError, test_fee, &nTxFee, &nTxBytes, fund_from_rct, rct_ring_size, &cctl)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("SecureMsgFund failed %s", sError));
    }

#else
    throw JSONRPCError(RPC_MISC_ERROR, "Wallet is disabled.");
#endif

    if (!test_fee) {
        for (size_t k = 0; k < v_psmsgs.size(); ++k) {
            const smsg::SecureMessage &smsg = *v_psmsgs[k];
            if (0 != smsgModule.SubmitMsg(smsg, v_msg_addr_to[k], false, sError)) {
                LogPrintf("SubmitMsg failed: %s.\n", HexStr(smsgModule.GetMsgID(smsg)));
                // throw
            } else {
                 // Erase from stash
                LOCK(smsg::cs_smsgDB);
                smsg::SecMsgDB dbMsg;
                if (!dbMsg.Open("cr+")) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
                }
                auto vMsgId = smsgModule.GetMsgID(smsg);

                uint8_t chKey[30];
                chKey[0] = 'T'; // stashed
                chKey[1] = 'M';
                memcpy(chKey + 2, vMsgId.data(), 28);
                dbMsg.EraseSmesg(chKey);
            }
        }
    }

    if (!test_fee) {
        uint256 txid;
        const smsg::SecureMessage &smsg = *v_psmsgs[0];
        smsg.GetFundingTxid(txid);
        result.pushKV("txid", txid.ToString());
    }

    result.pushKV("fee", ValueFromAmount(nTxFee));
    result.pushKV("tx_vsize", (int)nTxBytes);

    return result;
},
    };
}

static RPCHelpMan smsgsendanon()
{
    return RPCHelpMan{"smsgsendanon",
                "\nDEPRECATED. Send an anonymous encrypted message to addrTo.\n",
                {
                    {"address_to", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to send to."},
                    {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "Message to send."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string addrTo    = request.params[0].get_str();
    std::string msg       = request.params[1].get_str();

    CKeyID kiFrom, kiTo;
    CBitcoinAddress coinAddress(addrTo);
    if (!coinAddress.IsValid() || !coinAddress.GetKeyID(kiTo)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to address.");
    }

    uint32_t ttl = smsg::SMSG_FREE_MSG_DAYS * smsg::SMSG_SECONDS_IN_DAY;
    UniValue result(UniValue::VOBJ);
    std::string sError;
    smsg::SecureMessage smsgOut;
    if (smsgModule.Send(kiFrom, kiTo, msg, smsgOut, sError, false, ttl) != 0) {
        result.pushKV("result", "Send failed.");
        result.pushKV("error", sError);
    } else {
        result.pushKV("msgid", HexStr(smsgModule.GetMsgID(smsgOut)));
        result.pushKV("result", "Sent.");
    }

    return result;
},
    };
}

static RPCHelpMan smsginbox()
{
    return RPCHelpMan{"smsginbox",
                "\nDecrypt and display received messages.\n"
                "Warning: clear will delete all messages.\n",
                {
                    {"mode", RPCArg::Type::STR, RPCArg::Default{"unread"}, "\"all|unread|count|clear\" List all messages, unread messages, count or delete all messages."},
                    {"filter", RPCArg::Type::STR, RPCArg::Default{""}, "Filter messages when in list mode. Applied to from, to and text fields."},
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"updatestatus", RPCArg::Type::BOOL, RPCArg::Default{true}, "Update read status if true."},
                            {"encoding", RPCArg::Type::STR, RPCArg::Default{"text"}, "Display message data in encoding, values: \"text\", \"hex\", \"none\"."},
                            {"offset", RPCArg::Type::NUM, RPCArg::Default{""}, "Skip the first \"offset\" messages"},
                            {"max_results", RPCArg::Type::NUM, RPCArg::Default{""}, "Return only \"max_results\" messages"},
                            {"unread_only", RPCArg::Type::BOOL, RPCArg::Default{false}, "Count only unread messages"},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "result", "No of messages or error"},
                        {RPCResult::Type::ARR, "messages", /*optional=*/true, "", {
                            {RPCResult::Type::OBJ, "", "", {
                                {RPCResult::Type::STR_HEX, "msgid", "Message id"},
                                {RPCResult::Type::STR, "version", "The message version"},
                                {RPCResult::Type::NUM_TIME, "received", /*optional=*/true, "Time the message was received"},
                                {RPCResult::Type::STR, "received_local", /*optional=*/true, "Time the message was received"},
                                {RPCResult::Type::STR, "received_utc", /*optional=*/true, "Time the message was received"},
                                {RPCResult::Type::NUM_TIME, "sent", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::STR, "sent_local", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::STR, "sent_utc", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::NUM, "daysretention", /*optional=*/true, "DEPRECATED Number of days message will stay in the network for"},
                                {RPCResult::Type::NUM, "ttl", /*optional=*/true, "Seconds message will stay in the network for"},
                                {RPCResult::Type::NUM_TIME, "expiration", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::STR, "expiration_local", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::STR, "expiration_utc", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::NUM, "payloadsize", /*optional=*/true, "Size in bytes of payload"},
                                {RPCResult::Type::BOOL, "paid", /*optional=*/true, "True if paid message"},
                                {RPCResult::Type::STR, "from", /*optional=*/true, "Address the message was sent from"},
                                {RPCResult::Type::STR, "to", /*optional=*/true, "Address the message was sent to"},
                                {RPCResult::Type::STR, "text", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "hex", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "unknown_encoding", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "status", /*optional=*/true, "Message status"},
                                {RPCResult::Type::STR, "error", /*optional=*/true, "Message error"},
                            }},
                        }},
                        {RPCResult::Type::STR, "expected", /*optional=*/true, "values understood"},
                        {RPCResult::Type::NUM, "num_messages", /*optional=*/true, "Number of messages counted"},
                }},
                RPCExamples{
                    "Display unread received messages:"
                    + HelpExampleCli("smsginbox", "") +
                    "Display all received messages that match \"address\":"
                    + HelpExampleCli("smsginbox", "\"all\" \"address\"")
                    + HelpExampleRpc("smsginbox", "\"all\", \"address\"") +
                    "Count unread messages:"
                    + HelpExampleCli("smsginbox", "\"count\" \"\" \"{\\\"unread_only\\\":true}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string mode = request.params[0].isStr() ? request.params[0].get_str() : "unread";
    std::string filter = request.params[1].isStr() ? request.params[1].get_str() : "";

    std::string sEnc = "text";
    bool update_status = true;
    bool unread_only = false;
    int offset = 0, max_results = -1;
    if (request.params[2].isObject()) {
        UniValue options = request.params[2].get_obj();
        if (options["updatestatus"].isBool()) {
            update_status = options["updatestatus"].get_bool();
        }
        if (options["encoding"].isStr()) {
            sEnc = options["encoding"].get_str();
        }
        if (options["unread_only"].isBool()) {
            unread_only = options["unread_only"].get_bool();
        }
        if (options["offset"].isNum()) {
            offset = options["offset"].getInt<int>();
        }
        if (options["max_results"].isNum()) {
            max_results = options["max_results"].getInt<int>();
        }
    }

    UniValue result(UniValue::VOBJ);

    {
        smsg::SecMsgDB dbInbox;
        if (!dbInbox.Open("cr+")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        uint32_t nMessages = 0;
        uint8_t chKey[30];

        if (mode == "count") {
            LOCK(smsg::cs_smsgDB);

            smsg::SecMsgStored smsgStored;
            leveldb::Iterator *it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
            while (dbInbox.NextSmesg(it, smsg::DBK_INBOX, chKey, smsgStored)) {
                if (unread_only &&
                    !(smsgStored.status & SMSG_MASK_UNREAD)) {
                    continue;
                }
                nMessages++;
            }
            delete it;

            result.pushKV("result", strprintf("Counted %s messages", unread_only ? "unread" : "all"));
            result.pushKV("num_messages", (int)nMessages);
        } else
        if (mode == "clear") {
            LOCK(smsg::cs_smsgDB);
            dbInbox.TxnBegin();

            leveldb::Iterator *it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
            while (dbInbox.NextSmesgKey(it, smsg::DBK_INBOX, chKey)) {
                dbInbox.EraseSmesg(chKey);
                nMessages++;
            }
            delete it;
            dbInbox.TxnCommit();

            result.pushKV("result", strprintf("Deleted %u messages.", nMessages));
        } else
        if (mode == "all" ||
            mode == "unread") {
            int fCheckReadStatus = mode == "unread" ? 1 : 0;

            smsg::SecMsgStored smsgStored;
            smsg::MessageData msg;

            dbInbox.TxnBegin();

            leveldb::Iterator *it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
            UniValue messageList(UniValue::VARR);

            while (dbInbox.NextSmesg(it, smsg::DBK_INBOX, chKey, smsgStored)) {
                if (fCheckReadStatus &&
                    !(smsgStored.status & SMSG_MASK_UNREAD)) {
                    continue;
                }
                if (offset > 0) {
                    offset--;
                    continue;
                }
                if (max_results >= 0 && (int)nMessages >= max_results) {
                    break;
                }
                const unsigned char *pHeader = smsgStored.vchMessage.data();
                smsg::SecureMessage smsg(pHeader);
                const smsg::SecureMessage *psmsg = &smsg;

                UniValue objM(UniValue::VOBJ);
                objM.pushKV("msgid", HexStr(Span<const unsigned char>(&chKey[2], 28))); // timestamp+hash
                objM.pushKV("version", strprintf("%02x%02x", psmsg->version[0], psmsg->version[1]));

                uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
                int rv = smsgModule.Decrypt(false, smsgStored.addrTo, pHeader, pHeader + smsg::SMSG_HDR_LEN, nPayload, msg);
                if (rv == 0) {
                    std::string sAddrTo = EncodeDestination(PKHash(smsgStored.addrTo));
                    std::string sText = std::string((char*)msg.vchMessage.data());
                    if (filter.size() > 0 &&
                        !(part::stringsMatchI(msg.sFromAddress, filter, 3) ||
                          part::stringsMatchI(sAddrTo, filter, 3) ||
                          part::stringsMatchI(sText, filter, 3))) {
                        continue;
                    }

                    PushTime(objM, "received", smsgStored.timeReceived);
                    PushTime(objM, "sent", msg.timestamp);
                    objM.pushKV("paid", UniValue(psmsg->IsPaidVersion()));

                    int64_t ttl = psmsg->m_ttl;
                    objM.pushKV("ttl", ttl);
                    int nDaysRetention = ttl / smsg::SMSG_SECONDS_IN_DAY;
                    objM.pushKV("daysretention", nDaysRetention);
                    PushTime(objM, "expiration", psmsg->timestamp + ttl);

                    uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
                    objM.pushKV("payloadsize", (int)nPayload);

                    objM.pushKV("from", msg.sFromAddress);
                    objM.pushKV("to", sAddrTo);
                    if (sEnc == "none") {
                    } else
                    if (sEnc == "text") {
                        objM.pushKV("text", sText);
                    } else
                    if (sEnc == "hex") {
                        objM.pushKV("hex", HexStr(msg.vchMessage));
                    } else {
                        objM.pushKV("unknown_encoding", sEnc);
                    }
                } else {
                    if (filter.size() > 0) {
                        continue;
                    }

                    objM.pushKV("status", "Decrypt failed");
                    objM.pushKV("error", smsg::GetString(rv));
                }

                messageList.push_back(objM);

                // Only set 'read' status if the message decrypted successfully and update_status is set
                if (fCheckReadStatus && rv == 0 && update_status) {
                    smsgStored.status &= ~SMSG_MASK_UNREAD;
                    dbInbox.WriteSmesg(chKey, smsgStored);
                }
                nMessages++;
            }
            delete it;
            dbInbox.TxnCommit();

            result.pushKV("messages", messageList);
            result.pushKV("result", strprintf("%u", nMessages));
        } else {
            result.pushKV("result", "Unknown Mode.");
            result.pushKV("expected", "all|unread|clear.");
        }
    } // cs_smsgDB

    return result;
},
    };
};

static RPCHelpMan smsgoutbox()
{
    return RPCHelpMan{"smsgoutbox",
                "\nDecrypt and display all sent messages.\n"
                "Warning: \"mode\"=\"clear\" will delete all sent messages.\n",
                {
                    {"mode", RPCArg::Type::STR, RPCArg::Default{"all"}, "\"all|count|clear\" List, count or clear messages."},
                    {"filter", RPCArg::Type::STR, RPCArg::Default{""}, "Filter messages when in list mode. Applied to from, to and text fields."},
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"encoding", RPCArg::Type::STR, RPCArg::Default{"text"}, "Display message data in encoding, values: \"text\", \"hex\", \"none\"."},
                            {"sending", RPCArg::Type::BOOL, RPCArg::Default{false}, "Display messages in sending queue."},
                            {"stashed", RPCArg::Type::BOOL, RPCArg::Default{false}, "Display stashed messages."},
                            {"offset", RPCArg::Type::NUM, RPCArg::Default{""}, "Skip the first \"offset\" messages"},
                            {"max_results", RPCArg::Type::NUM, RPCArg::Default{""}, "Return only \"max_results\" messages"},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "result", "No of messages or error"},
                        {RPCResult::Type::ARR, "messages", /*optional=*/true, "", {
                            {RPCResult::Type::OBJ, "", "", {
                                {RPCResult::Type::STR_HEX, "msgid", "Message id"},
                                {RPCResult::Type::STR, "version", "The message version"},
                                {RPCResult::Type::NUM_TIME, "sent", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::STR, "sent_local", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::STR, "sent_utc", /*optional=*/true, "Time the message was sent"},
                                {RPCResult::Type::NUM, "daysretention", /*optional=*/true, "DEPRECATED Number of days message will stay in the network for"},
                                {RPCResult::Type::NUM, "ttl", /*optional=*/true, "Seconds message will stay in the network for"},
                                {RPCResult::Type::NUM_TIME, "expiration", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::STR, "expiration_local", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::STR, "expiration_utc", /*optional=*/true, "Time Expired"},
                                {RPCResult::Type::NUM, "payloadsize", /*optional=*/true, "Size in bytes of payload"},
                                {RPCResult::Type::BOOL, "paid", /*optional=*/true, "True if paid message"},
                                {RPCResult::Type::STR, "from", /*optional=*/true, "Address the message was sent from"},
                                {RPCResult::Type::STR, "to", /*optional=*/true, "Address the message was sent to"},
                                {RPCResult::Type::STR, "text", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "hex", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "unknown_encoding", /*optional=*/true, "Message text"},
                                {RPCResult::Type::STR, "status", /*optional=*/true, "Message status"},
                                {RPCResult::Type::STR, "error", /*optional=*/true, "Message error"},
                            }},
                        }},
                        {RPCResult::Type::STR, "expected", /*optional=*/true, "values understood"},
                        {RPCResult::Type::NUM, "num_messages", /*optional=*/true, "Number of messages counted"},
                }},
                RPCExamples{
                    HelpExampleCli("smsgoutbox", "")
                    + HelpExampleRpc("smsgoutbox", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string mode = request.params[0].isStr() ? request.params[0].get_str() : "all";
    std::string filter = request.params[1].isStr() ? request.params[1].get_str() : "";

    bool show_sending = false;
    bool show_stashed = false;
    std::string sEnc = "text";
    int offset = 0, max_results = -1;
    if (request.params[2].isObject()) {
        UniValue options = request.params[2].get_obj();
        if (options["encoding"].isStr()) {
            sEnc = options["encoding"].get_str();
        }
        if (options["sending"].isBool()) {
            show_sending = options["sending"].get_bool();
        }
        if (options["stashed"].isBool()) {
            show_stashed = options["stashed"].get_bool();
        }
        if (options["offset"].isNum()) {
            offset = options["offset"].getInt<int>();
        }
        if (options["max_results"].isNum()) {
            max_results = options["max_results"].getInt<int>();
        }
    }

    if (show_sending && show_stashed) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Select sending or stashed.");
    }

    UniValue result(UniValue::VOBJ);

    uint8_t chKey[30];
    memset(&chKey[0], 0, sizeof(chKey));

    {
        LOCK(smsg::cs_smsgDB);

        smsg::SecMsgDB dbOutbox;
        if (!dbOutbox.Open("cr+")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        uint32_t nMessages = 0;

        std::string db_prefix = show_sending ? smsg::DBK_QUEUED : show_stashed ? smsg::DBK_STASHED : smsg::DBK_OUTBOX;
        if (mode == "count") {
            leveldb::Iterator *it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());
            while (dbOutbox.NextSmesgKey(it, db_prefix, chKey)) {
                nMessages++;
            }
            delete it;

            result.pushKV("result", "Counted sent messages");
            result.pushKV("num_messages", (int)nMessages);
        } else
        if (mode == "clear") {
            dbOutbox.TxnBegin();

            leveldb::Iterator *it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());
            while (dbOutbox.NextSmesgKey(it, db_prefix, chKey)) {
                dbOutbox.EraseSmesg(chKey);
                nMessages++;
            }
            delete it;
            dbOutbox.TxnCommit();

            result.pushKV("result", strprintf("Deleted %u messages.", nMessages));
        } else
        if (mode == "all") {
            smsg::SecMsgStored smsgStored;
            smsg::MessageData msg;
            leveldb::Iterator *it = dbOutbox.pdb->NewIterator(leveldb::ReadOptions());

            UniValue messageList(UniValue::VARR);

            while (dbOutbox.NextSmesg(it, db_prefix, chKey, smsgStored)) {
                if (offset > 0) {
                    offset--;
                    continue;
                }
                if (max_results >= 0 && (int)nMessages >= max_results) {
                    break;
                }
                const unsigned char *pHeader = smsgStored.vchMessage.data();
                smsg::SecureMessage smsg(pHeader);
                const smsg::SecureMessage *psmsg = &smsg;

                UniValue objM(UniValue::VOBJ);
                objM.pushKV("msgid", HexStr(Span<const unsigned char>(&chKey[2], 28))); // timestamp+hash
                objM.pushKV("version", strprintf("%02x%02x", psmsg->version[0], psmsg->version[1]));

                uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
                int rv = smsgModule.Decrypt(false, smsgStored.addrOutbox, pHeader, pHeader + smsg::SMSG_HDR_LEN, nPayload, msg);
                if (rv == 0) {
                    std::string sAddrTo = EncodeDestination(PKHash(smsgStored.addrTo));
                    std::string sText = std::string((char*)msg.vchMessage.data());
                    if (filter.size() > 0 &&
                        !(part::stringsMatchI(msg.sFromAddress, filter, 3) ||
                          part::stringsMatchI(sAddrTo, filter, 3) ||
                          part::stringsMatchI(sText, filter, 3))) {
                        continue;
                    }

                    PushTime(objM, "sent", msg.timestamp);
                    objM.pushKV("paid", UniValue(psmsg->IsPaidVersion()));

                    int64_t ttl = psmsg->m_ttl;
                    objM.pushKV("ttl", ttl);
                    int nDaysRetention = ttl / smsg::SMSG_SECONDS_IN_DAY;
                    objM.pushKV("daysretention", nDaysRetention);
                    PushTime(objM, "expiration", psmsg->timestamp + ttl);

                    uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
                    objM.pushKV("payloadsize", (int)nPayload);

                    objM.pushKV("from", msg.sFromAddress);
                    objM.pushKV("to", sAddrTo);
                    if (sEnc == "none") {
                    } else
                    if (sEnc == "text") {
                        objM.pushKV("text", sText);
                    } else
                    if (sEnc == "hex") {
                        objM.pushKV("hex", HexStr(msg.vchMessage));
                    } else {
                        objM.pushKV("unknown_encoding", sEnc);
                    }
                } else {
                    if (filter.size() > 0) {
                        continue;
                    }

                    objM.pushKV("status", "Decrypt failed");
                    objM.pushKV("error", smsg::GetString(rv));
                }
                messageList.push_back(objM);
                nMessages++;
            }
            delete it;

            result.pushKV("messages" ,messageList);
            result.pushKV("result", strprintf("%u", nMessages));
        } else {
            result.pushKV("result", "Unknown Mode.");
            result.pushKV("expected", "all|clear.");
        }
    }

    return result;
},
    };
};

static RPCHelpMan smsgbuckets()
{
    return RPCHelpMan{"smsgbuckets",
                "\nDisplay message bucket information.\n",
                {
                    {"mode", RPCArg::Type::STR, RPCArg::Default{"stats"}, "stats|total|dump. \"dump\" will remove all buckets."},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgbuckets", "")
                    + HelpExampleRpc("smsgbuckets", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string mode = "stats";
    if (request.params.size() > 0) {
        mode = request.params[0].get_str();
    }

    UniValue result(UniValue::VOBJ);
    UniValue arrBuckets(UniValue::VARR);

    char cbuf[256];
    if (mode == "stats" || mode == "total") {
        bool show_buckets = mode != "total" ? true : false;
        uint32_t nBuckets = 0;
        uint32_t nMessages = 0;
        uint64_t nBytes = 0;
        {
            LOCK(smsgModule.cs_smsg);
            std::map<int64_t, smsg::SecMsgBucket>::const_iterator it;
            for (it = smsgModule.buckets.begin(); it != smsgModule.buckets.end(); ++it) {
                const std::set<smsg::SecMsgToken> &tokenSet = it->second.setTokens;

                std::string sBucket = ToString(it->first);
                std::string sFile = sBucket + "_01.dat";
                std::string sHash = ToString((int64_t)it->second.hash);

                size_t nActiveMessages = it->second.CountActive();

                nBuckets++;
                nMessages += nActiveMessages;

                UniValue objM(UniValue::VOBJ);
                if (show_buckets) {
                    objM.pushKV("bucket", sBucket);
                    PushTime(objM, "time", it->first);
                    objM.pushKV("no. messages", strprintf("%u", tokenSet.size()));
                    objM.pushKV("active messages", strprintf("%u", nActiveMessages));
                    objM.pushKV("hash", sHash);
                    objM.pushKV("last changed", part::GetTimeString(it->second.timeChanged, cbuf, sizeof(cbuf)));
                }

                fs::path fullPath = gArgs.GetDataDirNet() / fs::PathFromString(smsg::STORE_DIR) / fs::PathFromString(sFile);
                if (!fs::exists(fullPath)) {
                    if (tokenSet.size() == 0) {
                        objM.pushKV("file size", "Empty bucket.");
                    } else {
                        objM.pushKV("file size, error", "File not found.");
                    }
                } else {
                    try {
                        uint64_t nFBytes = 0;
                        nFBytes = fs::file_size(fullPath);
                        nBytes += nFBytes;
                        if (show_buckets) {
                            objM.pushKV("file size", part::BytesReadable(nFBytes));
                        }
                    } catch (const fs::filesystem_error& ex) {
                        objM.pushKV("file size, error", ex.what());
                    }
                }
                if (objM.size() > 0) {
                    arrBuckets.push_back(objM);
                }
            }
        } // cs_smsg

        UniValue objM(UniValue::VOBJ);
        objM.pushKV("numbuckets", (int)nBuckets);
        objM.pushKV("numpurged", (int)smsgModule.setPurged.size());
        objM.pushKV("messages", (int)nMessages);
        objM.pushKV("size", part::BytesReadable(nBytes));
        if (arrBuckets.size() > 0) {
            result.pushKV("buckets", arrBuckets);
        }
        result.pushKV("total", objM);
    } else
    if (mode == "dump") {
        {
            LOCK(smsgModule.cs_smsg);
            std::map<int64_t, smsg::SecMsgBucket>::iterator it;
            for (it = smsgModule.buckets.begin(); it != smsgModule.buckets.end(); ++it) {
                std::string sFile = ToString(it->first) + "_01.dat";

                try {
                    fs::path fullPath = gArgs.GetDataDirNet() / fs::PathFromString(smsg::STORE_DIR) / fs::PathFromString(sFile);
                    fs::remove(fullPath);
                } catch (const fs::filesystem_error& ex) {
                    //objM.push_back(Pair("file size, error", ex.what()));
                    LogPrintf("Error removing bucket file %s.\n", ex.what());
                }
            }
            smsgModule.buckets.clear();
            smsgModule.start_time = GetAdjustedTimeInt();
        } // cs_smsg

        result.pushKV("result", "Removed all buckets.");
    } else {
        result.pushKV("result", "Unknown Mode.");
        result.pushKV("expected", "stats|total|dump.");
    }

    return result;
},
    };
};

#ifdef ENABLE_WALLET
static bool sortMsgAsc(const std::pair<int64_t, UniValue> &a, const std::pair<int64_t, UniValue> &b)
{
    return a.first < b.first;
};

static bool sortMsgDesc(const std::pair<int64_t, UniValue> &a, const std::pair<int64_t, UniValue> &b)
{
    return a.first > b.first;
};
#endif

static RPCHelpMan smsgview()
{
    return RPCHelpMan{"smsgview",
                "\nView messages by address.\n"
                "Setting address to '*' will match all addresses\n"
                "'abc*' will match addresses with labels beginning 'abc'\n"
                "'*abc' will match addresses with labels ending 'abc'\n"
                "Full date/time format for from and to is yyyy-mm-ddThh:mm:ss\n"
                "From and to will accept incomplete inputs like: -from 2016\n",
                {
                    {"arg1", RPCArg::Type::STR, RPCArg::Default{"*"}, "address/label"},
                    {"arg2", RPCArg::Type::STR, RPCArg::Default{"asc"}, "asc/desc"},
                    {"arg3", RPCArg::Type::STR, RPCArg::Default{""}, "-from yyyy-mm-dd"},
                    {"arg4", RPCArg::Type::STR, RPCArg::Default{""}, "-to yyyy-mm-dd"},
                },
                RPCResult{
                    RPCResult::Type::ANY, "", ""
                },
                RPCExamples{
                    HelpExampleCli("smsgview", "")
                    + HelpExampleRpc("smsgview", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

#ifdef ENABLE_WALLET

    char cbuf[256];
    bool fMatchAll = false;
    bool fDesc = false;
    int64_t tFrom = 0, tTo = 0;
    std::vector<CKeyID> vMatchAddress;
    std::string sTemp;

    if (request.params.size() > 0) {
        sTemp = request.params[0].get_str();

        // Blank address or "*" will match all
        if (sTemp.length() < 1) { // Error instead?
            fMatchAll = true;
        } else
        if (sTemp.length() == 1 && sTemp[0] == '*') {
            fMatchAll = true;
        }

        if (!fMatchAll) {
            CBitcoinAddress checkValid(sTemp);

            if (checkValid.IsValid()) {
                CKeyID ki;
                checkValid.GetKeyID(ki);
                vMatchAddress.push_back(ki);
            } else {
                // Lookup address by label, can match multiple addresses

                // TODO: Use Boost.Regex?
                int matchType = 0; // 0 full match, 1 startswith, 2 endswith
                if (sTemp[0] == '*') {
                    matchType = 1;
                    sTemp.erase(0, 1);
                } else
                if (sTemp[sTemp.length()-1] == '*') {
                    matchType = 2;
                    sTemp.erase(sTemp.length()-1, 1);
                }

                std::map<CTxDestination, CAddressBookData>::iterator itl;

                for (const auto &pw : smsgModule.m_vpwallets) {
                    LOCK(pw->cs_wallet);
                    for (itl = pw->m_address_book.begin(); itl != pw->m_address_book.end(); ++itl) {
                        if (part::stringsMatchI(itl->second.GetLabel(), sTemp, matchType)) {
                            CBitcoinAddress checkValid(itl->first);
                            if (checkValid.IsValid()) {
                                CKeyID ki;
                                checkValid.GetKeyID(ki);
                                vMatchAddress.push_back(ki);
                            } else {
                                LogPrintf("Warning: matched invalid address: %s\n", checkValid.ToString());
                            }
                        }
                    }
                }
            }
        }
    } else {
        fMatchAll = true;
    }

    size_t i = 1;
    while (i < request.params.size()) {
        sTemp = request.params[i].get_str();
        if (sTemp == "-from") {
            if (i >= request.params.size()-1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Argument required for: " + sTemp);
            }
            i++;
            sTemp = request.params[i].get_str();
            tFrom = part::strToEpoch(sTemp.c_str());
            if (tFrom < 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "from format error: " + SysErrorString(errno));
            }
        } else
        if (sTemp == "-to") {
            if (i >= request.params.size()-1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Argument required for: " + sTemp);
            }
            i++;
            sTemp = request.params[i].get_str();
            tTo = part::strToEpoch(sTemp.c_str());
            if (tTo < 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "to format error: " + SysErrorString(errno));
            }
        } else
        if (sTemp == "asc") {
            fDesc = false;
        } else
        if (sTemp == "desc") {
            fDesc = true;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown parameter: " + sTemp);
        }

        i++;
    }

    if (!fMatchAll && vMatchAddress.size() < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No address found.");
    }

    UniValue result(UniValue::VOBJ);

    std::map<CKeyID, std::string> mLabelCache;
    std::vector<std::pair<int64_t, UniValue> > vMessages;

    std::vector<std::string> vPrefixes;
    vPrefixes.push_back(smsg::DBK_INBOX);
    vPrefixes.push_back(smsg::DBK_OUTBOX);

    uint8_t chKey[30];
    size_t nMessages = 0;
    UniValue messageList(UniValue::VARR);

    size_t debugEmptySent = 0;

    {
        LOCK(smsg::cs_smsgDB);
        smsg::SecMsgDB dbMsg;
        if (!dbMsg.Open("cr")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        std::vector<std::string>::iterator itp;
        std::vector<CKeyID>::iterator its;
        for (itp = vPrefixes.begin(); itp < vPrefixes.end(); ++itp) {
            bool fInbox = *itp == smsg::DBK_INBOX;

            dbMsg.TxnBegin();

            leveldb::Iterator *it = dbMsg.pdb->NewIterator(leveldb::ReadOptions());
            smsg::SecMsgStored smsgStored;
            smsg::MessageData msg;

            while (dbMsg.NextSmesg(it, *itp, chKey, smsgStored)) {
                if (!fInbox && smsgStored.addrOutbox.IsNull()) {
                    debugEmptySent++;
                    continue;
                }

                uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
                int rv;
                if ((rv = smsgModule.Decrypt(false, fInbox ? smsgStored.addrTo : smsgStored.addrOutbox,
                    smsgStored.vchMessage.data(), &smsgStored.vchMessage[smsg::SMSG_HDR_LEN], nPayload, msg)) == 0) {
                    if ((tFrom > 0 && msg.timestamp < tFrom)
                        || (tTo > 0 && msg.timestamp > tTo)) {
                        continue;
                    }

                    CKeyID kiFrom;
                    CBitcoinAddress addrFrom(msg.sFromAddress);
                    if (addrFrom.IsValid()) {
                        addrFrom.GetKeyID(kiFrom);
                    }

                    if (!fMatchAll) {
                        bool fSkip = true;

                        for (its = vMatchAddress.begin(); its < vMatchAddress.end(); ++its) {
                            if (*its == kiFrom
                                || *its == smsgStored.addrTo) {
                                fSkip = false;
                                break;
                            }
                        }

                        if (fSkip) {
                            continue;
                        }
                    }

                    // Get labels for addresses, cache found labels.
                    std::string lblFrom, lblTo;
                    std::map<CKeyID, std::string>::iterator itl;

                    if ((itl = mLabelCache.find(kiFrom)) != mLabelCache.end()) {
                        lblFrom = itl->second;
                    } else {
                        PKHash pkh = PKHash(kiFrom);
                        lblFrom = smsgModule.LookupLabel(pkh);
                        mLabelCache[kiFrom] = lblFrom;
                    }

                    if ((itl = mLabelCache.find(smsgStored.addrTo)) != mLabelCache.end()) {
                        lblTo = itl->second;
                    } else {
                        PKHash pkh = PKHash(smsgStored.addrTo);
                        lblTo = smsgModule.LookupLabel(pkh);
                        mLabelCache[smsgStored.addrTo] = lblTo;
                    }

                    std::string sFrom = kiFrom.IsNull() ? "anon" : EncodeDestination(PKHash(kiFrom));
                    std::string sTo = EncodeDestination(PKHash(smsgStored.addrTo));
                    if (lblFrom.length() != 0) {
                        sFrom += " (" + lblFrom + ")";
                    }
                    if (lblTo.length() != 0) {
                        sTo += " (" + lblTo + ")";
                    }

                    UniValue objM(UniValue::VOBJ);
                    PushTime(objM, "sent", msg.timestamp);
                    objM.pushKV("from", sFrom);
                    objM.pushKV("to", sTo);
                    objM.pushKV("text", std::string((char*)&msg.vchMessage[0]));

                    vMessages.push_back(std::make_pair(msg.timestamp, objM));
                } else {
                    LogPrintf("%s: SecureMsgDecrypt failed, %s.\n", __func__, HexStr(Span<const unsigned char>(chKey, 18)));
                }
            }
            delete it;

            dbMsg.TxnCommit();
        }
    } // cs_smsgDB


    std::sort(vMessages.begin(), vMessages.end(), fDesc ? sortMsgDesc : sortMsgAsc);

    std::vector<std::pair<int64_t, UniValue> >::iterator itm;
    for (itm = vMessages.begin(); itm < vMessages.end(); ++itm) {
        messageList.push_back(itm->second);
        nMessages++;
    }

    result.pushKV("messages", messageList);

    if (LogAcceptCategory(BCLog::SMSG, BCLog::Level::Debug)) {
        result.pushKV("debug empty sent", (int)debugEmptySent);
    }

    result.pushKV("result", strprintf("Displayed %u messages.", nMessages));
    if (tFrom > 0) {
        result.pushKV("from", part::GetTimeString(tFrom, cbuf, sizeof(cbuf)));
    }
    if (tTo > 0) {
        result.pushKV("to", part::GetTimeString(tTo, cbuf, sizeof(cbuf)));
    }
#else
    UniValue result(UniValue::VOBJ);
    throw JSONRPCError(RPC_MISC_ERROR, "No wallet.");
#endif
    return result;
},
    };
}

static RPCHelpMan smsgone()
{
    return RPCHelpMan{"smsg",
                "\nView smsg by msgid.\n",
                {
                    {"msgid", RPCArg::Type::STR, RPCArg::Optional::NO, "Id of the message to view."},
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"delete", RPCArg::Type::BOOL, RPCArg::Default{false}, "Delete msg if true."},
                            {"setread", RPCArg::Type::BOOL, RPCArg::Default{false}, "Set read status to value."},
                            {"encoding", RPCArg::Type::STR, RPCArg::Default{"text"}, "Display message data in encoding, values: \"text\", \"hex\", \"none\"."},
                            {"export", RPCArg::Type::BOOL, RPCArg::Default{false}, "Display the full smsg as a hex encoded string."},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR_HEX, "msgid", "Message id"},
                        {RPCResult::Type::STR, "version", "The message version"},
                        {RPCResult::Type::STR, "location", "inbox|outbox|sending"},
                        {RPCResult::Type::NUM_TIME, "received", "Time the message was received"},
                        {RPCResult::Type::STR, "received_local", /*optional=*/true, "Time the message was received"},
                        {RPCResult::Type::STR, "received_utc", /*optional=*/true, "Time the message was received"},
                        {RPCResult::Type::BOOL, "read", "Read status"},
                        {RPCResult::Type::NUM_TIME, "sent", "Time the message was created"},
                        {RPCResult::Type::STR, "sent_local", /*optional=*/true, "Time the message was created"},
                        {RPCResult::Type::STR, "sent_utc", /*optional=*/true, "Time the message was created"},
                        {RPCResult::Type::BOOL, "paid", "Paid or free message"},
                        {RPCResult::Type::NUM, "daysretention", "DEPRECATED Number of days message will stay in the network for"},
                        {RPCResult::Type::NUM, "ttl", "Seconds message will stay in the network for"},
                        {RPCResult::Type::NUM_TIME, "expiration", "Time the message will be dropped from the network"},
                        {RPCResult::Type::STR, "expiration_local", /*optional=*/true, "Time Expired"},
                        {RPCResult::Type::STR, "expiration_utc", /*optional=*/true, "Time Expired"},
                        {RPCResult::Type::NUM, "payloadsize", "Size of user message"},
                        {RPCResult::Type::STR, "from", "Address the message was sent from"},
                        {RPCResult::Type::STR, "to", "Address the message was sent to"},
                        {RPCResult::Type::STR, "text", /*optional=*/true, "Message text"},
                        {RPCResult::Type::STR, "hex", /*optional=*/true, "Message text"},
                        {RPCResult::Type::STR, "unknown_encoding", /*optional=*/true, "Message text"},
                        {RPCResult::Type::STR, "raw", /*optional=*/true, "Complete message hex encoded"},
                        {RPCResult::Type::STR, "operation", /*optional=*/true, "Operation performed"},
                }},
                RPCExamples{
            HelpExampleCli("smsg", "\"msgid\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsg", "\"msgid\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    RPCTypeCheckObj(request.params,
        {
            {"msgid",             UniValueType(UniValue::VSTR)},
            {"options",           UniValueType(UniValue::VOBJ)},
        }, true, false);

    std::string sMsgId = request.params[0].get_str();

    if (!IsHex(sMsgId) || sMsgId.size() != 56) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "msgid must be 28 bytes in hex string.");
    }
    std::vector<uint8_t> vMsgId = ParseHex(sMsgId.c_str());
    std::string sType;

    uint8_t chKey[30];
    chKey[1] = 'M';
    memcpy(chKey + 2, vMsgId.data(), 28);
    smsg::SecMsgStored smsgStored;

    UniValue result(UniValue::VOBJ);
    UniValue options = request.params[1];
    {
        LOCK(smsg::cs_smsgDB);
        smsg::SecMsgDB dbMsg;
        if (!dbMsg.Open("cr+")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        if ((chKey[0] = 'I') && dbMsg.ReadSmesg(chKey, smsgStored)) {
            sType = "inbox";
        } else
        if ((chKey[0] = 'S') && dbMsg.ReadSmesg(chKey, smsgStored)) {
            sType = "outbox";
        } else
        if ((chKey[0] = 'Q') && dbMsg.ReadSmesg(chKey, smsgStored)) {
            sType = "sending";
        } else
        if ((chKey[0] = 'T') && dbMsg.ReadSmesg(chKey, smsgStored)) {
            sType = "stashed";
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown message id.");
        }

        if (options.isObject()) {
            options = request.params[1].get_obj();
            if (options["delete"].isBool() && options["delete"].get_bool() == true) {
                if (!dbMsg.EraseSmesg(chKey)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "EraseSmesg failed.");
                }
                result.pushKV("operation", "Deleted");
            } else {
                // Can't mix delete and other operations
                if (options["setread"].isBool()) {
                    bool nv = options["setread"].get_bool();
                    if (nv) {
                        smsgStored.status &= ~SMSG_MASK_UNREAD;
                    } else {
                        smsgStored.status |= SMSG_MASK_UNREAD;
                    }

                    if (!dbMsg.WriteSmesg(chKey, smsgStored)) {
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "WriteSmesg failed.");
                    }
                    result.pushKV("operation", strprintf("Set read status to: %s", nv ? "true" : "false"));
                }
            }
        }
    }

    smsg::SecureMessage smsg(smsgStored.vchMessage.data());
    const smsg::SecureMessage *psmsg = &smsg;

    result.pushKV("msgid", sMsgId);
    result.pushKV("version", strprintf("%02x%02x", psmsg->version[0], psmsg->version[1]));
    result.pushKV("location", sType);
    PushTime(result, "received", smsgStored.timeReceived);
    result.pushKV("to", EncodeDestination(PKHash(smsgStored.addrTo)));
    //result.pushKV("addressoutbox", CBitcoinAddress(smsgStored.addrOutbox).ToString());
    result.pushKV("read", UniValue(bool(!(smsgStored.status & SMSG_MASK_UNREAD))));

    PushTime(result, "sent", psmsg->timestamp);
    result.pushKV("paid", UniValue(psmsg->IsPaidVersion()));

    int64_t ttl = psmsg->m_ttl;
    result.pushKV("ttl", ttl);
    int nDaysRetention = ttl / smsg::SMSG_SECONDS_IN_DAY;
    result.pushKV("daysretention", nDaysRetention);
    PushTime(result, "expiration", psmsg->timestamp + ttl);


    smsg::MessageData msg;
    bool fInbox = sType == "inbox" ? true : false;
    uint32_t nPayload = smsgStored.vchMessage.size() - smsg::SMSG_HDR_LEN;
    result.pushKV("payloadsize", (int)nPayload);

    std::string sEnc;
    if (options.isObject() && options["encoding"].isStr()) {
        sEnc = options["encoding"].get_str();
    }

    bool export_smsg = options.isObject() && options["export"].isBool() ? options["export"].get_bool() : false;
    if (export_smsg) {
        result.pushKV("raw", HexStr(smsgStored.vchMessage));
    }

    int rv;
    if ((rv = smsgModule.Decrypt(false, fInbox ? smsgStored.addrTo : smsgStored.addrOutbox,
        smsgStored.vchMessage.data(), &smsgStored.vchMessage[smsg::SMSG_HDR_LEN], nPayload, msg)) == 0) {
        result.pushKV("from", msg.sFromAddress);

        if (sEnc == "none") {
        } else
        if (sEnc == "") {
            // TODO: detect non ascii chars
            if (msg.vchMessage.size() < smsg::SMSG_MAX_MSG_BYTES) {
                result.pushKV("text", std::string((char*)msg.vchMessage.data()));
            } else {
                result.pushKV("hex", HexStr(msg.vchMessage));
            }
        } else
        if (sEnc == "text") {
            result.pushKV("text", std::string((char*)msg.vchMessage.data()));
        } else
        if (sEnc == "hex") {
            result.pushKV("hex", HexStr(msg.vchMessage));
        } else {
            result.pushKV("unknown_encoding", sEnc);
        }
    } else {
        result.pushKV("error", "decrypt failed");
    }

    return result;
},
    };
}

static RPCHelpMan smsgimport()
{
    return RPCHelpMan{"smsgimport",
                "\nImport smsg from hex string.\n",
                {
                    {"msg", RPCArg::Type::STR, RPCArg::Optional::NO, "Hex encoded smsg."},
                    {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                        {
                            {"submitmsg", RPCArg::Type::BOOL, RPCArg::Default{false}, "Submit msg to network if true."},
                            {"setread", RPCArg::Type::BOOL, RPCArg::Default{false}, "Set read status to value."},
                        },
                    },
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR_HEX, "msgid", "The message identifier"},
                }},
                RPCExamples{
            HelpExampleCli("smsgimport", "\"msg\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsgimport", "\"msg\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    RPCTypeCheckObj(request.params,
        {
            {"msg",             UniValueType(UniValue::VSTR)},
            {"option",          UniValueType(UniValue::VOBJ)},
        }, true, false);

    std::string str_msg = request.params[0].get_str();

    if (!IsHex(str_msg)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "msg must be a hex string.");
    }

    std::vector<uint8_t> vsmsg = ParseHex(str_msg.c_str());
    smsg::SecureMessage smsg(vsmsg.data());
    smsg.pPayload = vsmsg.data() + smsg::SMSG_HDR_LEN;

    UniValue result(UniValue::VOBJ);
    std::string str_error;
    bool setread = false;
    bool submitmsg = false;
    UniValue options = request.params[1];
    if (options.isObject() && options["setread"].isBool()) {
        setread = options["setread"].get_bool();
    }
    if (options.isObject() && options["submitmsg"].isBool()) {
        submitmsg = options["submitmsg"].get_bool();
    }

    if (smsgModule.Import(&smsg, str_error, setread, submitmsg) != 0) {
        smsg.pPayload = nullptr;
        throw JSONRPCError(RPC_MISC_ERROR, "Import failed: " + str_error);
    }
    result.pushKV("msgid", HexStr(smsgModule.GetMsgID(smsg)));

    smsg.pPayload = nullptr;

    return result;
},
    };
}

static RPCHelpMan smsgpurge()
{
    return RPCHelpMan{"smsgpurge",
                "\nPurge smsg by msgid.\n",
                {
                    {"msgid", RPCArg::Type::STR_HEX, RPCArg::Default{""}, "Id of the message to purge."},
                },
                RPCResult{
                    RPCResult::Type::NONE, "", "None"},
                RPCExamples{
            HelpExampleCli("smsgpurge", "\"msgid\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsgpurge", "\"msgid\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    if (!request.params[0].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "msgid must be a string.");
    }

    std::string sMsgId = request.params[0].get_str();

    if (!IsHex(sMsgId) || sMsgId.size() != 56) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "msgid must be 28 bytes in hex string.");
    }
    std::vector<uint8_t> vMsgId = ParseHex(sMsgId.c_str());

    std::string sError;
    if (smsg::SMSG_NO_ERROR != smsgModule.Purge(vMsgId, sError)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Error: " + sError);
    }

    return UniValue::VNULL;
},
    };
}

static RPCHelpMan smsggetfeerate()
{
    return RPCHelpMan{"smsggetfeerate",
                "\nReturn paid SMSG fee rate.\n"
                "The fee is set per thousand message bytes per day.\n",
                {
                    {"height", RPCArg::Type::NUM, RPCArg::DefaultHint{"current height"}, "Chain height to get fee rate for, pass a negative number for more detailed output."},
                },
                {
                    RPCResult{"Default", RPCResult::Type::NUM, "", "Fee rate in satoshis"},
                    RPCResult{"With height", RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "currentrate", "Fee rate in satoshis"},
                        {RPCResult::Type::NUM, "inactiveuntil", /*optional=*/true, "Time smsg fees activates on the chain"},
                        {RPCResult::Type::NUM, "currentrateblockheight", "Block height the current rate was sampled from"},
                        {RPCResult::Type::NUM, "targetrate", "Rate at chain tip"},
                        {RPCResult::Type::NUM, "targetblockheight", "Block tip height"},
                        {RPCResult::Type::NUM, "nextratechangeheight", "Height next fee rate will activate"},
                    }}
                },
                RPCExamples{
            HelpExampleCli("smsggetfeerate", "1000") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsggetfeerate", "1000")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    LOCK(cs_main);
    ChainstateManager &chainman = EnsureAnyChainman(request.context);

    CBlockIndex *pblockindex = nullptr;
    if (!request.params[0].isNull()) {
        int nHeight = request.params[0].getInt<int>();

        if (nHeight < 0) {
            UniValue result(UniValue::VOBJ);
            const CBlockIndex *pTip = chainman.ActiveChain().Tip();
            const Consensus::Params &consensusParams = Params().GetConsensus();
            int chain_height = pTip->nHeight;

            if (pTip->nTime < consensusParams.smsg_fee_time) {
                result.pushKV("inactiveuntil", int64_t(consensusParams.smsg_fee_time));
                return result;
            }

            result.pushKV("currentrate", particl::GetSmsgFeeRate(chainman, nullptr));
            int fee_height = (chain_height / consensusParams.smsg_fee_period) * consensusParams.smsg_fee_period;
            result.pushKV("currentrateblockheight", fee_height);

            int64_t smsg_fee_rate_target;
            CBlock block;
            if (!node::ReadBlockFromDisk(block, pTip, Params().GetConsensus())) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
            }
            block.vtx[0]->GetSmsgFeeRate(smsg_fee_rate_target);
            result.pushKV("targetrate", smsg_fee_rate_target);
            result.pushKV("targetblockheight", chain_height);
            result.pushKV("nextratechangeheight", int(fee_height + consensusParams.smsg_fee_period));
            return result;
        }
        if (nHeight > chainman.ActiveChain().Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }
        pblockindex = chainman.ActiveChain()[nHeight];
    }

    return particl::GetSmsgFeeRate(chainman, pblockindex);
},
    };
}

static RPCHelpMan smsggetdifficulty()
{
    return RPCHelpMan{"smsggetdifficulty",
                "\nReturn free SMSG difficulty.\n",
                {
                    {"time", RPCArg::Type::NUM, RPCArg::DefaultHint{"Current time"}, "Chain time to get smsg difficulty for."},
                },
                RPCResult{
                    RPCResult::Type::NUM, "difficulty", "smsg difficulty"
                },
                RPCExamples{
            HelpExampleCli("smsggetdifficulty", "1552688834") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsggetdifficulty", "1552688834")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    LOCK(cs_main);

    ChainstateManager &chainman = EnsureAnyChainman(request.context);

    int64_t chain_time = chainman.ActiveChain().Tip()->nTime;
    if (!request.params[0].isNull()) {
        chain_time = request.params[0].getInt<int64_t>();
        if (chain_time > chainman.ActiveChain().Tip()->nTime) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Time out of range");
        }
    }

    uint32_t target_compact = particl::GetSmsgDifficulty(chainman, chain_time);
    return smsg::GetDifficulty(target_compact);
},
    };
}

static RPCHelpMan smsggetinfo()
{
    return RPCHelpMan{"smsggetinfo",
                "\nReturns an object containing SMSG-related information.\n",
                {
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::BOOL, "enabled", "True if SMSG is enabled"},
                        {RPCResult::Type::STR, "active_wallet", /*optional=*/true, "name of the currently active wallet or \"None set\""},
                        {RPCResult::Type::ARR, "enabled_wallets", /*optional=*/true, "Names of enabled wallets",
                        {
                            {RPCResult::Type::STR_HEX, "", "wallet_name"},
                        }}
                    },
                },
                RPCExamples{
            HelpExampleCli("smsggetinfo", "") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("smsggetinfo", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("enabled", smsg::fSecMsgEnabled.load());
    if (smsg::fSecMsgEnabled) {
        obj.pushKV("active_wallet", smsgModule.GetWalletName());
#ifdef ENABLE_WALLET
        UniValue wallet_names(UniValue::VARR);
        for (const auto &pw : smsgModule.m_vpwallets) {
            wallet_names.push_back(pw->GetName());
        }
        obj.pushKV("enabled_wallets", wallet_names);
#endif
    }

    return obj;
},
    };
}

static RPCHelpMan smsgpeers()
{
    return RPCHelpMan{"smsgpeers",
        "\nReturns data about each connected SMSG node as a json array of objects.\n",
        {
            {"index", RPCArg::Type::NUM, RPCArg::DefaultHint{"All"}, "Peer index, omit for list."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "id", "Peer index"},
                    {RPCResult::Type::STR, "address", "Node address"},
                    {RPCResult::Type::NUM, "version", "Peer version"},
                    {RPCResult::Type::NUM, "ignoreuntil", "Peer ignored until time"},
                    {RPCResult::Type::NUM, "misbehaving", "Misbehaviour counter"},
                    {RPCResult::Type::NUM, "numwantsent", "Number of smsges requested from peer"},
                    {RPCResult::Type::NUM, "receivecounter", "Messages received from peer in window"},
                    {RPCResult::Type::NUM, "ignoredcounter", "Number of times peer has been ignored"},
                    {RPCResult::Type::NUM, "num_pending_inv", "Number of buckets peer has to show"},
                    {RPCResult::Type::NUM, "num_shown_buckets", "Number of buckets peer showed last"},
                    {RPCResult::Type::OBJ, "pending_inv_buckets", /*optional=*/true, "", {
                        {RPCResult::Type::NUM, "active", "Active messages in bucket"},
                        {RPCResult::Type::STR, "hash", "Bucket hash"},
                    }},
                    {RPCResult::Type::OBJ, "shown_buckets", /*optional=*/true, "", {
                        {RPCResult::Type::NUM, "time", "Entry time"},
                        {RPCResult::Type::NUM, "last_shown", "Number of buckets peer showed"},
                    }},
                }},
            }
        },
        RPCExamples{
    HelpExampleCli("smsgpeers", "") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("smsgpeers", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    int index = request.params[0].isNull() ? -1 : request.params[0].getInt<int>();

    UniValue result(UniValue::VARR);

    smsgModule.GetNodesStats(index, result);

    return result;
},
    };
}

static RPCHelpMan smsgzmqpush()
{
    return RPCHelpMan{"smsgzmqpush",
            "\nResend ZMQ notifications.\n",
            {
                {"options", RPCArg::Type::OBJ, RPCArg::Default{UniValue::VOBJ}, "",
                    {
                        {"timefrom", RPCArg::Type::NUM, RPCArg::Default{0}, "Skip messages received before timestamp."},
                        {"timeto", RPCArg::Type::NUM, RPCArg::Default{"max_int"}, "Skip messages received after timestamp."},
                        {"unreadonly", RPCArg::Type::BOOL, RPCArg::Default{true}, "Resend only unread messages."},
                    },
                },
            },
            RPCResult{
                RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::NUM, "numsent", "Number of notifications sent"},
            }},
            RPCExamples{
        HelpExampleCli("smsgzmqpush", "'{ \"unreadonly\": false }'") +
        "\nAs a JSON-RPC call\n"
        + HelpExampleRpc("smsgzmqpush", "{ \"unreadonly\": false }")
            },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    bool unreadonly = true;
    int64_t timefrom = 0;
    int64_t timeto = std::numeric_limits<int64_t>::max();
    int num_sent = 0;

    UniValue options = request.params[0];
    if (options.isObject()) {
        RPCTypeCheckObj(options,
        {
            {"timefrom",        UniValueType(UniValue::VNUM)},
            {"timeto",          UniValueType(UniValue::VNUM)},
            {"unreadonly",      UniValueType(UniValue::VBOOL)},
        }, true, true);
        if (options["timefrom"].isNum()) {
            timefrom = options["timefrom"].getInt<int64_t>();
        }
        if (options["timeto"].isNum()) {
            timeto = options["timeto"].getInt<int64_t>();
        }
        if (options["unreadonly"].isBool()) {
            unreadonly = options["unreadonly"].get_bool();
        }
    }

    {
        LOCK(smsg::cs_smsgDB);

        smsg::SecMsgDB dbInbox;
        if (!dbInbox.Open("cr+")) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
        }

        uint8_t chKey[30];
        smsg::SecMsgStored smsgStored;
        leveldb::Iterator *it = dbInbox.pdb->NewIterator(leveldb::ReadOptions());
        while (dbInbox.NextSmesg(it, smsg::DBK_INBOX, chKey, smsgStored)) {
            if (unreadonly
                && !(smsgStored.status & SMSG_MASK_UNREAD)) {
                continue;
            }
            if (smsgStored.timeReceived < timefrom ||
                smsgStored.timeReceived > timeto) {
                continue;
            }

            smsg::SecureMessage smsg(smsgStored.vchMessage.data());
            const smsg::SecureMessage *psmsg = &smsg;

            std::vector<uint8_t> vchUint160;
            vchUint160.resize(20);
            memcpy(vchUint160.data(), &chKey[10], 20);
            uint160 hash(vchUint160);

            GetMainSignals().NewSecureMessage(psmsg, hash);
            num_sent++;
        }
        delete it;
    } // cs_smsgDB

    UniValue result(UniValue::VOBJ);

    result.pushKV("numsent", num_sent);

    return result;
},
    };
};

static RPCHelpMan smsgdebug()
{
    return RPCHelpMan{"smsgdebug",
        "\nCommands useful for debugging.\n",
        {
            {"command", RPCArg::Type::STR, RPCArg::Default{""}, "\"clearbanned\",\"dumpids\",\"dumpfundingtxids\"."},
            {"arg1", RPCArg::Type::STR, RPCArg::Default{""}, ""},
        },
        RPCResult{
            RPCResult::Type::ANY, "", ""
        },
        RPCExamples{
    HelpExampleCli("smsgdebug", "") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("smsgdebug", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    EnsureSMSGIsEnabled();

    std::string mode = "none";
    if (request.params.size() > 0) {
        mode = request.params[0].get_str();
    }

    UniValue result(UniValue::VOBJ);

    if (mode == "clearbanned") {
        result.pushKV("command", mode);
        smsgModule.ClearBanned();
    } else
    if (mode == "dumpids") {
        fs::path filepath = gArgs.GetDataDirNet() / "smsg_ids.txt";
        if (fs::exists(filepath)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "smsg_ids.txt already exists in the datadir. Please move it out of the way first");
        }

        bool active_only = request.params.size() > 1 ? GetBool(request.params[1]) : true;
        int64_t now = GetAdjustedTimeInt();

        std::ofstream file;
        file.open(filepath);
        if (!file.is_open()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot open dump file");
        }

        int num_messages = 0;
        LOCK(smsgModule.cs_smsg);
        std::map<int64_t, smsg::SecMsgBucket>::const_iterator it;
        std::vector<uint8_t> vch_msg;
        for (it = smsgModule.buckets.begin(); it != smsgModule.buckets.end(); ++it) {
            const std::set<smsg::SecMsgToken> &token_set = it->second.setTokens;
            for (auto token : token_set) {
                if (active_only && token.timestamp + token.ttl < now) {
                    continue; // Skip expired
                }
                if (smsgModule.Retrieve(token, vch_msg) != smsg::SMSG_NO_ERROR) {
                    LogPrintf("SecureMsgRetrieve failed %d.\n", token.timestamp);
                    continue;
                }
                smsg::SecureMessage smsg(vch_msg.data());
                const smsg::SecureMessage *psmsg = &smsg;
                if (psmsg->version[0] == 0 && psmsg->version[1] == 0) {
                    continue; // Skip purged
                }
                file << strprintf("%d,%s\n", it->first, HexStr(smsgModule.GetMsgID(psmsg, vch_msg.data() + smsg::SMSG_HDR_LEN)));
                num_messages++;
            }
        }

        file.close();
        result.pushKV("active_only", active_only);
        result.pushKV("messages", num_messages);
    } else
    if (mode == "dumpfundingtxids") {
        smsgModule.ShowFundingTxns(result);
    } else
    if (mode == "clearbestblock") {
        smsgModule.ClearBestBlock();
        result.pushKV("result", "Cleared best block");
    } else
    if (mode == "setinvalidbestblock") {
        uint256 block_hash;
        *block_hash.begin() = 123;
        const CBlockIndex *tip;
        {
        LOCK(cs_main);
        ChainstateManager &chainman = EnsureAnyChainman(request.context);
        tip = chainman.ActiveChain().Tip();
        }
        int height = 0;
        if (request.params.size() > 1) {
            std::string s = request.params[1].get_str();
            if (!ParseInt32(s, &height)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid height integer");
            }
        } else
        if (tip && tip->nHeight > 0) {
            height = tip->nHeight -1;
        }
        {
            LOCK(smsg::cs_smsgDB);
            smsg::SecMsgDB db;
            if (!db.Open("cw")) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not open DB");
            }

            db.WriteBestBlock(block_hash, height);
        }
        result.pushKV("result", "Set invalid best block");
    } else
    if (mode == "none") {
        uint256 best_block_hash;
        int best_block_height{-1};
        smsgModule.ReadBestBlock(best_block_hash, best_block_height);
        result.pushKV("best_block_hash", best_block_hash.ToString());
        result.pushKV("best_block_height", best_block_height);
    } else {
        result.pushKV("error", "Unknown command");
    }

    return result;
},
    };
}

void RegisterSmsgRPCCommands(CRPCTable &t)
{
    static const CRPCCommand commands[]{
        {"smsg", &smsgenable},
        {"smsg", &smsgsetwallet},
        {"smsg", &smsgdisable},
        {"smsg", &smsgoptions},
        {"smsg", &smsglocalkeys},
        {"smsg", &smsgscanchain},
        {"smsg", &smsgscanbuckets},
        {"smsg", &smsgaddaddress },
        {"smsg", &smsgaddlocaladdress},
        {"smsg", &smsgimportprivkey},
        {"smsg", &smsgdumpprivkey},
        {"smsg", &smsggetpubkey},
        {"smsg", &smsgsend},
        {"smsg", &smsgfund},
        {"smsg", &smsgsendanon},
        {"smsg", &smsginbox},
        {"smsg", &smsgoutbox},
        {"smsg", &smsgbuckets},
        {"smsg", &smsgview},
        {"smsg", &smsgone},
        {"smsg", &smsgimport},
        {"smsg", &smsgpurge},
        {"smsg", &smsggetfeerate},
        {"smsg", &smsggetdifficulty},
        {"smsg", &smsggetinfo},
        {"smsg", &smsgpeers},
        {"smsg", &smsgzmqpush},
        {"smsg", &smsgdebug},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
