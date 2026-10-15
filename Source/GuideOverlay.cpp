#include "GuideOverlay.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    // What looking around won't teach you, as (heading, body). Not "what is
    // MIDI": the hover strip, the hidden quantize fader, recording, where a
    // recording's clip goes.
    struct Section { const char* heading; const char* body; };

    const Section kSections[]
    {
        { "Drop or record",
          "Drop any audio file on the window: voice memos, loops, whole songs up to 15 minutes. "
          "Or press the dot under play to record what's playing into this track. It starts when "
          "your DAW plays and transcribes the moment it stops." },
        { "Drag out",
          "Drag an instrument's chip onto a track for its MIDI, or \"all\" for every instrument at "
          "once (muted ones are left out). A recording's MIDI starts at the bar named at the top: "
          "drop it there. Right-click a chip to save the file instead." },
        { "Listen",
          "Play previews the notes over your audio; drag \"mix\" to hear more of either. Hover a "
          "chip for mute and solo. Click or drag in the waveform to move around." },
        { "Timing",
          "If the grid is off, drag the tempo or double-click it to type. Hover the quantize "
          "percentage for a fader that pulls notes toward the grid; the grid size sits beside it." },
        { "Model",
          "Balanced suits most things. Fast is quicker; best is the most accurate and the slowest. "
          "Each downloads once, the first time you choose it from the model menu." },
    };

    // The model's licence (CC BY-NC 4.0) asks for both of these.
    const char* const kLicence = "Free for non-commercial use.\nOnly transcribe audio you have the rights to.";

    const Section kCredits[]
    {
        { "Transcription", "MuScriptor by Kyutai and Mirelo (CC BY-NC 4.0), running on muscriptor.cpp by Damien Ronssin (MIT)." },
        { "Preview sounds", "GeneralUser GS by S. Christian Collins, played by TinySoundFont by Bernhard Schelling (MIT)." },
        { "Type and icons", "IBM Plex Mono and Indie Flower (SIL Open Font License). Icons from Tabler (MIT)." },
    };

    constexpr const char* kSiteUrl = "https://audioflower.art";

    // EQ's guide metrics, so the family reads as one.
    constexpr float kTitleFont = 18.0f, kHeadingFont = 14.0f, kBodyFont = 13.0f, kSmallFont = 11.5f;
    constexpr int kPanelW = 430, kPadX = 26, kPadY = 22;
    constexpr int kTitleH = 26, kTitleGap = 16;
    constexpr int kHeadingH = 21, kSectionGap = 16;
    constexpr int kSmallHeadingH = 17, kSmallGap = 8;
    constexpr int kLinkH = 30, kLinkGap = 12, kMaxH = 460;
    constexpr int kCreditsRuleGap = 14;
    constexpr float kCornerRadius = 10.0f;

    // Glass over glass reads poorly as text: the panel lays an extra veil of
    // the ground over its glass, and the scene behind is dimmed and blurred.
    constexpr float kPanelVeil = 0.32f, kScrim = 0.25f;
    constexpr float kSnapshotScale = 0.25f;   // the stretch back up is the blur (EQ's frost recipe)

    // Measured and drawn with the same layout, so the gaps between
    // paragraphs are exactly what the metrics say.
    juce::TextLayout layoutFor (const juce::String& text, float fontSize, juce::Colour colour, int width)
    {
        juce::AttributedString as;
        as.append (text, LnF::uiFont (fontSize), colour);
        juce::TextLayout layout;
        layout.createLayout (as, (float) width);
        return layout;
    }

    int textHeight (const juce::String& text, float fontSize, int width)
    {
        return (int) std::ceil (layoutFor (text, fontSize, {}, width).getHeight());
    }

    /** Draws `text` wrapped at the top of `area` and takes its height off. */
    void drawParagraph (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& text, float fontSize, juce::Colour colour)
    {
        const auto layout = layoutFor (text, fontSize, colour, area.getWidth());
        layout.draw (g, area.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
    }

    int chromeHeight() { return kPadY * 2 + kTitleH + kTitleGap + kLinkGap + kLinkH; }

    /** Box blur of an ARGB image, in place, two passes per axis (≈ Gaussian).
        Runs on the quarter-size snapshot, so it is cheap; without it the
        stretch back up shows its pixels as squares. */
    void blur (juce::Image& image, int radius)
    {
        const int w = image.getWidth(), h = image.getHeight();
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        std::vector<juce::Colour> line ((size_t) juce::jmax (w, h));

        const auto pass = [&] (bool horizontal)
        {
            const int outer = horizontal ? h : w, inner = horizontal ? w : h;
            for (int o = 0; o < outer; ++o)
            {
                const auto at = [&] (int i) { return horizontal ? data.getPixelColour (juce::jlimit (0, w - 1, i), o)
                                                                : data.getPixelColour (o, juce::jlimit (0, h - 1, i)); };
                for (int i = 0; i < inner; ++i)
                {
                    float r = 0, g = 0, b = 0;
                    for (int k = -radius; k <= radius; ++k)
                    {
                        const auto c = at (i + k);
                        r += c.getFloatRed(); g += c.getFloatGreen(); b += c.getFloatBlue();
                    }
                    const float n = (float) (radius * 2 + 1);
                    line[(size_t) i] = juce::Colour::fromFloatRGBA (r / n, g / n, b / n, 1.0f);
                }
                for (int i = 0; i < inner; ++i)
                {
                    if (horizontal) data.setPixelColour (i, o, line[(size_t) i]);
                    else            data.setPixelColour (o, i, line[(size_t) i]);
                }
            }
        };

        for (int k = 0; k < 2; ++k) { pass (true); pass (false); }
    }
}

