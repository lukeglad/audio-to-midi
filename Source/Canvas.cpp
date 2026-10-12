#include "Canvas.h"
#include "AudioFlowerLookAndFeel.h"
#include "Engine/MidiExport.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    // Alphas from the brand alpha hierarchy (ReferenceSheet section 06).
    constexpr float kAlphaHot       = 1.00f;   // directly engaged
    constexpr float kAlphaResting   = 0.75f;   // resting interactive / primary message
    constexpr float kAlphaSecondary = 0.45f;   // secondary text, the progress line
    constexpr float kAlphaHairline  = 0.40f;   // every floating surface's border
    constexpr float kAlphaDivider   = 0.16f;   // section dividers; the waveform
    constexpr float kAlphaGrid      = 0.12f;   // octave lines in the roll
    constexpr float kAlphaWatermark = 0.10f;

    // Notes: the selected layer is copper; the rest step down as another
    // layer is lit.
    constexpr float kNoteRest     = 0.30f;
    constexpr float kNoteRecessed = 0.12f;     // while another layer is lit
    constexpr float kSelectedRecessed = 0.35f; // the copper layer while another is lit

    // Layout, in design-space px.
    constexpr int kBandH  = 90;
    constexpr int kRulerH = 18;
    constexpr int kKeysW  = 44;
    constexpr int kTimeInset = 8;              // first and last notes never sit on an edge
    constexpr int kWaveInsetY = 10;
    constexpr int kChipInset = 8, kChipGap = 6;
    constexpr float kDrumTickW = 3.0f;

    constexpr int kPrimaryH = 20, kSecondaryH = 18, kLineGap = 4, kActionGap = 8;

    constexpr bool isBlackKey (int pitch) noexcept
    {
        const int pc = pitch % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    juce::String formatTime (double seconds)
    {
        const int total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    const juce::String kDot = juce::String (juce::CharPointer_UTF8 (" \xC2\xB7 "));

    float textWidth (const juce::String& text, float size)
    {
        return text.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (LnF::uiFont (size), text);
    }
}

//==============================================================================
Canvas::Canvas()
{
    actionChip.onClick = [this] { if (onAction) onAction(); };
    addChildComponent (actionChip);
}

Canvas::~Canvas()
{
    clearLayers();
}

void Canvas::setSource (std::shared_ptr<const SourceAudio> newSource)
{
    if (newSource == source)
        return;

    source = std::move (newSource);
    repaint();
}

void Canvas::setLoadStatus (const juce::String& loading, const juce::String& error)
{
    if (loading == loadingFileName && error == loadError)
        return;

    loadingFileName = loading;
    loadError = error;
    layoutActionChip();
    repaint();
}

void Canvas::setDragHover (bool isHovering)
{
    if (isHovering == dragHover)
        return;

    dragHover = isHovering;
    layoutActionChip();
    repaint();
}

void Canvas::setWordmarkTypeface (juce::Typeface::Ptr face)
{
    wordmarkTypeface = std::move (face);
    repaint();
}

void Canvas::setStatus (const Status& newStatus)
{
    if (newStatus.primary == status.primary && newStatus.secondary == status.secondary
        && newStatus.action == status.action && juce::exactlyEqual (newStatus.primaryAlpha, status.primaryAlpha))
        return;

    status = newStatus;
    layoutActionChip();
    repaint (bandArea());
}

void Canvas::setGrid (std::optional<atm::TempoEstimate> newGrid)
{
    const bool same = grid.has_value() == newGrid.has_value()
        && (! grid.has_value()
            || (juce::exactlyEqual (grid->bpm, newGrid->bpm) && juce::exactlyEqual (grid->firstBeat, newGrid->firstBeat)
                && grid->downbeat == newGrid->downbeat && grid->beatsPerBar == newGrid->beatsPerBar));
    if (same)
        return;

    grid = newGrid;
    repaint (rollArea());
}

void Canvas::setProgressTime (double seconds)
{
    if (juce::exactlyEqual (seconds, progressTime))
        return;

    progressTime = seconds;
    repaint();
}

//==============================================================================
void Canvas::setTranscription (int newRunId, const std::vector<msl::Note>& allNotes)
{
    if (newRunId != runId || allNotes.size() < notes.size())
    {
        runId = newRunId;
        clearLayers();
        repaint();
    }

    if (allNotes.size() == notes.size())
        return;

    for (size_t i = notes.size(); i < allNotes.size(); ++i)
        addNote (allNotes[i]);

    layoutChips();
    updateSelection();
    refreshLayerStates();
    repaint();
}

void Canvas::clearLayers()
{
    for (auto& layer : layers)
        removeChildComponent (layer.chip.get());

    layers.clear();
    notes.clear();
    hasPitchRange = false;
    pitchLow = 48;
    pitchHigh = 72;
    selectedLayer = chipHover = noteHover = -1;
    userPickedLayer = false;
}

Canvas::Layer* Canvas::findLayer (int key)
{
    for (auto& layer : layers)
        if (layer.key == key)
            return &layer;

    return nullptr;
}

void Canvas::addNote (const msl::Note& note)
{
    notes.push_back (note);

    // Pitch range: whole octaves around everything seen, at least two of
    // them, and it only grows so streaming notes never make the roll jump.
    const int octaveLow  = (note.pitch / 12) * 12;
    const int octaveHigh = octaveLow + 12;

    if (! hasPitchRange)
    {
        pitchLow = octaveLow;
        pitchHigh = octaveHigh;
        hasPitchRange = true;
    }

    pitchLow  = juce::jmin (pitchLow, octaveLow);
    pitchHigh = juce::jmax (pitchHigh, octaveHigh);

    while (pitchHigh - pitchLow < 24)
        pitchHigh += 12;

    pitchHigh = juce::jmin (pitchHigh, 132);

    // The layer, created in key order (roughly GM family order, drums last)
    // so chips never reshuffle while notes stream in.
    const int key = atm::layerKeyFor (note);
    auto* layer = findLayer (key);

    if (layer == nullptr)
    {
        Layer fresh;
        fresh.key = key;
        fresh.name = atm::layerName (key);
        fresh.chip = std::make_unique<LayerChip> (fresh.name);

        auto& chip = *fresh.chip;
        chip.onHoverChanged = [this, key] (bool hovering)
        {
            chipHover = hovering ? key : (chipHover == key ? -1 : chipHover);
            refreshLayerStates();
            repaint();
        };
        chip.onClick = [this, key]
        {
            selectedLayer = key;
            userPickedLayer = true;
            refreshLayerStates();
            repaint();
        };
        chip.onContextMenu = [this, key] { showLayerMenu (key); };
        chip.makeDragFile = [this, key] { return makeLayerMidiFile ? makeLayerMidiFile (key) : juce::File(); };

        addAndMakeVisible (chip);

        const auto pos = std::find_if (layers.begin(), layers.end(), [key] (const Layer& l) { return l.key > key; });
        layer = &*layers.insert (pos, std::move (fresh));
    }

    ++layer->count;
    layer->lowest  = juce::jmin (layer->lowest, note.pitch);
    layer->highest = juce::jmax (layer->highest, note.pitch);
}

void Canvas::updateSelection()
{
    if (userPickedLayer && findLayer (selectedLayer) != nullptr)
        return;

    // The busiest layer, until the user picks one. Ties go to the earlier key.
    int best = -1, bestCount = -1;
    for (const auto& layer : layers)
        if (layer.count > bestCount)
        {
            best = layer.key;
            bestCount = layer.count;
        }

    selectedLayer = best;
}

void Canvas::refreshLayerStates()
{
    const int lit = litLayer();

    for (auto& layer : layers)
    {
        layer.chip->setLit (layer.key == lit);
        layer.chip->setSelected (layer.key == selectedLayer);
    }
}

void Canvas::showLayerMenu (int key)
{
    juce::PopupMenu menu;
    menu.addItem (1, "save as .mid" + juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA6")));

    auto* layer = findLayer (key);
    juce::Component::SafePointer<Canvas> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (layer != nullptr ? layer->chip.get() : nullptr),
                        [safeThis, key] (int result)
    {
        if (safeThis != nullptr && result == 1 && safeThis->onSaveLayer)
            safeThis->onSaveLayer (key);
    });
}

//==============================================================================
juce::Rectangle<int> Canvas::bandArea() const  { return getLocalBounds().removeFromTop (kBandH); }
juce::Rectangle<int> Canvas::rulerArea() const { return getLocalBounds().withTrimmedTop (kBandH).removeFromTop (kRulerH); }
juce::Rectangle<int> Canvas::rollArea() const  { return getLocalBounds().withTrimmedTop (kBandH + kRulerH); }
juce::Rectangle<int> Canvas::notesArea() const { return rollArea().withTrimmedLeft (kKeysW); }

float Canvas::timeToX (double seconds) const
{
    const float left  = (float) (kKeysW + kTimeInset);
    const float right = (float) (getWidth() - kTimeInset);
    const double length = source != nullptr ? source->getLengthSeconds() : 0.0;

    if (length <= 0.0)
        return left;

    return left + (right - left) * (float) juce::jlimit (0.0, 1.0, seconds / length);
}

float Canvas::rowHeight() const
{
    return (float) rollArea().getHeight() / (float) juce::jmax (1, pitchHigh - pitchLow);
}

float Canvas::pitchTop (int pitch) const
{
    return (float) rollArea().getBottom() - (float) (pitch - pitchLow + 1) * rowHeight();
}

juce::Rectangle<float> Canvas::noteBounds (const msl::Note& note) const
{
    const float rowH = rowHeight();
    const float inset = rowH >= 4.0f ? 1.0f : 0.0f;
    const float x = timeToX (note.onset);
    const float w = note.is_drum ? kDrumTickW : juce::jmax (2.0f, timeToX (note.offset) - x);

    return { x, pitchTop (note.pitch) + inset, w, juce::jmax (1.0f, rowH - 2.0f * inset) };
}

//==============================================================================
void Canvas::resized()
{
    layoutChips();
    layoutActionChip();
}

void Canvas::layoutChips()
{
    // Flowed left to right along the top of the roll, wrapping if a long
    // instrument list outruns the width. Each fills with the surface shade
    // at its own height so it disappears into the gradient.
    const auto area = notesArea().reduced (kChipInset);
    int x = area.getX(), y = area.getY();
    const auto base = LnF::background();

    for (auto& layer : layers)
    {
        const int w = layer.chip->getIdealWidth();
        if (x > area.getX() && x + w > area.getRight())
        {
            x = area.getX();
            y += LayerChip::kHeight + kChipGap;
        }

        layer.chip->setBounds (x, y, w, LayerChip::kHeight);
        layer.chip->setSurfaceColour (LnF::surfaceAt (base, (float) (y + LayerChip::kHeight / 2) / (float) juce::jmax (1, getHeight())));
        x += w + kChipGap;
    }

    // The hover readout follows the last chip, or starts a new row.
    if (x + 200 > area.getRight() && ! layers.empty())
    {
        x = area.getX();
        y += LayerChip::kHeight + kChipGap;
    }

    hoverInfoPos = { x + (layers.empty() ? 0 : 6), y };
}

void Canvas::layoutActionChip()
{
    const bool show = status.action.isNotEmpty() && loadingFileName.isEmpty() && ! dragHover && loadError.isEmpty();
    actionChip.setVisible (show);

    if (! show)
        return;

    actionChip.setText (status.action);

    const auto band = bandArea().withTrimmedLeft (kKeysW);
    const int blockH = kPrimaryH + (status.secondary.isNotEmpty() ? kLineGap + kSecondaryH : 0);
    const int groupH = blockH + kActionGap + Chip::kHeight;
    const int top = band.getCentreY() - groupH / 2 + blockH + kActionGap;

    actionChip.setBounds (juce::Rectangle<int> (actionChip.getIdealWidth(), Chip::kHeight)
                              .withCentre ({ band.getCentreX(), top + Chip::kHeight / 2 }));
}

//==============================================================================
void Canvas::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // One lit surface under both sections (see LnF::surfaceAt).
    const auto base = LnF::background();
    g.setGradientFill (juce::ColourGradient::vertical (LnF::surfaceAt (base, 0.0f), 0.0f,
                                                       LnF::surfaceAt (base, 1.0f), bounds.getHeight()));
    g.fillRect (bounds);

    paintRoll (g);
    paintGrid (g);
    paintNotes (g);
    paintKeys (g);
    paintWaveform (g);
    paintRuler (g);

    // Section dividers.
    g.setColour (LnF::ink().withAlpha (kAlphaDivider));
    g.drawHorizontalLine (rulerArea().getY(), 0.0f, bounds.getWidth());
    g.drawHorizontalLine (rollArea().getY(), 0.0f, bounds.getWidth());

    // Where the transcription has got to: a hairline through both sections.
    // Ink, not copper -- copper is the selected layer (and later the playhead).
    if (progressTime >= 0.0 && source != nullptr)
    {
        const float x = timeToX (progressTime);
        g.setColour (LnF::ink().withAlpha (kAlphaSecondary));
        g.drawLine (x, (float) kWaveInsetY, x, bounds.getBottom(), 1.0f);
    }

    paintHoverInfo (g);
    paintWordmark (g);
    paintMessage (g);

    if (dragHover)
    {
        g.setColour (LnF::ink().withAlpha (kAlphaHairline));
        g.drawRoundedRectangle (bounds.reduced (8.5f), 10.0f, 1.0f);
    }
}

