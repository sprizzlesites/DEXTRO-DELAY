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
    // Worst case: a 5 s delay with high feedback still ringing — under-reporting
    // this truncates the echoes on an offline/bounce render.
    double getTailLengthSeconds() const override { return 10.0; }

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
    float getCurrentBpm()      const { return currentBpm.load(); }

    //==========================================================================
    // Spectrum analyzer of the wet (post-duck, post-EQ) signal. The audio
    // thread fills a fifo; the editor windows + transforms the ready block.
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize  = 1 << kFftOrder;   // 2048
    bool   analyzerReady()      const { return analyzerBlockReady.load(); }
    void   clearAnalyzerReady()       { analyzerBlockReady.store (false); }
    float* analyzerData()             { return analyzerFftData.data(); }

private:
    DuckingDelay engine;

    // Preallocated scratch right-channel for the mono path (no audio-thread
    // allocation).
    juce::AudioBuffer<float> monoScratch;

    // scope ring
    std::array<std::atomic<float>, kScopeSize> scopeEnv;
    std::array<std::atomic<float>, kScopeSize> scopeDuck;
    std::atomic<int> scopeHead { 0 };
    std::atomic<float> currentDuck { 1.0f };
    std::atomic<float> currentBpm  { 120.0f };

    // analyzer fifo (audio thread writes, editor reads)
    std::array<float, (size_t) kFftSize>       analyzerFifo { };
    std::array<float, (size_t) (kFftSize * 2)> analyzerFftData { };
    std::atomic<bool> analyzerBlockReady { false };
    int analyzerFifoIndex = 0;
    void pushAnalyzerSample (float s);
    int   scopeDecim = 0;
    int   scopeDecimN = 32;   // push ~1 frame per 32 samples

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DextroDelayAudioProcessor)
};
