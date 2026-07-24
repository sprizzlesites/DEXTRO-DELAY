#include "PluginEditor.h"
#include "DextroDelayData.h"

using namespace juce;

//==============================================================================
//  ScopeView
//==============================================================================
ScopeView::ScopeView (DextroDelayAudioProcessor& p) : proc (p)
{
    frames.fill ({});
    setInterceptsMouseClicks (true, true);
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        freqP[b] = proc.apvts.getParameter ("eqfreq" + String (b));
        gainP[b] = proc.apvts.getParameter ("eqgain" + String (b));
    }
    startTimerHz (30);
}

ScopeView::~ScopeView() { stopTimer(); }

bool ScopeView::eqMode() const
{
    return proc.apvts.getRawParameterValue ("eqon")->load() > 0.5f;
}

void ScopeView::timerCallback()
{
    glowPhase += 0.06f;
    if (! eqMode())
        proc.readScope (frames);
    repaint();
}

//---------------------------------------------------------------- geometry
Rectangle<float> ScopeView::plot() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 12.0f);
}

float ScopeView::freqToX (float hz) const
{
    auto a = plot();
    const float t = std::log (hz / fMin) / std::log (fMax / fMin);
    return a.getX() + jlimit (0.0f, 1.0f, t) * a.getWidth();
}

float ScopeView::xToFreq (float x) const
{
    auto a = plot();
    const float t = jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return fMin * std::pow (fMax / fMin, t);
}

float ScopeView::gainToY (float db) const
{
    auto a = plot();
    const float t = (dxeq::kMaxGainDb - db) / (2.0f * dxeq::kMaxGainDb);
    return a.getY() + jlimit (0.0f, 1.0f, t) * a.getHeight();
}

float ScopeView::yToGain (float y) const
{
    auto a = plot();
    const float t = jlimit (0.0f, 1.0f, (y - a.getY()) / a.getHeight());
    return dxeq::kMaxGainDb - t * 2.0f * dxeq::kMaxGainDb;
}