//==============================================================================
int GuideOverlay::Sections::idealHeight (int width)
{
    int h = 0;
    for (const auto& s : kSections)
        h += kHeadingH + textHeight (s.body, kBodyFont, width) + kSectionGap;

    h += textHeight (kLicence, kSmallFont, width) + kCreditsRuleGap * 2;
    h += kSmallHeadingH;   // "Made with"
    for (const auto& c : kCredits)
        h += textHeight (juce::String (c.heading) + ". " + c.body, kSmallFont, width) + kSmallGap;

    return h - kSmallGap;
}

void GuideOverlay::Sections::paint (juce::Graphics& g)
{
    // No background: the glass panel behind already painted it.
    auto body = getLocalBounds();

    for (const auto& s : kSections)
    {
        g.setColour (LnF::ink().withAlpha (0.95f));
        g.setFont (LnF::uiFont (kHeadingFont));
        g.drawText (s.heading, body.removeFromTop (kHeadingH), juce::Justification::centredLeft);

        drawParagraph (g, body, s.body, kBodyFont, LnF::ink().withAlpha (0.75f));
        body.removeFromTop (kSectionGap);
    }

    // The licence line, set small: a condition, not a feature.
    drawParagraph (g, body, kLicence, kSmallFont, LnF::ink().withAlpha (0.55f));

    // The credits, quieter than the guide, under a hairline.
    body.removeFromTop (kCreditsRuleGap);
    g.setColour (LnF::ink().withAlpha (0.16f));
    g.drawHorizontalLine (body.getY(), (float) body.getX(), (float) body.getRight());
    body.removeFromTop (kCreditsRuleGap);

    g.setColour (LnF::ink().withAlpha (0.6f));
    g.setFont (LnF::uiFont (12.0f));
    g.drawText ("Made with", body.removeFromTop (kSmallHeadingH), juce::Justification::topLeft);

    for (const auto& c : kCredits)
    {
        juce::AttributedString as;
        as.append (juce::String (c.heading) + ". ", LnF::uiFont (kSmallFont), LnF::ink().withAlpha (0.75f));
        as.append (c.body, LnF::uiFont (kSmallFont), LnF::ink().withAlpha (0.5f));
        juce::TextLayout layout;
        layout.createLayout (as, (float) body.getWidth());
        const int h = (int) std::ceil (layout.getHeight());
        layout.draw (g, body.removeFromTop (h).toFloat());
        body.removeFromTop (kSmallGap);
    }
}

//==============================================================================
juce::Rectangle<float> GuideOverlay::Panel::closeBounds() const
{
    return { (float) getWidth() - 34.0f, 18.0f, 20.0f, 20.0f };
}

void GuideOverlay::Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto surface = LnF::surfaceAt (LnF::background(), 0.5f);

    if (! Glass::paint (g, *this, r, surface, std::nullopt, kCornerRadius))
    {
        g.setColour (LnF::background());
        g.fillRoundedRectangle (r, kCornerRadius);
    }

    g.setColour (LnF::background().withAlpha (kPanelVeil));
    g.fillRoundedRectangle (r.reduced (1.0f), kCornerRadius - 1.0f);

    // The pinned title.
    g.setColour (LnF::ink());
    g.setFont (LnF::uiFont (kTitleFont));
    g.drawText ("Quick guide", getLocalBounds().reduced (kPadX, kPadY).removeFromTop (kTitleH), juce::Justification::centredLeft);

    // The close cross, outline like every other glyph.
    const auto cross = closeBounds().reduced (5.0f);
    const bool hot = closeBounds().contains (getMouseXYRelative().toFloat()) && isMouseOver();
    g.setColour (LnF::ink().withAlpha (hot ? 1.0f : 0.55f));
    g.drawLine ({ cross.getTopLeft(), cross.getBottomRight() }, 1.4f);
    g.drawLine ({ cross.getTopRight(), cross.getBottomLeft() }, 1.4f);

    // Over the pinned link: a hairline only when there is more to scroll,
    // so it reads as the edge the text passes under.
    if (owner.viewport.getViewedComponent() != nullptr
        && owner.viewport.getViewedComponent()->getHeight() > owner.viewport.getMaximumVisibleHeight())
    {
        const float y = (float) (getHeight() - kPadY - kLinkH) - kLinkGap * 0.5f;
        g.setColour (LnF::ink().withAlpha (0.16f));
        g.drawLine ((float) kPadX, y, (float) (getWidth() - kPadX), y, 1.0f);
    }
}

