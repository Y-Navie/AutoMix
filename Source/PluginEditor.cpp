#include "PluginEditor.h"

namespace
{
    const juce::Colour bg      { 0xff12141a };
    const juce::Colour panel   { 0xff1b1e27 };
    const juce::Colour accent  { 0xff38d9c4 };
    const juce::Colour accent2 { 0xffb388ff };
    const juce::Colour text    { 0xffe6e9f0 };
    const juce::Colour dim     { 0xff8a90a0 };
}

GenreAutoMixEditor::GenreAutoMixEditor (GenreAutoMixProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2b2f3a));
    lnf.setColour (juce::Slider::thumbColourId, text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Slider::textBoxTextColourId, text);
    lnf.setColour (juce::ComboBox::backgroundColourId, panel);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2b2f3a));
    lnf.setColour (juce::TextEditor::backgroundColourId, panel);
    lnf.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2b2f3a));
    lnf.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    lnf.setColour (juce::TextButton::buttonColourId, panel);
    lnf.setColour (juce::Label::textColourId, text);
    setLookAndFeel (&lnf);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t, juce::Colour c = dim)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, c);
        addAndMakeVisible (l);
    };

    // mode
    setupLabel (modeLabel, "MODE");
    modeBox.addItemList ({ "Track", "Master" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<ComboAtt> (proc.apvts, "mode", modeBox);

    // ---------------- master ----------------
    setupLabel (promptLabel, "DESCRIBE YOUR MIX  (genre + vibe)");
    prompt.setTextToShowWhenEmpty ("e.g.  uplifting trance, bright, wide, pumping sidechain", dim);
    prompt.setText (proc.getPrompt(), false);
    prompt.setFont (juce::FontOptions (16.0f));
    prompt.onTextChange = [this] { proc.setPrompt (prompt.getText()); };
    prompt.onReturnKey  = [this] { lastStatus = proc.runAutoMix(); };
    addAndMakeVisible (prompt);

    setupLabel (promptInfo, "", accent);
    setupLabel (genreLabel, "GENRE (fallback)");
    genreBox.addItemList (gam::genreNames(), 1);
    addAndMakeVisible (genreBox);
    genreAtt = std::make_unique<ComboAtt> (proc.apvts, "genre", genreBox);

    learnBtn.setClickingTogglesState (false);
    learnBtn.onClick = [this]
    {
        if (proc.isLearning()) proc.stopLearning();
        else { proc.startLearning(); lastStatus = "Learning... play the song (intro, breakdown AND drop)."; }
    };
    mixBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    mixBtn.onClick = [this] { lastStatus = proc.runAutoMix(); };
    resetBtn.onClick = [this] { proc.resetMix(); lastStatus = "Mix reset to neutral."; };
    for (auto* b : { &learnBtn, &mixBtn, &resetBtn }) addAndMakeVisible (*b);

    setupLabel (status, "", text);
    setupLabel (meters, "", dim);

    report.setMultiLine (true);
    report.setReadOnly (true);
    report.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (report);

    addKnob (masterKnobs, "intensity",  "Intensity");
    addKnob (masterKnobs, "glue",       "Glue");
    addKnob (masterKnobs, "ceiling",    "Ceiling");
    addKnob (masterKnobs, "masterGain", "Master Gain");

    // ---------------- track ----------------
    setupLabel (roleLabel, "ROLE");
    roleBox.addItemList (gam::roleNames(), 1);
    addAndMakeVisible (roleBox);
    roleAtt = std::make_unique<ComboAtt> (proc.apvts, "role", roleBox);
    setupLabel (trackInfo, "", text);
    bypassBtn.setColour (juce::ToggleButton::textColourId, text);
    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassBtn);

    for (auto [id, name] : std::initializer_list<std::pair<const char*, const char*>> {
            { "trim", "Trim" }, { "hpf", "HPF" }, { "lowFreq", "Low Freq" }, { "lowGain", "Low" },
            { "midFreq", "Mid Freq" }, { "midGain", "Mid" }, { "highGain", "High" }, { "width", "Width" },
            { "compThr", "Threshold" }, { "compRatio", "Ratio" }, { "compAtk", "Attack" }, { "compRel", "Release" },
            { "makeup", "Makeup" }, { "duck", "Sidechain" }, { "duckRel", "SC Release" } })
        addKnob (trackKnobs, id, name);

    setResizable (true, true);
    setResizeLimits (640, 480, 1400, 1000);
    setSize (760, 600);
    refreshVisibility();
    startTimerHz (10);
}

GenreAutoMixEditor::~GenreAutoMixEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

GenreAutoMixEditor::Knob& GenreAutoMixEditor::addKnob (std::vector<std::unique_ptr<Knob>>& list,
                                                       const juce::String& id, const juce::String& name)
{
    auto k = std::make_unique<Knob>();
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, dim);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    list.push_back (std::move (k));
    return *list.back();
}

void GenreAutoMixEditor::refreshVisibility()
{
    const bool m = proc.isMaster();
    lastWasMaster = m;

    for (juce::Component* c : { (juce::Component*) &promptLabel, (juce::Component*) &prompt, (juce::Component*) &promptInfo,
                                (juce::Component*) &genreLabel, (juce::Component*) &genreBox, (juce::Component*) &learnBtn,
                                (juce::Component*) &mixBtn, (juce::Component*) &resetBtn, (juce::Component*) &status,
                                (juce::Component*) &meters, (juce::Component*) &report })
        c->setVisible (m);
    for (auto& k : masterKnobs) { k->slider.setVisible (m); k->label.setVisible (m); }

    for (juce::Component* c : { (juce::Component*) &roleLabel, (juce::Component*) &roleBox,
                                (juce::Component*) &trackInfo, (juce::Component*) &bypassBtn })
        c->setVisible (! m);
    for (auto& k : trackKnobs) { k->slider.setVisible (! m); k->label.setVisible (! m); }

    resized();
    repaint();
}

