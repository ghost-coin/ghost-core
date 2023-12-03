// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <key_io.h>
#include <policy/rbf.h>
#include <rpc/util.h>
#include <rpc/server.h>
#include <util/vector.h>
#include <wallet/receive.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>
#include <wallet/hdwallet.h>

using interfaces::FoundBlock;

namespace wallet {
void WalletTxToJSON(const CWallet& wallet, const CWalletTx& wtx, UniValue& entry, bool fFilterMode=false)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    interfaces::Chain& chain = wallet.chain();
    int confirms = wallet.GetTxDepthInMainChain(wtx);
    entry.pushKV("confirmations", confirms);
    if (wtx.IsCoinBase())
        entry.pushKV("generated", true);
    if (auto* conf = wtx.state<TxStateConfirmed>())
    {
        entry.pushKV("blockhash", conf->confirmed_block_hash.GetHex());
        entry.pushKV("blockheight", conf->confirmed_block_height);
        entry.pushKV("blockindex", conf->position_in_block);
        int64_t block_time;
        CHECK_NONFATAL(chain.findBlock(conf->confirmed_block_hash, FoundBlock().time(block_time)));
        PushTime(entry, "blocktime", block_time);
    } else {
        entry.pushKV("trusted", CachedTxIsTrusted(wallet, wtx));
    }
    uint256 hash = wtx.GetHash();
    entry.pushKV("txid", hash.GetHex());
    entry.pushKV("wtxid", wtx.GetWitnessHash().GetHex());
    UniValue conflicts(UniValue::VARR);
    for (const uint256& conflict : wallet.GetTxConflicts(wtx))
        conflicts.push_back(conflict.GetHex());
    entry.pushKV("walletconflicts", conflicts);
    PushTime(entry, "time", wtx.GetTxTime());
    PushTime(entry, "timereceived", wtx.nTimeReceived);

    // Add opt-in RBF status
    std::string rbfStatus = "no";
    if (confirms <= 0) {
        RBFTransactionState rbfState = chain.isRBFOptIn(*wtx.tx);
        if (rbfState == RBFTransactionState::UNKNOWN)
            rbfStatus = "unknown";
        else if (rbfState == RBFTransactionState::REPLACEABLE_BIP125)
            rbfStatus = "yes";
    }
    entry.pushKV("bip125-replaceable", rbfStatus);

    if (!fFilterMode) {
        for (const std::pair<const std::string, std::string>& item : wtx.mapValue) {
            // Filter out narrations, displayed elsewhere
            if (item.first.size() > 1 && item.first[0] != 'n') {
                entry.pushKV(item.first, item.second);
            }
        }
    }
}

static void AddSmsgFundingInfo(const CTransaction &tx, UniValue &entry)
{
    CAmount smsg_fees = 0;
    UniValue smsges(UniValue::VARR);
    for (const auto &v : tx.vpout) {
        if (!v->IsType(OUTPUT_DATA)) {
            continue;
        }
        CTxOutData *txd = (CTxOutData*) v.get();
        if (txd->vData.size() < 25 || txd->vData[0] != DO_FUND_MSG) {
            continue;
        }
        size_t n = (txd->vData.size()-1) / 24;
        for (size_t k = 0; k < n; ++k) {
            uint32_t nAmount;
            memcpy(&nAmount, &txd->vData[1+k*24+20], 4);
            nAmount = le32toh(nAmount);
            smsg_fees += nAmount;
            UniValue funded_smsg(UniValue::VOBJ);
            funded_smsg.pushKV("smsghash", HexStr(Span<const unsigned char>(&txd->vData[1+k*24], 20)));
            funded_smsg.pushKV("fee", ValueFromAmount(nAmount));
            smsges.push_back(funded_smsg);
        }
    }
    if (smsg_fees > 0) {
        entry.pushKV("smsgs_funded", smsges);
        entry.pushKV("smsg_fees", ValueFromAmount(smsg_fees));
    }
}

static void ListRecord(const CHDWallet *phdw, const uint256 &hash, const CTransactionRecord &rtx,
    const std::string &strAccount, int nMinDepth, bool with_tx_details, UniValue &ret, const isminefilter &filter) EXCLUSIVE_LOCKS_REQUIRED(phdw->cs_wallet)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    for (const auto &r : rtx.vout) {
        if (r.nFlags & ORF_CHANGE) {
            continue;
        }

        if (!(r.nFlags & ORF_FROM) && !(r.nFlags & ORF_OWNED) && !(filter & ISMINE_WATCH_ONLY)) {
            continue;
        }

        std::string account;
        CBitcoinAddress addr;
        CTxDestination dest;
        if (ExtractDestination(r.scriptPubKey, dest) && !r.scriptPubKey.IsUnspendable()) {
            addr.Set(dest);

            std::map<CTxDestination, CAddressBookData>::const_iterator mai = phdw->m_address_book.find(dest);
            if (mai != phdw->m_address_book.end() && !mai->second.GetLabel().empty()) {
                account = mai->second.GetLabel();
            }
        }

        if (!fAllAccounts && (account != strAccount)) {
            continue;
        }

        UniValue entry(UniValue::VOBJ);
        if (r.nFlags & ORF_OWN_WATCH) {
            entry.pushKV("involvesWatchonly", true);
        }
        entry.pushKV("account", account);

        if (r.vPath.size() > 0) {
            if (r.vPath[0] == ORA_STEALTH) {
                if (r.vPath.size() < 5) {
                    LogPrintf("%s: Warning, malformed vPath.\n", __func__);
                } else {
                    uint32_t sidx;
                    memcpy(&sidx, &r.vPath[1], 4);
                    CStealthAddress sx;
                    if (phdw->GetStealthByIndex(sidx, sx)) {
                        entry.pushKV("stealth_address", sx.Encoded());
                    }
                }
            }
        } else {
            PKHash *pkh = std::get_if<PKHash>(&dest);
            if (pkh) {
                CStealthAddress sx;
                CKeyID idK = ToKeyID(*pkh);
                if (phdw->GetStealthLinked(idK, sx)) {
                    entry.pushKV("stealth_address", sx.Encoded());
                }
            }
        }

        if (r.nFlags & ORF_LOCKED) {
            entry.pushKV("requires_unlock", true);
        }

        if (dest.index() == DI::_CNoDestination) {
            entry.pushKV("address", "none");
        } else {
            entry.pushKV("address", addr.ToString());
        }

        std::string sCategory;
        if (r.nFlags & ORF_OWNED && r.nFlags & ORF_FROM) {
            // Sent to self
            sCategory = "receive";
        } else
        if (r.nFlags & ORF_OWN_ANY) {
            sCategory = "receive";
        } else
        if (r.nFlags & ORF_FROM) {
            sCategory = "send";
        }

        entry.pushKV("category", sCategory);
        entry.pushKV("type", r.nType == OUTPUT_STANDARD ? "standard"
                           : r.nType == OUTPUT_CT ? "blind"
                           : r.nType == OUTPUT_RINGCT ? "anon" : "unknown");

        if (r.nFlags & ORF_OWNED && r.nFlags & ORF_FROM) {
            entry.pushKV("fromself", true);
        }

        CAmount amount = r.nValue * ((r.nFlags & ORF_OWN_ANY) ? 1 : -1);
        entry.pushKV("amount", ValueFromAmount(amount));

        entry.pushKV("vout", r.n);

        if (with_tx_details) {
            if (r.nFlags & ORF_FROM) {
                entry.pushKV("fee", ValueFromAmount(-rtx.nFee));
            }
            int confirms = phdw->GetDepthInMainChain(rtx);
            entry.pushKV("confirmations", confirms);
            if (confirms > 0) {
                entry.pushKV("blockhash", rtx.blockHash.GetHex());
                entry.pushKV("blockindex", rtx.nIndex);
                PushTime(entry, "blocktime", rtx.nBlockTime);
            } else {
                entry.pushKV("trusted", phdw->IsTrusted(hash, rtx));
            }

            entry.pushKV("txid", hash.ToString());

            UniValue conflicts(UniValue::VARR);
            std::set<uint256> setconflicts = phdw->GetConflicts(hash);
            setconflicts.erase(hash);
            for (const auto &conflict : setconflicts) {
                conflicts.push_back(conflict.GetHex());
            }
            entry.pushKV("walletconflicts", conflicts);

            PushTime(entry, "time", rtx.GetTxTime());
            entry.pushKV("abandoned", rtx.IsAbandoned());
        }

        if (!r.sNarration.empty()) {
            entry.pushKV("narration", r.sNarration);
        }

        ret.push_back(entry);
    }
}

