#include "PluginEditor.h"
#include "BinaryData.h"
#include "Engine/MidiExport.h"

namespace
{
    const juce::String kDot = juce::String (juce::CharPointer_UTF8 (" \xC2\xB7 "));
    const juce::String kEllipsis = juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA6"));

    juce::String percent (float fraction)
    {
        return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, fraction) * 100.0f)) + "%";
    }
}

//==============================================================================
AudioToMidiEditor::AudioToMidiEditor (AudioToMidiProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    // The house face was injected by `houseFonts` (see the header).
    setLookAndFeel (&lookAndFeel);

    canvas.setWordmarkTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::IndieFlowerRegular_ttf, (size_t) BinaryData::IndieFlowerRegular_ttfSize));

    addAndMakeVisible (content);
    content.addAndMakeVisible (canvas);
    content.addAndMakeVisible (topBar);

    topBar.onModelClicked = [this] { showModelMenu(); };
    topBar.onBpmChanged = [this] (double bpm) { processorRef.setBpm (bpm); };
    canvas.onQuantizeChanged = [this] (double amount)
    {
        auto settings = processorRef.getCleanup();
        settings.quantize = amount;
        processorRef.setCleanup (settings);
    };
    canvas.onGridChanged = [this] (atm::Grid grid)
    {
        auto settings = processorRef.getCleanup();
        settings.grid = grid;
        processorRef.setCleanup (settings);
    };

    // The preview.
    auto& preview = processorRef.getPreview();
    canvas.onPlayClicked = [this, &preview] { preview.togglePlay(); refreshFromProcessor(); };
    canvas.onRecordClicked = [this] { processorRef.toggleRecord(); refreshFromProcessor(); };
    topBar.onMixChanged = [&preview] (float mix) { preview.setMix (mix); };
    canvas.onSeek = [this, &preview] (double seconds) { preview.seek (seconds); refreshFromProcessor(); };
    canvas.isLayerMuted   = [&preview] (int layer) { return preview.isMuted (layer); };
    canvas.isLayerSoloed  = [&preview] (int layer) { return preview.isSoloed (layer); };
    canvas.isLayerAudible = [&preview] (int layer) { return preview.isAudible (layer); };
    canvas.onMuteToggled = [this, &preview] (int layer) { preview.setMuted (layer, ! preview.isMuted (layer)); canvas.refreshPreviewState(); };
    canvas.onSoloToggled = [this, &preview] (int layer) { preview.setSoloed (layer, ! preview.isSoloed (layer)); canvas.refreshPreviewState(); };
    canvas.onAction = [this]
    {
        const auto& t = processorRef.getTranscription();
        const auto smaller = AudioToMidiProcessor::smallerModel (t.model);

        if (t.isRunning())
            processorRef.cancelTranscription();
        else if (processorRef.getDownloader().busySize().has_value())
            processorRef.cancelDownload();
        else if (t.phase == TranscriptionPhase::failed && t.failure == atm::Failure::outOfMemory && smaller.has_value())
            processorRef.chooseModel (*smaller);
        else
            processorRef.retry();
    };

    // Drag a layer chip out: write that layer's MIDI to a scratch file and
    // hand it to the OS drag. Overwritten each time; the DAW copies it.
    canvas.makeLayerMidiFile = [this] (int layerKey)
    {
        const auto source = processorRef.getSource();
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AudioFlower MIDI");
        folder.createDirectory();
        return writeLayerMidi (layerKey, folder.getChildFile (atm::layerMidiFileName (source != nullptr ? source->fileName
                                                                                                       : juce::String(), layerKey)));
    };
    canvas.onSaveLayer = [this] (int layerKey) { saveLayerAs (layerKey); };
    canvas.makeAllMidiFile = [this] (const std::vector<int>& keys, bool allOfThem)
    {
        const auto source = processorRef.getSource();
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AudioFlower MIDI");
        folder.createDirectory();
        return writeLayersMidi (keys, folder.getChildFile (atm::layersMidiFileName (source != nullptr ? source->fileName : juce::String(),
                                                                                    (int) keys.size(), allOfThem)));
    };
    canvas.onSaveAll = [this] (const std::vector<int>& keys, bool allOfThem) { saveLayersAs (keys, allOfThem); };

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
// Every file drag is taken, so one that isn't audio can say so instead of
// silently doing nothing; the drop itself ignores it.
bool AudioToMidiEditor::isInterestedInFileDrag (const juce::StringArray&)
{
    return true;
}