void Canvas::paintWaveform (juce::Graphics& g) const
{
    if (source == nullptr)
        return;

    const auto& mins = source->waveform.mins;
    const auto& maxs = source->waveform.maxs;
    const int numBuckets = (int) mins.size();

    const float left = timeToX (0.0), right = timeToX (source->getLengthSeconds());
    const auto area = juce::Rectangle<float> (left, (float) kWaveInsetY, right - left, (float) (kBandH - 2 * kWaveInsetY));
    const int numColumns = juce::jmax (1, juce::roundToInt (area.getWidth()));

    if (numBuckets == 0 || area.isEmpty())
        return;

    // Normalised to the file's own peak: a quiet memo fills the band the same
    // as a mastered song.
    const float peak    = source->waveform.peak;
    const float gain    = peak > 1.0e-6f ? 1.0f / peak : 1.0f;
    const float centreY = area.getCentreY();
    const float halfH   = area.getHeight() * 0.5f;

    // One filled outline rather than a rect per column: the UI is scaled by
    // a non-integer factor, and per-column rects would overlap or gap.
    std::vector<float> bottoms ((size_t) numColumns);
    juce::Path outline;

    for (int c = 0; c < numColumns; ++c)
    {
        const int b0 = (int) ((int64_t) c * numBuckets / numColumns);
        const int b1 = juce::jmax (b0 + 1, (int) ((int64_t) (c + 1) * numBuckets / numColumns));

        float lo = 0.0f, hi = 0.0f;
        for (int b = b0; b < juce::jmin (b1, numBuckets); ++b)
        {
            lo = juce::jmin (lo, mins[(size_t) b]);
            hi = juce::jmax (hi, maxs[(size_t) b]);
        }

        const float top    = juce::jmin (centreY - 0.5f, centreY - hi * gain * halfH);
        const float bottom = juce::jmax (centreY + 0.5f, centreY - lo * gain * halfH);
        const float x      = area.getX() + area.getWidth() * ((float) c + 0.5f) / (float) numColumns;

        if (c == 0)
            outline.startNewSubPath (area.getX(), top);
        outline.lineTo (x, top);
        bottoms[(size_t) c] = bottom;
    }

    outline.lineTo (area.getRight(), bottoms.back());
    for (int c = numColumns - 1; c >= 0; --c)
        outline.lineTo (area.getX() + area.getWidth() * ((float) c + 0.5f) / (float) numColumns, bottoms[(size_t) c]);
    outline.lineTo (area.getX(), bottoms.front());
    outline.closeSubPath();

    // Brighter than the old under-the-notes ghost: the band is its own space now.
    g.setColour (LnF::ink().withAlpha (0.28f));
    g.fillPath (outline);
}