void RecordTxToJSON(interfaces::Chain& chain, const CHDWallet *phdw, const uint256 &hash, const CTransactionRecord& rtx, UniValue &entry, isminefilter filter, bool verbose) EXCLUSIVE_LOCKS_REQUIRED(phdw->cs_wallet)
{
    int confirms = phdw->GetDepthInMainChain(rtx);
    entry.pushKV("confirmations", confirms);

    if (rtx.IsCoinStake()) {
        entry.pushKV("coinstake", true);
    } else
    if (rtx.IsCoinBase()) {
        entry.pushKV("generated", true);
    }

    if (confirms > 0) {
        entry.pushKV("blockhash", rtx.blockHash.GetHex());
        entry.pushKV("blockindex", rtx.nIndex);
        PushTime(entry, "blocktime", rtx.nBlockTime);
    } else {
        entry.pushKV("trusted", phdw->IsTrusted(hash, rtx));
    }

    entry.pushKV("txid", hash.GetHex());
    UniValue conflicts(UniValue::VARR);
    for (const auto &conflict : phdw->GetConflicts(hash)) {
        conflicts.push_back(conflict.GetHex());
    }
    entry.pushKV("walletconflicts", conflicts);
    PushTime(entry, "time", rtx.GetTxTime());
    PushTime(entry, "timereceived", rtx.nTimeReceived);

    for (const auto &item : rtx.mapValue) {
        if (item.first == RTXVT_COMMENT) {
            entry.pushKV("comment", std::string(item.second.begin(), item.second.end()));
        } else
        if (item.first == RTXVT_TO) {
            entry.pushKV("comment_to", std::string(item.second.begin(), item.second.end()));
        }
    }

    entry.pushKV("amount", 0);  // Reserve position
    if (rtx.nFlags & ORF_ANON_IN) {
        entry.pushKV("type_in", "anon");
    } else
    if (rtx.nFlags & ORF_BLIND_IN) {
        entry.pushKV("type_in", "blind");
    } else {
        entry.pushKV("type_in", "plain");
    }

    /*
    // Add opt-in RBF status
    std::string rbfStatus = "no";
    if (confirms <= 0) {
        LOCK(mempool.cs);
        RBFTransactionState rbfState = IsRBFOptIn(wtx, mempool);
        if (rbfState == RBF_TRANSACTIONSTATE_UNKNOWN)
            rbfStatus = "unknown";
        else if (rbfState == RBF_TRANSACTIONSTATE_REPLACEABLE_BIP125)
            rbfStatus = "yes";
    }
    entry.push_back(Pair("bip125-replaceable", rbfStatus));
    */

    size_t num_owned{0}, num_from{0};
    CAmount sum_amounts{0};
    for (const auto &r : rtx.vout) {
        if (r.nFlags & ORF_CHANGE) {
            continue;
        }
        if (r.nFlags & ORF_FROM) {
            num_from++;
        }
        CAmount amount = r.nValue;
        if (r.nFlags & ((filter & ISMINE_WATCH_ONLY) ? ORF_OWN_ANY : ORF_OWNED)) {
            num_owned++;
        } else {
            amount *= -1;
        }
        sum_amounts += amount;
    }
    if (num_from > 0) {
        entry.pushKV("fee", ValueFromAmount(-rtx.nFee));
        sum_amounts -= rtx.nFee;
    }
    if (num_owned && num_from) {
        // Must check against the owned input value
        CAmount nInput = 0, nOutput = 0;
        for (const auto &vin : rtx.vin) {
            nInput += phdw->GetOwnedOutputValue(vin, filter);
        }
        for (const auto &r : rtx.vout) {
            if ((r.nFlags & ORF_OWNED && filter & ISMINE_SPENDABLE) ||
                (r.nFlags & ORF_OWN_WATCH && filter & ISMINE_WATCH_ONLY)) {
                nOutput += r.nValue;
            }
        }
        entry.pushKVEnd("amount", ValueFromAmount(nOutput - nInput));
    } else {
        entry.pushKVEnd("amount", ValueFromAmount(sum_amounts));
    }

    UniValue details(UniValue::VARR);
    ListRecord(phdw, hash, rtx, "*", 0, false, details, filter);
    entry.pushKV("details", details);

    CStoredTransaction stx;
    if (CHDWalletDB(phdw->GetDatabase()).ReadStoredTx(hash, stx)) { // TODO: cache
        std::string strHex = EncodeHexTx(*(stx.tx.get()), RPCSerializationFlags());
        entry.pushKV("hex", strHex);

        if (verbose) {
            UniValue decoded(UniValue::VOBJ);
            TxToUniv(*(stx.tx.get()), uint256(), decoded, false);
            entry.pushKV("decoded", decoded);
        }
        mapRTxValue_t::const_iterator mvi = rtx.mapValue.find(RTXVT_SMSG_FEES);
        if (mvi != rtx.mapValue.end()) {
            AddSmsgFundingInfo(*(stx.tx.get()), entry);
        }
    }
}

struct tallyitem
{
    CAmount nAmount{0};
    int nConf{std::numeric_limits<int>::max()};
    std::vector<uint256> txids;
    bool fIsWatchonly{false};
    tallyitem() = default;
};

