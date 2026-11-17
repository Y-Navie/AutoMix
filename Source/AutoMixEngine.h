#pragma once
#include "GenreProfiles.h"
#include "MixRegistry.h"
#include "DSP.h"

namespace gam
{
//==============================================================================
struct TrackFeatures
{
    double lufs = -70;
    double lowFrac = 0, lowMidFrac = 0, midFrac = 0, highFrac = 0;
    double crestDb = 0;
    double seconds = 0;
};

inline TrackFeatures featuresFromSlot (Slot& s)
{
    TrackFeatures f;
    const int n = s.activeBlocks.load();
    f.seconds = s.activeSeconds.load();
    if (n <= 0) return f;

    f.lufs = Analyzer::toLufs (s.sumMS.load() / n);
    const double lo = s.sumLow.load(), lm = s.sumLowMid.load(), mi = s.sumMid.load(), hi = s.sumHigh.load();
    const double tot = lo + lm + mi + hi + 1e-12;
    f.lowFrac = lo / tot; f.lowMidFrac = lm / tot; f.midFrac = mi / tot; f.highFrac = hi / tot;
    f.crestDb = s.sumCrestDb.load() / n;
    return f;
}

//==============================================================================
// Role detection: track name first (Ableton passes it), audio features second.
inline int classifyRole (const juce::String& trackName, const TrackFeatures& f)
{
    auto n = " " + trackName.toLowerCase() + " ";
    auto any = [&] (std::initializer_list<const char*> words)
    {
        for (auto* w : words) if (n.contains (w)) return true;
        return false;
    };

    if (any ({ "kick", " bd", "bassdrum" }))                                   return Kick;
    if (any ({ "hat", " hh", "ride", "cymbal", "shaker", "top", "crash" }))     return Hats;
    if (any ({ "snare", "clap", "perc", "drum", "tom", "rim", "loop" }))        return Drums;
    if (any ({ "bass", "sub", "808", "reese" }))                                return Bass;
    if (any ({ "vox", "vocal", "voc", "voice", "acapella", "chant" }))          return Vocal;
    if (any ({ "pad", "chord", "string", "piano", "keys", "organ", "choir" }))  return Pad;
    if (any ({ "lead", "pluck", "synth", "arp", "saw", "stab", "hook", "melody", "supersaw" })) return Lead;
    if (any ({ "fx", "riser", "sweep", "impact", "atmo", "noise", "uplifter", "downlifter", "crash", "texture" })) return FX;

    // audio-based fallback
    if (f.lowFrac > 0.55 && f.crestDb > 10.0) return Kick;
    if (f.lowFrac > 0.45)                     return Bass;
    if (f.highFrac > 0.50 && f.crestDb > 9.0) return Hats;
    if (f.crestDb > 12.0)                     return Drums;
    if (f.highFrac > 0.30)                    return Lead;
    if (f.crestDb < 8.0)                      return Pad;
    return Other;
}

//==============================================================================
struct MixContext
{
    int genre = 0;
    Mods mods;
    float intensity = 1.0f;
    static constexpr float kickReferenceLufs = -18.0f;   // leaves plenty of headroom on the sum
};

inline float masterTarget (const MixContext& c)
{
    return juce::jlimit (-24.0f, -4.0f, genres()[(size_t) c.genre].masterLufs + c.mods.lufs);
}

inline MixTargets computeTargets (const MixContext& c, int role, const TrackFeatures& f)
{
    const auto& g = genres()[(size_t) c.genre];
    const auto& b = roleBase (role);
    const auto& m = c.mods;
    const float k = c.intensity;
    MixTargets t;
    t.role = role;

    // ---- level --------------------------------------------------------------
    float target = MixContext::kickReferenceLufs + g.levels[(size_t) role];
    if (role == Vocal) target += m.vocal;
    if (role == Drums || role == Kick) target += m.drums * (role == Kick ? 0.5f : 1.0f);
    if (role == Bass)  target += m.bass;
    t.trim = juce::jlimit (-24.0f, 24.0f, (float) (target - f.lufs));

    // ---- EQ -----------------------------------------------------------------
    t.hpf = b.hpf;
    t.lowFreq = b.lowFreq;
    t.midFreq = b.midFreq;

    float low = b.lowGain + ((role == Kick || role == Bass) ? g.lowTilt + m.low : 0.0f);
    float mid = b.midGain;
    float high = b.highGain;
    if (role == Kick)      high += 0.5f * (g.highTilt + m.high);   // a little click, no fizz
    else if (role != Bass) high += g.highTilt + m.high;            // bass stays out of the top

    // spectral corrections from what we actually measured
    if (role != Kick && role != Bass)
    {
        const double mudExcess = f.lowMidFrac - 0.35;                  // boxy / muddy
        if (mudExcess > 0) mid -= (float) juce::jmin (3.0, mudExcess * 10.0);
    }
    if (role == Lead || role == Pad || role == Vocal || role == Hats)
    {
        const double expected = role == Hats ? 0.5 : 0.15;             // dull?
        if (f.highFrac < expected) high += (float) juce::jmin (2.0, (expected - f.highFrac) * 10.0);
    }
    if (role == Bass && f.highFrac > 0.15) high -= 1.5f;              // keep bass out of the top

    t.lowGain  = juce::jlimit (-12.0f, 12.0f, low * k);
    t.midGain  = juce::jlimit (-12.0f, 12.0f, mid * k);
    t.highGain = juce::jlimit (-12.0f, 12.0f, high * k);

    // ---- dynamics -----------------------------------------------------------
    const float compAmt = g.compScale * m.compScale * k;
    t.compRatio = juce::jlimit (1.0f, 10.0f, 1.0f + (b.ratio - 1.0f) * compAmt);
    const float crest = (float) juce::jlimit (3.0, 20.0, f.crestDb);
    t.compThr = juce::jlimit (-60.0f, 0.0f, target + crest * 0.5f - 2.0f * compAmt);
    t.compAtk = b.attackMs * (m.drums > 0 && (role == Kick || role == Drums) ? 1.8f : 1.0f);
    t.compRel = b.releaseMs;
    const float estGr = (1.0f - 1.0f / t.compRatio) * (crest * 0.5f + 2.0f * compAmt);
    t.makeup = juce::jlimit (0.0f, 12.0f, estGr * 0.4f);

    // ---- tempo-synced sidechain --------------------------------------------
    float duckAmt = g.duck + m.duckAdd;
    if (m.noSidechain) duckAmt = 0;
    if (g.duck <= 0 && ! m.forceSidechain) duckAmt = 0;
    t.duck = juce::jlimit (0.0f, 0.9f, b.duckWeight * duckAmt * k);
    t.duckRel = g.duckRelease;

    // ---- stereo -------------------------------------------------------------
    float w = b.width;
    if (w <= 0.0f) t.width = 0.0f;                                   // kick & bass: always mono
    else
    {
        if (w > 0.5f) w = w * g.widthScale + m.width;
        t.width = juce::jlimit (0.0f, 2.0f, 1.0f + (w - 1.0f) * k);
    }

    return t;
}

inline MixTargets neutralTargets (int role)
{
    MixTargets t;
    t.role = role;
    t.lowFreq = roleBase (role).lowFreq;
    t.midFreq = roleBase (role).midFreq;
    return t;
}

} // namespace gam
