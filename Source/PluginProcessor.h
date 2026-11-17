#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AutoMixEngine.h"

class GenreAutoMixProcessor : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    enum Mode { TrackMode = 0, MasterMode = 1 };
    enum class MasterPhase { Idle, Learning, MatchIn, MatchOut, Done };

    GenreAutoMixProcessor();
    ~GenreAutoMixProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void updateTrackProperties (const TrackProperties&) override;

    //==============================================================================
    // Master actions (message thread)
    void startLearning();
    void stopLearning();
    juce::String runAutoMix();          // returns a status / error string
    void resetMix();

    bool isMaster() const        { return (int) modeParam->load() == MasterMode; }
    bool isActiveMaster() const  { return registry->masterOwner.load() == this; }
    bool isLearning() const      { return registry->learning.load(); }
    MasterPhase getPhase() const { return phase.load(); }

    juce::String getPrompt() const;
    void setPrompt (const juce::String&);
    juce::String describePrompt() const;

    juce::String getTrackName() const;
    gam::TrackFeatures getOwnFeatures() const;
    int getResolvedRole() const { return slotIndex >= 0 ? registry->slot (slotIndex).resolvedRole.load() : 0; }
    juce::String buildReport() const;
    int countConnectedTracks() const;
    double getLearnedSeconds() const;
    float getMasterInLufs() const;
    float getMasterOutLufs() const;
    float getLimiterGrDb() const { return limiterGr.load(); }
    juce::String getMatchNote() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void applyTargets (const gam::MixTargets&);
    void setParam (const juce::String& id, float value);
    void updateDsp();
    void processTrack (juce::AudioBuffer<float>&);
    void processMaster (juce::AudioBuffer<float>&);
    void claimOrReleaseMaster();

    juce::SharedResourcePointer<gam::MixRegistry> registry;
    int slotIndex = -1;
    juce::uint32 appliedVersion = 0;
    juce::uint32 localLearnGen = 0;

    // cached raw parameter pointers
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* roleParam = nullptr;
    std::atomic<float>* p[32] {};

    // track DSP
    double sr = 44100.0;
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Filter hpf, lowShelf, midPeak, highShelf;
    juce::dsp::Compressor<float> comp;
    juce::SmoothedValue<float> trimGain, makeupGain, widthSm, masterGainSm;
    float cHpf = -1, cLowF = -1, cLowG = -99, cMidF = -1, cMidG = -99, cHighG = -99;
    gam::Analyzer analyzer;

    // local learning accumulators (audio thread)
    double aMS = 0, aLow = 0, aLowMid = 0, aMid = 0, aHigh = 0, aCrest = 0, aSec = 0;
    int aBlocks = 0;

    // master DSP
    juce::dsp::Compressor<float> glue;
    gam::LookaheadLimiter limiter;
    gam::Analyzer masterIn, masterOut;
    std::atomic<double> mInMS { 0 }, mInSec { 0 }, mOutMS { 0 }, mOutSec { 0 };
    std::atomic<int> mInBlocks { 0 }, mOutBlocks { 0 };
    std::atomic<bool> resetMasterMeters { false };
    std::atomic<float> limiterGr { 0 };
    std::atomic<MasterPhase> phase { MasterPhase::Idle };
    float masterTargetLufs = -8.0f;
    int matchIterations = 0;
    float baseMasterGain = 0.0f;
    bool limitedByHeadroom = false;
    int currentLatency = 0;

    juce::String trackName;
    mutable juce::CriticalSection nameLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenreAutoMixProcessor)
};
