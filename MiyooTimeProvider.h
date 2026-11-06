#pragma once

#include <ctime>

#include <deki/providers/ITimeProvider.h>

namespace DekiMiyoo
{

/// The device's monotonic clock.
class MiyooTimeProvider : public Deki::ITimeProvider
{
public:
    uint32_t GetTicksMs() const override { return static_cast<uint32_t>(GetTicksUs() / 1000u); }
    uint64_t GetTicksUs() const override
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000u + static_cast<uint64_t>(ts.tv_nsec) / 1000u;
    }
    void DelayMs(uint32_t ms) const override
    {
        timespec ts{ static_cast<time_t>(ms / 1000u), static_cast<long>((ms % 1000u) * 1000000u) };
        nanosleep(&ts, nullptr);
    }
};

}  // namespace DekiMiyoo
