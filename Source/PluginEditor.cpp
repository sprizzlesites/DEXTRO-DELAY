#include "PluginEditor.h"
#include "DextroDelayData.h"

using namespace juce;

//==============================================================================
//  ScopeView
//==============================================================================
ScopeView::ScopeView (DextroDelayAudioProcessor& p) : proc (p)
{
    frames.fill ({});
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

ScopeView::~ScopeView() { stopTimer(); }

void ScopeView::timerCallback()
{
    glowPhase += 0.06f;
    proc.readScope (frames);
    repaint();
}

void ScopeView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Deep purple-black glass.
    ColourGradient glass (neon::screenBg.brighter (0.06f), r.getCentreX(), r.getY(),
                          neon::screenBg.darker (0.35f), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (r, 8.0f);

    // Faint grid.
    g.setColour (neon::neonBlue.withAlpha (0.08f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / 8.0f;
        g.drawVerticalLine ((int) x, r.getY() + 4.0f, r.getBottom() - 4.0f);
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = r.getY() + r.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);
    }

    const int   N   = (int) frames.size();
    const float x0  = r.getX() + 6.0f;
    const float w   = r.getWidth() - 12.0f;
    const float top = r.getY() + 8.0f;
    const float bot = r.getBottom() - 8.0f;
    const float h   = bot - top;

    auto envToY  = [&] (float env)  { const float e = jlimit (0.0f, 1.0f, std::sqrt (env) * 1.4f); return bot - e * h; };
    auto duckToY = [&] (float duck) { return top + (1.0f - jlimit (0.0f, 1.0f, duck)) * h; };

    // Dry-vocal envelope — filled blue area from the bottom.
    {
        Path env;
        env.startNewSubPath (x0, bot);
        for (int i = 0; i < N; ++i)
        {
            const float x = x0 + w * (float) i / (float) (N - 1);
            env.lineTo (x, envToY (frames[(size_t) i].env));
        }
        env.lineTo (x0 + w, bot);
        env.closeSubPath();
        g.setColour (neon::neonBlue.withAlpha (0.22f));
        g.fillPath (env);
        g.setColour (neon::neonBlue.withAlpha (0.75f));
        g.strokePath (env, PathStrokeType (1.2f));
    }

    // Duck-gain history — bright purple neon line (top = echoes open, dip = ducked).
    {
        Path duck;
        for (int i = 0; i < N; ++i)
        {
            const float x = x0 + w * (float) i / (float) (N - 1);
            const float y = duckToY (frames[(size_t) i].duck);
            if (i == 0) duck.startNewSubPath (x, y);
            else        duck.lineTo (x, y);
        }
        const float bloom = 0.5f + 0.5f * std::sin (glowPhase);
        g.setColour (neon::neonPurple.withAlpha (0.18f + 0.10f * bloom));
        g.strokePath (duck, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (neon::neonPurple.brighter (0.3f));
        g.strokePath (duck, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Legend.
    g.setFont (neon::makeFont (11.0f, Font::bold));
    g.setColour (neon::neonBlue);
    g.drawText ("VOCAL", r.reduced (8.0f).removeFromTop (14.0f).removeFromLeft (60.0f),
                Justification::centredLeft);
    g.setColour (neon::neonPurple);
    g.drawText ("ECHO LEVEL", r.reduced (8.0f).removeFromTop (14.0f).withTrimmedLeft (60.0f)
                                .removeFromLeft (100.0f), Justification::centredLeft);

    // Live gain-reduction readout (top-right).
    const float duckNow = proc.getCurrentDuckGain();
    const float grDb    = 20.0f * std::log10 (jmax (1.0e-4f, duckNow));
    g.setColour (neon::textBright);
    g.setFont (neon::makeFont (12.0f, Font::bold));
    g.drawText (String (grDb, 1) + " dB", r.reduced (8.0f).removeFromTop (16.0f),
                Justification::topRight);

    // Tempo readout (bottom-right) — the clock the synced division follows.
    g.setColour (neon::textDim);
    g.setFont (neon::makeFont (10.5f, Font::bold));
    g.drawText (String (proc.getCurrentBpm(), 1) + " BPM",
                r.reduced (8.0f).removeFromBottom (14.0f).removeFromRight (90.0f),
                Justification::bottomRight);
}

//==============================================================================
//  Editor
//==============================================================================
DextroDelayAudioProcessorEditor::DextroDelayAudioProcessorEditor (DextroDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), scope (p)
{
    setLookAndFeel (&lnf);

    logo = neon::cropTransparentBorder (
        ImageFileFormat::loadFrom (DextroDelayData::Logo_png, (size_t) DextroDelayData::Logo_pngSize));

    addAndMakeVisible (scope);

    // Ping-pong toggle: neon-black button, purple border, glows blue when on.
    pingpong.setClickingTogglesState (true);
    pingpong.getProperties().set ("neonBlack", true);
    pingpong.getProperties().set ("neonPink", true);   // purple idle border
    pingpong.getProperties().set ("onCyan", true);     // glow blue when engaged
    pingpong.setColour (TextButton::textColourOffId, neon::neonPurple);
    pingpong.setColour (TextButton::textColourOnId, neon::neonBlue);
    addAndMakeVisible (pingpong);
    pingAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "pingpong", pingpong);

    // SYNC toggle: neon-black button, blue (delay-section) border, glows when on.
    syncBtn.setClickingTogglesState (true);
    syncBtn.getProperties().set ("neonBlack", true);
    syncBtn.getProperties().set ("neonPink", false);   // blue idle border
    syncBtn.setColour (TextButton::textColourOffId, neon::neonBlue);
    syncBtn.setColour (TextButton::textColourOnId, neon::neonBlue.brighter (0.4f));
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "sync", syncBtn);

    // Note-division knob shares the TIME cell (blue, like the delay section).
    divKnob = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag,
                                        Slider::TextBoxBelow);
    divKnob->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 15);
    divKnob->getProperties().set ("glowPink", false);
    divKnob->setColour (Slider::textBoxTextColourId, neon::neonBlue);
    addChildComponent (*divKnob);   // visibility managed by updateSyncUI()
    divAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "division", *divKnob);

    // Knob bank. Blue = delay/time domain, purple = self-duck/dynamics.
    const std::array<KnobSpec, 12> specs {{
        { "time",     "TIME",      false }, { "offset",  "R OFFSET",  false },
        { "feedback", "FEEDBACK",  false }, { "tone",    "TONE",      false },
        { "lowcut",   "LOW CUT",   false }, { "width",   "WIDTH",     false },
        { "duck",     "DUCK",      true  }, { "thresh",  "THRESHOLD", true  },
        { "attack",   "ATTACK",    true  }, { "release", "RELEASE",   true  },
        { "mix",      "MIX",       true  }, { "output",  "OUTPUT",    true  },
    }};
    for (auto& s : specs) addKnob (s);

    startTimerHz (30);
    setSize (780, 560);
    updateSyncUI();
}

