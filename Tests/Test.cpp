// Headless end-to-end test: 5 track instances + 1 master in one process.
#include "../Source/PluginProcessor.h"

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 140;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        return p;
    }
};

static float synth (int kind, double t, juce::Random& rng)
{
    const double beat = 60.0 / 140.0;
    const double ph = std::fmod (t, beat) / beat;               // 0..1 within the beat
    switch (kind)
    {
        case 0: { // kick
            const double tt = std::fmod (t, beat);
            const double f = 50.0 + 120.0 * std::exp (-tt * 40.0);
            return (float) (0.5 * std::sin (2 * juce::MathConstants<double>::pi * f * tt) * std::exp (-tt * 7.0));
        }
        case 1: { // offbeat bass (loud)
            const double env = (ph > 0.5 && ph < 0.95) ? 1.0 : 0.0;
            const double s = std::sin (2 * juce::MathConstants<double>::pi * 55 * t) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 110 * t);
            return (float) (0.7 * env * s);
        }
        case 2: { // pad (quiet, muddy)
            double s = 0;
            for (double f : { 220.0, 277.2, 329.6, 180.0 }) s += std::sin (2 * juce::MathConstants<double>::pi * f * t);
            return (float) (0.02 * s);
        }
        case 3: { // hats
            const double tt = std::fmod (t + beat * 0.5, beat);
            return (float) ((rng.nextFloat() * 2 - 1) * 0.3 * std::exp (-tt * 60.0));
        }
        default: { // supersaw-ish lead
            double s = 0;
            for (double d : { -0.3, 0.0, 0.25 }) s += 2.0 * std::fmod ((440.0 + d * 3) * t, 1.0) - 1.0;
            return (float) (0.15 * s);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000; const int bs = 512;

    const char* names[] = { "Kick", "Sub Bass", "Pads", "Hats", "Supersaw Lead" };
    std::vector<std::unique_ptr<GenreAutoMixProcessor>> tracks;
    FakePlayHead head;
    for (auto* n : names)
    {
        auto p = std::make_unique<GenreAutoMixProcessor>();
        juce::AudioProcessor::TrackProperties props; props.name = n;
        p->updateTrackProperties (props);
        p->setPlayHead (&head);
        p->prepareToPlay (sr, bs);
        tracks.push_back (std::move (p));
    }
    GenreAutoMixProcessor master;
    master.apvts.getParameter ("mode")->setValueNotifyingHost (1.0f);
    master.setPlayHead (&head);
    master.prepareToPlay (sr, bs);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    std::cout << "master active: " << master.isActiveMaster() << "  latency " << master.getLatencySamples() << "\n";

    juce::Random rng (1);
    double t = 0;
    std::vector<juce::AudioBuffer<float>> bufs (tracks.size(), juce::AudioBuffer<float> (2, bs));
    juce::AudioBuffer<float> sum (2, bs);
    juce::MidiBuffer midi;
    float peakOut = 0;

    auto run = [&] (double seconds, bool pump)
    {
        const int blocks = (int) (seconds * sr / bs);
        for (int b = 0; b < blocks; ++b)
        {
            sum.clear();
            for (size_t k = 0; k < tracks.size(); ++k)
            {
                auto& buf = bufs[k];
                for (int i = 0; i < bs; ++i)
                {
                    const float x = synth ((int) k, t + i / sr, rng);
                    buf.setSample (0, i, x); buf.setSample (1, i, k == 2 ? -x * 0.3f + x : x);
                }
                tracks[k]->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) sum.addFrom (c, 0, buf, c, 0, bs);
            }
            master.processBlock (sum, midi);
            peakOut = std::max (peakOut, sum.getMagnitude (0, bs));
            t += bs / sr;
            head.ppq = t * 140.0 / 60.0;
            if (pump && b % 90 == 0)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
                std::cout << "  t=" << juce::String (t, 1) << " phase " << (int) master.getPhase() << " gain "
                          << master.apvts.getParameter ("masterGain")->getCurrentValueAsText()
                          << " in " << master.getMasterInLufs() << " out " << master.getMasterOutLufs()
                          << " gr " << master.getLimiterGrDb() << "\n";
            }
        }
    };

    master.setPrompt ("uplifting trance, bright, wide, pumping");
    std::cout << "prompt -> " << master.describePrompt() << "\n";
    master.startLearning();
    run (10.0, false);
    std::cout << "\nBEFORE:\n" << master.buildReport();

    std::cout << "\n" << master.runAutoMix() << "\n";
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    std::cout << "\nAFTER AUTO MIX:\n" << master.buildReport() << "\n";

    for (size_t k = 0; k < tracks.size(); ++k)
    {
        auto& a = tracks[k]->apvts;
        auto v = [&] (const char* id) { return a.getParameter (id)->getCurrentValueAsText(); };
        std::cout << names[k] << ": trim " << v ("trim") << ", hpf " << v ("hpf") << ", low " << v ("lowGain")
                  << ", mid " << v ("midGain") << "@" << v ("midFreq") << ", high " << v ("highGain")
                  << ", comp " << v ("compRatio") << " @ " << v ("compThr") << ", sc " << v ("duck")
                  << ", width " << v ("width") << "\n";
    }

    peakOut = 0;
    run (30.0, true);
    std::cout << "\nmaster gain " << master.apvts.getParameter ("masterGain")->getCurrentValueAsText()
              << "  in " << master.getMasterInLufs() << "  out " << master.getMasterOutLufs()
              << " LUFS  phase " << (int) master.getPhase()
              << "  peak " << juce::Decibels::gainToDecibels (peakOut) << " dBFS\n";
    auto snap = [] (juce::AudioProcessor& pr, const char* file)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (pr.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (file));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    snap (master, "ui_master.png");
    snap (*tracks[1], "ui_track.png");
    return 0;
}