void AudioToMidiEditor::fileDragEnter (const juce::StringArray& files, int, int)
{
    const bool anyAudio = std::any_of (files.begin(), files.end(),
                                       [] (const juce::String& path) { return isSupportedAudioFile (juce::File (path)); });
    canvas.setDragHover (true, anyAudio);
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
    // Download and transcription progress are polled, as is the playhead;
    // everything else arrives as a change message. The playhead wants a
    // smoother rate than the rest.
    processorRef.pollRecording();

    const bool animating = processorRef.getPreview().isPlaying()
                        || processorRef.getTranscription().phase == TranscriptionPhase::transcribing
                        || processorRef.getRecorder().getPhase() != atm::Recorder::Phase::idle;
    if (getTimerInterval() != (animating ? 33 : 66))
        startTimer (animating ? 33 : 66);

    refreshFromProcessor();
}

void AudioToMidiEditor::refreshFromProcessor()
{
    const auto source = processorRef.getSource();
    const auto& t = processorRef.getTranscription();
    auto& downloader = processorRef.getDownloader();

    canvas.setSource (source);
    canvas.setNotes (processorRef.getTranscriptionRun(), processorRef.getNotesVersion(), processorRef.getCleanNotes());

    // Recording: the dot's state, the live waveform, and the top bar's
    // running readout.
    auto& recorder = processorRef.getRecorder();
    const auto recPhase = recorder.getPhase();
    const bool recording = recPhase == atm::Recorder::Phase::recording || recPhase == atm::Recorder::Phase::finishing;
    canvas.setRecordState (recording ? RecordButton::State::recording
                                     : recPhase == atm::Recorder::Phase::armed ? RecordButton::State::armed
                                                                               : RecordButton::State::idle);
    canvas.setLiveRecording (recording ? recorder.getLivePeaks() : std::vector<float>{},
                             atm::Recorder::kPeakBucketSeconds, recorder.getSeconds());
    {
        juce::String live;
        if (recording)
        {
            live = "recording";
            if (const auto host = recorder.getHostTiming())
                live += kDot + "from bar " + juce::String (host->startBar);
            const int total = (int) recorder.getSeconds();
            live += kDot + juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
        }
        topBar.setLiveText (live);
    }

    const auto& tempo = processorRef.getTempo();
    topBar.setTempo (tempo.hasEstimate && source != nullptr && ! recording, tempo.bpm, ! tempo.fromHost);

    canvas.setQuantize (processorRef.getCleanup().quantize, processorRef.getCleanup().grid, tempo.hasEstimate);
    canvas.setGridDivisions ((int) processorRef.getCleanup().grid);

    auto& preview = processorRef.getPreview();
    topBar.setMix (source != nullptr, preview.getMix());
    canvas.setPlaying (preview.isPlaying());
    canvas.setPlayhead (source != nullptr && (preview.isPlaying() || preview.getPosition() > 0.0) ? preview.getPosition() : -1.0);
    canvas.setGrid (tempo.hasEstimate && source != nullptr ? std::optional (tempo.effective()) : std::nullopt);
    canvas.setLoadStatus (processorRef.getLoadingFileName(), processorRef.getLoadError());

    if (source != nullptr)
    {
        // A recording from a host says which of its bars it covers.
        juce::String name = source->fileName;
        if (tempo.fromHost && tempo.hasEstimate)
        {
            const auto& g = tempo.detected;
            const int startBar = g.firstBarNumber - (g.downbeat > 0 ? 1 : 0);
            const double beatsPastDownbeat = g.beatAt (source->getLengthSeconds()) - g.downbeat;
            const int endBar = beatsPastDownbeat >= 0.0 ? g.firstBarNumber + (int) std::floor (beatsPastDownbeat / g.beatsPerBar) : startBar;
            name += kDot + (endBar > startBar ? "bars " + juce::String (startBar) + juce::String (juce::CharPointer_UTF8 ("\xE2\x80\x93")) + juce::String (endBar)
                                              : "bar " + juce::String (startBar));
        }
        topBar.setFile (name, source->getLengthSeconds());
    }
    else
        topBar.setFile ({}, 0.0);

    // The model control: the preferred model, with download progress while
    // one is coming down.
    const auto preferred = atm::preferredModel();
    juce::String modelLabel = atm::modelInfo (preferred).name;
    if (const auto busy = downloader.busySize())
        modelLabel = juce::String (atm::modelInfo (*busy).name) + kDot + percent (downloader.status (*busy).progress());
    topBar.setModelLabel (modelLabel);

    // The status message: what's happening in plain words, with at most one
    // action (mockups/first-launch-errors.html). A download shows whether or
    // not audio is waiting for it; otherwise the transcription phase decides.
    Canvas::Status status;
    double progressTime = -1.0;

    // Recording claims the band: ready, or quiet while it runs (the top bar
    // carries the readout), or the one thing worth saying mid-recording.
    std::optional<Canvas::Status> recordingStatus;
    if (recPhase == atm::Recorder::Phase::armed && recorder.hasTransport())
    {
        recordingStatus = Canvas::Status { "ready to record", "press play in your DAW", 1.0f, true, {} };
    }
    else if (recording)
    {
        recordingStatus = Canvas::Status {};
        if (recorder.getSeconds() >= 2.0 && recorder.getPeakSoFar() < atm::Recorder::kSilenceThreshold)
            recordingStatus = Canvas::Status { "no audio is reaching the plugin", "check what's playing into it", 0.75f, false, {} };
    }

    using DPhase = atm::ModelDownloader::Phase;
    const bool audioWaiting = source != nullptr && t.phase == TranscriptionPhase::waitingForModel;

    if (const auto busy = downloader.busySize())
    {
        const auto ds = downloader.status (*busy);
        const auto& info = atm::modelInfo (*busy);
        const bool forThisAudio = audioWaiting && *busy == t.model;
        const auto modelPhrase = forThisAudio ? juce::String ("the model") : "the " + juce::String (info.name) + " model";

        if (ds.phase == DPhase::verifying)
        {
            status.primary = forThisAudio ? "getting ready" : "checking " + modelPhrase;
            status.secondary = forThisAudio ? "checking the download" : "";
        }
        else if (ds.phase == DPhase::waiting)
        {
            status.primary = (forThisAudio ? juce::String ("getting ready") : "downloading " + modelPhrase) + kDot + percent (ds.progress());
            status.secondary = "another window is downloading " + modelPhrase;
        }
        else if (ds.secondCopy)
        {
            status.primary = "downloading " + modelPhrase + " again" + kDot + percent (ds.progress());
            status.secondary = "the first copy didn't check out";
            status.action = "cancel";
        }
        else if (forThisAudio && t.updatingModel)
        {
            status.primary = "updating the model" + kDot + percent (ds.progress());
            status.secondary = juce::String (info.sizeLabel) + ", then it carries on";
            status.action = "cancel";
        }
        else if (forThisAudio)
        {
            status.primary = "getting ready" + kDot + percent (ds.progress());
            status.secondary = "downloading the model (" + juce::String (info.sizeLabel) + ", just this once)";
            status.action = "cancel";
        }
        else
        {
            status.primary = "downloading " + modelPhrase + kDot + percent (ds.progress());
            status.action = "cancel";
        }
    }
    else
    {
        switch (t.phase)
        {
            case TranscriptionPhase::none:
                break;

            case TranscriptionPhase::waitingForModel:
            {
                const auto ds = downloader.status (t.model);
                const auto sizeLabel = juce::String (atm::modelInfo (t.model).sizeLabel);
                using F = atm::ModelDownloader::Failure;

                // What a download that stopped partway has kept.
                const auto paused = [&]
                {
                    status.primary = "the download paused at " + percent (ds.progress());
                    status.secondary = "it'll pick up where it left off";
                    status.action = "resume";
                };

                if (ds.phase != DPhase::failed)
                {
                    // Cancelled by the user.
                    if (ds.downloadedBytes > 0)
                        paused();
                    else
                    {
                        status.primary = "the model isn't downloaded yet";
                        status.secondary = sizeLabel + ", just this once";
                        status.action = "download";
                    }
                    break;
                }

                status.action = "try again";

                switch (ds.failure)
                {
                    case F::none:
                    case F::connection:
                    case F::server:
                        if (ds.downloadedBytes > 0)
                            paused();
                        else if (ds.failure == F::server)
                        {
                            status.primary = "the download didn't start";
                            status.secondary = "try again in a few minutes";
                        }
                        else
                        {
                            status.primary = "can't connect to download the model";
                            status.secondary = "check your internet and try again";
                        }
                        break;

                    case F::noSpace:
                        status.primary = "not enough space for the model";
                        status.secondary = "it needs " + sizeLabel + " free";
                        break;

                    case F::cantSave:
                        status.primary = "the model couldn't be saved";
                        status.secondary = "check your disk and try again";
                        break;

                    case F::corrupted:
                        status.primary = "the download didn't check out";
                        status.secondary = "try again in a few minutes";
                        break;
                }
                break;
            }

            case TranscriptionPhase::queued:
            case TranscriptionPhase::resampling:
                status.primary = "preparing" + kEllipsis;
                status.action = "cancel";
                break;

            case TranscriptionPhase::loadingModel:
                status.primary = "loading " + juce::String (atm::modelInfo (t.model).name) + " model" + kEllipsis;
                status.action = "cancel";
                break;

            case TranscriptionPhase::transcribing:
            {
                if (progressSmoother.run != processorRef.getTranscriptionRun())
                {
                    progressSmoother = {};
                    progressSmoother.run = processorRef.getTranscriptionRun();
                }

                const double length = source != nullptr ? source->getLengthSeconds() : 0.0;
                const float shown = progressSmoother.update (juce::Time::getMillisecondCounterHiRes(),
                                                             t.progress, length, t.model);
                status.primary = "transcribing" + kDot + percent (shown);
                // Not a problem, just slower: a quiet note while it runs.
                if (t.device == "cpu")
                    status.secondary = "on the cpu, so it'll take a little longer";
                status.action = "cancel";
                progressTime = shown * length;
                break;
            }

            case TranscriptionPhase::done:
                // The layer chips say what was found. Only nothing needs explaining.
                if (t.notes.empty())
                {
                    status.primary = "no notes found";
                    status.secondary = "try a clearer or louder recording";
                }
                break;

            case TranscriptionPhase::cancelled:
                status.primary = "transcription stopped";
                status.action = "start again";
                break;

            case TranscriptionPhase::failed:
                if (t.failure == atm::Failure::outOfMemory)
                {
                    if (const auto smaller = AudioToMidiProcessor::smallerModel (t.model))
                    {
                        status.primary = "not enough memory for the " + juce::String (atm::modelInfo (t.model).name) + " model";
                        status.action = "use " + juce::String (atm::modelInfo (*smaller).name);
                    }
                    else
                    {
                        status.primary = "not enough memory to transcribe";
                        status.secondary = "close some apps and try again";
                        status.action = "try again";
                    }
                }
                else
                {
                    status.primary = "something went wrong transcribing this";
                    status.action = "try again";
                }
                break;
        }
    }

    canvas.setStatus (recordingStatus.value_or (status));
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

//==============================================================================
atm::ExportTiming AudioToMidiEditor::exportTiming() const
{
    const auto& tempo = processorRef.getTempo();
    atm::ExportTiming timing;
    timing.hostBpm = processorRef.getHostBpm();
    timing.conform = tempo.hasEstimate;   // always, once a tempo exists
    timing.tempo   = tempo.effective();
    timing.earliestOnset = atm::earliestOnsetOf (processorRef.getCleanNotes());
    if (tempo.fromHost)
        timing.fixedLeadInBeats = atm::leadInBeatsFor (timing.tempo);   // the clip starts at the bar it was recorded from
    return timing;
}

juce::File AudioToMidiEditor::writeLayerMidi (int layerKey, const juce::File& destination) const
{
    const auto midi = atm::buildLayerMidi (processorRef.getCleanNotes(), layerKey, exportTiming());
    return atm::writeMidiFile (midi, destination) ? destination : juce::File();
}

juce::File AudioToMidiEditor::writeLayersMidi (const std::vector<int>& layerKeys, const juce::File& destination) const
{
    // Every layer in one file shares the same timing, including the same
    // lead-in, so the tracks stay lined up with each other.
    const auto midi = atm::buildLayersMidi (processorRef.getCleanNotes(), layerKeys, exportTiming());
    return atm::writeMidiFile (midi, destination) ? destination : juce::File();
}

void AudioToMidiEditor::saveLayersAs (const std::vector<int>& layerKeys, bool allOfThem)
{
    const auto source = processorRef.getSource();
    const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                               .getChildFile (atm::layersMidiFileName (source != nullptr ? source->fileName : juce::String(),
                                                                       (int) layerKeys.size(), allOfThem));

    saveChooser = std::make_unique<juce::FileChooser> ("save the layers as midi", suggested, "*.mid");

    juce::Component::SafePointer<AudioToMidiEditor> safeThis (this);
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, layerKeys] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        if (! file.hasFileExtension ("mid;midi"))
            file = file.withFileExtension ("mid");

        safeThis->writeLayersMidi (layerKeys, file);
    });
}

