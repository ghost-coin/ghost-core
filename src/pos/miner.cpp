// Copyright (c) 2017-2023 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos/miner.h>

#include <pos/kernel.h>
#include <node/miner.h>
#include <chainparams.h>
#include <util/thread.h>
#include <util/syserror.h>
#include <util/moneystr.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <sync.h>
#include <net.h>
#include <timedata.h>
#include <consensus/validation.h>
#include <node/blockstorage.h>
#include <validation.h>

#include <wallet/hdwallet.h>
#include <wallet/spend.h>

#include <stdint.h>

typedef CWallet* CWalletRef;
std::vector<StakeThread*> vStakeThreads;

std::atomic<bool> fStopMinerProc(false);
std::atomic<bool> fTryToSync(false);
std::atomic<bool> fIsStaking(false);


int nMinStakeInterval = 0;  // Minimum stake interval in seconds
int nMinerSleep = 500;  // In milliseconds
std::atomic<int64_t> nTimeLastStake(0);

bool CheckStake(ChainstateManager &chainman, const CBlock *pblock)
{
    uint256 proofHash, hashTarget;
    uint256 hashBlock = pblock->GetHash();

    if (!pblock->IsProofOfStake()) {
        return error("%s: %s is not a proof-of-stake block.", __func__, hashBlock.GetHex());
    }

    if (!particl::CheckStakeUnique(*pblock, false)) { // Check in SignBlock also
        return error("%s: %s CheckStakeUnique failed.", __func__, hashBlock.GetHex());
    }

    // Verify hash target and signature of coinstake tx
    {
        LOCK(cs_main);

        node::BlockMap::const_iterator mi = chainman.BlockIndex().find(pblock->hashPrevBlock);
        if (mi == chainman.BlockIndex().end()) {
            return error("%s: %s prev block not found: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
        }

        if (!chainman.ActiveChain().Contains(&mi->second)) {
            return error("%s: %s prev block in active chain: %s.", __func__, hashBlock.GetHex(), pblock->hashPrevBlock.GetHex());
        }

        BlockValidationState state;
        if (!CheckProofOfStake(chainman.ActiveChainstate(), state, &mi->second, *pblock->vtx[0], pblock->nTime, pblock->nBits, proofHash, hashTarget)) {
            return error("%s: proof-of-stake checking failed.", __func__);
        }
        if (pblock->hashPrevBlock != chainman.ActiveChain().Tip()->GetBlockHash()) { // hashbestchain
            return error("%s: Generated block is stale.", __func__);
        }
    }

    // debug print
    LogPrintf("CheckStake(): New proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", hashBlock.GetHex(), proofHash.GetHex(), hashTarget.GetHex());
    if (LogAcceptCategory(BCLog::POS, BCLog::Level::Debug)) {
        LogPrintf("block %s\n", pblock->ToString());
        LogPrintf("out %s\n", FormatMoney(pblock->vtx[0]->GetValueOut()));
    }

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!chainman.ProcessNewBlock(shared_pblock, true, /*min_pow_checked=*/true, nullptr)) {
        return error("%s: Block not accepted.", __func__);
    }

    return true;
}