static UniValue ListReceived(const CWallet& wallet, const UniValue& params, bool by_label, const bool include_immature_coinbase) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (!params[0].isNull())
        nMinDepth = params[0].getInt<int>();

    // Whether to include empty labels
    bool fIncludeEmpty = false;
    if (!params[1].isNull())
        fIncludeEmpty = params[1].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;

    if (ParseIncludeWatchonly(params[2], wallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    std::optional<CTxDestination> filtered_address{std::nullopt};
    if (!by_label && !params[3].isNull() && !params[3].get_str().empty()) {
        if (!IsValidDestinationString(params[3].get_str())) {
            throw JSONRPCError(RPC_WALLET_ERROR, "address_filter parameter was invalid");
        }
        filtered_address = DecodeDestination(params[3].get_str());
    }

    // Tally
    std::map<CTxDestination, tallyitem> mapTally;
    for (const std::pair<const uint256, CWalletTx>& pairWtx : wallet.mapWallet) {
        const CWalletTx& wtx = pairWtx.second;

        int nDepth = wallet.GetTxDepthInMainChain(wtx);
        if (nDepth < nMinDepth)
            continue;

        // Coinbase with less than 1 confirmation is no longer in the main chain
        if ((wtx.IsCoinBase() && (nDepth < 1)) ||
            (wallet.IsTxImmatureCoinBase(wtx) && !include_immature_coinbase)) {
            continue;
        }

        for (auto &txout : wtx.tx->vpout) {
            if (!txout->IsType(OUTPUT_STANDARD)) {
                continue;
            }
            CTxOutStandard *pOut = (CTxOutStandard*)txout.get();

            CTxDestination address;
            if (!ExtractDestination(pOut->scriptPubKey, address)) {
                continue;
            }
            isminefilter mine = wallet.IsMine(address);
            if (!(mine & filter)) {
                continue;
            }
            tallyitem& item = mapTally[address];
            item.nAmount += pOut->nValue;
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY) {
                item.fIsWatchonly = true;
            }
        }

        for (const CTxOut& txout : wtx.tx->vout) {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            if (filtered_address && !(filtered_address == address)) {
                continue;
            }

            isminefilter mine = wallet.IsMine(address);
            if (!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = std::min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    std::map<std::string, tallyitem> label_tally;

    const auto& func = [&](const CTxDestination& address, const std::string& label, bool is_change, const std::optional<AddressPurpose>& purpose) {
        if (is_change) return; // no change addresses

        auto it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            return;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end()) {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (by_label) {
            tallyitem& _item = label_tally[label];
            _item.nAmount += nAmount;
            _item.nConf = std::min(_item.nConf, nConf);
            _item.fIsWatchonly = fIsWatchonly;
        } else {
            UniValue obj(UniValue::VOBJ);
            if (fIsWatchonly) obj.pushKV("involvesWatchonly", true);
            obj.pushKV("address",       EncodeDestination(address));
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            obj.pushKV("label", label);
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end()) {
                for (const uint256& _item : (*it).second.txids) {
                    transactions.push_back(_item.GetHex());
                }
            }
            obj.pushKV("txids", transactions);
            ret.push_back(obj);
        }
    };

    if (filtered_address) {
        const auto& entry = wallet.FindAddressBookEntry(*filtered_address, /*allow_change=*/false);
        if (entry) func(*filtered_address, entry->GetLabel(), entry->IsChange(), entry->purpose);
    } else {
        // No filtered addr, walk-through the addressbook entry
        wallet.ForEachAddrBookEntry(func);
    }

    if (by_label) {
        for (const auto& entry : label_tally) {
            CAmount nAmount = entry.second.nAmount;
            int nConf = entry.second.nConf;
            UniValue obj(UniValue::VOBJ);
            if (entry.second.fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf));
            obj.pushKV("label",         entry.first);
            ret.push_back(obj);
        }
    }

    return ret;
}

RPCHelpMan listreceivedbyaddress()
{
    return RPCHelpMan{"listreceivedbyaddress",
                "\nList balances by receiving address.\n",
                {
                    {"minconf", RPCArg::Type::NUM, RPCArg::Default{1}, "The minimum number of confirmations before payments are included."},
                    {"include_empty", RPCArg::Type::BOOL, RPCArg::Default{false}, "Whether to include addresses that haven't received any payments."},
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"}, "Whether to include watch-only addresses (see 'importaddress')"},
                    {"address_filter", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If present and non-empty, only return information on this address."},
                    {"include_immature_coinbase", RPCArg::Type::BOOL, RPCArg::Default{false}, "Include immature coinbase transactions."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::BOOL, "involvesWatchonly", /*optional=*/true, "Only returns true if imported addresses were involved in transaction"},
                            {RPCResult::Type::STR, "address", "The receiving address"},
                            {RPCResult::Type::STR_AMOUNT, "amount", "The total amount in " + CURRENCY_UNIT + " received by the address"},
                            {RPCResult::Type::NUM, "confirmations", "The number of confirmations of the most recent transaction included"},
                            {RPCResult::Type::STR, "label", "The label of the receiving address. The default label is \"\""},
                            {RPCResult::Type::ARR, "txids", "",
                            {
                                {RPCResult::Type::STR_HEX, "txid", "The ids of transactions received with the address"},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listreceivedbyaddress", "6 true")
            + HelpExampleCli("listreceivedbyaddress", "6 true true \"\" true")
            + HelpExampleRpc("listreceivedbyaddress", "6, true, true")
            + HelpExampleRpc("listreceivedbyaddress", "6, true, true, \"" + EXAMPLE_ADDRESS[0] + "\", true")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    const bool include_immature_coinbase{request.params[4].isNull() ? false : request.params[4].get_bool()};

    LOCK(pwallet->cs_wallet);

    return ListReceived(*pwallet, request.params, false, include_immature_coinbase);
},
    };
}

RPCHelpMan listreceivedbylabel()
{
    return RPCHelpMan{"listreceivedbylabel",
                "\nList received transactions by label.\n",
                {
                    {"minconf", RPCArg::Type::NUM, RPCArg::Default{1}, "The minimum number of confirmations before payments are included."},
                    {"include_empty", RPCArg::Type::BOOL, RPCArg::Default{false}, "Whether to include labels that haven't received any payments."},
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"}, "Whether to include watch-only addresses (see 'importaddress')"},
                    {"include_immature_coinbase", RPCArg::Type::BOOL, RPCArg::Default{false}, "Include immature coinbase transactions."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::BOOL, "involvesWatchonly", /*optional=*/true, "Only returns true if imported addresses were involved in transaction"},
                            {RPCResult::Type::STR_AMOUNT, "amount", "The total amount received by addresses with this label"},
                            {RPCResult::Type::NUM, "confirmations", "The number of confirmations of the most recent transaction included"},
                            {RPCResult::Type::STR, "label", "The label of the receiving address. The default label is \"\""},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listreceivedbylabel", "")
            + HelpExampleCli("listreceivedbylabel", "6 true")
            + HelpExampleRpc("listreceivedbylabel", "6, true, true, true")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    const bool include_immature_coinbase{request.params[3].isNull() ? false : request.params[3].get_bool()};

    LOCK(pwallet->cs_wallet);

    return ListReceived(*pwallet, request.params, true, include_immature_coinbase);
},
    };
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    if (IsValidDestination(dest)) {
        entry.pushKV("address", EncodeDestination(dest));
    }
}

/**
 * List transactions based on the given criteria.
 *
 * @param  wallet         The wallet.
 * @param  wtx            The wallet transaction.
 * @param  nMinDepth      The minimum confirmation depth.
 * @param  fLong          Whether to include the JSON version of the transaction.
 * @param  ret            The vector into which the result is stored.
 * @param  filter_ismine  The "is mine" filter flags.
 * @param  filter_label   Optional label string to filter incoming transactions.
 */
template <class Vec>
static void ListTransactions(const CWallet& wallet, const CWalletTx& wtx, int nMinDepth, bool fLong,
                             Vec& ret, const isminefilter& filter_ismine, const std::optional<std::string>& filter_label,
                             bool include_change = false)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    CAmount nFee;
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;
    std::list<COutputEntry> listStaked;

    CachedTxGetAmounts(wallet, wtx, listReceived, listSent, listStaked, nFee, filter_ismine, include_change);

    bool involvesWatchonly = CachedTxIsFromMe(wallet, wtx, ISMINE_WATCH_ONLY);

    // Sent
    if (!filter_label.has_value())
    {
        for (const COutputEntry& s : listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (wallet.IsMine(s.destination) & ISMINE_WATCH_ONLY) || (s.ismine & ISMINE_WATCH_ONLY)) {
                entry.pushKV("involvesWatchonly", true);
            }
            MaybePushAddress(entry, s.destination);
            if (s.destStake.index() != DI::_CNoDestination) {
                entry.pushKV("coldstake_address", EncodeDestination(s.destStake));
            }
            entry.pushKV("category", "send");
            entry.pushKV("amount", ValueFromAmount(-s.amount));
            const auto* address_book_entry = wallet.FindAddressBookEntry(s.destination);
            if (address_book_entry) {
                entry.pushKV("label", address_book_entry->GetLabel());
            }
            entry.pushKV("vout", s.vout);
            entry.pushKV("fee", ValueFromAmount(-nFee));
            if (fLong) {
                WalletTxToJSON(wallet, wtx, entry);
            }
            {
                std::string sNarrKey = strprintf("n%d", s.vout);
                mapValue_t::const_iterator mi = wtx.mapValue.find(sNarrKey);
                if (mi != wtx.mapValue.end() && !mi->second.empty()) {
                    entry.pushKV("narration", mi->second);
                }
            }
            entry.pushKV("abandoned", wtx.isAbandoned());

            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wallet.GetTxDepthInMainChain(wtx) >= nMinDepth) {
        for (const COutputEntry& r : listReceived)
        {
            std::string label;
            const auto* address_book_entry = wallet.FindAddressBookEntry(r.destination);
            if (address_book_entry) {
                label = address_book_entry->GetLabel();
            }
            if (filter_label.has_value() && label != filter_label.value()) {
                continue;
            }
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (wallet.IsMine(r.destination) & ISMINE_WATCH_ONLY) || (r.ismine & ISMINE_WATCH_ONLY)) {
                entry.pushKV("involvesWatchonly", true);
            }

            if (wallet.IsParticlWallet() &&
                r.destination.index() == DI::_PKHash) {
                CStealthAddress sx;
                CKeyID idK = ToKeyID(std::get<PKHash>(r.destination));
                if (GetParticlWallet(&wallet)->GetStealthLinked(idK, sx)) {
                    entry.pushKV("stealth_address", sx.Encoded());
                }
            }

            MaybePushAddress(entry, r.destination);
            if (r.destStake.index() != DI::_CNoDestination) {
                entry.pushKV("coldstake_address", EncodeDestination(r.destStake));
            }
            const CScript *ps = nullptr;
            if (wallet.IsParticlWallet()) {
                ps = wtx.tx->vpout.at(r.vout)->GetPScriptPubKey();
            } else {
                ps = &wtx.tx->vout.at(r.vout).scriptPubKey;
            }
            if (ps) {
                PushParentDescriptors(wallet, *ps, entry);
            }
            if (wtx.IsCoinBase()) {
                if (wallet.GetTxDepthInMainChain(wtx) < 1) {
                    entry.pushKV("category", "orphan");
                } else if (wallet.IsTxImmatureCoinBase(wtx)) {
                    entry.pushKV("category", "immature");
                } else {
                    entry.pushKV("category", (fParticlMode ? "coinbase" : "generate"));
                }
            } else {
                entry.pushKV("category", "receive");
            }
            entry.pushKV("amount", ValueFromAmount(r.amount));
            if (address_book_entry) {
                entry.pushKV("label", label);
                entry.pushKV("account", label); // For exchanges
            }
            entry.pushKV("vout", r.vout);
            entry.pushKV("abandoned", wtx.isAbandoned());
            if (fLong) {
                WalletTxToJSON(wallet, wtx, entry);
            }
            {
                std::string sNarrKey = strprintf("n%d", r.vout);
                mapValue_t::const_iterator mi = wtx.mapValue.find(sNarrKey);
                if (mi != wtx.mapValue.end() && !mi->second.empty()) {
                    entry.pushKV("narration", mi->second);
                }
            }
            ret.push_back(entry);
        }
    }

    // Staked
    if (listStaked.size() > 0 && wallet.GetTxDepthInMainChain(wtx) >= nMinDepth) {
        for (const auto &s : listStaked) {
            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (s.ismine & ISMINE_WATCH_ONLY)) {
                entry.pushKV("involvesWatchonly", true);
            }
            MaybePushAddress(entry, s.destination);
            if (s.destStake.index() != DI::_CNoDestination) {
                entry.pushKV("coldstake_address", EncodeDestination(s.destStake));
            }
            entry.pushKV("category", wallet.GetTxDepthInMainChain(wtx) < 1 ? "orphaned_stake" : "stake");

            entry.pushKV("amount", ValueFromAmount(s.amount));
            const auto* address_book_entry = wallet.FindAddressBookEntry(s.destination);
            if (address_book_entry) {
                entry.pushKV("label", address_book_entry->GetLabel());
            }
            entry.pushKV("vout", s.vout);
            entry.pushKV("reward", ValueFromAmount(-nFee));
            if (fLong) {
                WalletTxToJSON(wallet, wtx, entry);
            }
            {
                std::string sNarrKey = strprintf("n%d", s.vout);
                mapValue_t::const_iterator mi = wtx.mapValue.find(sNarrKey);
                if (mi != wtx.mapValue.end() && !mi->second.empty()) {
                    entry.pushKV("narration", mi->second);
                }
            }
            entry.pushKV("abandoned", wtx.isAbandoned());
            ret.push_back(entry);
        }
    }
}

static std::vector<RPCResult> TransactionDescriptionString()
{
    return{{RPCResult::Type::NUM, "confirmations", "The number of confirmations for the transaction. Negative confirmations means the\n"
               "transaction conflicted that many blocks ago."},
           {RPCResult::Type::BOOL, "generated", /*optional=*/true, "Only present if the transaction's only input is a coinbase one."},
           {RPCResult::Type::BOOL, "trusted", /*optional=*/true, "Whether we consider the transaction to be trusted and safe to spend from.\n"
                "Only present when the transaction has 0 confirmations (or negative confirmations, if conflicted)."},
           {RPCResult::Type::STR_HEX, "blockhash", /*optional=*/true, "The block hash containing the transaction."},
           {RPCResult::Type::NUM, "blockheight", /*optional=*/true, "The block height containing the transaction."},
           {RPCResult::Type::NUM, "blockindex", /*optional=*/true, "The index of the transaction in the block that includes it."},
           {RPCResult::Type::NUM_TIME, "blocktime", /*optional=*/true, "The block time expressed in " + UNIX_EPOCH_TIME + "."},
           {RPCResult::Type::STR, "blocktime_local", /*optional=*/true, "Human readable blocktime with local offset."},
           {RPCResult::Type::STR, "blocktime_utc", /*optional=*/true, "Human readable blocktime in UTC."},
           {RPCResult::Type::STR_HEX, "txid", "The transaction id."},
           {RPCResult::Type::STR_HEX, "wtxid", /*optional=*/true, "The hash of serialized transaction, including witness data."},
           {RPCResult::Type::ARR, "walletconflicts", "Conflicting transaction ids.",
           {
               {RPCResult::Type::STR_HEX, "txid", "The transaction id."},
           }},
           {RPCResult::Type::STR_HEX, "replaced_by_txid", /*optional=*/true, "The txid if this tx was replaced."},
           {RPCResult::Type::STR_HEX, "replaces_txid", /*optional=*/true, "The txid if the tx replaces one."},
           {RPCResult::Type::STR, "to", /*optional=*/true, "If a comment to is associated with the transaction."},
           {RPCResult::Type::STR, "time_local", /*optional=*/true, "Human readable time with local offset."},
           {RPCResult::Type::STR, "time_utc", /*optional=*/true, "Human readable time in UTC."},
           {RPCResult::Type::NUM_TIME, "time", "The transaction time expressed in " + UNIX_EPOCH_TIME + "."},
           {RPCResult::Type::NUM_TIME, "timereceived", /*optional=*/true, "The time received expressed in " + UNIX_EPOCH_TIME + "."},
           {RPCResult::Type::STR, "timereceived_local", /*optional=*/true, "Human readable timereceived with local offset."},
           {RPCResult::Type::STR, "timereceived_utc", /*optional=*/true, "Human readable timereceived in UTC."},
           {RPCResult::Type::STR, "comment", /*optional=*/true, "If a comment is associated with the transaction, only present if not empty."},
           {RPCResult::Type::STR, "comment_to", /*optional=*/true, "If a comment_to is associated with the transaction, only present if not empty."},
           {RPCResult::Type::STR, "narration", /*optional=*/true, "If a narration is embedded in the transaction, only present if not empty."},
           {RPCResult::Type::BOOL, "fromself", /*optional=*/true, "True if this wallet owned an input of the transaction."},
           {RPCResult::Type::STR, "bip125-replaceable", /*optional=*/true, "(\"yes|no|unknown\") Whether this transaction signals BIP125 replaceability or has an unconfirmed ancestor signaling BIP125 replaceability.\n"
               "May be unknown for unconfirmed transactions not in the mempool because their unconfirmed ancestors are unknown."},
           {RPCResult::Type::ARR, "parent_descs", /*optional=*/true, "Only if 'category' is 'received'. List of parent descriptors for the scriptPubKey of this coin.", {
               {RPCResult::Type::STR, "desc", "The descriptor string."},
           }},
           {RPCResult::Type::BOOL, "abandoned", /*optional=*/true, "'true' if the transaction has been abandoned (inputs are respendable)."},
           };
}

RPCHelpMan listtransactions()
{
    return RPCHelpMan{"listtransactions",
                "\nIf a label name is provided, this will return only incoming transactions paying to addresses with the specified label.\n"
                "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions.\n",
                {
                    {"label|dummy", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If set, should be a valid label name to return only incoming transactions\n"
                          "with the specified label, or \"*\" to disable filtering and return all transactions."},
                    {"count", RPCArg::Type::NUM, RPCArg::Default{10}, "The number of transactions to return"},
                    {"skip", RPCArg::Type::NUM, RPCArg::Default{0}, "The number of transactions to skip"},
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"}, "Include transactions to watch-only addresses (see 'importaddress')"},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                        {
                            {RPCResult::Type::BOOL, "involvesWatchonly", /*optional=*/true, "Only returns true if imported addresses were involved in transaction."},
                            {RPCResult::Type::STR, "address", /*optional=*/true, "The particl address of the transaction (not returned if the output does not have an address, e.g. OP_RETURN null data)."},
                            {RPCResult::Type::STR, "stealth_address", /*optional=*/true, "The stealth address the transaction was received on."},
                            {RPCResult::Type::STR, "coldstake_address", /*optional=*/true, "The address the transaction is staking on."},
                            {RPCResult::Type::STR, "type", /*optional=*/true, "anon/blind/standard."},
                            {RPCResult::Type::STR, "category", "The transaction category.\n"
                                "\"send\"                  Transactions sent.\n"
                                "\"receive\"               Non-coinbase transactions received.\n"
                                "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                                "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                                "\"orphan\"                Orphaned coinbase transactions received."},
                            {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and is positive\n"
                                "for all other categories"},
                            {RPCResult::Type::STR, "label", /*optional=*/true, "A comment for the address/transaction, if any"},
                            {RPCResult::Type::STR, "account", /*optional=*/true, "Duplicate of \"label\", requested by exchanges."},
                            {RPCResult::Type::NUM, "vout", "the vout value"},
                            {RPCResult::Type::STR_AMOUNT, "fee", /*optional=*/true, "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the\n"
                                 "'send' category of transactions."},
                            {RPCResult::Type::STR_AMOUNT, "reward", /*optional=*/true, "The reward if the transaction is a coinstake."},
                        },
                        TransactionDescriptionString()),
                        {
                            {RPCResult::Type::BOOL, "abandoned", "'true' if the transaction has been abandoned (inputs are respendable)."},
                        })},
                    }
                },
                RPCExamples{
            "\nList the most recent 10 transactions in the systems\n"
            + HelpExampleCli("listtransactions", "") +
            "\nList transactions 100 to 120\n"
            + HelpExampleCli("listtransactions", "\"*\" 20 100") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    std::optional<std::string> filter_label;
    if (!request.params[0].isNull() && request.params[0].get_str() != "*") {
        filter_label.emplace(LabelFromValue(request.params[0]));
        if (filter_label.value().empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Label argument must be a valid label name or \"*\".");
        }
    }
    int nCount = 10;
    if (!request.params[1].isNull())
        nCount = request.params[1].getInt<int>();
    int nFrom = 0;
    if (!request.params[2].isNull())
        nFrom = request.params[2].getInt<int>();
    isminefilter filter = ISMINE_SPENDABLE;

    if (ParseIncludeWatchonly(request.params[3], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    // NOTE: nFrom and nCount seem to apply to the individual json entries, not the txn
    //  a txn producing 2 entries will output only 1 entry if nCount is 1
    // TODO: Change to count on unique txids?

    std::vector<UniValue> ret;
    {
        LOCK(pwallet->cs_wallet);
        const CWallet::TxItems &txOrdered = pwallet->wtxOrdered;

        // iterate backwards until we have nCount items to return:
        for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
            CWalletTx *const pwtx = (*it).second;
            ListTransactions(*pwallet, *pwtx, 0, true, ret, filter, filter_label);
            if ((int)ret.size() >= (nCount+nFrom)) break;
        }
    }

    // Need the full dataset here, can only trim once the tx records are merged in.
    // Must be ordered from newest to oldest
    UniValue result{UniValue::VARR};
    for(int i = ret.size() - 1; i >= 0; --i) {
        result.push_back(ret[i]);
    }

    if (pwallet->IsParticlWallet()) {
        const CHDWallet *phdw = GetParticlWallet(pwallet.get());
        LOCK(phdw->cs_wallet);
        const RtxOrdered_t &txOrdered = phdw->rtxOrdered;

        // TODO: Combine finding and inserting into ret loops
        UniValue retRecords(UniValue::VARR);
        for (RtxOrdered_t::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
            std::string strAccount = "*";
            ListRecord(phdw, it->second->first, it->second->second, strAccount, 0, true, retRecords, filter);
            if ((int)retRecords.size() >= nCount + nFrom) {
                break;
            }
        }

        size_t nSearchStart = 0;
        for(int i = (int)retRecords.size() - 1; i >= 0; --i) {
            int64_t nInsertTime = retRecords[i].find_value("time").getInt<int64_t>();
            bool fFound = false;
            for (size_t k = nSearchStart; k < result.size(); k++) {
                nSearchStart = k;
                int64_t nTime = result[k].find_value("time").getInt<int64_t>();
                if (nTime > nInsertTime) {
                    result.insert(k, retRecords[i]);
                    fFound = true;
                    break;
                }
            }

            if (!fFound) {
                result.push_back(retRecords[i]);
            }
        }

        if (nFrom > 0 && result.size() > 0) {
            result.erase(std::max((size_t)0, result.size() - nFrom), result.size());
        }
        if (result.size() > (size_t)nCount) {
            result.erase(0, result.size() - nCount);
        }
    }
    return result;
},
    };
}