void GuideOverlay::Panel::mouseUp (const juce::MouseEvent& e)
{
    if (closeBounds().contains (e.position))
        owner.close();
}

//==============================================================================
GuideOverlay::GuideOverlay()
{
    addAndMakeVisible (panel);

    viewport.setViewedComponent (&sections, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    // A faint thumb, no track: it says "there's more" without a bar of
    // full ink beside the text.
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, LnF::ink().withAlpha (0.25f));
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    panel.addAndMakeVisible (viewport);

    siteLink.setButtonText ("audioflower.art");
    siteLink.setURL (juce::URL (kSiteUrl));
    siteLink.setFont (LnF::uiFont (kBodyFont), false, juce::Justification::centred);
    // The one copper outside the canvas: a link is genuinely interactive
    // (as in EQ's guide).
    siteLink.setColour (juce::HyperlinkButton::textColourId, LnF::accent());
    panel.addAndMakeVisible (siteLink);

    setVisible (false);
}

void GuideOverlay::open (std::vector<juce::Component*> behind)
{
    covered.clear();
    for (auto* c : behind)
        covered.emplace_back (c);

    viewport.setViewPosition (0, 0);   // always from the top
    setVisible (true);
    toFront (false);
    takeSnapshot();
    repaint();
}

void GuideOverlay::close()
{
    setVisible (false);
    backdrop = {};
}

void GuideOverlay::takeSnapshot()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // The covered components, drawn small...
    const int sw = juce::jmax (1, juce::roundToInt ((float) getWidth() * kSnapshotScale));
    const int sh = juce::jmax (1, juce::roundToInt ((float) getHeight() * kSnapshotScale));
    juce::Image small (juce::Image::ARGB, sw, sh, true);
    {
        juce::Graphics g (small);
        g.addTransform (juce::AffineTransform::scale (kSnapshotScale));
        g.fillAll (LnF::background());
        for (auto& c : covered)
            if (c != nullptr && c->isVisible())
            {
                juce::Graphics::ScopedSaveState state (g);
                g.setOrigin (c->getPosition() - getPosition());
                c->paintEntireComponent (g, true);
            }
    }

    // ...softened there, where it's cheap...
    blur (small, 2);

    // ...and stretched back to full size.
    backdrop = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (backdrop);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (small, getLocalBounds().toFloat());
}

juce::Rectangle<int> GuideOverlay::panelBounds() const
{
    const int wanted = chromeHeight() + Sections::idealHeight (kPanelW - kPadX * 2);
    const int h = juce::jmin (wanted, kMaxH, getHeight() - 24);
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kPanelW, getWidth() - 24), h);
}

void GuideOverlay::resized()
{
    panel.setBounds (panelBounds());

    auto inner = panel.getLocalBounds().reduced (kPadX, kPadY);
    inner.removeFromTop (kTitleH + kTitleGap);
    siteLink.setBounds (inner.removeFromBottom (kLinkH));
    inner.removeFromBottom (kLinkGap);
    viewport.setBounds (inner);

    // Measured twice, as in EQ: the width depends on whether a scrollbar
    // shows, which depends on the height at that width.
    const int w0 = viewport.getMaximumVisibleWidth();
    sections.setSize (w0, Sections::idealHeight (w0));
    if (const int w1 = viewport.getMaximumVisibleWidth(); w1 != w0)
        sections.setSize (w1, Sections::idealHeight (w1));

    if (isVisible())
        takeSnapshot();
}

void GuideOverlay::paint (juce::Graphics& g)
{
    // The plugin behind, blurred and dimmed, so the panel is the only live
    // thing on screen.
    if (backdrop.isValid())
        g.drawImageAt (backdrop, 0, 0);
    else
        g.fillAll (LnF::background());

    g.fillAll (juce::Colours::black.withAlpha (kScrim));
}

void GuideOverlay::mouseDown (const juce::MouseEvent& e)
{
    // Anywhere outside the panel closes it; nothing behind is reachable
    // while it's up.
    if (! panelBounds().contains (e.getPosition()))
        close();
}
