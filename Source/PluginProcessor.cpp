#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace gam;

namespace
{
    enum P { pTrim, pHpf, pLowF, pLowG, pMidF, pMidG, pHighG, pThr, pRatio, pAtk, pRel, pMakeup,
             pDuck, pDuckRel, pWidth, pBypass, pGenre, pIntensity, pCeiling, pMasterGain, pGlue, pCount };

    const char* ids[pCount] = { "trim", "hpf", "lowFreq", "lowGain", "midFreq", "midGain", "highGain",
                                "compThr", "compRatio", "compAtk", "compRel", "makeup",
                                "duck", "duckRel", "width", "bypass",
                                "genre", "intensity", "ceiling", "masterGain", "glue" };

    juce::NormalisableRange<float> freqRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout GenreAutoMixProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto db = [] (float x, int) { return String (x, 1) + " dB"; };
    auto hz = [] (float x, int) { return x >= 1000 ? String (x / 1000.0f, 2) + " kHz" : String ((int) x) + " Hz"; };

    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Track", "Master" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "role", 1 }, "Role", roleNames(), 0));

    auto F = [&] (const char* id, const char* name, NormalisableRange<float> r, float def,
                  std::function<String (float, int)> fmt)
    {
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };

    F ("trim",     "Trim",          { -24.0f, 24.0f, 0.01f }, 0.0f, db);
    F ("hpf",      "HPF",           freqRange (20.0f, 1000.0f), 20.0f, hz);
    F ("lowFreq",  "Low Freq",      freqRange (30.0f, 500.0f), 100.0f, hz);
    F ("lowGain",  "Low Gain",      { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    F ("midFreq",  "Mid Freq",      freqRange (150.0f, 8000.0f), 1000.0f, hz);
    F ("midGain",  "Mid Gain",      { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    F ("highGain", "High Gain",     { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    F ("compThr",  "Comp Thresh",   { -60.0f, 0.0f, 0.01f }, 0.0f, db);
    F ("compRatio","Comp Ratio",    { 1.0f, 10.0f, 0.01f, 0.5f }, 1.0f, [] (float x, int) { return juce::String (x, 1) + ":1"; });
    F ("compAtk",  "Comp Attack",   { 0.1f, 100.0f, 0.01f, 0.4f }, 10.0f, [] (float x, int) { return juce::String (x, 1) + " ms"; });
    F ("compRel",  "Comp Release",  { 10.0f, 1000.0f, 0.1f, 0.4f }, 100.0f, [] (float x, int) { return juce::String ((int) x) + " ms"; });
    F ("makeup",   "Makeup",        { 0.0f, 12.0f, 0.01f }, 0.0f, db);
    F ("duck",     "Sidechain",     { 0.0f, 0.9f, 0.001f }, 0.0f, [] (float x, int) { return juce::String ((int) std::round (x * 100)) + " %"; });
    F ("duckRel",  "SC Release",    { 0.05f, 0.95f, 0.001f }, 0.45f, [] (float x, int) { return juce::String (x, 2) + " beat"; });
    F ("width",    "Width",         { 0.0f, 2.0f, 0.001f }, 1.0f, [] (float x, int) { return juce::String ((int) std::round (x * 100)) + " %"; });
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "AutoMix Bypass", false));

    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "genre", 1 }, "Genre", genreNames(), 0));
    F ("intensity",  "Intensity",   { 0.0f, 1.0f, 0.001f }, 0.8f, [] (float x, int) { return juce::String ((int) std::round (x * 100)) + " %"; });
    F ("ceiling",    "Ceiling",     { -3.0f, 0.0f, 0.01f }, -1.0f, db);
    F ("masterGain", "Master Gain", { -12.0f, 24.0f, 0.01f }, 0.0f, db);
    F ("glue",       "Glue",        { 0.0f, 1.0f, 0.001f }, 0.5f, [] (float x, int) { return juce::String ((int) std::round (x * 100)) + " %"; });

    return { v.begin(), v.end() };
}

//==============================================================================
GenreAutoMixProcessor::GenreAutoMixProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GenreAutoMix", createLayout())
{
    modeParam = apvts.getRawParameterValue ("mode");
    roleParam = apvts.getRawParameterValue ("role");
    for (int i = 0; i < pCount; ++i)
        p[i] = apvts.getRawParameterValue (ids[i]);

    apvts.state.setProperty ("prompt", "", nullptr);

    slotIndex = registry->acquireSlot();
    startTimerHz (20);
}

GenreAutoMixProcessor::~GenreAutoMixProcessor()
{
    stopTimer();
    void* me = this;
    registry->masterOwner.compare_exchange_strong (me, nullptr);
    registry->releaseSlot (slotIndex);
}

juce::String GenreAutoMixProcessor::getMatchNote() const
{
    if (phase.load() != MasterPhase::Done) return {};
    return limitedByHeadroom ? "Target loudness capped to avoid over-limiting (mix too dynamic / low-end heavy)."
                             : juce::String();
}

bool GenreAutoMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void GenreAutoMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const auto nCh = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, nCh };

    for (auto* f : { &hpf, &lowShelf, &midPeak, &highShelf }) { f->prepare (spec); f->reset(); }
    comp.prepare (spec);
    glue.prepare (spec);
    limiter.prepare (sampleRate, (int) nCh);
    analyzer.prepare (sampleRate);
    masterIn.prepare (sampleRate);
    masterOut.prepare (sampleRate);

    for (auto* s : { &trimGain, &makeupGain, &widthSm, &masterGainSm })
        s->reset (sampleRate, 0.05);

    cHpf = cLowF = cMidF = -1;
    cLowG = cMidG = cHighG = -99;
    updateDsp();

    currentLatency = isMaster() ? limiter.getLatency() : 0;
    setLatencySamples (currentLatency);
}

