#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

#include "DSP/DuckingDelay.h"

//==============================================================================
/** DEXTRO DELAY — a self-ducking stereo delay.

    The dry input is used as its own sidechain: an envelope follower ducks the
    wet echoes while the vocal is present and lets them swell back up in the
    gaps. All processing lives in the JUCE-free DuckingDelay engine; this class
    is the JUCE/APVTS wrapper and exposes a small metering ring for the UI scope.
*/
class DextroDelayAudioProcessor : public juce::AudioProcessor
{
public:
    DextroDelayAudioProcessor();
    ~DextroDelayAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DEXTRO DELAY"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==========================================================================
    // Lock-free metering ring for the editor's scope: pairs of
    // (dry input envelope 0..1, duck gain 0..1), newest at head.
    static constexpr int kScopeSize = 512;
    struct ScopeFrame { float env = 0.0f; float duck = 1.0f; };

    int  readScope (std::array<ScopeFrame, kScopeSize>& out) const;
    float getCurrentDuckGain() const { return currentDuck.load(); }

private:
    DuckingDelay engine;

    // scope ring
    std::array<std::atomic<float>, kScopeSize> scopeEnv;
    std::array<std::atomic<float>, kScopeSize> scopeDuck;
    std::atomic<int> scopeHead { 0 };
    std::atomic<float> currentDuck { 1.0f };
    int   scopeDecim = 0;
    int   scopeDecimN = 32;   // push ~1 frame per 32 samples

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DextroDelayAudioProcessor)
};
