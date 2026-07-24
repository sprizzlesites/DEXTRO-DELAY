#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>
#include <vector>

//==============================================================================
namespace pid
{
    static const juce::String sync     = "sync";
    static const juce::String division = "division";
    static const juce::String time     = "time";
    static const juce::String offset   = "offset";
    static const juce::String feedback = "feedback";
    static const juce::String tone     = "tone";
    static const juce::String lowcut   = "lowcut";
    static const juce::String width    = "width";
    static const juce::String duck     = "duck";
    static const juce::String thresh   = "thresh";
    static const juce::String attack   = "attack";
    static const juce::String release  = "release";
    static const juce::String mix      = "mix";
    static const juce::String output   = "output";
    static const juce::String pingpong = "pingpong";
    static const juce::String eqon     = "eqon";
    static juce::String eqFreq (int i) { return "eqfreq" + juce::String (i); }
    static juce::String eqGain (int i) { return "eqgain" + juce::String (i); }
}

namespace synced
{
    // Straight note values only, longest -> shortest.
    static const juce::StringArray divisionNames { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    // Length of each in quarter notes.
    static const float quarterMult[6] { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };

    // Delay time in ms for a note division at a given tempo.
    inline float divisionMs (int index, double bpm)
    {
        index = juce::jlimit (0, 5, index);
        const double quarterMs = 60000.0 / juce::jmax (20.0, bpm);
        return (float) (quarterMs * quarterMult[index]);
    }
}

//==============================================================================
DextroDelayAudioProcessor::DextroDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& a : scopeEnv)  a.store (0.0f);
    for (auto& a : scopeDuck) a.store (1.0f);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DextroDelayAudioProcessor::createLayout()
{
    using P    = juce::AudioParameterFloat;
    using R    = juce::NormalisableRange<float>;
    using Attr = juce::AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto ms   = [] (float v) { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; };
    auto hz   = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                                    : juce::String (v, 0) + " Hz"; };
    auto pct  = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto db   = [] (float v) { return juce::String (v, 1) + " dB"; };

    // Tempo sync: ON by default. When on, the Time knob locks to note values.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        pid::sync, "Sync", true));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        pid::division, "Division", synced::divisionNames, 3));   // default 1/8

    params.push_back (std::make_unique<P> (pid::time, "Time",
        R (5.0f, 2000.0f, 0.01f, 0.3f), 380.0f,
        Attr().withStringFromValueFunction ([ms] (float v, int) { return ms (v); })));

    params.push_back (std::make_unique<P> (pid::offset, "R Offset",
        R (0.0f, 250.0f, 0.01f, 0.5f), 0.0f,
        Attr().withStringFromValueFunction ([ms] (float v, int) { return ms (v); })));

    params.push_back (std::make_unique<P> (pid::feedback, "Feedback",
        R (0.0f, 0.98f, 0.001f), 0.45f,
        Attr().withStringFromValueFunction ([pct] (float v, int) { return pct (v); })));

    params.push_back (std::make_unique<P> (pid::tone, "Tone",
        R (500.0f, 18000.0f, 1.0f, 0.3f), 6500.0f,
        Attr().withStringFromValueFunction ([hz] (float v, int) { return hz (v); })));

    params.push_back (std::make_unique<P> (pid::lowcut, "Low Cut",
        R (20.0f, 1000.0f, 1.0f, 0.5f), 120.0f,
        Attr().withStringFromValueFunction ([hz] (float v, int) { return hz (v); })));

    params.push_back (std::make_unique<P> (pid::width, "Width",
        R (0.0f, 1.0f, 0.001f), 1.0f,
        Attr().withStringFromValueFunction ([pct] (float v, int) { return pct (v); })));

    params.push_back (std::make_unique<P> (pid::duck, "Duck",
        R (0.0f, 36.0f, 0.1f), 18.0f,
        Attr().withStringFromValueFunction ([db] (float v, int) { return db (v); })));

    params.push_back (std::make_unique<P> (pid::thresh, "Threshold",
        R (-60.0f, 0.0f, 0.1f), -32.0f,
        Attr().withStringFromValueFunction ([db] (float v, int) { return db (v); })));

    params.push_back (std::make_unique<P> (pid::attack, "Attack",
        R (1.0f, 150.0f, 0.1f, 0.5f), 12.0f,
        Attr().withStringFromValueFunction ([ms] (float v, int) { return ms (v); })));

    params.push_back (std::make_unique<P> (pid::release, "Release",
        R (20.0f, 2000.0f, 1.0f, 0.4f), 320.0f,
        Attr().withStringFromValueFunction ([ms] (float v, int) { return ms (v); })));

    params.push_back (std::make_unique<P> (pid::mix, "Mix",
        R (0.0f, 1.0f, 0.001f), 0.35f,
        Attr().withStringFromValueFunction ([pct] (float v, int) { return pct (v); })));

    params.push_back (std::make_unique<P> (pid::output, "Output",
        R (-24.0f, 12.0f, 0.1f), 0.0f,
        Attr().withStringFromValueFunction ([db] (float v, int) { return db (v); })));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        pid::pingpong, "Ping-Pong", false));

    // --- 5-point wet EQ ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        pid::eqon, "EQ On", false));

    const auto& eqCfg = dxeq::bands();
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        params.push_back (std::make_unique<P> (pid::eqFreq (b), "EQ " + juce::String (b + 1) + " Freq",
            R (eqCfg[(size_t) b].minFreq, eqCfg[(size_t) b].maxFreq, 1.0f, 0.3f),
            eqCfg[(size_t) b].defFreq,
            Attr().withStringFromValueFunction ([hz] (float v, int) { return hz (v); })));

        params.push_back (std::make_unique<P> (pid::eqGain (b), "EQ " + juce::String (b + 1) + " Gain",
            R (-dxeq::kMaxGainDb, dxeq::kMaxGainDb, 0.1f), 0.0f,
            Attr().withStringFromValueFunction ([db] (float v, int) { return db (v); })));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
void DextroDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    scopeDecimN = juce::jmax (1, (int) (sampleRate / 1500.0));   // ~1500 frames/sec
    scopeDecim  = 0;
}

bool DextroDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void DextroDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    // Host tempo (for sync). Modern AudioPlayHead API — valid on JUCE 7 & 8.
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    currentBpm.store ((float) bpm);

    // Pull parameters and map into the engine.
    DuckingDelay::Params p;
    const bool  sync = apvts.getRawParameterValue (pid::sync)->load() > 0.5f;
    const int   div  = (int) apvts.getRawParameterValue (pid::division)->load();
    const float t    = apvts.getRawParameterValue (pid::time)->load();
    const float off  = apvts.getRawParameterValue (pid::offset)->load();

    // Base delay: a locked note value when synced, else the free ms knob.
    const float base = sync ? synced::divisionMs (div, bpm) : t;
    p.delayMsL       = juce::jmin (5000.0f, base);
    p.delayMsR       = juce::jmin (5000.0f, base + off);
    p.feedback      = apvts.getRawParameterValue (pid::feedback)->load();
    p.dampHz        = apvts.getRawParameterValue (pid::tone)->load();
    p.lowCutHz      = apvts.getRawParameterValue (pid::lowcut)->load();
    p.width         = apvts.getRawParameterValue (pid::width)->load();
    p.duckDepthDb   = apvts.getRawParameterValue (pid::duck)->load();
    p.thresholdDb   = apvts.getRawParameterValue (pid::thresh)->load();
    p.attackMs      = apvts.getRawParameterValue (pid::attack)->load();
    p.releaseMs     = apvts.getRawParameterValue (pid::release)->load();
    p.mix           = apvts.getRawParameterValue (pid::mix)->load();
    p.outputGainDb  = apvts.getRawParameterValue (pid::output)->load();
    p.pingpong      = apvts.getRawParameterValue (pid::pingpong)->load() > 0.5f;
    p.eqOn          = apvts.getRawParameterValue (pid::eqon)->load() > 0.5f;
    for (int b = 0; b < dxeq::kNumBands; ++b)
    {
        p.eqFreq[b] = apvts.getRawParameterValue (pid::eqFreq (b))->load();
        p.eqGain[b] = apvts.getRawParameterValue (pid::eqGain (b))->load();
    }
    engine.setParams (p);

    if (numCh >= 2)
    {
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    }
    else if (numCh == 1)
    {
        // Mono: duplicate into a scratch right channel, process, take the left.
        juce::HeapBlock<float> tmp (n);
        auto* l = buffer.getWritePointer (0);
        std::memcpy (tmp.get(), l, sizeof (float) * (size_t) n);
        engine.process (l, tmp.get(), n);
    }

    // Feed the scope ring (decimated) so the UI can draw env + duck history.
    const float env  = engine.getInputEnv();
    const float duck = engine.getDuckGain();
    currentDuck.store (duck);
    for (int i = 0; i < n; ++i)
    {
        if (++scopeDecim >= scopeDecimN)
        {
            scopeDecim = 0;
            int h = scopeHead.load (std::memory_order_relaxed);
            scopeEnv[(size_t) h].store (env,  std::memory_order_relaxed);
            scopeDuck[(size_t) h].store (duck, std::memory_order_relaxed);
            scopeHead.store ((h + 1) % kScopeSize, std::memory_order_release);
        }
    }
}

//==============================================================================
int DextroDelayAudioProcessor::readScope (std::array<ScopeFrame, kScopeSize>& out) const
{
    const int head = scopeHead.load (std::memory_order_acquire);
    for (int i = 0; i < kScopeSize; ++i)
    {
        const int idx = (head + i) % kScopeSize;   // oldest -> newest
        out[(size_t) i].env  = scopeEnv[(size_t) idx].load (std::memory_order_relaxed);
        out[(size_t) i].duck = scopeDuck[(size_t) idx].load (std::memory_order_relaxed);
    }
    return head;
}

//==============================================================================
juce::AudioProcessorEditor* DextroDelayAudioProcessor::createEditor()
{
    return new DextroDelayAudioProcessorEditor (*this);
}

//==============================================================================
void DextroDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DextroDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DextroDelayAudioProcessor();
}
