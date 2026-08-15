// Offscreen UI snapshot: construct the processor + editor and render the GUI to
// a PNG, so the neon look can be eyeballed without a DAW.
//
//   Snapshot <output.png> [eq]
//
// With "eq" the EQ view is enabled, a demo curve is dialled in, and noise is fed
// through the plugin while the message loop is pumped, so the editor's real
// timer path fills the spectrum analyzer before the snapshot is taken.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    const juce::String outPath = argc > 1 ? argv[1] : "dextro_ui.png";
    const bool eqMode = (argc > 2 && juce::String (argv[2]) == "eq");

    juce::ScopedJuceInitialiser_GUI juceInit;

    DextroDelayAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    juce::Random rng (1234);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto feed = [&]
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);
        proc.processBlock (buf, midi);
    };

    if (eqMode)
    {
        auto set = [&] (const juce::String& id, float v)
        {
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (v));
        };
        set ("eqon", 1.0f);
        set ("eqgain0", 6.0f);    set ("eqfreq0", 90.0f);    set ("eqq0", 0.7f);   // low shelf up
        set ("eqgain1", -6.0f);   set ("eqfreq1", 320.0f);   set ("eqq1", 3.2f);   // narrow low-mid dip
        set ("eqgain2", 4.0f);    set ("eqfreq2", 1200.0f);  set ("eqq2", 1.4f);   // presence
        set ("eqgain3", -3.0f);   set ("eqfreq3", 3500.0f);  set ("eqq3", 4.5f);   // surgical notch
        set ("eqgain4", 7.0f);    set ("eqfreq4", 9000.0f);  set ("eqq4", 0.7f);   // air

        for (int k = 0; k < 120; ++k) feed();   // prime the delay so echoes exist
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->resized();

    if (eqMode)
    {
        // Interleave audio with message-loop time so the editor's 30 Hz timer
        // consumes fresh analyzer blocks (the spectrum's fall-off is on that
        // timer, so the bars must be topped up right before the snapshot).
        for (int round = 0; round < 6; ++round)
        {
            for (int k = 0; k < 6; ++k) feed();          // > one 2048-sample FFT block
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
    }

    juce::Image img = editor->createComponentSnapshot (editor->getLocalBounds(), false, 2.0f);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (outPath));
    out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, *os);
        std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(),
                     img.getWidth(), img.getHeight());
        return 0;
    }
    std::fprintf (stderr, "failed to write %s\n", out.getFullPathName().toRawUTF8());
    return 1;
}
