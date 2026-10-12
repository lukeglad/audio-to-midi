#include "PluginEditor.h"
#include "BinaryData.h"

#include <map>

namespace
{
    const juce::String kDot = juce::String (juce::CharPointer_UTF8 (" \xC2\xB7 "));
    const juce::String kEllipsis = juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA6"));

    juce::String percent (float fraction)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, fraction) * 100.0f)) + "%";
    }

    /** "acoustic guitar, drums, voice" from the notes, most notes first, at most four named. */
    juce::String instrumentSummary (const std::vector<msl::Note>& notes)
    {
        std::map<int, int> counts;
        for (const auto& note : notes)
            ++counts[note.program];

        std::vector<std::pair<int, int>> ordered (counts.begin(), counts.end());
        std::sort (ordered.begin(), ordered.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });

        juce::StringArray names;
        for (const auto& [program, count] : ordered)
            names.add (atm::instrumentDisplayName (program));

        constexpr int kMax = 4;
        if (names.size() > kMax)
        {
            const int extra = names.size() - kMax;
            names.removeRange (kMax, extra);
            names.add ("+" + juce::String (extra) + " more");
        }

        return names.joinIntoString (", ");
    }
}

//==============================================================================
AudioToMidiEditor::AudioToMidiEditor (AudioToMidiProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    // House face for every bit of UI text (see AudioFlowerLookAndFeel.h).
    AudioFlowerLookAndFeel::setUiTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexMonoRegular_ttf, (size_t) BinaryData::IBMPlexMonoRegular_ttfSize));
    setLookAndFeel (&lookAndFeel);

    canvas.setWordmarkTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::IndieFlowerRegular_ttf, (size_t) BinaryData::IndieFlowerRegular_ttfSize));

    addAndMakeVisible (content);
    content.addAndMakeVisible (canvas);
    content.addAndMakeVisible (topBar);

    topBar.onModelClicked = [this] { showModelMenu(); };
    canvas.onAction = [this]
    {
        if (processorRef.getTranscription().isRunning())
            processorRef.cancelTranscription();
        else if (processorRef.getDownloader().busySize().has_value())
            processorRef.cancelDownload();
        else
            processorRef.retry();
    };

    processorRef.addChangeListener (this);
    refreshFromProcessor();
    startTimerHz (15);

    // Constrainer before setResizable, or JUCE installs its own default one.
    sizeConstrainer.setSizeLimits (kMinWidth, kMinHeight, 2400, 1500);
    setConstrainer (&sizeConstrainer);
    setResizable (true, true);

    const int w = processorRef.lastEditorWidth  >= kMinWidth  ? processorRef.lastEditorWidth  : kDefaultWidth;
    const int h = processorRef.lastEditorHeight >= kMinHeight ? processorRef.lastEditorHeight : kDefaultHeight;
    setSize (w, h);

    // Re-apply the remembered size once the host has finished opening the
    // window: FL pushes a stale size straight after construction.
    juce::Component::SafePointer<AudioToMidiEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        const int rw = safeThis->processorRef.lastEditorWidth;
        const int rh = safeThis->processorRef.lastEditorHeight;
        if (rw >= kMinWidth && rh >= kMinHeight && (rw != safeThis->getWidth() || rh != safeThis->getHeight()))
            safeThis->setSize (rw, rh);
    });
}

AudioToMidiEditor::~AudioToMidiEditor()
{
    stopTimer();
    processorRef.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void AudioToMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (AudioFlowerLookAndFeel::background());
}

void AudioToMidiEditor::resized()
{
    if (sizeConstrainer.userDragActive && getWidth() >= kMinWidth && getHeight() >= kMinHeight)
    {
        processorRef.lastEditorWidth  = getWidth();
        processorRef.lastEditorHeight = getHeight();
    }

    // Full kUiScale from about 806x624 up; eases to 1.0 by 620x480 so a small
    // window still fits everything instead of clipping it.
    const float scale = juce::jlimit (1.0f, kUiScale,
                                      juce::jmin ((float) getWidth()  / 620.0f,
                                                  (float) getHeight() / 480.0f));
    const int cw = (int) std::ceil ((float) getWidth()  / scale);
    const int ch = (int) std::ceil ((float) getHeight() / scale);

    content.setBounds (0, 0, cw, ch);
    content.setTransform (juce::AffineTransform::scale (scale));

    auto r = juce::Rectangle<int> (0, 0, cw, ch).reduced (8);
    topBar.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    canvas.setBounds (r);
}

//==============================================================================
bool AudioToMidiEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile (juce::File (path)))
            return true;

    return false;
}

void AudioToMidiEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    canvas.setDragHover (true);
}

void AudioToMidiEditor::fileDragExit (const juce::StringArray&)
{
    canvas.setDragHover (false);
}

void AudioToMidiEditor::filesDropped (const juce::StringArray& files, int, int)
{
    canvas.setDragHover (false);

    // One file in, by design. With several dropped, the first usable one wins.
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (isSupportedAudioFile (file))
        {
            processorRef.loadFile (file);
            return;
        }
    }
}

