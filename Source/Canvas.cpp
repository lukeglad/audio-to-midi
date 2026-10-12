#include "Canvas.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    // Alphas from the brand alpha hierarchy (ReferenceSheet section 06).
    constexpr float kAlphaHot       = 1.00f;   // directly engaged
    constexpr float kAlphaResting   = 0.75f;   // resting interactive / primary message
    constexpr float kAlphaSecondary = 0.45f;   // secondary text
    constexpr float kAlphaHairline  = 0.40f;   // every floating surface's border
    constexpr float kAlphaGhost     = 0.16f;   // the waveform: present, never competing with notes
    constexpr float kAlphaWatermark = 0.10f;

    // The waveform keeps clear of the canvas edges so the first and last
    // notes never sit on the border.
    constexpr float kWaveformInsetX = 16.0f, kWaveformInsetY = 24.0f;
}

Canvas::Canvas()
{
    // Clicks fall through to the editor for now; nothing on the canvas is
    // interactive until notes and layers arrive.
    setInterceptsMouseClicks (false, false);
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
    repaint();
}

void Canvas::setDragHover (bool isHovering)
{
    if (isHovering == dragHover)
        return;

    dragHover = isHovering;
    repaint();
}

void Canvas::setWordmarkTypeface (juce::Typeface::Ptr face)
{
    wordmarkTypeface = std::move (face);
    repaint();
}

//==============================================================================
void Canvas::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // The lit surface (see LnF::surfaceAt): anything floating on the canvas
    // later fills with the shade at its own height so it disappears into this.
    const auto base = LnF::background();
    g.setGradientFill (juce::ColourGradient::vertical (LnF::surfaceAt (base, 0.0f), 0.0f,
                                                       LnF::surfaceAt (base, 1.0f), bounds.getHeight()));
    g.fillRect (bounds);

    if (source != nullptr)
        paintWaveform (g, bounds.reduced (kWaveformInsetX, kWaveformInsetY));

    paintWordmark (g);
    paintMessage (g);

    if (dragHover)
    {
        g.setColour (LnF::ink().withAlpha (kAlphaHairline));
        g.drawRoundedRectangle (bounds.reduced (8.5f), 10.0f, 1.0f);
    }
}

void Canvas::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& mins = source->waveform.mins;
    const auto& maxs = source->waveform.maxs;
    const int numBuckets = (int) mins.size();
    const int numColumns = juce::jmax (1, juce::roundToInt (area.getWidth()));

    if (numBuckets == 0 || area.isEmpty())
        return;

    // Normalised to the file's own peak: a quiet memo fills the height the
    // same as a mastered song.
    const float peak    = source->waveform.peak;
    const float gain    = peak > 1.0e-6f ? 1.0f / peak : 1.0f;
    const float centreY = area.getCentreY();
    const float halfH   = area.getHeight() * 0.5f;

    // One filled outline (top edge left to right, bottom edge back) rather
    // than a rect per column: the editor scales the whole UI by a
    // non-integer factor, and per-column rects would overlap or gap there.
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

        // Half a pixel either side of centre at minimum, so silence reads
        // as a thin line instead of vanishing.
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
        outline.lineTo (area.getX() + area.getWidth() * ((float) c + 0.5f) / (float) numColumns,
                        bottoms[(size_t) c]);
    outline.lineTo (area.getX(), bottoms.front());
    outline.closeSubPath();

    g.setColour (LnF::ink().withAlpha (kAlphaGhost));
    g.fillPath (outline);
}

void Canvas::paintWordmark (juce::Graphics& g) const
{
    // Indie Flower: brand marks only. Same size and alpha as EQ's watermark.
    if (wordmarkTypeface == nullptr)
        return;

    g.setFont (juce::Font (juce::FontOptions (26.0f).withTypeface (wordmarkTypeface)));
    g.setColour (LnF::ink().withAlpha (kAlphaWatermark));
    g.drawText ("AudioFlower",
                getLocalBounds().removeFromBottom (48).withTrimmedBottom (14),
                juce::Justification::centred, false);
}

void Canvas::paintMessage (juce::Graphics& g) const
{
    juce::String primary, secondary;
    float primaryAlpha = kAlphaResting;

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
    else if (source == nullptr)
    {
        primary = "drop an audio file";
        secondary = supportedFormatsLabel();
    }

    if (primary.isEmpty())
        return;

    constexpr int primaryH = 20, secondaryH = 18, gap = 4;
    const int blockH = primaryH + (secondary.isNotEmpty() ? gap + secondaryH : 0);
    auto block = getLocalBounds().reduced (24, 0).withSizeKeepingCentre (getWidth() - 48, blockH);

    g.setFont (LnF::uiFont (14.0f));
    g.setColour (LnF::ink().withAlpha (primaryAlpha));
    g.drawFittedText (primary, block.removeFromTop (primaryH), juce::Justification::centred, 1, 1.0f);

    if (secondary.isNotEmpty())
    {
        block.removeFromTop (gap);
        g.setFont (LnF::uiFont (12.0f));
        g.setColour (LnF::ink().withAlpha (kAlphaSecondary));
        g.drawFittedText (secondary, block.removeFromTop (secondaryH), juce::Justification::centred, 1, 1.0f);
    }
}
