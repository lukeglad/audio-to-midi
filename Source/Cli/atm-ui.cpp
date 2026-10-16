// atm-ui: renders the real editor to PNGs without a host or a screen, so UI
// changes can be checked headlessly (the user does the hands-on testing; this
// is for looking before handing over).
//
//   atm-ui OUT_DIR [AUDIO_FILE]
//
// Writes the editor at its default size and at its minimum size, each with
// and without the quick guide open. With an audio file it waits for the
// transcription first (needs the model installed), so the roll has notes.

#include <JuceHeader.h>

#include "DragValue.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    void print (const juce::String& line) { std::printf ("%s\n", line.toRawUTF8()); std::fflush (stdout); }

    void pump (int ms)
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < until)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    void save (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        png.writeImageToStream (image, out);
        print ("wrote " + file.getFullPathName() + " (" + juce::String (c.getWidth()) + "x" + juce::String (c.getHeight()) + ")");
    }

    juce::Component* findGuide (juce::Component& root)
    {
        for (auto* child : root.getChildren())
        {
            if (dynamic_cast<GuideOverlay*> (child) != nullptr)
                return child;
            if (auto* found = findGuide (*child))
                return found;
        }
        return nullptr;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        print ("usage: atm-ui OUT_DIR [AUDIO_FILE]");
        return 2;
    }

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    outDir.createDirectory();

    AudioToMidiProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    if (argc >= 3)
    {
        processor.loadFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        for (int i = 0; i < 600 && processor.getTranscription().phase != TranscriptionPhase::done
                                 && processor.getTranscription().phase != TranscriptionPhase::failed; ++i)
            pump (100);
        print ("transcription: " + juce::String (processor.getCleanNotes().size()) + " notes");
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setOpaque (true);

    for (const auto size : { juce::Point<int> (960, 600), juce::Point<int> (540, 404) })
    {
        editor->setSize (size.x, size.y);
        pump (300);
        const auto tag = juce::String (size.x) + "x" + juce::String (size.y);
        save (*editor, outDir.getChildFile ("editor-" + tag + ".png"));

        if (auto* guide = dynamic_cast<GuideOverlay*> (findGuide (*editor)))
        {
            std::vector<juce::Component*> behind;
            for (auto* sibling : guide->getParentComponent()->getChildren())
                if (sibling != guide)
                    behind.push_back (sibling);
            guide->open (behind);
            pump (200);
            save (*editor, outDir.getChildFile ("guide-" + tag + ".png"));

            // Scrolled to the end, where the credits are.
            std::function<juce::Viewport* (juce::Component&)> findViewport = [&] (juce::Component& c) -> juce::Viewport*
            {
                if (auto* v = dynamic_cast<juce::Viewport*> (&c)) return v;
                for (auto* child : c.getChildren()) if (auto* v = findViewport (*child)) return v;
                return nullptr;
            };
            if (auto* viewport = findViewport (*guide))
            {
                viewport->setViewPosition (0, 1 << 20);
                pump (100);
                save (*editor, outDir.getChildFile ("guide-end-" + tag + ".png"));
            }
            guide->close();
        }

        // Typing into the tempo (the grid pill's DragValue that says BPM).
        std::function<DragValue* (juce::Component&)> findTempo = [&] (juce::Component& c) -> DragValue*
        {
            if (auto* d = dynamic_cast<DragValue*> (&c); d != nullptr && d->getText().contains ("BPM")) return d;
            for (auto* child : c.getChildren()) if (auto* d = findTempo (*child)) return d;
            return nullptr;
        };
        if (auto* tempo = findTempo (*editor))
        {
            tempo->showEditor();
            pump (100);
            if (auto* ed = tempo->getCurrentTextEditor())
                ed->setHighlightedRegion ({});   // the caret, not the select-all, so the type shows plainly
            pump (100);
            save (*editor, outDir.getChildFile ("tempo-edit-" + tag + ".png"));
            tempo->hideEditor (true);
        }

        // Record armed, then recording (no transport here, so it starts at
        // once when audio flows), then stopped.
        if (size.x == 960)
        {
            processor.toggleRecord();
            pump (150);
            save (*editor, outDir.getChildFile ("record-armed.png"));

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    block.setSample (0, i, 0.3f * std::sin ((float) (b * 512 + i) * 0.05f)), block.setSample (1, i, block.getSample (0, i));
                processor.processBlock (block, midi);
            }
            pump (250);
            save (*editor, outDir.getChildFile ("record-recording.png"));
            processor.toggleRecord();
            pump (600);
        }

        // The model menu, opened the way a click opens it.
        std::function<TopBar* (juce::Component&)> findTopBar = [&] (juce::Component& c) -> TopBar*
        {
            if (auto* t = dynamic_cast<TopBar*> (&c)) return t;
            for (auto* child : c.getChildren()) if (auto* t = findTopBar (*child)) return t;
            return nullptr;
        };
        if (auto* topBar = findTopBar (*editor))
            if (auto* button = dynamic_cast<juce::Button*> (&topBar->getModelControl()))
            {
                button->triggerClick();
                pump (200);
                save (*editor, outDir.getChildFile ("model-menu-" + tag + ".png"));
                if (auto* menu = GlassMenu::find (*topBar))
                    menu->dismiss();
            }
    }

    editor.reset();
    return 0;
}