void GenreAutoMixProcessor::updateDsp()
{
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const float nyq = (float) sr * 0.45f;

    if (! juce::exactlyEqual (p[pHpf]->load(), cHpf))
    { cHpf = p[pHpf]->load(); *hpf.state = AC::makeHighPass (sr, juce::jmin (cHpf, nyq), 0.7071f); }

    if (! juce::exactlyEqual (p[pLowF]->load(), cLowF) || ! juce::exactlyEqual (p[pLowG]->load(), cLowG))
    {
        cLowF = p[pLowF]->load(); cLowG = p[pLowG]->load();
        *lowShelf.state = AC::makeLowShelf (sr, cLowF, 0.7071f, juce::Decibels::decibelsToGain (cLowG));
    }
    if (! juce::exactlyEqual (p[pMidF]->load(), cMidF) || ! juce::exactlyEqual (p[pMidG]->load(), cMidG))
    {
        cMidF = p[pMidF]->load(); cMidG = p[pMidG]->load();
        *midPeak.state = AC::makePeakFilter (sr, juce::jmin (cMidF, nyq), 1.0f, juce::Decibels::decibelsToGain (cMidG));
    }
    if (! juce::exactlyEqual (p[pHighG]->load(), cHighG))
    {
        cHighG = p[pHighG]->load();
        *highShelf.state = AC::makeHighShelf (sr, juce::jmin (8000.0f, nyq), 0.7071f, juce::Decibels::decibelsToGain (cHighG));
    }

    comp.setThreshold (p[pThr]->load());
    comp.setRatio (p[pRatio]->load());
    comp.setAttack (p[pAtk]->load());
    comp.setRelease (p[pRel]->load());

    trimGain.setTargetValue (juce::Decibels::decibelsToGain (p[pTrim]->load()));
    makeupGain.setTargetValue (juce::Decibels::decibelsToGain (p[pMakeup]->load()));
    widthSm.setTargetValue (p[pWidth]->load());
}

//==============================================================================
void GenreAutoMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (isMaster()) processMaster (buffer);
    else            processTrack (buffer);
}

