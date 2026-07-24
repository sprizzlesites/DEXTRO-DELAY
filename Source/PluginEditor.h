#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"
#include "DSP/Equalizer.h"

//==============================================================================
/** The "wave box" screen. Two modes, switched by the EQ toggle:
      * SCOPE — the dry-vocal envelope (blue) vs the duck-gain history (purple),
      * EQ    — an interactive 5-point EQ curve editor for the wet delay signal,
                with draggable nodes writing straight to the parameters. */
class ScopeView : public juce::Component,
                  private juce::Timer
{
public:
    explicit ScopeView (DextroDelayAudioProcessor& p);
    ~ScopeView() override;

    void paint (juce::Graphics&) override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    bool eqMode() const;
    void paintScope (juce::Graphics&);
    void paintEq    (juce::Graphics&);
    void updateSpectrum();

    juce::Rectangle<float> plot() const;
    float freqToX (float hz) const;
    float xToFreq (float x)  const;
    float gainToY (float db) const;
    float yToGain (float y)  const;
    int   nodeAt  (juce::Point<float> pos) const;

    DextroDelayAudioProcessor& proc;
    std::array<DextroDelayAudioProcessor::ScopeFrame, DextroDelayAudioProcessor::kScopeSize> frames;
    float glowPhase = 0.0f;

    int dragBand = -1;
    int hoverBand = -1;
    juce::RangedAudioParameter* freqP[dxeq::kNumBands] { };
    juce::RangedAudioParameter* gainP[dxeq::kNumBands] { };
    juce::RangedAudioParameter* qP   [dxeq::kNumBands] { };

    static constexpr float fMin = 20.0f, fMax = 20000.0f;

    // Reactive spectrum analyzer (cube-pixel bars behind the EQ curve).
    static constexpr int kBars = 30;
    juce::dsp::FFT forwardFFT { DextroDelayAudioProcessor::kFftOrder };
    juce::dsp::WindowingFunction<float> window
        { (size_t) DextroDelayAudioProcessor::kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kBars> spectrum { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};

//==============================================================================
class DextroDelayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit DextroDelayAudioProcessorEditor (DextroDelayAudioProcessor&);
    ~DextroDelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct KnobSpec { juce::String id, label; bool purple; };

    void addKnob (const KnobSpec& spec);
    void updateSyncUI();

    DextroDelayAudioProcessor& processor;
    neon::NeonLookAndFeel lnf;

    juce::Image faceplate;
    juce::Image logo;

    ScopeView scope;

    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>>  labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    juce::TextButton pingpong { "PING-PONG" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pingAtt;

    // Tempo-sync: a SYNC toggle plus a note-division knob that shares the TIME
    // cell — the division knob shows when synced, the free-ms knob when not.
    juce::TextButton syncBtn { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    std::unique_ptr<juce::Slider> divKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  divAtt;
    int lastSyncState = -1;

    // EQ toggle on the wave box.
    juce::TextButton eqBtn { "EQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqAtt;

    // Input section in the top bar — a volume (delay-send) knob + a horizontal
    // green pan slider.
    juce::Slider inVolKnob;
    juce::Label  inVolLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inVolAtt;
    juce::Slider panSlider;
    juce::Label  panLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAtt;
    juce::Rectangle<int> panArea;

    // Panel rectangles (set in resized, drawn in paint).
    juce::Rectangle<int> delayPanel, dynPanel, wavePanel;

    float rimGlow = 0.0f;
    float glowPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DextroDelayAudioProcessorEditor)
};