RPCHelpMan listsinceblock()
{
    return RPCHelpMan{"listsinceblock",
                "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted.\n"
                "If \"blockhash\" is no longer a part of the main chain, transactions from the fork point onward are included.\n"
                "Additionally, if include_removed is set, transactions affecting the wallet which were removed are returned in the \"removed\" array.\n",
                {
                    {"blockhash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "If set, the block hash to list transactions since, otherwise list all transactions."},
                    {"target_confirmations", RPCArg::Type::NUM, RPCArg::Default{1}, "Return the nth block hash from the main chain. e.g. 1 would mean the best block hash. Note: this is not used as a filter, but only affects [lastblock] in the return value"},
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"}, "Include transactions to watch-only addresses (see 'importaddress')"},
                    {"include_removed", RPCArg::Type::BOOL, RPCArg::Default{true}, "Show transactions that were removed due to a reorg in the \"removed\" array\n"
                                                                       "(not guaranteed to work on pruned nodes)"},
                    {"include_change", RPCArg::Type::BOOL, RPCArg::Default{false}, "Also add entries for change outputs.\n"},
                    {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Return only incoming transactions paying to addresses with the specified label.\n"},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::ARR, "transactions", "",
                        {
                            {RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                            {
                                {RPCResult::Type::BOOL, "involvesWatchonly", /*optional=*/true, "Only returns true if imported addresses were involved in transaction."},
                                {RPCResult::Type::STR, "address", /*optional=*/true, "The particl address of the transaction (not returned if the output does not have an address, e.g. OP_RETURN null data)."},
                                {RPCResult::Type::STR, "account", /*optional=*/true, "Alias of label."},
                                {RPCResult::Type::STR, "stealth_address", /*optional=*/true, "The stealth address the transaction was received on."},
                                {RPCResult::Type::STR, "coldstake_address", /*optional=*/true, "The address the transaction is staking on."},
                                {RPCResult::Type::STR, "type", /*optional=*/true, "anon/blind/standard."},
                                {RPCResult::Type::STR, "category", "The transaction category.\n"
                                    "\"send\"                  Transactions sent.\n"
                                    "\"receive\"               Non-coinbase transactions received.\n"
                                    "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                                    "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                                    "\"orphan\"                Orphaned coinbase transactions received."},
                                {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and is positive\n"
                                    "for all other categories"},
                                {RPCResult::Type::NUM, "vout", "the vout value"},
                                {RPCResult::Type::STR_AMOUNT, "fee", /*optional=*/true, "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the\n"
                                     "'send' category of transactions."},
                                {RPCResult::Type::STR_AMOUNT, "reward", /*optional=*/true, "The reward if the transaction is a coinstake."},
                            },
                            TransactionDescriptionString()),
                            {
                                {RPCResult::Type::BOOL, "abandoned", "'true' if the transaction has been abandoned (inputs are respendable)."},
                                {RPCResult::Type::STR, "label", /*optional=*/true, "A comment for the address/transaction, if any"},
                            })},
                        }},
                        {RPCResult::Type::ARR, "removed", /*optional=*/true, "<structure is the same as \"transactions\" above, only present if include_removed=true>\n"
                            "Note: transactions that were re-added in the active chain will appear as-is in this array, and may thus have a positive confirmation count."
                        , {{RPCResult::Type::ELISION, "", ""},}},
                        {RPCResult::Type::STR_HEX, "lastblock", "The hash of the block (target_confirmations-1) from the best block on the main chain, or the genesis hash if the referenced block does not exist yet. This is typically used to feed back into listsinceblock the next time you call it. So you would generally use a target_confirmations of say 6, so you will be continually re-notified of transactions until they've reached 6 confirmations plus any new ones"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listsinceblock", "")
            + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
            + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    const CWallet& wallet = *pwallet;
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    wallet.BlockUntilSyncedToCurrentChain();

    LOCK(wallet.cs_wallet);

    std::optional<int> height;    // Height of the specified block or the common ancestor, if the block provided was in a deactivated chain.
    std::optional<int> altheight; // Height of the specified block, even if it's in a deactivated chain.
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    uint256 blockId;
    if (!request.params[0].isNull() && !request.params[0].get_str().empty()) {
        blockId = ParseHashV(request.params[0], "blockhash");
        height = int{};
        altheight = int{};
        if (!wallet.chain().findCommonAncestor(blockId, wallet.GetLastBlockHash(), /*ancestor_out=*/FoundBlock().height(*height), /*block1_out=*/FoundBlock().height(*altheight))) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    if (!request.params[1].isNull()) {
        target_confirms = request.params[1].getInt<int>();

        if (target_confirms < 1) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }
    }

    if (ParseIncludeWatchonly(request.params[2], wallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    bool include_removed = (request.params[3].isNull() || request.params[3].get_bool());
    bool include_change = (!request.params[4].isNull() && request.params[4].get_bool());

    // Only set it if 'label' was provided.
    std::optional<std::string> filter_label;
    if (!request.params[5].isNull()) filter_label.emplace(LabelFromValue(request.params[5]));

    int depth = height ? wallet.GetLastBlockHeight() + 1 - *height : -1;

    UniValue transactions(UniValue::VARR);

    for (const std::pair<const uint256, CWalletTx>& pairWtx : wallet.mapWallet) {
        const CWalletTx& tx = pairWtx.second;

        if (depth == -1 || abs(wallet.GetTxDepthInMainChain(tx)) < depth) {
            ListTransactions(wallet, tx, 0, true, transactions, filter, filter_label, include_change);
        }
    }

    if (IsParticlWallet(&wallet)) {
        const CHDWallet *phdw = GetParticlWallet(&wallet);
        LOCK_ASSERTION(phdw->cs_wallet);

        for (const auto &ri : phdw->mapRecords) {
            const uint256 &txhash = ri.first;
            const CTransactionRecord &rtx = ri.second;
            if (depth == -1 || phdw->GetDepthInMainChain(rtx) < depth) {
                ListRecord(phdw, txhash, rtx, "*", 0, true, transactions, filter);
            }
        }
    }


    // when a reorg'd block is requested, we also list any relevant transactions
    // in the blocks of the chain that was detached
    UniValue removed(UniValue::VARR);
    while (include_removed && altheight && *altheight > *height) {
        CBlock block;
        if (!wallet.chain().findBlock(blockId, FoundBlock().data(block)) || block.IsNull()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }
        for (const CTransactionRef& tx : block.vtx) {
            auto it = wallet.mapWallet.find(tx->GetHash());
            if (it != wallet.mapWallet.end()) {
                // We want all transactions regardless of confirmation count to appear here,
                // even negative confirmation ones, hence the big negative.
                ListTransactions(wallet, it->second, -100000000, true, removed, filter, filter_label, include_change);
            } else
            if (IsParticlWallet(&wallet)) {
                const CHDWallet *phdw = GetParticlWallet(&wallet);
                LOCK_ASSERTION(phdw->cs_wallet);
                const uint256 &txhash = tx->GetHash();
                MapRecords_t::const_iterator mri = phdw->mapRecords.find(txhash);
                if (mri != phdw->mapRecords.end()) {
                    const CTransactionRecord &rtx = mri->second;
                    ListRecord(phdw, txhash, rtx, "*", -100000000, true, removed, filter);
                }
            }
        }
        blockId = block.hashPrevBlock;
        --*altheight;
    }

    uint256 lastblock;
    target_confirms = std::min(target_confirms, wallet.GetLastBlockHeight() + 1);
    CHECK_NONFATAL(wallet.chain().findAncestorByHeight(wallet.GetLastBlockHash(), wallet.GetLastBlockHeight() + 1 - target_confirms, FoundBlock().hash(lastblock)));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("transactions", transactions);
    if (include_removed) ret.pushKV("removed", removed);
    ret.pushKV("lastblock", lastblock.GetHex());

    return ret;
},
    };
}

RPCHelpMan gettransaction()
{
    return RPCHelpMan{"gettransaction",
                "\nGet detailed information about in-wallet transaction <txid>\n",
                {
                    {"txid", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction id"},
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::DefaultHint{"true for watch-only wallets, otherwise false"},
                            "Whether to include watch-only addresses in balance calculation and details[]"},
                    {"verbose", RPCArg::Type::BOOL, RPCArg::Default{false},
                            "Whether to include a `decoded` field containing the decoded transaction (equivalent to RPC decoderawtransaction)"},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", Cat(Cat<std::vector<RPCResult>>(
                    {
                        {RPCResult::Type::STR_AMOUNT, "amount", /*optional=*/true, "The amount in " + CURRENCY_UNIT},
                        {RPCResult::Type::STR_AMOUNT, "fee", /*optional=*/true, "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the\n"
                                     "'send' category of transactions."},
                        {RPCResult::Type::STR, "type_in", /*optional=*/true, "The balance type of the transaction inputs: plain/blind/anon/coinbase"},
                    },
                    TransactionDescriptionString()),
                    {
                        {RPCResult::Type::ARR, "details", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::BOOL, "involvesWatchonly", /*optional=*/true, "Only returns true if imported addresses were involved in transaction."},
                                {RPCResult::Type::STR, "address", /*optional=*/true, "The particl address involved in the transaction."},
                                {RPCResult::Type::STR, "stealth_address", /*optional=*/true, "The stealth address the transaction was received on."},
                                {RPCResult::Type::STR, "coldstake_address", /*optional=*/true, "The address the transaction is staking on."},
                                {RPCResult::Type::STR, "category", "The transaction category.\n"
                                    "\"send\"                  Transactions sent.\n"
                                    "\"receive\"               Non-coinbase transactions received.\n"
                                    "\"generate\"              Coinbase transactions received with more than 100 confirmations.\n"
                                    "\"immature\"              Coinbase transactions received with 100 or fewer confirmations.\n"
                                    "\"orphan\"                Orphaned coinbase transactions received."},
                                {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                                {RPCResult::Type::STR, "label", /*optional=*/true, "A comment for the address/transaction, if any"},
                                {RPCResult::Type::STR, "account", /*optional=*/true, "Duplicate of \"label\", requested by exchanges."},
                                {RPCResult::Type::NUM, "vout", "the vout value"},
                                {RPCResult::Type::STR_AMOUNT, "fee", /*optional=*/true, "The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
                                    "'send' category of transactions."},
                                {RPCResult::Type::STR_AMOUNT, "reward", /*optional=*/true, "The block reward in " + CURRENCY_UNIT},
                                {RPCResult::Type::BOOL, "abandoned", /*optional=*/true, "'true' if the transaction has been abandoned (inputs are respendable)."},
                                {RPCResult::Type::ARR, "parent_descs", /*optional=*/true, "Only if 'category' is 'received'. List of parent descriptors for the scriptPubKey of this coin.", {
                                    {RPCResult::Type::STR, "desc", "The descriptor string."},
                                }},
                                {RPCResult::Type::STR, "type", /*optional=*/true, "anon/blind/standard."},
                                {RPCResult::Type::STR, "stealth_address", /*optional=*/true, "The stealth address the output was received on."},
                                {RPCResult::Type::STR, "narration", /*optional=*/true, "If a narration is embedded in the output, only present if not empty."},
                                {RPCResult::Type::BOOL, "fromself", /*optional=*/true, "True if this wallet owned an input of the transaction."},
                                {RPCResult::Type::BOOL, "requires_unlock", /*optional=*/true, "True if this wallet must be unlocked to decrypt output value."},
                            }},
                        }},
                        {RPCResult::Type::STR_HEX, "hex", "Raw data for transaction"},
                        {RPCResult::Type::OBJ, "decoded", /*optional=*/true, "The decoded transaction (only present when `verbose` is passed)",
                        {
                            {RPCResult::Type::ELISION, "", "Equivalent to the RPC decoderawtransaction method, or the RPC getrawtransaction method when `verbose` is passed."},
                        }},
                        RESULT_LAST_PROCESSED_BLOCK,
                        {RPCResult::Type::ARR, "smsgs_funded", /*optional=*/true, "", {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "smsghash", "Hash of SMSG being funded"},
                                {RPCResult::Type::STR_AMOUNT, "fee", "Funding fee."},
                            }},
                        }},
                        {RPCResult::Type::STR_AMOUNT, "smsg_fees", /*optional=*/true, "Total SMSG fees"},
                    })
                },
                RPCExamples{
                    HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" false true")
            + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    if (!request.fSkipBlock)
        pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    uint256 hash(ParseHashV(request.params[0], "txid"));

    isminefilter filter = ISMINE_SPENDABLE;

    if (ParseIncludeWatchonly(request.params[1], *pwallet)) {
        filter |= ISMINE_WATCH_ONLY;
    }

    bool verbose = request.params[2].isNull() ? false : request.params[2].get_bool();

    UniValue entry(UniValue::VOBJ);
    auto it = pwallet->mapWallet.find(hash);
    if (it == pwallet->mapWallet.end()) {
        if (IsParticlWallet(pwallet.get())) {
            const CHDWallet *phdw = GetParticlWallet(pwallet.get());
            LOCK_ASSERTION(phdw->cs_wallet);
            MapRecords_t::const_iterator mri = phdw->mapRecords.find(hash);

            if (mri != phdw->mapRecords.end()) {
                const CTransactionRecord &rtx = mri->second;
                RecordTxToJSON(pwallet->chain(), phdw, mri->first, rtx, entry, filter, verbose);
                entry.pushKV("abandoned", rtx.IsAbandoned());
                AppendLastProcessedBlock(entry, *pwallet);
                return entry;
            }
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    }
    const CWalletTx& wtx = it->second;

    CAmount nCredit = CachedTxGetCredit(*pwallet, wtx, filter);
    CAmount nDebit = CachedTxGetDebit(*pwallet, wtx, filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (CachedTxIsFromMe(*pwallet, wtx, filter) ? wtx.tx->GetValueOut() - nDebit : 0);

    CAmount nAmount = wtx.IsCoinStake() ? nFee : nNet - nFee;
    entry.pushKV("amount", ValueFromAmount(nAmount));
    entry.pushKV("type_in", wtx.IsCoinBase() ? "coinbase" : "plain");
    if (CachedTxIsFromMe(*pwallet, wtx, filter) && !wtx.IsCoinStake())
        entry.pushKV("fee", ValueFromAmount(nFee));

    WalletTxToJSON(*pwallet, wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(*pwallet, wtx, 0, false, details, filter, /*filter_label=*/std::nullopt);
    entry.pushKV("details", details);

    std::string strHex = EncodeHexTx(*wtx.tx, pwallet->chain().rpcSerializationFlags());
    entry.pushKV("hex", strHex);

    if (verbose) {
        UniValue decoded(UniValue::VOBJ);
        TxToUniv(*wtx.tx, /*block_hash=*/uint256(), /*entry=*/decoded, /*include_hex=*/false);
        entry.pushKV("decoded", decoded);
    }
    AddSmsgFundingInfo(*wtx.tx, entry);

    AppendLastProcessedBlock(entry, *pwallet);
    return entry;
},
    };
}

