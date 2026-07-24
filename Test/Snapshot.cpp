// Offscreen UI snapshot: construct the processor + editor and render the GUI to
// a PNG, so the neon (purple / deep-blue) look can be eyeballed without a DAW.
//
//   Snapshot <output.png>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    const juce::String outPath = argc > 1 ? argv[1] : "dextro_ui.png";

    juce::ScopedJuceInitialiser_GUI juceInit;

    DextroDelayAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (780, 560);

    // Give the layout a chance to settle.
    editor->resized();

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