void Canvas::paintRuler (juce::Graphics& g) const
{
    if (source == nullptr || source->getLengthSeconds() <= 0.0)
        return;

    const auto ruler = rulerArea();
    const double length = source->getLengthSeconds();
    const float pxPerSecond = (timeToX (length) - timeToX (0.0)) / (float) length;

    // The smallest step that keeps labels ~56 px apart.
    double step = 300.0;
    for (const double candidate : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
        if (candidate * pxPerSecond >= 56.0f)
        {
            step = candidate;
            break;
        }

    g.setFont (LnF::uiFont (11.0f));

    for (double t = 0.0; t <= length + 1.0e-6; t += step)
    {
        const float x = timeToX (t);
        g.setColour (LnF::ink().withAlpha (kAlphaHairline));
        g.drawVerticalLine (juce::roundToInt (x), (float) ruler.getY(), (float) ruler.getY() + 5.0f);

        g.setColour (LnF::ink().withAlpha (kAlphaSecondary));
        g.drawText (formatTime (t), juce::Rectangle<float> (x + 3.0f, (float) ruler.getY() + 2.0f, 48.0f, (float) ruler.getHeight() - 2.0f),
                    juce::Justification::centredLeft, false);
    }
}

void Canvas::paintRoll (juce::Graphics& g) const
{
    const auto area = notesArea().toFloat();
    const float rowH = rowHeight();

    for (int p = pitchLow; p < pitchHigh; ++p)
    {
        const float y = pitchTop (p);

        // Black-key rows a shade darker, like a DAW's piano roll. Black at
        // low alpha is an effect colour (a dimming wash), not a palette token.
        if (isBlackKey (p))
        {
            g.setColour (juce::Colours::black.withAlpha (0.12f));
            g.fillRect (area.getX(), y, area.getWidth(), rowH);
        }

        // A faint line under every C.
        if (p % 12 == 0)
        {
            g.setColour (LnF::ink().withAlpha (kAlphaGrid));
            g.drawHorizontalLine (juce::roundToInt (y + rowH), area.getX(), area.getRight());
        }
    }
}

void Canvas::paintGrid (juce::Graphics& g) const
{
    if (! grid.has_value() || source == nullptr || source->getLengthSeconds() <= 0.0)
        return;

    const auto area = notesArea().toFloat();
    const double length = source->getLengthSeconds();
    const double beat = grid->beatSeconds();
    const float beatPx = (float) beat * (timeToX (length) - timeToX (0.0)) / (float) length;

    // Beats only when they have room to read as a grid; bars always.
    const bool drawBeats = beatPx >= 7.0f;
    const bool drawNumbers = beatPx * (float) grid->beatsPerBar >= 26.0f;

    const int firstK = (int) std::floor (-grid->firstBeat / beat);
    const int lastK  = (int) std::ceil ((length - grid->firstBeat) / beat);

    g.setFont (LnF::uiFont (11.0f));

    for (int k = firstK; k <= lastK; ++k)
    {
        const double t = grid->beatTime (k);
        if (t < 0.0 || t > length)
            continue;

        const int fromDownbeat = k - grid->downbeat;
        const bool bar = ((fromDownbeat % grid->beatsPerBar) + grid->beatsPerBar) % grid->beatsPerBar == 0;
        if (! bar && ! drawBeats)
            continue;

        const float x = timeToX (t);
        g.setColour (LnF::ink().withAlpha (bar ? kAlphaDivider : 0.06f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

        // Bar 1 is the first downbeat; anything before it is a pickup and
        // goes unnumbered.
        const int barNumber = fromDownbeat / grid->beatsPerBar + 1;
        if (bar && drawNumbers && fromDownbeat >= 0)
        {
            g.setColour (LnF::ink().withAlpha (0.30f));
            g.drawText (juce::String (barNumber), juce::Rectangle<float> (x + 3.0f, area.getBottom() - 15.0f, 40.0f, 13.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}

void Canvas::paintKeys (juce::Graphics& g) const
{
    const auto keys = rollArea().withWidth (kKeysW).toFloat();
    const float rowH = rowHeight();
    const auto whiteKey = LnF::ink().withAlpha (0.55f);
    const auto gap = LnF::background();

    // White first, full width; black keys over them at 62%, the way a real
    // keyboard reads at a glance.
    g.setColour (whiteKey);
    g.fillRect (keys);

    for (int p = pitchLow; p < pitchHigh; ++p)
    {
        const float y = pitchTop (p);

        if (isBlackKey (p))
        {
            g.setColour (gap);
            g.fillRect (keys.getX(), y, keys.getWidth() * 0.62f, rowH);
        }
        else if (! isBlackKey (p + 1))
        {
            // Two white keys meeting (E-F, B-C): the seam between them.
            g.setColour (gap);
            g.drawHorizontalLine (juce::roundToInt (y), keys.getX(), keys.getRight());
        }

        if (p % 12 == 0 && rowH * 12.0f >= 24.0f)
        {
            g.setColour (gap);
            g.setFont (LnF::uiFont (juce::jlimit (8.0f, 11.0f, rowH + 2.0f)));
            g.drawText (atm::pitchName (p), juce::Rectangle<float> (keys.getX(), y + rowH - 12.0f, keys.getWidth() - 3.0f, 12.0f),
                        juce::Justification::bottomRight, false);
        }
    }

    g.setColour (gap);
    g.drawVerticalLine ((int) keys.getRight(), keys.getY(), keys.getBottom());
}

void Canvas::paintNotes (juce::Graphics& g) const
{
    if (source == nullptr || notes.empty())
        return;

    const int lit = litLayer();

    const auto colourFor = [&] (int key)
    {
        if (key == selectedLayer)
            return LnF::accent().withAlpha (lit >= 0 && lit != key ? kSelectedRecessed : 1.0f);
        if (key == lit)
            return LnF::ink();
        return LnF::ink().withAlpha (lit >= 0 ? kNoteRecessed : kNoteRest);
    };

    // Recessed layers first, then the selected layer, then the lit one on top.
    const auto drawLayer = [&] (int key)
    {
        g.setColour (colourFor (key));
        for (const auto& note : notes)
            if (atm::layerKeyFor (note) == key)
                g.fillRoundedRectangle (noteBounds (note), 1.5f);
    };

    g.saveState();
    g.reduceClipRegion (notesArea());

    for (const auto& layer : layers)
        if (layer.key != selectedLayer && layer.key != lit)
            drawLayer (layer.key);

    if (selectedLayer >= 0 && selectedLayer != lit)
        drawLayer (selectedLayer);
    if (lit >= 0)
        drawLayer (lit);

    g.restoreState();
}

void Canvas::paintHoverInfo (juce::Graphics& g) const
{
    const int lit = litLayer();
    const auto* layer = [&]() -> const Layer*
    {
        for (const auto& l : layers)
            if (l.key == lit)
                return &l;
        return nullptr;
    }();

    if (layer == nullptr)
        return;

    const bool drums = layer->key == atm::kDrumLayer;
    juce::String text = juce::String (layer->count) + (drums ? (layer->count == 1 ? " hit" : " hits")
                                                             : (layer->count == 1 ? " note" : " notes"));
    if (! drums)
        text += kDot + atm::pitchName (layer->lowest) + juce::String (juce::CharPointer_UTF8 ("\xE2\x80\x93"))
              + atm::pitchName (layer->highest);

    g.setFont (LnF::uiFont (12.0f));
    g.setColour (LnF::ink().withAlpha (kAlphaSecondary));
    g.drawText (text, juce::Rectangle<int> (hoverInfoPos.x, hoverInfoPos.y, 260, LayerChip::kHeight),
                juce::Justification::centredLeft, false);
}

void Canvas::paintWordmark (juce::Graphics& g) const
{
    // Indie Flower: brand marks only. Same size and alpha as EQ's watermark.
    if (wordmarkTypeface == nullptr)
        return;

    g.setFont (juce::Font (juce::FontOptions (26.0f).withTypeface (wordmarkTypeface)));
    g.setColour (LnF::ink().withAlpha (kAlphaWatermark));
    g.drawText ("AudioFlower", notesArea().removeFromBottom (48).withTrimmedBottom (14),
                juce::Justification::centred, false);
}

void Canvas::paintMessage (juce::Graphics& g) const
{
    juce::String primary, secondary;
    float primaryAlpha = kAlphaResting;
    bool hasAction = false;

    if (loadingFileName.isNotEmpty())
    {
        primary = "reading " + loadingFileName + juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA6"));
    }
    else if (dragHover)
    {
        primary = source != nullptr ? "drop to replace" : "drop to load";
        primaryAlpha = kAlphaHot;
    }
    else if (loadError.isNotEmpty())
    {
        primary = loadError;
        secondary = "try another file";
    }
    else if (status.primary.isNotEmpty())
    {
        primary = status.primary;
        secondary = status.secondary;
        primaryAlpha = status.primaryAlpha;
        hasAction = status.action.isNotEmpty();
    }
    else if (source == nullptr)
    {
        primary = "drop an audio file";
        secondary = supportedFormatsLabel();
    }

    if (primary.isEmpty())
        return;

    const auto band = bandArea().withTrimmedLeft (kKeysW);
    const int blockH = kPrimaryH + (secondary.isNotEmpty() ? kLineGap + kSecondaryH : 0);
    const int groupH = blockH + (hasAction ? kActionGap + Chip::kHeight : 0);
    auto block = juce::Rectangle<int> (band.getX() + 16, band.getCentreY() - groupH / 2, band.getWidth() - 32, blockH);

    // A message over audio sits on a shade of the surface so the waveform
    // doesn't run through the text.
    if (source != nullptr)
    {
        const float textW = juce::jmax (textWidth (primary, 14.0f), textWidth (secondary, 12.0f));
        const auto plate = block.toFloat().withSizeKeepingCentre (juce::jmin ((float) block.getWidth(), textW + 32.0f),
                                                                  (float) blockH + 8.0f);
        g.setColour (LnF::surfaceAt (LnF::background(), plate.getCentreY() / (float) juce::jmax (1, getHeight())).withAlpha (0.85f));
        g.fillRoundedRectangle (plate, plate.getHeight() * 0.5f);
    }

    g.setFont (LnF::uiFont (14.0f));
    g.setColour (LnF::ink().withAlpha (primaryAlpha));
    g.drawFittedText (primary, block.removeFromTop (kPrimaryH), juce::Justification::centred, 1, 1.0f);

    if (secondary.isNotEmpty())
    {
        block.removeFromTop (kLineGap);
        g.setFont (LnF::uiFont (12.0f));
        g.setColour (LnF::ink().withAlpha (kAlphaSecondary));
        g.drawFittedText (secondary, block.removeFromTop (kSecondaryH), juce::Justification::centred, 1, 1.0f);
    }
}

//==============================================================================
int Canvas::layerAt (juce::Point<float> pos) const
{
    if (source == nullptr || ! notesArea().toFloat().contains (pos))
        return -1;

    // In resting stacking order (selected on top, then key order) rather
    // than lit-first: the hover this feeds must not change its own answer,
    // or moving across overlapping notes would stick to whatever lit first.
    const auto hits = [&] (int key)
    {
        for (const auto& note : notes)
            if (atm::layerKeyFor (note) == key && noteBounds (note).expanded (1.0f, 0.0f).contains (pos))
                return true;
        return false;
    };

    if (selectedLayer >= 0 && hits (selectedLayer))
        return selectedLayer;

    for (const auto& layer : layers)
        if (layer.key != selectedLayer && hits (layer.key))
            return layer.key;

    return -1;
}

void Canvas::mouseMove (const juce::MouseEvent& e)
{
    // Notes light their layer from here; chips do it themselves.
    const int under = layerAt (e.position);

    if (under != noteHover)
    {
        noteHover = under;
        refreshLayerStates();
        repaint();
    }

    setMouseCursor (under >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Canvas::mouseExit (const juce::MouseEvent&)
{
    if (noteHover >= 0)
    {
        noteHover = -1;
        refreshLayerStates();
        repaint();
    }
}

void Canvas::mouseUp (const juce::MouseEvent& e)
{
    // Clicking a note selects its layer, the same as clicking its chip.
    if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu())
        return;

    if (const int key = layerAt (e.position); key >= 0)
    {
        selectedLayer = key;
        userPickedLayer = true;
        refreshLayerStates();
        repaint();
    }
}