void AudioToMidiEditor::saveLayerAs (int layerKey)
{
    const auto source = processorRef.getSource();
    const auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                               .getChildFile (atm::layerMidiFileName (source != nullptr ? source->fileName : juce::String(), layerKey));

    saveChooser = std::make_unique<juce::FileChooser> ("save " + atm::layerName (layerKey) + " as midi", suggested, "*.mid");

    juce::Component::SafePointer<AudioToMidiEditor> safeThis (this);
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, layerKey] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        if (! file.hasFileExtension ("mid;midi"))
            file = file.withFileExtension ("mid");

        safeThis->writeLayerMidi (layerKey, file);
    });
}

//==============================================================================
float AudioToMidiEditor::ProgressSmoother::update (double nowMs, float reported, double lengthSeconds, atm::ModelSize model)
{
    reported = juce::jlimit (0.0f, 1.0f, reported);
    if (reported >= 1.0f)
        return shown = 1.0f;

    const int chunks = juce::jmax (1, (int) std::ceil (lengthSeconds / 5.0));
    const float step = 1.0f / (float) chunks;

    // The first call of a run: start the clock, with a typical pace for the
    // model until a real chunk has been timed (medium is ~1.8 s per chunk on
    // an M5 Pro's GPU; slower machines correct this after one chunk).
    if (lastReportMs < 0.0)
    {
        lastReportMs = nowMs;
        msPerChunk = model == atm::ModelSize::small ? 1200.0 : model == atm::ModelSize::large ? 8000.0 : 2500.0;
        base = reported;
    }

    // A new report: measure how long the chunk(s) since the last one took.
    if (reported > base + 1.0e-4f)
    {
        const int advanced = juce::jmax (1, juce::roundToInt ((reported - base) / step));
        const double perChunk = juce::jmax (100.0, (nowMs - lastReportMs) / advanced);
        msPerChunk = measured ? 0.5 * msPerChunk + 0.5 * perChunk : perChunk;
        measured = true;
        base = reported;
        lastReportMs = nowMs;
    }

    const float ahead = (float) ((nowMs - lastReportMs) / msPerChunk) * step;
    const float target = juce::jmin (base + juce::jmin (ahead, step * 0.95f), 0.99f);
    shown = juce::jmax (shown, target);
    return shown;
}