RPCHelpMan abandontransaction()
{
    return RPCHelpMan{"abandontransaction",
                "\nMark in-wallet transaction <txid> as abandoned\n"
                "This will mark this transaction and all its in-wallet descendants as abandoned which will allow\n"
                "for their inputs to be respent.  It can be used to replace \"stuck\" or evicted transactions.\n"
                "It only works on transactions which are not included in a block and are not currently in the mempool.\n"
                "It has no effect on transactions which are already abandoned.\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleRpc("abandontransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    uint256 hash(ParseHashV(request.params[0], "txid"));

    if (!pwallet->mapWallet.count(hash)) {
        if (!IsParticlWallet(pwallet.get())) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
        }
        CHDWallet *phdw = GetParticlWallet(pwallet.get());
        if (!phdw) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
        }
        LOCK_ASSERTION(phdw->cs_wallet);
        if (!phdw->HaveTransaction(hash)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
        }
    }
    if (!pwallet->AbandonTransaction(hash)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not eligible for abandonment");
    }

    return UniValue::VNULL;
},
    };
}

RPCHelpMan rescanblockchain()
{
    return RPCHelpMan{"rescanblockchain",
                "\nRescan the local blockchain for wallet related transactions.\n"
                "Note: Use \"getwalletinfo\" to query the scanning progress.\n"
                "The rescan is significantly faster when used on a descriptor wallet\n"
                "and block filters are available (using startup option \"-blockfilterindex=1\").\n",
                {
                    {"start_height", RPCArg::Type::NUM, RPCArg::Default{0}, "block height where the rescan should start"},
                    {"stop_height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "the last block height that should be scanned. If none is provided it will rescan up to the tip at return time of this call."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::NUM, "start_height", "The block height where the rescan started (the requested height or 0)"},
                        {RPCResult::Type::NUM, "stop_height", "The height of the last rescanned block. May be null in rare cases if there was a reorg and the call didn't scan any blocks because they were already scanned in the background."},
                    }
                },
                RPCExamples{
                    HelpExampleCli("rescanblockchain", "100000 120000")
            + HelpExampleRpc("rescanblockchain", "100000, 120000")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;
    CWallet& wallet{*pwallet};

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    wallet.BlockUntilSyncedToCurrentChain();

    WalletRescanReserver reserver(*pwallet);
    if (!reserver.reserve(/*with_passphrase=*/true)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    int start_height = 0;
    std::optional<int> stop_height;
    uint256 start_block;

    LOCK(pwallet->m_relock_mutex);
    {
        LOCK(pwallet->cs_wallet);
        EnsureWalletIsUnlocked(*pwallet);
        int tip_height = pwallet->GetLastBlockHeight();

        if (!request.params[0].isNull()) {
            start_height = request.params[0].getInt<int>();
            if (start_height < 0 || start_height > tip_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid start_height");
            }
        }

        if (!request.params[1].isNull()) {
            stop_height = request.params[1].getInt<int>();
            if (*stop_height < 0 || *stop_height > tip_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid stop_height");
            } else if (*stop_height < start_height) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "stop_height must be greater than start_height");
            }
        }

        // We can't rescan beyond non-pruned blocks, stop and throw an error
        if (!pwallet->chain().hasBlocks(pwallet->GetLastBlockHash(), start_height, stop_height)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Can't rescan beyond pruned data. Use RPC call getblockchaininfo to determine your pruned height.");
        }

        CHECK_NONFATAL(pwallet->chain().findAncestorByHeight(pwallet->GetLastBlockHash(), start_height, FoundBlock().hash(start_block)));
    }

    CWallet::ScanResult result =
        pwallet->ScanForWalletTransactions(start_block, start_height, stop_height, reserver, /*fUpdate=*/true, /*save_progress=*/false);
    switch (result.status) {
    case CWallet::ScanResult::SUCCESS:
        break;
    case CWallet::ScanResult::FAILURE:
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan failed. Potentially corrupted data files.");
    case CWallet::ScanResult::USER_ABORT:
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted.");
        // no default case, so the compiler can warn about missing cases
    }
    UniValue response(UniValue::VOBJ);
    response.pushKV("start_height", start_height);
    response.pushKV("stop_height", result.last_scanned_height ? *result.last_scanned_height : UniValue());
    return response;
},
    };
}

RPCHelpMan abortrescan()
{
    return RPCHelpMan{"abortrescan",
                "\nStops current wallet rescan triggered by an RPC call, e.g. by an importprivkey call.\n"
                "Note: Use \"getwalletinfo\" to query the scanning progress.\n",
                {},
                RPCResult{RPCResult::Type::BOOL, "", "Whether the abort was successful"},
                RPCExamples{
            "\nImport a private key\n"
            + HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nAbort the running wallet rescan\n"
            + HelpExampleCli("abortrescan", "") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("abortrescan", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    if (!pwallet->IsScanning() || pwallet->IsAbortingRescan()) return false;
    pwallet->AbortRescan();
    return true;
},
    };
}
} // namespace wallet