bool ImportOutputs(node::CBlockTemplate *pblocktemplate, int nHeight)
{
    LogPrint(BCLog::POS, "%s, nHeight %d\n", __func__, nHeight);

    CBlock *pblock = &pblocktemplate->block;
    if (pblock->vtx.size() < 1) {
        return error("%s: Malformed block.", __func__);
    }

    fs::path fPath = gArgs.GetDataDirNet() / "genesisOutputs.txt";
    if (!fs::exists(fPath)) {
        return error("%s: File not found 'genesisOutputs.txt'.", __func__);
    }

    const int nMaxOutputsPerTxn = 80;
    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fs::PathToString(fPath).c_str(), "rb"))) {
        return error("%s - Can't open file, error: %s.", __func__, SysErrorString(errno));
    }

    CMutableTransaction txn;
    txn.nVersion = GHOST_TXN_VERSION;
    txn.SetType(TXN_COINBASE);
    txn.nLockTime = 0;
    txn.vin.push_back(CTxIn()); // null prevout

    // scriptsig len must be > 2
    const char *s = "import";
    txn.vin[0].scriptSig = CScript() << std::vector<unsigned char>((const unsigned char*)s, (const unsigned char*)s + strlen(s));

    int nOutput = 0, nAdded = 0;
    char cLine[512];
    char *pAddress, *pAmount, *token;

    while (fgets(cLine, 512, fp)) {
        cLine[511] = '\0'; // safety
        size_t len = strlen(cLine);
        while (IsSpace(cLine[len-1]) && len>0) {
            cLine[len-1] = '\0', len--;
        }

        if (!(pAddress = strtok_r(cLine, ",", &token)) ||
            !(pAmount = strtok_r(nullptr, ",", &token))) {
            continue;
        }

        nOutput++;
        if (nOutput <= nMaxOutputsPerTxn * (nHeight-1)) {
            continue;
        }

        uint64_t amount;
        if (!ParseUInt64(std::string(pAmount), &amount) || !MoneyRange(amount)) {
            LogPrintf("Warning: %s - Skipping invalid amount: %s, %s\n", __func__, pAmount, SysErrorString(errno));
            continue;
        }

        std::string addrStr(pAddress);
        CBitcoinAddress addr(addrStr);

        CKeyID id;
        if (!addr.IsValid() ||
            !addr.GetKeyID(id)) {
            LogPrintf("Warning: %s - Skipping invalid address: %s\n", __func__, pAddress);
            continue;
        }

        CScript script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(id) << OP_EQUALVERIFY << OP_CHECKSIG;
        OUTPUT_PTR<CTxOutStandard> txout = MAKE_OUTPUT<CTxOutStandard>();
        txout->nValue = amount;
        txout->scriptPubKey = script;
        txn.vpout.push_back(txout);

        nAdded++;
        if (nAdded >= nMaxOutputsPerTxn) {
            break;
        }
    }

    fclose(fp);
    pblock->vtx.insert(pblock->vtx.begin()+1, MakeTransactionRef(txn));

    return true;
}

void StartThreadStakeMiner(wallet::WalletContext &wallet_context, ChainstateManager &chainman)
{
    nMinStakeInterval = gArgs.GetIntArg("-minstakeinterval", 0);
    nMinerSleep = gArgs.GetIntArg("-minersleep", 500);

    if (!gArgs.GetBoolArg("-staking", true)) {
        LogPrintf("Staking disabled\n");
    } else {
        auto vpwallets = GetWallets(wallet_context);
        size_t nWallets = vpwallets.size();

        if (nWallets < 1) {
            return;
        }
        size_t nThreads = std::min(nWallets, (size_t)gArgs.GetIntArg("-stakingthreads", 1));

        size_t nPerThread = nWallets / nThreads;
        for (size_t i = 0; i < nThreads; ++i) {
            size_t nStart = nPerThread * i;
            size_t nEnd = (i == nThreads-1) ? nWallets : nPerThread * (i+1);
            StakeThread *t = new StakeThread();
            vStakeThreads.push_back(t);
            GetParticlWallet(vpwallets[i].get())->nStakeThread = i;
            t->sName = strprintf("miner%d", i);
            t->thread = std::thread(&util::TraceThread, t->sName.c_str(), std::function<void()>(std::bind(&ThreadStakeMiner, i, vpwallets, nStart, nEnd, &chainman)));
        }
    }

    fStopMinerProc = false;
}

void StopThreadStakeMiner()
{
    if (vStakeThreads.size() < 1 || // no thread created
        fStopMinerProc) {
        return;
    }
    LogPrint(BCLog::POS, "StopThreadStakeMiner\n");
    fStopMinerProc = true;

    for (auto t : vStakeThreads) {
        t->m_thread_interrupt();
        t->thread.join();
        delete t;
    }
    vStakeThreads.clear();
}

void WakeThreadStakeMiner(CHDWallet *pwallet)
{
    size_t nStakeThread = 0;
    {
    LOCK(pwallet->cs_wallet);
    nStakeThread = pwallet->nStakeThread;
    if (nStakeThread >= vStakeThreads.size() || pwallet->IsScanning()) {
        return;
    }
    pwallet->nLastCoinStakeSearchTime = 0;
    LogPrint(BCLog::POS, "WakeThreadStakeMiner: wallet %s, thread %d\n", pwallet->GetName(), nStakeThread);
    }
    StakeThread *t = vStakeThreads[nStakeThread];
    t->m_thread_interrupt();
}