int ScopeView::nodeAt (Point<float> pos) const
{
    int best = -1; float bestD = 15.0f;
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        if (! freqP[b] || ! gainP[b]) continue;
        const float f = freqP[b]->getNormalisableRange().convertFrom0to1 (freqP[b]->getValue());
        const float g = gainP[b]->getNormalisableRange().convertFrom0to1 (gainP[b]->getValue());
        const float d = pos.getDistanceFrom ({ freqToX (f), gainToY (g) });
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

//---------------------------------------------------------------- painting
static void drawGlass (Graphics& g, Rectangle<float> r)
{
    ColourGradient glass (neon::screenBg.brighter (0.06f), r.getCentreX(), r.getY(),
                          neon::screenBg.darker (0.35f), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (r, 8.0f);
}

void ScopeView::paint (Graphics& g)
{
    if (eqMode()) paintEq (g);
    else          paintScope (g);
}

void ScopeView::paintScope (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawGlass (g, r);

    g.setColour (neon::neonBlue.withAlpha (0.08f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY() + 4.0f, r.getBottom() - 4.0f);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX() + 4.0f, r.getRight() - 4.0f);

    const int   N   = (int) frames.size();
    const float x0  = r.getX() + 6.0f;
    const float w   = r.getWidth() - 12.0f;
    const float top = r.getY() + 8.0f;
    const float bot = r.getBottom() - 8.0f;
    const float h   = bot - top;

    auto envToY  = [&] (float env)  { return bot - jlimit (0.0f, 1.0f, std::sqrt (env) * 1.4f) * h; };
    auto duckToY = [&] (float duck) { return top + (1.0f - jlimit (0.0f, 1.0f, duck)) * h; };

    {
        Path env;
        env.startNewSubPath (x0, bot);
        for (int i = 0; i < N; ++i)
            env.lineTo (x0 + w * (float) i / (float) (N - 1), envToY (frames[(size_t) i].env));
        env.lineTo (x0 + w, bot);
        env.closeSubPath();
        g.setColour (neon::neonBlue.withAlpha (0.22f));
        g.fillPath (env);
        g.setColour (neon::neonBlue.withAlpha (0.75f));
        g.strokePath (env, PathStrokeType (1.2f));
    }
    {
        Path duck;
        for (int i = 0; i < N; ++i)
        {
            const float x = x0 + w * (float) i / (float) (N - 1);
            const float y = duckToY (frames[(size_t) i].duck);
            if (i == 0) duck.startNewSubPath (x, y); else duck.lineTo (x, y);
        }
        const float bloom = 0.5f + 0.5f * std::sin (glowPhase);
        g.setColour (neon::neonPurple.withAlpha (0.18f + 0.10f * bloom));
        g.strokePath (duck, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (neon::neonPurple.brighter (0.3f));
        g.strokePath (duck, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setFont (neon::makeFont (11.0f, Font::bold));
    g.setColour (neon::neonBlue);
    g.drawText ("VOCAL", r.reduced (8.0f).removeFromTop (14.0f).removeFromLeft (60.0f), Justification::centredLeft);
    g.setColour (neon::neonPurple);
    g.drawText ("ECHO LEVEL", r.reduced (8.0f).removeFromTop (14.0f).withTrimmedLeft (60.0f).removeFromLeft (100.0f), Justification::centredLeft);

    const float grDb = 20.0f * std::log10 (jmax (1.0e-4f, proc.getCurrentDuckGain()));
    g.setColour (neon::textBright);
    g.setFont (neon::makeFont (12.0f, Font::bold));
    g.drawText (String (grDb, 1) + " dB", r.reduced (8.0f).removeFromTop (16.0f), Justification::topRight);

    g.setColour (neon::textDim);
    g.setFont (neon::makeFont (10.5f, Font::bold));
    g.drawText (String (proc.getCurrentBpm(), 1) + " BPM",
                r.reduced (8.0f).removeFromBottom (14.0f).removeFromRight (90.0f), Justification::bottomRight);
}

void ScopeView::paintEq (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawGlass (g, r);
    auto a = plot();

    double sr = proc.getSampleRate();
    if (sr < 8000.0) sr = 44100.0;

    // --- grid + labels ---
    g.setFont (neon::makeFont (9.5f, Font::plain));
    const float freqTicks[] { 100.0f, 1000.0f, 10000.0f };
    const char* freqLbls[]  { "100", "1k", "10k" };
    for (int i = 0; i < 3; ++i)
    {
        const float x = freqToX (freqTicks[i]);
        g.setColour (neon::neonBlue.withAlpha (0.10f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (neon::textDim.withAlpha (0.7f));
        g.drawText (freqLbls[i], Rectangle<float> (x - 16.0f, a.getBottom() - 12.0f, 32.0f, 12.0f), Justification::centred);
    }
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour ((db == 0 ? neon::textDim.withAlpha (0.35f) : neon::neonBlue.withAlpha (0.08f)));
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        if (db != 0)
        {
            g.setColour (neon::textDim.withAlpha (0.6f));
            g.drawText (String (db > 0 ? "+" : "") + String (db), Rectangle<float> (a.getX() + 2.0f, y - 6.0f, 26.0f, 12.0f), Justification::centredLeft);
        }
    }

    // --- compute per-band coeffs from current param values ---
    const auto& cfg = dxeq::bands();
    dxeq::Coeffs co[dxeq::kNumBands];
    float bf[dxeq::kNumBands], bg[dxeq::kNumBands];
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        bf[b] = freqP[b] ? freqP[b]->getNormalisableRange().convertFrom0to1 (freqP[b]->getValue()) : cfg[(size_t) b].defFreq;
        bg[b] = gainP[b] ? gainP[b]->getNormalisableRange().convertFrom0to1 (gainP[b]->getValue()) : 0.0f;
        co[b] = dxeq::computeCoeffs (cfg[(size_t) b].type, (double) bf[b], (double) bg[b], (double) cfg[(size_t) b].q, sr);
    }

    // --- composite response curve ---
    const bool active = eqMode();
    Path curve;
    const int steps = 220;
    for (int i = 0; i <= steps; ++i)
    {
        const float x  = a.getX() + a.getWidth() * (float) i / (float) steps;
        const float hz = xToFreq (x);
        double dbSum = 0.0;
        for (int b = 0; b < dxeq::kNumBands; ++b) dbSum += dxeq::magnitudeDb (co[b], hz, sr);
        const float y = gainToY ((float) dbSum);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    const float bloom = 0.5f + 0.5f * std::sin (glowPhase);
    g.setColour (neon::neonPurple.withAlpha ((active ? 0.22f : 0.10f) + 0.08f * bloom));
    g.strokePath (curve, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (active ? neon::neonPurple.brighter (0.35f) : neon::neonPurple.withAlpha (0.5f));
    g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // --- draggable nodes (shelves purple, bells blue) ---
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        const bool shelf = (cfg[(size_t) b].type != dxeq::Type::Peak);
        const auto col = shelf ? neon::neonPurple : neon::neonBlue;
        const float x = freqToX (bf[b]);
        const float y = gainToY (bg[b]);
        const bool hot = (b == dragBand || b == hoverBand);
        const float rad = hot ? 8.5f : 6.5f;

        for (int k = 3; k >= 1; --k) { g.setColour (col.withAlpha (0.10f * (float) (4 - k) * (hot ? 1.4f : 1.0f))); g.drawEllipse (x - rad - (float) k, y - rad - (float) k, (rad + (float) k) * 2.0f, (rad + (float) k) * 2.0f, 1.4f); }
        g.setColour (Colour (0xff0a0a12));
        g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (col.brighter (hot ? 0.5f : 0.2f));
        g.drawEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f, hot ? 2.2f : 1.6f);
        g.setColour (Colours::white.withAlpha (hot ? 0.9f : 0.5f));
        g.fillEllipse (x - 1.6f, y - 1.6f, 3.2f, 3.2f);
        g.setColour (neon::textDim);
        g.setFont (neon::makeFont (9.0f, Font::bold));
        g.drawText (String (b + 1), Rectangle<float> (x - 8.0f, y - 18.0f, 16.0f, 11.0f), Justification::centred);
    }

    // --- header ---
    g.setColour (neon::neonPurple);
    g.setFont (neon::makeFont (11.0f, Font::bold));
    g.drawText ("WET EQ", r.reduced (8.0f).removeFromTop (14.0f).removeFromLeft (80.0f), Justification::centredLeft);
    if (! active)
    {
        g.setColour (neon::textDim);
        g.drawText ("(bypassed)", r.reduced (8.0f).removeFromTop (14.0f).withTrimmedLeft (60.0f).removeFromLeft (90.0f), Justification::centredLeft);
    }
}

//---------------------------------------------------------------- mouse
void ScopeView::mouseDown (const MouseEvent& e)
{
    if (! eqMode()) return;
    const int n = nodeAt (e.position);
    if (n >= 0 && freqP[n] && gainP[n])
    {
        dragBand = n;
        freqP[n]->beginChangeGesture();
        gainP[n]->beginChangeGesture();
    }
}

void ScopeView::mouseDrag (const MouseEvent& e)
{
    if (! eqMode() || dragBand < 0) return;
    auto* fp = freqP[dragBand]; auto* gp = gainP[dragBand];
    fp->setValueNotifyingHost (fp->getNormalisableRange().convertTo0to1 (xToFreq (e.position.x)));
    gp->setValueNotifyingHost (gp->getNormalisableRange().convertTo0to1 (yToGain (e.position.y)));
    repaint();
}

void ScopeView::mouseUp (const MouseEvent&)
{
    if (dragBand >= 0)
    {
        freqP[dragBand]->endChangeGesture();
        gainP[dragBand]->endChangeGesture();
        dragBand = -1;
    }
}

void ScopeView::mouseMove (const MouseEvent& e)
{
    if (! eqMode()) { if (hoverBand != -1) { hoverBand = -1; repaint(); } return; }
    const int n = nodeAt (e.position);
    if (n != hoverBand)
    {
        hoverBand = n;
        setMouseCursor (n >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void ScopeView::mouseDoubleClick (const MouseEvent& e)
{
    if (! eqMode()) return;
    const int n = nodeAt (e.position);
    if (n >= 0 && gainP[n])
    {
        gainP[n]->beginChangeGesture();
        gainP[n]->setValueNotifyingHost (gainP[n]->getNormalisableRange().convertTo0to1 (0.0f));
        gainP[n]->endChangeGesture();
        repaint();
    }
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

    // Ping-pong toggle.
    pingpong.setClickingTogglesState (true);
    pingpong.getProperties().set ("neonBlack", true);
    pingpong.getProperties().set ("neonPink", false);   // blue idle border (delay section)
    pingpong.getProperties().set ("onCyan", true);
    pingpong.setColour (TextButton::textColourOffId, neon::neonBlue);
    pingpong.setColour (TextButton::textColourOnId, neon::neonBlue.brighter (0.4f));
    addAndMakeVisible (pingpong);
    pingAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "pingpong", pingpong);

    // SYNC toggle.
    syncBtn.setClickingTogglesState (true);
    syncBtn.getProperties().set ("neonBlack", true);
    syncBtn.getProperties().set ("neonPink", false);
    syncBtn.setColour (TextButton::textColourOffId, neon::neonBlue);
    syncBtn.setColour (TextButton::textColourOnId, neon::neonBlue.brighter (0.4f));
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "sync", syncBtn);

    // Division knob shares the TIME cell.
    divKnob = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
    divKnob->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 15);
    divKnob->getProperties().set ("glowPink", false);
    divKnob->setColour (Slider::textBoxTextColourId, neon::neonBlue);
    addChildComponent (*divKnob);
    divAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "division", *divKnob);

    // EQ toggle on the wave box (purple — it shapes the wet tone).
    eqBtn.setClickingTogglesState (true);
    eqBtn.getProperties().set ("neonBlack", true);
    eqBtn.getProperties().set ("neonPink", true);       // purple border
    eqBtn.setColour (TextButton::textColourOffId, neon::neonPurple);
    eqBtn.setColour (TextButton::textColourOnId, neon::neonPurple.brighter (0.4f));
    addAndMakeVisible (eqBtn);
    eqAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "eqon", eqBtn);

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
    setSize (780, 600);
    updateSyncUI();
}

void DextroDelayAudioProcessorEditor::updateSyncUI()
{
    const bool synced = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    lastSyncState = synced ? 1 : 0;
    if (! knobs.empty()) knobs[0]->setVisible (! synced);
    if (divKnob)         divKnob->setVisible (synced);
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
    auto knob = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
    knob->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 15);
    knob->getProperties().set ("glowPink", spec.purple);
    knob->setColour (Slider::textBoxTextColourId, spec.purple ? neon::neonPurple : neon::neonBlue);
    addAndMakeVisible (*knob);
    attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, spec.id, *knob));

    auto label = std::make_unique<Label> (juce::String(), spec.label);
    label->setJustificationType (Justification::centred);
    label->setFont (neon::makeFont (11.5f, Font::bold));
    label->setColour (Label::textColourId, spec.purple ? neon::neonBlue : neon::neonPurple);
    addAndMakeVisible (*label);

    knobs.push_back (std::move (knob));
    labels.push_back (std::move (label));
}

