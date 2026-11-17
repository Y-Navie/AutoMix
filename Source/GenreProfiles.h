#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace gam
{
//==============================================================================
enum Role { Auto = 0, Kick, Bass, Drums, Hats, Lead, Pad, Vocal, FX, Other, NumRoles };

inline const juce::StringArray& roleNames()
{
    static const juce::StringArray names { "Auto", "Kick", "Bass", "Drums/Perc", "Hats/Tops",
                                           "Lead/Synth", "Pad/Chords", "Vocal", "FX/Atmos", "Other" };
    return names;
}

//==============================================================================
// Per-role mixing defaults (genre-independent starting point)
struct RoleBase
{
    float hpf, lowFreq, lowGain, midFreq, midGain, highGain;
    float ratio, attackMs, releaseMs;
    float duckWeight;   // how much this role follows the genre's sidechain amount
    float width;        // 0 = mono, 1 = untouched, 2 = extra wide
};

inline const RoleBase& roleBase (int role)
{
    static const std::array<RoleBase, NumRoles> table {{
        //  hpf  lowF  lowG  midF   midG  highG ratio atk  rel  duckW width
        {  20,  100,  0.0f, 1000,  0.0f, 0.0f, 1.0f, 10, 100, 0.0f, 1.0f },  // Auto (unused)
        {  30,   60,  1.0f,  350, -3.0f, 1.0f, 3.0f, 12,  80, 0.0f, 0.0f },  // Kick
        {  30,   80,  0.5f,  250, -1.5f, 0.0f, 4.0f,  5, 120, 1.0f, 0.0f },  // Bass
        {  80,  200,  0.0f,  450, -2.0f, 1.0f, 3.0f,  8, 100, 0.0f, 0.9f },  // Drums/Perc
        { 400,  500,  0.0f, 2500, -1.0f, 1.5f, 2.0f,  3,  60, 0.15f, 1.2f }, // Hats/Tops
        { 150,  200,  0.0f,  400, -2.0f, 1.0f, 2.5f, 15, 150, 0.5f, 1.1f },  // Lead
        { 200,  200,  0.0f,  300, -2.5f, 0.0f, 2.0f, 30, 250, 0.8f, 1.3f },  // Pad
        { 100,  200,  0.0f,  300, -1.5f, 2.0f, 4.0f,  5, 120, 0.2f, 1.0f },  // Vocal
        { 250,  300,  0.0f,  500, -1.0f, 0.0f, 1.5f, 30, 300, 0.6f, 1.4f },  // FX
        { 100,  200,  0.0f,  400, -1.0f, 0.0f, 2.0f, 15, 150, 0.4f, 1.0f },  // Other
    }};
    return table[(size_t) juce::jlimit (0, (int) NumRoles - 1, role)];
}

//==============================================================================
struct GenreProfile
{
    juce::String name;
    juce::StringArray keywords;
    float masterLufs;                      // final master loudness target
    float glueRatio;                       // master bus compression
    std::array<float, NumRoles> levels;    // dB relative to the kick
    float duck;                            // tempo-synced sidechain depth (0..1)
    float duckRelease;                     // fraction of a beat
    float compScale;                       // multiplies per-role compression
    float lowTilt;                         // extra low-shelf on kick/bass
    float highTilt;                        // global "air"
    float widthScale;
};

inline const std::vector<GenreProfile>& genres()
{
    //                 levels:  Auto Kick Bass Drum Hats Lead  Pad  Voc   FX  Oth
    static const std::vector<GenreProfile> g {
        { "Uplifting Trance", { "uplifting", "euphoric", "epic trance", "vocal trance" },
          -7.0f, 1.8f, { 0, 0, -2, -6, -11, -2, -6, -2, -10, -6 }, 0.55f, 0.45f, 1.0f, 0.0f,  1.0f, 1.15f },
        { "Trance", { "trance", "progressive trance", "tech trance" },
          -7.5f, 1.8f, { 0, 0, -2, -6, -10, -3, -7, -2, -10, -6 }, 0.50f, 0.45f, 1.0f, 0.0f,  0.5f, 1.10f },
        { "Psytrance", { "psy", "psytrance", "goa", "full on" },
          -6.5f, 2.0f, { 0, 0, -1, -6, -10, -4, -9, -4,  -8, -7 }, 0.10f, 0.25f, 1.1f, 0.5f,  0.5f, 1.05f },
        { "Techno", { "techno", "hard techno", "industrial", "minimal", "peak time", "melodic techno" },
          -7.0f, 2.0f, { 0, 0, -4, -4,  -8, -6, -9, -5,  -9, -6 }, 0.35f, 0.50f, 1.2f, 1.0f,  0.0f, 1.00f },
        { "House", { "house", "tech house", "disco", "funky house", "bass house" },
          -8.0f, 1.8f, { 0, 0, -2, -4,  -8, -5, -7, -1, -10, -6 }, 0.35f, 0.40f, 1.0f, 0.5f,  0.5f, 1.05f },
        { "Deep House", { "deep house", "organic house", "afro house" },
          -9.0f, 1.6f, { 0, 0, -1, -5, -10, -6, -6, -2, -11, -6 }, 0.30f, 0.45f, 0.9f, 0.5f, -1.0f, 1.10f },
        { "Progressive House", { "progressive", "progressive house", "prog house", "melodic house" },
          -8.0f, 1.8f, { 0, 0, -2, -5, -10, -4, -6, -2, -10, -6 }, 0.45f, 0.45f, 1.0f, 0.0f,  0.5f, 1.15f },
        { "Drum & Bass", { "dnb", "d&b", "drum and bass", "drum & bass", "jungle", "neuro", "liquid" },
          -6.5f, 2.0f, { 0, 0, -1,  0,  -8, -4, -8, -2,  -9, -6 }, 0.00f, 0.30f, 1.3f, 0.5f,  1.0f, 1.05f },
        { "Dubstep", { "dubstep", "riddim", "bass music", "brostep" },
          -5.5f, 2.2f, { 0, 0,  1, -1,  -9, -3, -9, -3,  -9, -6 }, 0.00f, 0.30f, 1.4f, 1.0f,  0.5f, 1.00f },
        { "Hip-Hop", { "hip hop", "hip-hop", "hiphop", "rap", "trap", "boom bap", "drill" },
          -8.5f, 1.8f, { 0, 0,  0, -3,  -9, -6, -8,  2, -10, -6 }, 0.00f, 0.30f, 1.1f, 1.0f,  0.0f, 1.00f },
        { "Pop", { "pop", "radio", "edm pop", "dance pop" },
          -9.0f, 1.6f, { 0, 0, -3, -4,  -9, -4, -7,  3, -10, -6 }, 0.00f, 0.40f, 1.1f, 0.0f,  1.0f, 1.10f },
        { "Lo-Fi", { "lofi", "lo-fi", "chillhop", "lo fi" },
          -13.0f, 1.5f, { 0, 0, -2, -4, -12, -4, -5, -1,  -8, -5 }, 0.00f, 0.40f, 0.8f, 0.5f, -2.5f, 1.00f },
        { "Ambient / Chill", { "ambient", "chill", "chillout", "downtempo", "cinematic" },
          -15.0f, 1.3f, { 0, -2, -3, -6, -12, -3,  0, -1,  -4, -4 }, 0.00f, 0.50f, 0.6f, 0.0f,  0.0f, 1.30f },
    };
    return g;
}

inline juce::StringArray genreNames()
{
    juce::StringArray s;
    for (auto& g : genres()) s.add (g.name);
    return s;
}

inline bool isFourOnTheFloor (int genreIndex) { return genres()[(size_t) genreIndex].duck > 0.0f; }

//==============================================================================
// Modifiers parsed from the free-text prompt ("loud", "bright", "pumping" ...)
struct Mods
{
    float lufs = 0, high = 0, low = 0, compScale = 1, duckAdd = 0, width = 0;
    float vocal = 0, drums = 0, bass = 0;
    bool noSidechain = false, forceSidechain = false;
    juce::StringArray tags;
};

struct ParsedPrompt
{
    int genreIndex = -1;
    juce::String matchedKeyword;
    Mods mods;
};

inline ParsedPrompt parsePrompt (const juce::String& raw)
{
    ParsedPrompt p;
    auto text = " " + raw.toLowerCase().replaceCharacters (",.;:!/", "      ") + " ";

    auto has = [&] (const char* word) { return text.contains (" " + juce::String (word)) ; };

    // genre: longest keyword wins ("deep house" beats "house", "uplifting" beats "trance")
    int bestLen = 0;
    for (size_t i = 0; i < genres().size(); ++i)
        for (auto& kw : genres()[i].keywords)
            if (text.contains (" " + kw) && kw.length() > bestLen)
            {
                bestLen = kw.length();
                p.genreIndex = (int) i;
                p.matchedKeyword = kw;
            }

    auto& m = p.mods;
    auto tag = [&] (const char* t) { m.tags.addIfNotAlreadyThere (t); };

    if (has ("loud") || has ("club") || has ("banger") || has ("aggressive") || has ("hard "))
        { m.lufs += 1.0f; m.compScale *= 1.15f; tag ("loud"); }
    if (has ("dynamic") || has ("soft") || has ("gentle") || has ("quiet") || has ("streaming") || has ("spotify"))
        { m.lufs -= 2.5f; m.compScale *= 0.8f; tag ("dynamic"); }
    if (has ("punch"))                                   { m.drums += 1.0f; tag ("punchy"); }
    if (has ("warm"))                                    { m.high -= 1.0f; m.low += 0.5f; tag ("warm"); }
    if (has ("bright") || has ("airy") || has ("crisp") || has ("sparkl"))
        { m.high += 1.5f; tag ("bright"); }
    if (has ("dark"))                                    { m.high -= 1.5f; tag ("dark"); }
    if (has ("wide") || has ("big") || has ("epic") || has ("huge") || has ("spacious"))
        { m.width += 0.15f; tag ("wide"); }
    if (has ("mono") || has ("narrow") || has ("tight")) { m.width -= 0.2f; tag ("tight"); }
    if (has ("no sidechain") || has ("no pump"))         { m.noSidechain = true; tag ("no sidechain"); }
    else if (has ("pump") || has ("sidechain") || has ("ducking"))
        { m.duckAdd += 0.25f; m.forceSidechain = true; tag ("pumping"); }
    if (has ("vocal") || has ("vox"))                    { m.vocal += 1.5f; tag ("vocal forward"); }
    if (has ("bass heavy") || has ("heavy bass") || has ("bassy") || has ("sub heavy") || has ("fat"))
        { m.bass += 1.5f; m.low += 1.0f; tag ("bass heavy"); }
    if (has ("clean") || has ("transparent") || has ("natural"))
        { m.compScale *= 0.75f; tag ("clean"); }
    if (has ("glue") || has ("compressed") || has ("dense"))
        { m.compScale *= 1.2f; tag ("glued"); }

    return p;
}

} // namespace gam