void WakeAllThreadStakeMiner()
{
    LogPrint(BCLog::POS, "WakeAllThreadStakeMiner\n");
    for (auto t : vStakeThreads) {
        t->m_thread_interrupt();
    }
}

bool ThreadStakeMinerStopped()
{
    return fStopMinerProc;
}

static inline void condWaitFor(size_t nThreadID, int ms)
{
    assert(vStakeThreads.size() > nThreadID);
    StakeThread *t = vStakeThreads[nThreadID];
    t->m_thread_interrupt.reset();
    t->m_thread_interrupt.sleep_for(std::chrono::milliseconds(ms));
}

void ThreadStakeMiner(size_t nThreadID, std::vector<std::shared_ptr<wallet::CWallet>> &vpwallets, size_t nStart, size_t nEnd, ChainstateManager *chainman)
{
    LogPrintf("Starting staking thread %d, %d wallet%s.\n", nThreadID, nEnd - nStart, (nEnd - nStart) > 1 ? "s" : "");

    int nBestHeight; // TODO: set from new block signal?
    int64_t nBestTime;

    int nLastImportHeight = Params().GetLastImportHeight();

    if (!gArgs.GetBoolArg("-staking", true)) {
        LogPrint(BCLog::POS, "%s: -staking is false.\n", __func__);
        return;
    }

    const size_t stake_thread_cond_delay_ms = gArgs.GetIntArg("-stakethreadconddelayms", 60000);
    const bool check_peer_height = gArgs.GetBoolArg("-checkpeerheight", true);
    LogPrint(BCLog::POS, "Stake thread conditional delay set to %d.\n", stake_thread_cond_delay_ms);

    while (!fStopMinerProc) {
        if (node::fReindex || chainman->m_blockman.m_importing || particl::fBusyImporting) {
            fIsStaking = false;
            LogPrint(BCLog::POS, "%s: Block import/reindex.\n", __func__);
            condWaitFor(nThreadID, 30000);
            continue;
        }

        int num_blocks_of_peers, num_nodes;
        {
            LOCK(cs_main);
            nBestHeight = chainman->ActiveChain().Height();
            nBestTime = chainman->ActiveChain().Tip()->nTime;
            num_blocks_of_peers = particl::GetNumBlocksOfPeers();
            num_nodes = particl::GetNumPeers();
        }

        if (fTryToSync) {
            fTryToSync = false;
            if (num_nodes < 3 || nBestHeight < num_blocks_of_peers) {
                fIsStaking = false;
                LogPrint(BCLog::POS, "%s: TryToSync\n", __func__);
                condWaitFor(nThreadID, 30000);
                continue;
            }
        }

        if (check_peer_height && (num_nodes == 0 || chainman->ActiveChainstate().IsInitialBlockDownload())) {
            fIsStaking = false;
            fTryToSync = true;
            LogPrint(BCLog::POS, "%s: IsInitialBlockDownload\n", __func__);
            condWaitFor(nThreadID, 2000);
            continue;
        }

        if (check_peer_height && nBestHeight < num_blocks_of_peers - 1) {
            fIsStaking = false;
            LogPrint(BCLog::POS, "%s: nBestHeight < GetNumBlocksOfPeers(), %d, %d\n", __func__, nBestHeight, num_blocks_of_peers);
            condWaitFor(nThreadID, nMinerSleep * 4);
            continue;
        }

        if (nMinStakeInterval > 0 && nTimeLastStake + (int64_t)nMinStakeInterval > GetTime()) {
            LogPrint(BCLog::POS, "%s: Rate limited to 1 / %d seconds.\n", __func__, nMinStakeInterval);
            condWaitFor(nThreadID, nMinStakeInterval * 500); // nMinStakeInterval / 2 seconds
            continue;
        }

        int64_t nTime = GetAdjustedTimeInt();
        int64_t nMask = Params().GetStakeTimestampMask(nBestHeight + 1);
        int64_t nSearchTime = nTime & ~nMask;
        if (nSearchTime <= nBestTime) {
            if (nTime < nBestTime) {
                LogPrint(BCLog::POS, "%s: Can't stake before last block time.\n", __func__);
                condWaitFor(nThreadID, std::min(1000 + (nBestTime - nTime) * 1000, (int64_t)30000));
                continue;
            }

            int64_t nNextSearch = nSearchTime + nMask;
            condWaitFor(nThreadID, std::min(nMinerSleep + (nNextSearch - nTime) * 1000, (int64_t)10000));
            continue;
        }

        std::unique_ptr<node::CBlockTemplate> pblocktemplate;

        size_t nWaitFor = stake_thread_cond_delay_ms;
        CAmount reserve_balance;

        for (size_t i = nStart; i < nEnd; ++i) {
            auto pwallet = GetParticlWallet(vpwallets[i].get());

            if (!pwallet->fStakingEnabled) {
                pwallet->m_is_staking = CHDWallet::NOT_STAKING_DISABLED;
                continue;
            }

            {
            LOCK(pwallet->cs_wallet);
            if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
                nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                continue;
            }

            if (pwallet->nStakeLimitHeight && nBestHeight >= pwallet->nStakeLimitHeight) {
                pwallet->m_is_staking = CHDWallet::NOT_STAKING_LIMITED;
                nWaitFor = std::min(nWaitFor, (size_t)30000);
                continue;
            }

            if (pwallet->IsLocked()) {
                pwallet->m_is_staking = CHDWallet::NOT_STAKING_LOCKED;
                nWaitFor = std::min(nWaitFor, (size_t)30000);
                continue;
            }
            reserve_balance = pwallet->nReserveBalance;
            }
            CAmount balance = pwallet->GetSpendableBalance();

            if (balance <= reserve_balance) {
                LOCK(pwallet->cs_wallet);
                pwallet->m_is_staking = CHDWallet::NOT_STAKING_BALANCE;
                nWaitFor = std::min(nWaitFor, (size_t)60000);
                pwallet->nLastCoinStakeSearchTime = nSearchTime + stake_thread_cond_delay_ms / 1000;
                LogPrint(BCLog::POS, "%s: %s, low balance.\n", __func__, pwallet->GetName());
                continue;
            }

            if (!pblocktemplate.get()) {
                pblocktemplate = pwallet->CreateNewBlock();
                if (!pblocktemplate.get()) {
                    fIsStaking = false;
                    nWaitFor = std::min(nWaitFor, (size_t)nMinerSleep);
                    LogPrint(BCLog::POS, "%s: Couldn't create new block.\n", __func__);
                    continue;
                }

                if (nBestHeight + 1 <= nLastImportHeight &&
                    !ImportOutputs(pblocktemplate.get(), nBestHeight + 1)) {
                    fIsStaking = false;
                    nWaitFor = std::min(nWaitFor, (size_t)30000);
                    LogPrint(BCLog::POS, "%s: ImportOutputs failed.\n", __func__);
                    continue;
                }
            }
            pwallet->m_is_staking = CHDWallet::IS_STAKING;

            nWaitFor = nMinerSleep;
            fIsStaking = true;
            if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight + 1, nSearchTime)) {
                CBlock *pblock = &pblocktemplate->block;
                if (CheckStake(*chainman, pblock)) {
                     nTimeLastStake = GetTime();
                     break;
                }
            } else {
                int nRequiredDepth = std::min((int)(Params().GetStakeMinConfirmations() - 1), (int)(nBestHeight / 2));
                LOCK(pwallet->cs_wallet);
                if (pwallet->m_greatest_txn_depth < nRequiredDepth - 4) {
                    pwallet->m_is_staking = CHDWallet::NOT_STAKING_DEPTH;
                    size_t nSleep = (nRequiredDepth - pwallet->m_greatest_txn_depth) / 4;
                    nWaitFor = std::min(nWaitFor, (size_t)(nSleep * 1000));
                    pwallet->nLastCoinStakeSearchTime = nSearchTime + nSleep;
                    LogPrint(BCLog::POS, "%s: %s, no outputs with required depth. Sleeping for %ds.\n", __func__, pwallet->GetName(), nSleep);
                    continue;
                }
            }
        }

        condWaitFor(nThreadID, nWaitFor);
    }
}