//==============================================================================
void AudioToMidiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void AudioToMidiEditor::timerCallback()
{
    // Download and transcription progress are polled; everything else arrives
    // as a change message.
    refreshFromProcessor();
}

void AudioToMidiEditor::refreshFromProcessor()
{
    const auto source = processorRef.getSource();
    const auto& t = processorRef.getTranscription();
    auto& downloader = processorRef.getDownloader();

    canvas.setSource (source);
    canvas.setLoadStatus (processorRef.getLoadingFileName(), processorRef.getLoadError());

    if (source != nullptr)
        topBar.setFile (source->fileName, source->getLengthSeconds());
    else
        topBar.setFile ({}, 0.0);

    // The model control: the preferred model, with download progress while
    // one is coming down.
    const auto preferred = atm::preferredModel();
    juce::String modelLabel = atm::modelInfo (preferred).name;
    if (const auto busy = downloader.busySize())
        modelLabel = juce::String (atm::modelInfo (*busy).name) + kDot + percent (downloader.status (*busy).progress());
    topBar.setModelLabel (modelLabel);

    // The status message. A download in progress shows whether or not audio
    // is loaded; otherwise the transcription phase decides.
    Canvas::Status status;
    double progressTime = -1.0;

    if (const auto busy = downloader.busySize())
    {
        const auto ds = downloader.status (*busy);
        status.primary = (ds.phase == atm::ModelDownloader::Phase::verifying ? "checking " : "downloading ")
                         + juce::String (atm::modelInfo (*busy).name) + " model" + kDot + percent (ds.progress());
        status.action = ds.phase == atm::ModelDownloader::Phase::downloading ? "cancel" : "";
    }
    else
    {
        const auto modelName = juce::String (atm::modelInfo (t.model).name);

        switch (t.phase)
        {
            case TranscriptionPhase::none:
                break;

            case TranscriptionPhase::waitingForModel:
            {
                const auto ds = downloader.status (t.model);
                if (ds.phase == atm::ModelDownloader::Phase::failed)
                {
                    status.primary = "download failed" + kDot + ds.error;
                    status.action = "retry";
                }
                else
                {
                    status.primary = "no model installed yet";
                    status.secondary = "pick one at the top right to transcribe";
                }
                break;
            }

            case TranscriptionPhase::queued:
            case TranscriptionPhase::resampling:
                status.primary = "preparing" + kEllipsis;
                status.action = "cancel";
                break;

            case TranscriptionPhase::loadingModel:
                status.primary = "loading " + modelName + " model" + kEllipsis;
                status.action = "cancel";
                break;

            case TranscriptionPhase::transcribing:
                status.primary = "transcribing" + kDot + percent (t.progress);
                status.action = "cancel";
                progressTime = t.finalizedThrough;
                break;

            case TranscriptionPhase::done:
                status.primary = juce::String (t.notes.size()) + (t.notes.size() == 1 ? " note" : " notes")
                                 + (t.notes.empty() ? "" : kDot + instrumentSummary (t.notes))
                                 + kDot + modelName + " on " + t.device;
                status.primaryAlpha = 0.45f;
                break;

            case TranscriptionPhase::cancelled:
                status.primary = "transcription cancelled";
                status.action = "retry";
                break;

            case TranscriptionPhase::failed:
                status.primary = "transcription failed" + kDot + t.error;
                status.action = "retry";
                break;
        }
    }

    canvas.setStatus (status);
    canvas.setProgressTime (progressTime);
}

void AudioToMidiEditor::showModelMenu()
{
    auto& downloader = processorRef.getDownloader();
    const auto preferred = atm::preferredModel();
    const auto busy = downloader.busySize();

    juce::PopupMenu menu;

    for (const auto size : atm::kAllModelSizes)
    {
        const auto& info = atm::modelInfo (size);
        juce::String text = info.name;

        if (atm::isModelInstalled (size))
            text += kDot + info.sizeLabel;
        else if (busy == size)
            text += kDot + "downloading " + percent (downloader.status (size).progress());
        else
            text += kDot + "download " + info.sizeLabel;

        menu.addItem (1 + (int) size, text, busy != size, size == preferred);
    }

    menu.addSeparator();
    if (busy.has_value())
        menu.addItem (10, "cancel download");
    menu.addItem (11, "open models folder");

    juce::Component::SafePointer<AudioToMidiEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (topBar.getModelControl()),
                        [safeThis] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result >= 1 && result <= (int) atm::kAllModelSizes.size())
            safeThis->processorRef.chooseModel ((atm::ModelSize) (result - 1));
        else if (result == 10)
            safeThis->processorRef.cancelDownload();
        else if (result == 11)
        {
            const auto folder = atm::modelsDirectory();
            folder.createDirectory();
            folder.revealToUser();
        }

        safeThis->refreshFromProcessor();
    });
}