void DextroDelayAudioProcessorEditor::updateSyncUI()
{
    const bool synced = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    lastSyncState = synced ? 1 : 0;
    if (! knobs.empty()) knobs[0]->setVisible (! synced);   // free-ms TIME knob
    if (divKnob)          divKnob->setVisible (synced);      // note-division knob
    if (labels.size() > 0)
        labels[0]->setText (synced ? "SYNC" : "TIME", juce::dontSendNotification);
}

DextroDelayAudioProcessorEditor::~DextroDelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DextroDelayAudioProcessorEditor::addKnob (const KnobSpec& spec)
{
    auto knob = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag,
                                          Slider::TextBoxBelow);
    knob->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 15);
    knob->getProperties().set ("glowPink", spec.purple);   // purple vs blue ring
    knob->setColour (Slider::textBoxTextColourId, spec.purple ? neon::neonPurple : neon::neonBlue);
    addAndMakeVisible (*knob);
    attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, spec.id, *knob));

    auto label = std::make_unique<Label> (juce::String(), spec.label);
    label->setJustificationType (Justification::centred);
    label->setFont (neon::makeFont (11.5f, Font::bold));
    label->setColour (Label::textColourId, spec.purple ? neon::neonBlue : neon::neonPurple); // cross-colour
    addAndMakeVisible (*label);

    knobs.push_back (std::move (knob));
    labels.push_back (std::move (label));
}

void DextroDelayAudioProcessorEditor::timerCallback()
{
    glowPhase += 0.045f;
    rimGlow = 0.65f + 0.35f * std::sin (glowPhase);

    // React to the SYNC toggle (from the button or host automation).
    const int synced = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f ? 1 : 0;
    if (synced != lastSyncState)
        updateSyncUI();

    repaint();
}

