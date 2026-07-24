#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"

//==============================================================================
/** Live "screen" scope: draws the dry-vocal envelope (blue) and the duck-gain
    history (purple) scrolling right-to-left, so you can watch the echoes get
    pulled down under the vocal and swell back up in the gaps. */
class ScopeView : public juce::Component,
                  private juce::Timer
{
public:
    explicit ScopeView (DextroDelayAudioProcessor& p);
    ~ScopeView() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    DextroDelayAudioProcessor& proc;
    std::array<DextroDelayAudioProcessor::ScopeFrame, DextroDelayAudioProcessor::kScopeSize> frames;
    float glowPhase = 0.0f;

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

    float rimGlow = 0.0f;
    float glowPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DextroDelayAudioProcessorEditor)
};