void DextroDelayAudioProcessorEditor::timerCallback()
{
    glowPhase += 0.045f;
    rimGlow = 0.65f + 0.35f * std::sin (glowPhase);

    const int synced = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f ? 1 : 0;
    if (synced != lastSyncState)
        updateSyncUI();

    repaint();
}

void DextroDelayAudioProcessorEditor::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (neon::voidBg);

    auto plate = b.reduced (14.0f);
    if (faceplate.isValid())
        g.drawImage (faceplate, plate, RectanglePlacement::stretchToFit);
    neon::glowRoundedRect (g, plate, 16.0f, neon::neonPurple, 0.5f + 0.35f * rimGlow, 5);
    g.setColour (neon::neonBlue.withAlpha (0.18f * rimGlow));
    g.drawRoundedRectangle (plate.reduced (2.0f), 14.0f, 1.0f);

    // Title.
    {
        auto tRect = Rectangle<float> (plate.getX() + 18.0f, plate.getY() + 12.0f, 360.0f, 40.0f);
        g.setFont (neon::makeFont (30.0f, Font::bold));
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (neon::neonPurple.withAlpha (0.10f * rimGlow * (float) i));
            g.drawText ("DEXTRO DELAY", tRect.expanded ((float) i * 0.4f), Justification::centredLeft);
        }
        g.setColour (neon::neonPurple.brighter (0.35f));
        g.drawText ("DEXTRO DELAY", tRect, Justification::centredLeft);
        g.setFont (neon::makeFont (11.0f, Font::bold));
        g.setColour (neon::neonBlue.withAlpha (0.9f));
        g.drawText ("SELF-DUCKING DELAY", tRect.translated (2.0f, 24.0f), Justification::centredLeft);
    }

    // Wave box neon border (the scope glass sits inside).
    neon::glowRoundedRect (g, wavePanel.toFloat(), 8.0f, neon::neonPurple, 0.5f + 0.3f * rimGlow, 4);

    // DELAY panel (blue).
    neon::drawInsetPanel (g, delayPanel.toFloat(), 10.0f, neon::neonBlue, 0.7f);
    // DYNAMICS panel (purple).
    neon::drawInsetPanel (g, dynPanel.toFloat(), 10.0f, neon::neonPurple, 0.7f);

    // Panel headers.
    g.setFont (neon::makeFont (12.0f, Font::bold));
    g.setColour (neon::neonBlue);
    g.drawText ("DELAY  ///  ECHO", Rectangle<float> (delayPanel.getX() + 12.0f, delayPanel.getY() + 7.0f, 240.0f, 16.0f), Justification::centredLeft);
    g.setColour (neon::neonPurple);
    g.drawText ("SELF-DUCK  ///  DYNAMICS + OUTPUT", Rectangle<float> (dynPanel.getX() + 12.0f, dynPanel.getY() + 7.0f, 320.0f, 16.0f), Justification::centredLeft);

    // Logo, top-right (in the title strip, clear of the knob panels).
    if (logo.isValid())
    {
        const float lh = 40.0f;
        const float lw = lh * (float) logo.getWidth() / (float) logo.getHeight();
        auto lr = Rectangle<float> (plate.getRight() - lw - 20.0f, plate.getY() + 14.0f, lw, lh);
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
    const int x0 = plate.getX() + 18;
    const int w  = plate.getWidth() - 36;

    // Wave box.
    const int waveY = plate.getY() + 60;
    const int waveH = 140;
    wavePanel = { x0, waveY, w, waveH };
    scope.setBounds (wavePanel.reduced (2));
    eqBtn.setBounds (x0 + 8, waveY + waveH - 30, 44, 22);

    // Panels.
    const int panelH = 158;
    const int delayY = waveY + waveH + 12;
    const int dynY   = delayY + panelH + 12;
    delayPanel = { x0, delayY, w, panelH };
    dynPanel   = { x0, dynY,   w, panelH };

    // Delay-section buttons in the delay panel header (top-right).
    const int hy = delayPanel.getY() + 7;
    pingpong.setBounds (delayPanel.getRight() - 12 - 92, hy, 92, 20);
    syncBtn.setBounds  (pingpong.getX() - 8 - 60, hy, 60, 20);

    // Knob rows.
    const int cols = 6;
    const int cellW = w / cols;
    const int knobH = 80, valH = 15, nameH = 15;
    const int delayKnobY = delayPanel.getY() + 40;
    const int dynKnobY   = dynPanel.getY()   + 40;

    auto place = [&] (int index, int y, int col)
    {
        auto& knob  = *knobs[(size_t) index];
        auto& label = *labels[(size_t) index];
        const int cx = x0 + col * cellW + cellW / 2;
        const int kw = jmin (cellW - 8, 96);
        knob.setBounds (cx - kw / 2, y, kw, knobH + valH);
        label.setBounds (cx - cellW / 2, y + knobH + valH, cellW, nameH);
    };
    for (int i = 0; i < 6; ++i) place (i,     delayKnobY, i);
    for (int i = 0; i < 6; ++i) place (i + 6, dynKnobY,   i);

    if (divKnob && ! knobs.empty())
        divKnob->setBounds (knobs[0]->getBounds());
}