void GenreAutoMixProcessor::processTrack (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nCh = buffer.getNumChannels();
    if (n == 0 || slotIndex < 0) return;
    auto& slot = registry->slot (slotIndex);

    // ---- 1. learn (measure the raw input) ---------------------------------
    const auto gen = registry->learnGeneration.load();
    if (gen != localLearnGen)
    {
        localLearnGen = gen;
        aMS = aLow = aLowMid = aMid = aHigh = aCrest = aSec = 0; aBlocks = 0;
        MixRegistry::resetStats (slot);
    }
    if (registry->learning.load())
    {
        auto b = analyzer.process (buffer.getReadPointer (0), nCh > 1 ? buffer.getReadPointer (1) : nullptr, n);
        if (b.active)
        {
            aMS += b.ms; aLow += b.low; aLowMid += b.lowMid; aMid += b.mid; aHigh += b.high;
            aCrest += b.crestDb; aSec += n / sr; ++aBlocks;
            slot.sumMS = aMS; slot.sumLow = aLow; slot.sumLowMid = aLowMid; slot.sumMid = aMid;
            slot.sumHigh = aHigh; slot.sumCrestDb = aCrest; slot.activeSeconds = aSec;
            slot.activeBlocks = aBlocks;
        }
    }

    if (p[pBypass]->load() > 0.5f) return;

    // ---- 2. mix chain --------------------------------------------------------
    updateDsp();

    trimGain.applyGain (buffer, n);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    hpf.process (ctx);
    lowShelf.process (ctx);
    midPeak.process (ctx);
    highShelf.process (ctx);
    if (p[pRatio]->load() > 1.01f) comp.process (ctx);
    makeupGain.applyGain (buffer, n);

    // ---- 3. tempo-synced sidechain (sample-accurate from host playhead) -----
    const float depth = p[pDuck]->load();
    if (depth > 0.001f)
    {
        if (auto* ph = getPlayHead())
        {
            auto pos = ph->getPosition();
            if (pos && pos->getIsPlaying() && pos->getPpqPosition() && pos->getBpm())
            {
                const double ppq0 = *pos->getPpqPosition();
                const double beatsPerSample = *pos->getBpm() / 60.0 / sr;
                const double atk = 0.006;
                const double rel = juce::jmax (0.05f, p[pDuckRel]->load());

                for (int i = 0; i < n; ++i)
                {
                    const double ppq = ppq0 + i * beatsPerSample;
                    const double frac = ppq - std::floor (ppq);
                    double env;
                    if (frac < atk) env = frac / atk;
                    else
                    {
                        const double t = juce::jlimit (0.0, 1.0, (frac - atk) / rel);
                        env = (1.0 - t) * (1.0 - t);
                    }
                    const float g = 1.0f - depth * (float) env;
                    for (int c = 0; c < nCh; ++c)
                        buffer.getWritePointer (c)[i] *= g;
                }
            }
        }
    }

    // ---- 4. width (M/S) ----------------------------------------------------
    if (nCh >= 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float w = widthSm.getNextValue();
            const float m = 0.5f * (l[i] + r[i]);
            const float s = 0.5f * (l[i] - r[i]) * w;
            l[i] = m + s; r[i] = m - s;
        }
    }
}

