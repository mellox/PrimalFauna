#pragma once

// Engine-independent policy, shared by runtime and the offline executable tests.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PFAPopulationPolicy
{
    // The requested world population feature has its own switch. The generic
    // experimental option is deliberately not a dependency of this policy.
    template <typename ConfigType>
    inline bool IsEnabled(const ConfigType& Config)
    {
        return Config.Enabled && Config.EnableWorldPopulation;
    }

    constexpr int MaxPacksPerBiome = 32;
    constexpr int MaxWorldSlots = 512;
    constexpr double SafePlayerDistanceCm = 8000.0;
    constexpr double TerrainResidentDistanceCm = 25000.0;
    constexpr double StarterClearanceCm = 35000.0;
    constexpr double FactoryClearanceCm = 15000.0;

    inline int Budget(int low, int high, double unitRoll, double density,
        int sampledCells, bool scaleLarge, int overrideCount = -1)
    {
        low = std::clamp(low, 0, 16);
        high = std::clamp(high, 0, 16);
        if (low > high) { std::swap(low, high); }
        const int rolled = low + std::min(high - low,
            static_cast<int>(std::clamp(unitRoll, 0.0, 1.0) * (high - low + 1)));
        // 200m cells: 100 samples ~= 4 square km. Size bonus capped at 2x.
        const double areaScale = scaleLarge ? std::clamp(sampledCells / 100.0, 1.0, 2.0) : 1.0;
        const double base = overrideCount >= 0 ? std::clamp(overrideCount, 0, MaxPacksPerBiome) : rolled * areaScale;
        if (!std::isfinite(density)) { density = 1.0; }
        return std::clamp(static_cast<int>(std::floor(base * std::clamp(density, 0.0, 3.0) + 0.5)), 0, MaxPacksPerBiome);
    }

    inline int GuardBudget(int budget, bool enabled, int cap)
    {
        return enabled ? std::clamp(cap, 0, budget) : 0;
    }

    inline bool KeepPlacement(bool wanted, bool samePlacement, bool protectedSite, bool spaced)
    {
        return wanted && samePlacement && !protectedSite && spaced;
    }

    // WaitingForSpawnLocation is a synchronous location-query retry marker in the
    // verified shipping spawner, not a callback/handle. Stop native ticking first,
    // then discard only that transient retry state. Never rebuild the slot array
    // or touch Creature / WasKilled / NumTimesKilled / KilledOnDayNr.
    // A negative result means no cancellation occurred (e.g. native reentrancy).
    template <typename SlotRange, typename StopNative>
    inline int QuiesceLocationRetries(bool managed, bool allowed, int nativeDepth,
        SlotRange& slots, StopNative stopNative)
    {
        if (!managed || allowed || nativeDepth != 0) { return -1; }
        if (!stopNative()) { return -1; }
        int cancelled = 0;
        for (auto& slot : slots)
        {
            if (slot.WaitingForSpawnLocation)
            {
                slot.WaitingForSpawnLocation = false;
                slot.SpawnLocationWaitTimeStamp = -1.0f;
                ++cancelled;
            }
        }
        return cancelled;
    }

    inline bool SafeToRetire(double nearestPlayerCm, bool engaged, bool waiting,
        int nativeDepth = 0, bool active = false)
    {
        return nearestPlayerCm >= SafePlayerDistanceCm && !engaged && !waiting
            && nativeDepth == 0 && !active;
    }

    template <typename SlotRange, typename IsPresent>
    inline bool CanRelocateSlots(bool safeToRetire, const SlotRange& slots, IsPresent present)
    {
        if (!safeToRetire) { return false; }
        for (const auto& slot : slots)
        {
            if (present(slot.Creature)) { return false; }
        }
        return true;
    }

    inline bool CanMaterialize(double nearestPlayerCm, bool protectedSite, bool grounded)
    {
        return nearestPlayerCm >= SafePlayerDistanceCm && nearestPlayerCm <= TerrainResidentDistanceCm
            && !protectedSite && grounded;
    }
}