void DextroDelayAudioProcessorEditor::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // The void.
    g.fillAll (neon::voidBg);

    // Metallic-black faceplate inset from the edges, with a breathing neon rim.
    auto plate = b.reduced (14.0f);
    if (faceplate.isValid())
        g.drawImage (faceplate, plate, RectanglePlacement::stretchToFit);
    neon::glowRoundedRect (g, plate, 16.0f, neon::neonPurple, 0.5f + 0.35f * rimGlow, 5);
    g.setColour (neon::neonBlue.withAlpha (0.18f * rimGlow));
    g.drawRoundedRectangle (plate.reduced (2.0f), 14.0f, 1.0f);

    // Title — DEXTRO DELAY, neon purple with a soft bloom + blue subtitle.
    auto header = plate.reduced (18.0f, 0.0f).withTrimmedTop (14.0f).withHeight (42.0f);
    {
        auto titleFont = neon::makeFont (30.0f, Font::bold);
        g.setFont (titleFont);
        auto tRect = header.removeFromLeft (360.0f);
        for (int i = 4; i >= 1; --i)   // bloom
        {
            g.setColour (neon::neonPurple.withAlpha (0.10f * rimGlow * (float) i));
            g.drawText ("DEXTRO DELAY", tRect.translated (0.0f, 0.0f).expanded ((float) i * 0.4f),
                        Justification::centredLeft);
        }
        g.setColour (neon::neonPurple.brighter (0.35f));
        g.drawText ("DEXTRO DELAY", tRect, Justification::centredLeft);

        g.setFont (neon::makeFont (11.0f, Font::bold));
        g.setColour (neon::neonBlue.withAlpha (0.9f));
        g.drawText ("SELF-DUCKING DELAY", tRect.translated (2.0f, 24.0f),
                    Justification::centredLeft);
    }

    // Section captions above the two knob rows.
    g.setFont (neon::makeFont (11.0f, Font::bold));
    if (! knobs.empty())
    {
        auto capA = Rectangle<float> (plate.getX() + 18.0f, knobs[0]->getY() - 15.0f, 200.0f, 13.0f);
        g.setColour (neon::neonBlue.withAlpha (0.85f));
        g.drawText ("DELAY  ///  ECHO", capA, Justification::centredLeft);

        auto capB = Rectangle<float> (plate.getX() + 18.0f, knobs[6]->getY() - 15.0f, 260.0f, 13.0f);
        g.setColour (neon::neonPurple.withAlpha (0.9f));
        g.drawText ("SELF-DUCK  ///  DYNAMICS + OUTPUT", capB, Justification::centredLeft);
    }

    // Logo, bottom-right on the faceplate.
    if (logo.isValid())
    {
        const float lw = 118.0f;
        const float lh = lw * (float) logo.getHeight() / (float) logo.getWidth();
        auto lr = Rectangle<float> (plate.getRight() - lw - 20.0f,
                                    plate.getBottom() - lh - 14.0f, lw, lh);
        g.setOpacity (0.95f);
        g.drawImage (logo, lr, RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }
}

void DextroDelayAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    faceplate = neon::makeChromePlate (b.getWidth(), b.getHeight());

    auto plate = b.reduced (14);
    auto content = plate.reduced (18, 0);
    const int x0 = content.getX();
    const int w  = content.getWidth();

    // Screen scope under the header.
    const int screenY = plate.getY() + 66;
    const int screenH = 150;
    scope.setBounds (x0, screenY, w, screenH);

    // Transport buttons in the screen's bottom-left corner.
    syncBtn.setBounds  (x0 + 8, screenY + screenH - 30, 64, 22);
    pingpong.setBounds (x0 + 8 + 64 + 8, screenY + screenH - 30, 92, 22);

    // Two rows of six knobs.
    const int cols = 6;
    const int cellW = w / cols;
    const int rowAy = screenY + screenH + 26;
    const int knobH = 82;
    const int valH  = 15;
    const int nameH = 15;
    const int rowH  = knobH + valH + nameH + 8;
    const int rowBy = rowAy + rowH;

    auto place = [&] (int index, int row, int col)
    {
        auto& knob  = *knobs[(size_t) index];
        auto& label = *labels[(size_t) index];
        const int cx = x0 + col * cellW + cellW / 2;
        const int y  = (row == 0 ? rowAy : rowBy);
        const int kw = jmin (cellW - 8, 96);
        knob.setBounds (cx - kw / 2, y, kw, knobH + valH);
        label.setBounds (cx - cellW / 2, y + knobH + valH, cellW, nameH);
    };

    for (int i = 0; i < 6; ++i)  place (i,     0, i);
    for (int i = 0; i < 6; ++i)  place (i + 6, 1, i);

    // The note-division knob overlays the TIME cell (index 0).
    if (divKnob && ! knobs.empty())
        divKnob->setBounds (knobs[0]->getBounds());
}
