// Copyright (c) 2018-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <zmq.h>
#include <zmq/zmqrpc.h>

#include <rpc/server.h>
#include <rpc/util.h>
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>

#include <util/strencodings.h>

#include <univalue.h>

int GetNewZMQKeypair(char *server_public_key, char *server_secret_key)
{
    return zmq_curve_keypair(server_public_key, server_secret_key);
}

static RPCHelpMan getzmqnotifications()
{
    return RPCHelpMan{"getzmqnotifications",
                "\nReturns information about the active ZeroMQ notifications.\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::STR, "type", "Type of notification"},
                            {RPCResult::Type::STR, "address", "Address of the publisher"},
                            {RPCResult::Type::NUM, "hwm", "Outbound message high water mark"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getzmqnotifications", "")
            + HelpExampleRpc("getzmqnotifications", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue result(UniValue::VARR);
    if (g_zmq_notification_interface != nullptr) {
        for (const auto* n : g_zmq_notification_interface->GetActiveNotifiers()) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("type", n->GetType());
            obj.pushKV("address", n->GetAddress());
            obj.pushKV("hwm", n->GetOutboundMessageHighWaterMark());
            result.push_back(obj);
        }
    }

    return result;
},
    };
}

static RPCHelpMan getnewzmqserverkeypair()
{
    return RPCHelpMan{"getnewzmqserverkeypair",
                "\nReturns a newly generated server keypair for use with zmq.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR, "server_secret_key", ""},
                        {RPCResult::Type::STR, "server_public_key", ""},
                        {RPCResult::Type::STR_HEX, "server_secret_key_b64", ""}
                    }
                },
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    char server_public_key[41], server_secret_key[41];
    if (0 != GetNewZMQKeypair(server_public_key, server_secret_key)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zmq_curve_keypair failed.");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("server_secret_key", server_secret_key);
    obj.pushKV("server_public_key", server_public_key);

    std::string sBase64 = EncodeBase64(MakeUCharSpan(server_secret_key));
    obj.pushKV("server_secret_key_b64", sBase64);

    return obj;
},
    };
}

void RegisterZMQRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category              actor (function)
  //  --------------------- -----------------------
    { "zmq",                &getzmqnotifications                  },
    { "zmq",                &getnewzmqserverkeypair               },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