void GenreAutoMixEditor::timerCallback()
{
    if (proc.isMaster() != lastWasMaster) refreshVisibility();

    if (proc.isMaster())
    {
        if (! proc.isActiveMaster())
        {
            status.setText ("Another Master instance already exists. Use only one (on the Master track).",
                            juce::dontSendNotification);
            return;
        }

        promptInfo.setText (proc.describePrompt(), juce::dontSendNotification);

        const bool learning = proc.isLearning();
        learnBtn.setButtonText (learning ? "STOP LEARN" : "LEARN");
        learnBtn.setColour (juce::TextButton::buttonColourId, learning ? juce::Colour (0xffc2410c) : panel);

        juce::String s;
        s << proc.countConnectedTracks() << " tracks linked";
        if (learning) s << "  |  learning: " << juce::String (proc.getLearnedSeconds(), 1) << " s heard";
        const auto ph = proc.getPhase();
        if (ph == GenreAutoMixProcessor::MasterPhase::MatchIn || ph == GenreAutoMixProcessor::MasterPhase::MatchOut)
            s << "  |  matching master loudness...";
        else if (ph == GenreAutoMixProcessor::MasterPhase::Done)
            s << "  |  master loudness matched";
        if (proc.getMatchNote().isNotEmpty()) s << "  (" << proc.getMatchNote() << ")";
        status.setText (s + (lastStatus.isNotEmpty() ? "\n" + lastStatus : juce::String()), juce::dontSendNotification);

        meters.setText ("Master in " + juce::String (proc.getMasterInLufs(), 1) + " LUFS   out "
                        + juce::String (proc.getMasterOutLufs(), 1) + " LUFS   limiter "
                        + juce::String (proc.getLimiterGrDb(), 1) + " dB", juce::dontSendNotification);

        if (! report.hasKeyboardFocus (true))
            report.setText (proc.buildReport(), false);
    }
    else
    {
        auto f = proc.getOwnFeatures();
        const int role = proc.getResolvedRole();
        auto name = proc.getTrackName();
        juce::String s;
        s << (name.isNotEmpty() ? name : juce::String ("(track name not provided by host)"));
        s << "   |   mixed as: " << (role > 0 ? gam::roleNames()[role] : juce::String ("not yet"));
        if (f.seconds > 0)
            s << "   |   heard " << juce::String (f.seconds, 1) << " s, " << juce::String (f.lufs, 1) << " LUFS"
              << ", low " << (int) std::round (f.lowFrac * 100) << "% / high " << (int) std::round (f.highFrac * 100) << "%";
        trackInfo.setText (s, juce::dontSendNotification);
    }
}

void GenreAutoMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto top = getLocalBounds().removeFromTop (48);
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.25f), 0, 0, accent2.withAlpha (0.15f), (float) getWidth(), 0, false));
    g.fillRect (top);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("GENRE AUTOMIX", top.reduced (16, 0), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (proc.isMaster() ? "MASTER  -  put on the Master track" : "TRACK  -  put on every channel",
                top.reduced (16, 0).withTrimmedLeft (190), juce::Justification::centredLeft);
}

void GenreAutoMixEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (48).reduced (12, 10);
    auto modeArea = top.removeFromRight (180);
    modeLabel.setBounds (modeArea.removeFromLeft (50));
    modeBox.setBounds (modeArea);

    r.reduce (14, 10);

    auto layoutKnobs = [] (std::vector<std::unique_ptr<Knob>>& knobs, juce::Rectangle<int> area, int perRow)
    {
        const int rows = ((int) knobs.size() + perRow - 1) / perRow;
        const int h = area.getHeight() / juce::jmax (1, rows);
        const int w = area.getWidth() / perRow;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int row = (int) i / perRow, col = (int) i % perRow;
            juce::Rectangle<int> cell (area.getX() + col * w, area.getY() + row * h, w, h);
            knobs[i]->label.setBounds (cell.removeFromTop (16));
            cell.removeFromBottom (6);
            knobs[i]->slider.setBounds (cell.reduced (4, 0));
        }
    };

    if (proc.isMaster())
    {
        promptLabel.setBounds (r.removeFromTop (18));
        auto row = r.removeFromTop (34);
        prompt.setBounds (row);
        r.removeFromTop (4);
        promptInfo.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);

        auto row2 = r.removeFromTop (30);
        genreLabel.setBounds (row2.removeFromLeft (110));
        genreBox.setBounds (row2.removeFromLeft (190));
        row2.removeFromLeft (12);
        resetBtn.setBounds (row2.removeFromRight (80));
        row2.removeFromRight (8);
        mixBtn.setBounds (row2.removeFromRight (130));
        row2.removeFromRight (8);
        learnBtn.setBounds (row2.removeFromRight (120));

        r.removeFromTop (6);
        status.setBounds (r.removeFromTop (38));
        meters.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);

        layoutKnobs (masterKnobs, r.removeFromBottom (110), 4);
        r.removeFromBottom (6);
        report.setBounds (r);
    }
    else
    {
        auto row = r.removeFromTop (28);
        roleLabel.setBounds (row.removeFromLeft (44));
        roleBox.setBounds (row.removeFromLeft (160));
        bypassBtn.setBounds (row.removeFromRight (150));
        r.removeFromTop (4);
        trackInfo.setBounds (r.removeFromTop (36));
        r.removeFromTop (8);
        layoutKnobs (trackKnobs, r, 5);
    }
}
