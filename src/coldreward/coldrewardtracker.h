#ifndef COLDREWARDTRACKER_H
#define COLDREWARDTRACKER_H

#include "blockheightrange.h"
#include <boost/optional.hpp>
#include <functional>
#include <map>
#include <vector>

#include "consensus/amount.h"
#include "uint256.h"

#include "serialize.h"

/**
 * @brief The ColdRewardTracker class
 *
 * GVR: Ghost Veteran Payment
 *
 * This class is made as an interface and wrapper around the functionality that checks whether an address is eligible for a reward
 * over a long range of blocks.
 *
 * The way this works is by defining time intervals, or ranges, for addresses that own over 20k GHOST over certain periods of time.
 * A range is a period of time, in block height. The range [0,10] is the timespan between block 0 and block 10.
 *
 * For every address change in balance, we add that address and the block height that did that change.
 * This information is stored in two forms:
 * 1. The balance of every address on the blockchain
 * 2. The set of ranges where an address had 20k+ GHOST
 *
 * The vector addressesRanges stores the ranges mentioned above. here are some examples on how this looks like:
 * {} (an empty vector): This address's balance never went over 20k
 * {[10,10]}: This address's balance changed at block 10 and became 20k+
 * {[10,100]}: This address's balance changed at block 10 and became 20k+, changed again at block 100 but is still 20k+
 * {[10,10],[100,100]}: This address's balance changed at block 10 and became 20k+, but then at block 100, it's not anymore 20k+
 *
 * Notice that the existence of a [A,A] entry means that there's an interruption in the balance being 20k+.
 * No period where balance is below 20k is recorded.
 *
 * To get a vector of addresses eligible for GVR, we call the function getEligibleAddresses()
 *
 * Transactional nature:
 * For performance reasons, this class has an internal cache for balances and for ranges. The developer using this class
 * has to define the way balances (of a certain address) are retrieved and stored in the database. Same for ranges.
 * For this, there are setter functions for the setters and getters of these.
 * To aid performance, all these actions should be done within a transaction.
 *
 * To do any change, do the following:
 * 1. call startPersistedTransaction()
 * 2. call addAddressTransaction() or removeAddressTransaction() based on your needs, to add store changes in balances
 * 3a. if you want to persist the cached results, call endPersistedTransaction()
 * 3b. if you want to revert the result, call revertPersistedTransaction()
 *
 */
class ColdRewardTracker
{
public:
    using AddressType = std::vector<uint8_t>;

    CAmount GVRThreshold;
    int MinimumRewardRangeSpan;

private:
    std::map<AddressType, std::vector<BlockHeightRange>> addressesRanges;
    std::map<AddressType, CAmount> balances;
    boost::optional<int> lastCheckpoint;

    std::function<CAmount(const AddressType&)> balanceGetter;
    std::function<void(const AddressType&, const CAmount&)> balanceSetter;

    std::function<std::vector<BlockHeightRange>(const AddressType&)> rangesGetter;
    std::function<void(const AddressType&, const std::vector<BlockHeightRange>&)> rangesSetter;

    /// retrieves and stores the last checkpoint in the database. If not found, return 0
    std::function<int()> checkpointGetter;
    std::function<void(int)> checkpointSetter;

    std::function<void()> transactionStarter;
    std::function<void()> transactionEnder;

    std::function<std::map<AddressType, std::vector<BlockHeightRange>>()> allRangesGetter;

protected:
    boost::optional<CAmount> getBalanceInCache(const AddressType& addr);
    boost::optional<std::vector<BlockHeightRange>> getAddressRangesInCache(const AddressType& addr);
    CAmount getBalance(const AddressType& addr);
    std::vector<BlockHeightRange> getAddressRanges(const AddressType& addr);
    void updateAddressRangesCache(const AddressType& addr, std::vector<BlockHeightRange>&& ranges);
    boost::optional<int> getCheckpointInCache();
    void updateCheckpointCache(int new_checkpoint);
    int getCheckpoint();


    static void AssertTrue(bool valueShouldBeTrue, const std::string& functionName, const std::string& msg);
    void RemoveOldData(int lastCheckpoint, std::vector<BlockHeightRange>& ranges);

public:
    ColdRewardTracker() = default;

    ColdRewardTracker(CAmount threshold, int rewardSpan) : GVRThreshold(threshold), MinimumRewardRangeSpan(rewardSpan){}

    void startPersistedTransaction();
    void endPersistedTransaction();
    void revertPersistedTransaction();

    static boost::optional<int> GetLastCheckpoint(const std::map<int, uint256>& checkpoints, int currentBlockHeight);
    /// Given a set of ranges of an address, this gives all the multipliers that have to do with the reward at currentBlockHeight
    unsigned ExtractRewardMultiplierFromRanges(int currentBlockHeight, const std::vector<BlockHeightRange>& addressRanges);


    std::vector<std::pair<AddressType, unsigned>> getEligibleAddresses(int currentBlockHeight);

    void addAddressTransaction(int blockHeight, const AddressType& address, const CAmount& balanceChange, const std::map<int, uint256>& checkpoints);
    void removeAddressTransaction(int blockHeight, const AddressType& address, const CAmount& balanceChangeInBlock);

    void setPersistedBalanceGetter(const std::function<CAmount(const AddressType&)>& func);
    void setPersistedBalanceSetter(const std::function<void(const AddressType&, const CAmount&)>& func);

    void setPersistedRangesGetter(const std::function<std::vector<BlockHeightRange>(const AddressType&)>& func);
    void setPersistedRangesSetter(const std::function<void(const AddressType&, const std::vector<BlockHeightRange>&)>& func);

    void setPersistedTransactionStarter(const std::function<void()>& func);
    void setPersisterTransactionEnder(const std::function<void()>& func);

    void setPersistedCheckpointGetter(const std::function<int()>& func);
    void setPersistedCheckpointSetter(const std::function<void(int)>& func);

    void setAllRangesGetter(const std::function<std::map<AddressType, std::vector<BlockHeightRange>>()>& func);

    const std::map<AddressType, std::vector<BlockHeightRange>>& getAllRanges() const;
    const std::vector<std::pair<AddressType, CAmount>> getBalances();

    void setGvrThreshold(const CAmount& amount) { 
        GVRThreshold = amount;
    }

    void setMinRewardRangeSpan(int reward) {
        MinimumRewardRangeSpan = reward;
    }

    SERIALIZE_METHODS(ColdRewardTracker, obj)
    {
        READWRITE(obj.addressesRanges);
        READWRITE(obj.balances);
        READWRITE(obj.lastCheckpoint);
    }
};

// This is used to store undo data
struct ColdRewardUndo {
    using AddressType = std::vector<uint8_t>;

    std::map<int, std::vector<std::pair<AddressType, CAmount>> > inputs;
    std::map<int, std::vector<std::pair<AddressType, CAmount>> > outputs;

    SERIALIZE_METHODS(ColdRewardUndo, obj){
        READWRITE(obj.inputs);
        READWRITE(obj.outputs);
    }

};

#endif // COLDREWARDTRACKER_H
