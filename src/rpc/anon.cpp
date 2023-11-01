// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <rpc/server_util.h>
#include <rpc/blockchain.h>

#include <validation.h>
#include <txdb.h>
#include <anon.h>


static bool IsDigits(const std::string &str)
{
    return str.length() && std::all_of(str.begin(), str.end(), IsDigit);
};

static RPCHelpMan anonoutput()
{
    return RPCHelpMan{"anonoutput",
                "\nReturns an anon output at index or by publickey hex.\n"
                "If no output is provided returns the last index.\n",
                {
                    {"output", RPCArg::Type::STR, RPCArg::Default{""}, "Output to view, specified by index or hex of publickey."},
                },
                {
                RPCResult{"No arguments",
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "lastindex", "Number of anon outputs"},
                }},
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "index", "Position in chain of anon output"},
                        {RPCResult::Type::STR_HEX, "publickey", "Public key of anon out"},
                        {RPCResult::Type::STR_HEX, "txnhash", "Hash of transaction found in"},
                        {RPCResult::Type::NUM, "n", "Offset in transaction found in"},
                        {RPCResult::Type::NUM, "blockheight", "Height of block found in"},
                }}
                },
                RPCExamples{
            HelpExampleCli("anonoutput", "\"1\"")
            + HelpExampleRpc("anonoutput", "\"2\"")
            },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    UniValue result(UniValue::VOBJ);

    if (request.params.size() == 0) {
        LOCK(cs_main);
        result.pushKV("lastindex", (int)chainman.ActiveChain().Tip()->nAnonOutputs);
        return result;
    }

    std::string sIn = request.params[0].get_str();
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};

    int64_t nIndex;
    if (IsDigits(sIn)) {
        if (!ParseInt64(sIn, &nIndex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid index");
        }
    } else {
        if (!IsHex(sIn)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, sIn + " is not a hexadecimal or decimal string.");
        }
        std::vector<uint8_t> vIn = ParseHex(sIn);
        CCmpPubKey pk(vIn.begin(), vIn.end());

        if (!pk.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, sIn + " is not a valid compressed public key.");
        }

        if (!pblocktree->ReadRCTOutputLink(pk, nIndex)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Output not indexed.");
        }
    }

    CAnonOutput ao;
    if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unknown index.");
    }

    result.pushKV("index", int(nIndex));
    result.pushKV("publickey", HexStr(Span<const unsigned char>(ao.pubkey.begin(), 33)));
    result.pushKV("txnhash", ao.outpoint.hash.ToString());
    result.pushKV("n", int(ao.outpoint.n));
    result.pushKV("blockheight", ao.nBlockHeight);

    return result;
},
    };
};

static RPCHelpMan checkkeyimage()
{
    return RPCHelpMan{"checkkeyimage",
            "\nCheck if keyimage is spent in the chain.\n",
            {
                {"keyimage", RPCArg::Type::STR, RPCArg::Optional::NO, "Hex encoded keyimage."},
            },
            RPCResult{
                RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::BOOL, "spent", "Keyimage found in chain or not"},
                    {RPCResult::Type::STR_HEX, "txid", /*optional=*/true, "ID of spending transaction"},
                    {RPCResult::Type::NUM, "height", /*optional=*/true, "Chain height of containing block"},
            }},
            RPCExamples{
        HelpExampleCli("checkkeyimage", "\"keyimage\"")
        + HelpExampleRpc("checkkeyimage", "\"keyimage\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    auto& pblocktree{chainman.m_blockman.m_block_tree_db};

    UniValue result(UniValue::VOBJ);

    std::string s = request.params[0].get_str();
    if (!IsHex(s) || !(s.size() == 66)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Keyimage must be 33 bytes and hex encoded.");
    }
    std::vector<uint8_t> v = ParseHex(s);
    CCmpPubKey ki(v.begin(), v.end());

    CAnonKeyImageInfo ki_data;
    bool spent_in_chain = pblocktree->ReadRCTKeyImage(ki, ki_data);

    result.pushKV("spent", spent_in_chain);
    if (spent_in_chain) {
        result.pushKV("txid", ki_data.txid.ToString());
        if (ki_data.height > 0) {
            result.pushKV("height", ki_data.height);
        }
    }

    return result;
},
    };
}

static RPCHelpMan rollbackrctindex()
{
    return RPCHelpMan{"rollbackrctindex",
            "\nRollback RCT index to current chain tip..\n",
            {
            },
            RPCResult{
                RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::NUM, "height", "Current chain height"},
            }},
            RPCExamples{
        HelpExampleCli("rollbackrctindex", "")
        + HelpExampleRpc("rollbackrctindex", "")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    LOCK(cs_main);
    const CBlockIndex *pindex = chainman.ActiveChain().Tip();

    std::set<CCmpPubKey> setKi; // unused
    int64_t nTestExists = 0;
    RollBackRCTIndex(chainman, pindex->nAnonOutputs, nTestExists, pindex->nHeight, setKi);

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", pindex->nHeight);

    return result;
},
    };
}

void RegisterAnonRPCCommands(CRPCTable &t)
{
    static const CRPCCommand commands[]{
        {"anon", &anonoutput},
        {"anon", &checkkeyimage},
        {"anon", &rollbackrctindex},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
