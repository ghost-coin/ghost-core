#include "blockheightrange.h"
#include <logging.h>
#include <stdexcept>

int BlockHeightRange::getEnd() const
{
    return end;
}

int BlockHeightRange::getStart() const
{
    return start;
}

unsigned BlockHeightRange::getRewardMultiplier() const
{
    return rewardMultiplier;
}

unsigned BlockHeightRange::getPrevRewardMultiplier() const
{
    return prevMultiplier;
}

void BlockHeightRange::newEnd(const int value)
{
    end = value;
    if(start > end) {
        LogPrintf("%s INVALID RANGE ", __func__);
        throw std::runtime_error("Invalid range: [" + std::to_string(start) + "," + std::to_string(end) + "]");
    }
}

void BlockHeightRange::newStart(const int value) {
    start = value;
    if(start > end) {
        LogPrintf("%s INVALID RANGE \n", __func__);
        throw std::runtime_error("Invalid range: [" + std::to_string(start) + "," + std::to_string(end) + "]");
    }
}

BlockHeightRange::BlockHeightRange(const int Start, const int End, const unsigned RewardMultiplier, unsigned PrevRewardMultiplier)
    : start(Start),
      end(End),
      rewardMultiplier(RewardMultiplier),
      prevMultiplier(PrevRewardMultiplier)
{
    if(Start > End) {
        LogPrintf("%s INVALID RANGE \n", __func__);
        throw std::runtime_error("Invalid range: [" + std::to_string(Start) + "," + std::to_string(End) + "]");
    }
}