void GenreAutoMixProcessor::processMaster (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nCh = buffer.getNumChannels();
    if (n == 0) return;

    if (resetMasterMeters.exchange (false))
    {
        mInMS = 0; mInSec = 0; mInBlocks = 0;
        mOutMS = 0; mOutSec = 0; mOutBlocks = 0;
    }

    auto bIn = masterIn.process (buffer.getReadPointer (0), nCh > 1 ? buffer.getReadPointer (1) : nullptr, n);
    if (bIn.active) { mInMS = mInMS + bIn.ms; mInSec = mInSec + n / sr; mInBlocks = mInBlocks + 1; }

    masterGainSm.setTargetValue (juce::Decibels::decibelsToGain (p[pMasterGain]->load()));
    masterGainSm.applyGain (buffer, n);

    // glue compressor around the target loudness
    const float glueAmt = p[pGlue]->load();
    if (glueAmt > 0.01f)
    {
        const int g = (int) p[pGenre]->load();
        glue.setThreshold (masterTargetLufs + 3.0f - 3.0f * glueAmt);
        glue.setRatio (1.0f + (genres()[(size_t) g].glueRatio - 1.0f) * glueAmt);
        glue.setAttack (30.0f);
        glue.setRelease (150.0f);
        juce::dsp::AudioBlock<float> block (buffer);
        glue.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    limiter.setCeilingDb (p[pCeiling]->load());
    limiter.process (buffer);
    limiterGr = limiter.getGainReductionDb();

    auto bOut = masterOut.process (buffer.getReadPointer (0), nCh > 1 ? buffer.getReadPointer (1) : nullptr, n);
    if (bOut.active) { mOutMS = mOutMS + bOut.ms; mOutSec = mOutSec + n / sr; mOutBlocks = mOutBlocks + 1; }
}

//==============================================================================
void GenreAutoMixProcessor::timerCallback()
{
    claimOrReleaseMaster();

    // latency changes with mode (limiter look-ahead only on master)
    const int wanted = isMaster() ? limiter.getLatency() : 0;
    if (wanted != currentLatency) { currentLatency = wanted; setLatencySamples (wanted); }

    if (slotIndex < 0) return;
    auto& slot = registry->slot (slotIndex);
    slot.isMaster = isMaster();
    slot.userRole = (int) roleParam->load();

    {
        const juce::ScopedLock sl (registry->lock);
        slot.name = getTrackName();
    }

    // Track: pick up new settings from the master
    const auto v = slot.targetVersion.load();
    if (! isMaster() && v != appliedVersion)
    {
        appliedVersion = v;
        MixTargets t;
        { const juce::ScopedLock sl (registry->lock); t = slot.targets; }
        applyTargets (t);
    }

    // Master: automatic loudness matching after Auto Mix
    if (isActiveMaster() && ! resetMasterMeters.load())   // wait until audio thread has cleared the meters
    {
        const auto ph = phase.load();
        if (ph == MasterPhase::MatchIn && mInSec.load() >= 6.0 && mInBlocks.load() > 0)
        {
            const float in = (float) Analyzer::toLufs (mInMS.load() / mInBlocks.load());
            baseMasterGain = juce::jlimit (-12.0f, 24.0f, masterTargetLufs - in);
            setParam ("masterGain", baseMasterGain);
            resetMasterMeters = true;
            matchIterations = 0;
            phase = MasterPhase::MatchOut;
        }
        else if (ph == MasterPhase::MatchOut && mOutSec.load() >= 6.0 && mOutBlocks.load() > 0)
        {
            const float out = (float) Analyzer::toLufs (mOutMS.load() / mOutBlocks.load());
            const float err = masterTargetLufs - out;
            // never push more than +4 dB beyond the clean gain: protects against over-limiting
            const float maxGain = juce::jmin (24.0f, baseMasterGain + 4.0f);
            const float cur = p[pMasterGain]->load();
            const float next = juce::jlimit (-12.0f, maxGain, cur + juce::jlimit (-6.0f, 6.0f, err * 1.2f));
            setParam ("masterGain", next);
            resetMasterMeters = true;
            limitedByHeadroom = err > 0.5f && next >= maxGain - 0.01f;
            if (std::abs (err) < 0.5f || limitedByHeadroom || ++matchIterations >= 3) phase = MasterPhase::Done;
        }
    }
}

void GenreAutoMixProcessor::claimOrReleaseMaster()
{
    void* me = this;
    if (isMaster())
    {
        void* expected = nullptr;
        registry->masterOwner.compare_exchange_strong (expected, me);
    }
    else
    {
        registry->masterOwner.compare_exchange_strong (me, nullptr);
    }
}

void GenreAutoMixProcessor::setParam (const juce::String& id, float value)
{
    if (auto* param = apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void GenreAutoMixProcessor::applyTargets (const MixTargets& t)
{
    setParam ("trim", t.trim);       setParam ("hpf", t.hpf);
    setParam ("lowFreq", t.lowFreq); setParam ("lowGain", t.lowGain);
    setParam ("midFreq", t.midFreq); setParam ("midGain", t.midGain);
    setParam ("highGain", t.highGain);
    setParam ("compThr", t.compThr); setParam ("compRatio", t.compRatio);
    setParam ("compAtk", t.compAtk); setParam ("compRel", t.compRel);
    setParam ("makeup", t.makeup);
    setParam ("duck", t.duck);       setParam ("duckRel", t.duckRel);
    setParam ("width", t.width);
}

//==============================================================================
void GenreAutoMixProcessor::startLearning()
{
    if (! isActiveMaster()) return;
    registry->learnGeneration = registry->learnGeneration.load() + 1;
    registry->learning = true;
    phase = MasterPhase::Learning;
}

void GenreAutoMixProcessor::stopLearning()
{
    registry->learning = false;
    if (phase.load() == MasterPhase::Learning) phase = MasterPhase::Idle;
}

juce::String GenreAutoMixProcessor::runAutoMix()
{
    if (! isActiveMaster()) return "Only the Master instance can run Auto Mix.";
    stopLearning();

    // genre: prompt wins, dropdown is the fallback
    MixContext ctx;
    auto parsed = parsePrompt (getPrompt());
    ctx.genre = parsed.genreIndex >= 0 ? parsed.genreIndex : (int) p[pGenre]->load();
    if (parsed.genreIndex >= 0) setParam ("genre", (float) parsed.genreIndex);
    ctx.mods = parsed.mods;
    ctx.intensity = p[pIntensity]->load();

    int mixed = 0, skipped = 0;
    for (int i = 0; i < MixRegistry::maxSlots; ++i)
    {
        auto& s = registry->slot (i);
        if (! s.inUse.load() || s.isMaster.load()) continue;

        auto f = featuresFromSlot (s);
        if (f.seconds < 0.75) { ++skipped; continue; }   // never heard it play

        juce::String name;
        { const juce::ScopedLock sl (registry->lock); name = s.name; }

        const int user = s.userRole.load();
        const int role = user != Auto ? user : classifyRole (name, f);
        s.resolvedRole = role;

        auto t = computeTargets (ctx, role, f);
        {
            const juce::ScopedLock sl (registry->lock);
            s.targets = t;
        }
        s.targetVersion = s.targetVersion.load() + 1;
        ++mixed;
    }

    if (mixed == 0)
        return "No learned tracks. Press Learn, play the song (drop + breakdown), then Auto Mix.";

    masterTargetLufs = masterTarget (ctx);
    resetMasterMeters = true;
    phase = MasterPhase::MatchIn;

    return "Mixed " + juce::String (mixed) + " tracks as " + genres()[(size_t) ctx.genre].name
         + (skipped > 0 ? " (" + juce::String (skipped) + " silent tracks skipped)" : juce::String())
         + ". Keep playing ~12 s to auto-match master loudness ("
         + juce::String (masterTargetLufs, 1) + " LUFS).";
}

void GenreAutoMixProcessor::resetMix()
{
    for (int i = 0; i < MixRegistry::maxSlots; ++i)
    {
        auto& s = registry->slot (i);
        if (! s.inUse.load() || s.isMaster.load()) continue;
        {
            const juce::ScopedLock sl (registry->lock);
            s.targets = neutralTargets (s.resolvedRole.load());
        }
        s.targetVersion = s.targetVersion.load() + 1;
    }
    setParam ("masterGain", 0.0f);
    phase = MasterPhase::Idle;
}

//==============================================================================
juce::String GenreAutoMixProcessor::getPrompt() const { return apvts.state.getProperty ("prompt").toString(); }
void GenreAutoMixProcessor::setPrompt (const juce::String& s) { apvts.state.setProperty ("prompt", s, nullptr); }

juce::String GenreAutoMixProcessor::describePrompt() const
{
    auto parsed = parsePrompt (getPrompt());
    MixContext ctx;
    ctx.genre = parsed.genreIndex >= 0 ? parsed.genreIndex : (int) p[pGenre]->load();
    ctx.mods = parsed.mods;
    juce::String s = genres()[(size_t) ctx.genre].name;
    s << (parsed.genreIndex >= 0 ? "  (from prompt)" : "  (from dropdown)");
    if (! parsed.mods.tags.isEmpty()) s << "  |  " << parsed.mods.tags.joinIntoString (", ");
    s << "  |  target " << juce::String (masterTarget (ctx), 1) << " LUFS";
    return s;
}

juce::String GenreAutoMixProcessor::getTrackName() const
{
    const juce::ScopedLock sl (nameLock);
    return trackName;
}

void GenreAutoMixProcessor::updateTrackProperties (const TrackProperties& props)
{
    const juce::ScopedLock sl (nameLock);
    trackName = props.name;
}

gam::TrackFeatures GenreAutoMixProcessor::getOwnFeatures() const
{
    if (slotIndex < 0) return {};
    return featuresFromSlot (registry->slot (slotIndex));
}

int GenreAutoMixProcessor::countConnectedTracks() const
{
    int c = 0;
    for (int i = 0; i < MixRegistry::maxSlots; ++i)
    {
        auto& s = registry->slot (i);
        if (s.inUse.load() && ! s.isMaster.load()) ++c;
    }
    return c;
}

double GenreAutoMixProcessor::getLearnedSeconds() const
{
    double mx = 0;
    for (int i = 0; i < MixRegistry::maxSlots; ++i)
    {
        auto& s = registry->slot (i);
        if (s.inUse.load() && ! s.isMaster.load()) mx = std::max (mx, s.activeSeconds.load());
    }
    return mx;
}

float GenreAutoMixProcessor::getMasterInLufs() const
{
    const int b = mInBlocks.load();
    return b > 0 ? (float) Analyzer::toLufs (mInMS.load() / b) : -70.0f;
}

float GenreAutoMixProcessor::getMasterOutLufs() const
{
    const int b = mOutBlocks.load();
    return b > 0 ? (float) Analyzer::toLufs (mOutMS.load() / b) : -70.0f;
}

juce::String GenreAutoMixProcessor::buildReport() const
{
    juce::String r;
    r << juce::String ("TRACK").paddedRight (' ', 20) << juce::String ("ROLE").paddedRight (' ', 13)
      << juce::String ("HEARD").paddedRight (' ', 8) << juce::String ("LUFS").paddedRight (' ', 8)
      << juce::String ("GAIN").paddedRight (' ', 8) << "SC\n";

    int idx = 1;
    for (int i = 0; i < MixRegistry::maxSlots; ++i)
    {
        auto& s = registry->slot (i);
        if (! s.inUse.load() || s.isMaster.load()) continue;

        juce::String name; MixTargets t;
        { const juce::ScopedLock sl (registry->lock); name = s.name; t = s.targets; }
        if (name.isEmpty()) name = "Track " + juce::String (idx);
        ++idx;

        auto f = featuresFromSlot (s);
        const int user = s.userRole.load();
        const int role = user != Auto ? user : (s.resolvedRole.load() != 0 ? s.resolvedRole.load()
                                                                           : (f.seconds > 0 ? classifyRole (name, f) : 0));
        r << name.substring (0, 18).paddedRight (' ', 20)
          << (role == 0 ? juce::String ("?") : roleNames()[role]).paddedRight (' ', 13)
          << (juce::String (f.seconds, 1) + "s").paddedRight (' ', 8)
          << (f.seconds > 0 ? juce::String (f.lufs, 1) : juce::String ("-")).paddedRight (' ', 8)
          << ((t.trim >= 0 ? "+" : "") + juce::String (t.trim, 1)).paddedRight (' ', 8)
          << juce::String ((int) std::round (t.duck * 100)) << "%\n";
    }
    return r;
}

//==============================================================================
void GenreAutoMixProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void GenreAutoMixProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* GenreAutoMixProcessor::createEditor() { return new GenreAutoMixEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GenreAutoMixProcessor(); }
