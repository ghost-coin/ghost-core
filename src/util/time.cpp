// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <compat/compat.h>
#include <tinyformat.h>
#include <util/time.h>
#include <util/check.h>

#include <util/check.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <locale>
#include <thread>
#include <sstream>
#include <string>
#include <cstring>

void UninterruptibleSleep(const std::chrono::microseconds& n) { std::this_thread::sleep_for(n); }

static std::atomic<int64_t> nMockTime(0); //!< For testing
static std::atomic<bool> mockTimeOffset(false); //!< Treat nMockTime as an offset

bool ChronoSanityCheck()
{
    // std::chrono::system_clock.time_since_epoch and time_t(0) are not guaranteed
    // to use the Unix epoch timestamp, prior to C++20, but in practice they almost
    // certainly will. Any differing behavior will be assumed to be an error, unless
    // certain platforms prove to consistently deviate, at which point we'll cope
    // with it by adding offsets.

    // Create a new clock from time_t(0) and make sure that it represents 0
    // seconds from the system_clock's time_since_epoch. Then convert that back
    // to a time_t and verify that it's the same as before.
    const time_t time_t_epoch{};
    auto clock = std::chrono::system_clock::from_time_t(time_t_epoch);
    if (std::chrono::duration_cast<std::chrono::seconds>(clock.time_since_epoch()).count() != 0) {
        return false;
    }

    time_t time_val = std::chrono::system_clock::to_time_t(clock);
    if (time_val != time_t_epoch) {
        return false;
    }

    // Check that the above zero time is actually equal to the known unix timestamp.
    struct tm epoch;
#ifdef HAVE_GMTIME_R
    if (gmtime_r(&time_val, &epoch) == nullptr) {
#else
    if (gmtime_s(&epoch, &time_val) != 0) {
#endif
        return false;
    }

    if ((epoch.tm_sec != 0)  ||
       (epoch.tm_min  != 0)  ||
       (epoch.tm_hour != 0)  ||
       (epoch.tm_mday != 1)  ||
       (epoch.tm_mon  != 0)  ||
       (epoch.tm_year != 70)) {
        return false;
    }
    return true;
}

NodeClock::time_point NodeClock::now() noexcept
{
    const std::chrono::seconds mocktime{nMockTime.load(std::memory_order_relaxed)};

    if (mockTimeOffset) {
        const auto ret{
            mocktime.count() ?
                mocktime :
                std::chrono::system_clock::now().time_since_epoch() - mocktime};
        assert(ret > 0s);
        return time_point{ret};
    }

    const auto ret{
        mocktime.count() ?
            mocktime :
            std::chrono::system_clock::now().time_since_epoch()};
    assert(ret > 0s);
    return time_point{ret};
};

template <typename T>
static T GetSystemTime()
{
    const auto now = std::chrono::duration_cast<T>(std::chrono::system_clock::now().time_since_epoch());
    assert(now.count() > 0);
    return now;
}

void SetMockTime(int64_t nMockTimeIn)
{
    Assert(nMockTimeIn >= 0);
    mockTimeOffset = false;
    Assert(nMockTimeIn >= 0);
    nMockTime.store(nMockTimeIn, std::memory_order_relaxed);
}

void SetMockTimeOffset(int64_t nMockTimeIn)
{
    mockTimeOffset = true;
    nMockTime.store(time(nullptr) - nMockTimeIn, std::memory_order_relaxed);
}

void SetMockTime(std::chrono::seconds mock_time_in)
{
    nMockTime.store(mock_time_in.count(), std::memory_order_relaxed);
}

std::chrono::seconds GetMockTime()
{
    return std::chrono::seconds(nMockTime.load(std::memory_order_relaxed));
}

int64_t GetTimeMillis()
{
    return int64_t{GetSystemTime<std::chrono::milliseconds>().count()};
}

int64_t GetTime() { return GetTime<std::chrono::seconds>().count(); }

std::string FormatISO8601DateTime(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#ifdef HAVE_GMTIME_R
    if (gmtime_r(&time_val, &ts) == nullptr) {
#else
    if (gmtime_s(&ts, &time_val) != 0) {
#endif
        return {};
    }
    return strprintf("%04i-%02i-%02iT%02i:%02i:%02iZ", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec);
}

std::string FormatISO8601Date(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#ifdef HAVE_GMTIME_R
    if (gmtime_r(&time_val, &ts) == nullptr) {
#else
    if (gmtime_s(&ts, &time_val) != 0) {
#endif
        return {};
    }
    return strprintf("%04i-%02i-%02i", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday);
}

struct timeval MillisToTimeval(int64_t nTimeout)
{
    struct timeval timeout;
    timeout.tv_sec  = nTimeout / 1000;
    timeout.tv_usec = (nTimeout % 1000) * 1000;
    return timeout;
}

struct timeval MillisToTimeval(std::chrono::milliseconds ms)
{
    return MillisToTimeval(count_milliseconds(ms));
}

namespace part
{
std::string GetTimeString(int64_t timestamp, char *buffer, size_t nBuffer)
{
    struct tm* dt;
    time_t t = timestamp;
    dt = localtime(&t);

    strftime(buffer, nBuffer, "%Y-%m-%dT%H:%M:%S%z", dt); // %Z shows long strings on windows
    return std::string(buffer); // copies the null-terminated character sequence
}

static int daysInMonth(int year, int month)
{
    return month == 2 ? (year % 4 ? 28 : (year % 100 ? 29 : (year % 400 ? 28 : 29))) : ((month - 1) % 7 % 2 ? 30 : 31);
}

int64_t strToEpoch(const char *input, bool fFillMax)
{
    int year, month, day, hours, minutes, seconds;
    int n = sscanf(input, "%d-%d-%dT%d:%d:%d",
        &year, &month, &day, &hours, &minutes, &seconds);

    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    if (n > 0 && year >= 1970 && year <= 9999)
        tm.tm_year = year - 1900;
    if (n > 1 && month > 0 && month < 13)
        tm.tm_mon = month - 1;          else if (fFillMax) { tm.tm_mon = 11; month = 12; }
    if (n > 2 && day > 0 && day < 32)
        tm.tm_mday = day;               else tm.tm_mday = fFillMax ? daysInMonth(year, month) : 1;
    if (n > 3 && hours >= 0 && hours < 24)
        tm.tm_hour = hours;             else if (fFillMax) tm.tm_hour = 23;
    if (n > 4 && minutes >= 0 && minutes < 60)
        tm.tm_min = minutes;            else if (fFillMax) tm.tm_min = 59;
    if (n > 5 && seconds >= 0 && seconds < 60)
        tm.tm_sec = seconds;            else if (fFillMax) tm.tm_sec = 59;

    return (int64_t) mktime(&tm);
}
}
