#pragma once

#include <JuceHeader.h>
#include <optional>

#include "Engine/Cleanup.h"
#include "Engine/Tempo.h"

#include "Chip.h"
#include "LayerChip.h"
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
class Canvas : public juce::Component
{
public:
    /** What to say in the waveform band while nothing else claims it (a load
        in progress, a drag-over or a load error take precedence). */
    struct Status
    {
        juce::String primary, secondary;
        float primaryAlpha = 0.75f;
        juce::String action;   // chip text; empty = no chip
    };

    Canvas();
    ~Canvas() override;

    void setSource (std::shared_ptr<const SourceAudio>);
    void setLoadStatus (const juce::String& loadingFileName, const juce::String& loadError);
    void setDragHover (bool isHovering);
    void setWordmarkTypeface (juce::Typeface::Ptr);
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

    /** The preview playhead, in seconds; negative hides it. Copper: the
        brief's second reserved role for the accent. */
    void setPlayhead (double seconds);

    /** Preview state per layer, read when painting and refreshing chips. */
    std::function<bool (int layer)> isLayerMuted, isLayerSoloed, isLayerAudible;
    std::function<void (int layer)> onMuteToggled, onSoloToggled;

    /** Click in the waveform band or ruler: locate the preview. */
    std::function<void (double seconds)> onSeek;

    /** Re-reads the preview state into the chips and the roll. */
    void refreshPreviewState();

    std::function<void()> onAction;

    /** Writes a layer's MIDI for a drag and returns the file. */
    std::function<juce::File (int layerKey)> makeLayerMidiFile;

    /** Right-click → "save as .mid…" on a layer chip. */
    std::function<void (int layerKey)> onSaveLayer;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
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

    void paintWaveform (juce::Graphics&) const;
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

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;
    bool dragHover = false;
    juce::Typeface::Ptr wordmarkTypeface;
    Status status;
    double progressTime = -1.0;
    double playheadTime = -1.0;
    std::optional<atm::TempoEstimate> grid;
    int gridDivisions = 4;   // lines per beat: the quantize grid (1/16 = 4)
    Chip actionChip;

    int runId = -1;
    int notesVersion = -1;
    std::vector<atm::MidiNote> notes;
    std::vector<Layer> layers;                // ordered by key: GM family order, drums last
    int pitchLow = 48, pitchHigh = 72;        // [low, high), whole octaves; grows only
    bool hasPitchRange = false;
    int selectedLayer = -1, chipHover = -1, noteHover = -1;
    bool userPickedLayer = false;
    juce::Point<int> hoverInfoPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
