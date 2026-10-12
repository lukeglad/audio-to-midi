#pragma once

#include <JuceHeader.h>
#include <muscriptor/note.hpp>
#include <optional>

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

    /** The beat grid drawn through the roll (faint beats, stronger bars, bar
        numbers along the bottom). Empty hides it. */
    void setGrid (std::optional<atm::TempoEstimate>);

    /** The current run's notes, append-only within a run. A new `runId`
        clears the layers, selection and pitch range. Cheap to call often:
        only notes past the ones already seen are processed. */
    void setTranscription (int runId, const std::vector<msl::Note>& notes);

    std::function<void()> onAction;

    /** Writes a layer's MIDI for a drag and returns the file. */
    std::function<juce::File (int layerKey)> makeLayerMidiFile;

    /** Right-click → "save as .mid…" on a layer chip. */
    std::function<void (int layerKey)> onSaveLayer;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
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
    float pitchTop (int pitch) const;
    float rowHeight() const;
    juce::Rectangle<float> noteBounds (const msl::Note&) const;

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
    void addNote (const msl::Note&);
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
    std::optional<atm::TempoEstimate> grid;
    Chip actionChip;

    int runId = -1;
    std::vector<msl::Note> notes;
    std::vector<Layer> layers;                // ordered by key: GM family order, drums last
    int pitchLow = 48, pitchHigh = 72;        // [low, high), whole octaves; grows only
    bool hasPitchRange = false;
    int selectedLayer = -1, chipHover = -1, noteHover = -1;
    bool userPickedLayer = false;
    juce::Point<int> hoverInfoPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
