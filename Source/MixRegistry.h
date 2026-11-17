#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <array>

namespace gam
{
//==============================================================================
// Settings the Master instance pushes to each Track instance
struct MixTargets
{
    float trim = 0, hpf = 20, lowFreq = 100, lowGain = 0, midFreq = 1000, midGain = 0, highGain = 0;
    float compThr = 0, compRatio = 1, compAtk = 10, compRel = 100, makeup = 0;
    float duck = 0, duckRel = 0.45f, width = 1;
    int role = 0;
};

//==============================================================================
// One slot per plugin instance. Audio threads only touch atomics.
struct Slot
{
    std::atomic<bool> inUse { false };
    std::atomic<bool> isMaster { false };
    std::atomic<int>  userRole { 0 };
    std::atomic<int>  resolvedRole { 0 };

    // analysis accumulators published by the track's audio thread
    std::atomic<double> sumMS { 0 }, sumLow { 0 }, sumLowMid { 0 }, sumMid { 0 }, sumHigh { 0 },
                        sumCrestDb { 0 }, activeSeconds { 0 };
    std::atomic<int> activeBlocks { 0 };

    std::atomic<juce::uint32> targetVersion { 0 };

    // message-thread data, guarded by MixRegistry::lock
    juce::String name;
    MixTargets targets;
    juce::String lastReport;
};

//==============================================================================
// Process-wide singleton (shared by every instance loaded in the same DAW process
// via juce::SharedResourcePointer). This is how the Master "sees" all tracks.
class MixRegistry
{
public:
    static constexpr int maxSlots = 256;

    int acquireSlot()
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < maxSlots; ++i)
        {
            bool expected = false;
            if (slots[(size_t) i].inUse.compare_exchange_strong (expected, true))
            {
                auto& s = slots[(size_t) i];
                s.isMaster = false;
                s.userRole = 0;
                s.resolvedRole = 0;
                s.name = {};
                s.targets = {};
                resetStats (s);
                return i;
            }
        }
        return -1;
    }

    void releaseSlot (int i)
    {
        if (i < 0) return;
        const juce::ScopedLock sl (lock);
        slots[(size_t) i].inUse = false;
        slots[(size_t) i].isMaster = false;
    }

    Slot& slot (int i) { return slots[(size_t) i]; }

    static void resetStats (Slot& s)
    {
        s.sumMS = 0; s.sumLow = 0; s.sumLowMid = 0; s.sumMid = 0; s.sumHigh = 0;
        s.sumCrestDb = 0; s.activeSeconds = 0; s.activeBlocks = 0;
    }

    std::atomic<bool> learning { false };
    std::atomic<juce::uint32> learnGeneration { 0 };
    std::atomic<void*> masterOwner { nullptr };

    juce::CriticalSection lock;

private:
    std::array<Slot, maxSlots> slots;
};

} // namespace gam
