#pragma once

#include <JuceHeader.h>
#include <optional>

#include "Engine/Cleanup.h"
#include "Engine/Tempo.h"

#include "Chip.h"
#include "Glass.h"
#include "LayerChip.h"
#include "MixControl.h"
#include "PlayButton.h"
#include "QuantizeControl.h"
#include "RecordButton.h"
#include "SourceAudio.h"

//==============================================================================
/**
    The one surface under the top bar (house rule: everything on one surface,
    no side panels), in two stacked sections that share a time axis:

      - the waveform band: the source audio, and the status message with its
        cancel / retry chip;
      - a time ruler;
      - the piano roll: piano keys on the left edge, every instrument layer
        in one roll, and a floating chip per layer.

    Layers are told apart by focus, not colour: the selected layer is copper,
    the rest ink at low alpha; hovering a chip or a note lights its layer.
    The busiest layer is selected automatically until the user picks one.

    A view of state the editor pushes in. It owns only view state: the
    layers built from the notes, selection and hover, and the pitch range.
*/
class Canvas : public juce::Component,
               public GlassBackdrop,
               private juce::Timer
{
public:
    /** What to say in the waveform band while nothing else claims it (a load
        in progress, a drag-over or a load error take precedence). */
    struct Status
    {
        juce::String primary, secondary;
        float primaryAlpha = 0.75f;
        bool accent = false;   // the primary line in copper: something live (ready to record)
        juce::String action;   // chip text; empty = no chip
    };

    Canvas();
    ~Canvas() override;

    void setSource (std::shared_ptr<const SourceAudio>);
    void setLoadStatus (const juce::String& loadingFileName, const juce::String& loadError);
    /** `acceptable`: the dragged files include audio this can load. Anything
        else gets a dim outline and "audio files only" rather than nothing. */
    void setDragHover (bool isHovering, bool acceptable = true);
    void setWordmarkTypeface (juce::Typeface::Ptr);

    /** How octaves are numbered on the keys and in the hover readout: the
        host's own convention, so "C5" means the same here as in FL. */
    void setMiddleCOctave (int);
    void setStatus (const Status&);

    /** Where the transcription has got to, in seconds; negative hides the line. */
    void setProgressTime (double seconds);

    /** The beat grid drawn through the roll: bars, beats and the quantize
        grid's subdivisions, each fainter than the last, with bar numbers
        along the bottom. Empty hides it. */
    void setGrid (std::optional<atm::TempoEstimate>);

    /** The current run's notes after cleanup. A new `runId` clears the
        layers, selection and pitch range; within a run, layers are only ever
        added and the pitch range only grows, so nothing jumps. Cheap to call
        often: nothing happens unless `version` changed. */
    void setNotes (int runId, int version, const std::vector<atm::MidiNote>& notes);

    /** Lines per beat drawn between the beat lines: the quantize grid. */
    void setGridDivisions (int);

    /** The quantize control at the roll's bottom-right; hidden without a
        tempo grid to quantize to. */
    void setQuantize (double amount01, atm::Grid, bool hasGrid);
    std::function<void (double amount01)> onQuantizeChanged;

    /** The tempo in the grid pill (with quantize). `editable` false for a
        recording, whose tempo is the host's. */
    void setTempo (double bpm, bool editable);
    std::function<void (double bpm)> onTempoChanged;
    std::function<void (atm::Grid)> onGridChanged;

    /** The preview mix pill at the roll's bottom-left: shown with a file. */
    void setMix (bool hasFile, float mix01);
    std::function<void (float mix01)> onMixChanged;

    /** The preview playhead, in seconds; negative hides it. Copper: the
        brief's second reserved role for the accent. */
    void setPlayhead (double seconds);

    /** Preview state per layer, read when painting and refreshing chips. */
    std::function<bool (int layer)> isLayerMuted, isLayerSoloed, isLayerAudible;
    std::function<void (int layer)> onMuteToggled, onSoloToggled;

    /** Click in the waveform band or ruler: locate the preview. */
    std::function<void (double seconds)> onSeek;

    /** The play / stop glyph in the band's corner, above the keys. */
    void setPlaying (bool);
    std::function<void()> onPlayClicked;

    /** The record dot under it. */
    void setRecordState (RecordButton::State);
    std::function<void()> onRecordClicked;

    /** A recording in progress: its peaks so far (one per `secondsPerPeak`)
        drawn live in the band in place of the source. Empty: none. */
    void setLiveRecording (std::vector<float> peaks, double secondsPerPeak, double seconds);

    /** Re-reads the preview state into the chips and the roll. */
    void refreshPreviewState();

    std::function<void()> onAction;

    /** Writes a layer's MIDI for a drag and returns the file. */
    std::function<juce::File (int layerKey)> makeLayerMidiFile;

    /** Right-click → "save as .mid…" on a layer chip. */
    std::function<void (int layerKey)> onSaveLayer;

    /** The "all" chip, after the layer chips with two or more layers: every
        audible layer in one multi-track file. `allOfThem` is false when
        mute or solo leaves some out. */
    std::function<juce::File (const std::vector<int>& layerKeys, bool allOfThem)> makeAllMidiFile;
    std::function<void (const std::vector<int>& layerKeys, bool allOfThem)> onSaveAll;

    void paint (juce::Graphics&) override;

    /** What the canvas draws, without its children: the glass surfaces
        floating on it read from this. Rebuilt lazily after each repaint. */
    const juce::Image& getGlassBackdrop() override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    //==========================================================================
    /**
        The mute / solo strip that drops under a hovered layer chip (the EQ
        band hover-strip pattern): a 22 px stadium with two worded toggles,
        filled with ink when engaged. Its bounds reach up to the chip, so the
        pointer never crosses a gap moving from one to the other.
    */
    struct LayerStrip : juce::Component
    {
        LayerStrip();

        void showFor (int layer, bool muted, bool soloed);
        int getLayer() const noexcept { return layer; }

        static constexpr int kWidth = 112, kHeight = 22, kGap = 4;

        std::function<void (int layer)> onMute, onSolo;
        juce::Colour surface;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

    private:
        juce::Rectangle<float> stadium() const;
        juce::Rectangle<float> wordBounds (int index) const;

        int layer = -1;
        bool muted = false, soloed = false;
    };

    void timerCallback() override;
    void showStripFor (int layer);
    void hideStrip();
    void layoutQuantize();
    void showFader();

    struct Layer
    {
        int key = 0;
        juce::String name;
        int count = 0, lowest = 127, highest = 0;
        std::unique_ptr<LayerChip> chip;
    };

    // Geometry, in this component's coordinates.
    juce::Rectangle<int> bandArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> rollArea() const;      // keys + notes
    juce::Rectangle<int> notesArea() const;     // roll minus keys
    float timeToX (double seconds) const;
    double xToTime (float x) const;
    float pitchTop (int pitch) const;
    float rowHeight() const;
    juce::Rectangle<float> noteBounds (const atm::MidiNote&) const;

    void paintScene (juce::Graphics&);
    void paintWaveform (juce::Graphics&) const;
    void paintLiveWaveform (juce::Graphics&) const;
    void paintRuler (juce::Graphics&) const;
    void paintRoll (juce::Graphics&) const;
    void paintGrid (juce::Graphics&) const;
    void paintKeys (juce::Graphics&) const;
    void paintNotes (juce::Graphics&) const;
    void paintHoverInfo (juce::Graphics&) const;
    void paintWordmark (juce::Graphics&) const;
    void paintMessage (juce::Graphics&) const;

    void clearLayers();
    Layer& ensureLayer (int key);
    void growPitchRange (int pitch);
    Layer* findLayer (int key);
    void layoutChips();
    void layoutActionChip();
    void updateSelection();
    void refreshLayerStates();
    int litLayer() const noexcept { return chipHover >= 0 ? chipHover : noteHover; }
    int layerAt (juce::Point<float>) const;
    void showLayerMenu (int key);
    void showAllMenu();
    std::vector<int> audibleLayerKeys() const;
    void refreshAllChip();

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;
    bool dragHover = false, dragAcceptable = true;
    juce::Typeface::Ptr wordmarkTypeface;
    Status status;
    double progressTime = -1.0;
    double playheadTime = -1.0;
    bool scrubbing = false;   // a press that began in the band or ruler
    std::optional<atm::TempoEstimate> grid;
    int gridDivisions = 4;   // lines per beat: the quantize grid (1/16 = 4)
    int middleCOctave = 4;
    Chip actionChip;

    int runId = -1;
    juce::Image backdrop;
    bool backdropDirty = true;

    int notesVersion = -1;
    std::vector<atm::MidiNote> notes;
    LayerStrip strip;
    PlayButton playButton;
    RecordButton recordButton;
    std::vector<float> livePeaks;
    double livePeakSeconds = 0.02, liveSeconds = 0.0;
    QuantizeLabel quantizeLabel;
    MixPill mixPill;
    QuantizeFader quantizeFader;
    std::vector<Layer> layers;                // ordered by key: GM family order, drums last
    LayerChip allChip { "all" };
    juce::String allChipLabel { "all" };
    int pitchLow = 48, pitchHigh = 72;        // [low, high), whole octaves; grows only
    bool hasPitchRange = false;
    int selectedLayer = -1, chipHover = -1, noteHover = -1;
    bool userPickedLayer = false;
    juce::Point<int> hoverInfoPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
