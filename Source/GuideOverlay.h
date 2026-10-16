#pragma once

#include <JuceHeader.h>
#include "Glass.h"

//==============================================================================
/**
    The "?" quick guide: EQ's guide panel (same metrics, pinned title and
    link, scrolling sections, click outside to close), drawn as liquid glass
    over a blurred, dimmed snapshot of the plugin (user's pick 2026-10-15,
    mockups/guide.html). It explains only what looking won't teach, carries
    the model's licence condition in one line, and ends with the credits.

    Lives on top of the editor's content, covering it. open() takes the
    snapshot of the components behind, so the glass has something to bend.
*/
class GuideOverlay : public juce::Component,
                     public GlassBackdrop
{
public:
    GuideOverlay();

    /** Shows the guide over `behind` (the components it covers, in this
        overlay's parent's coordinates). */
    void open (std::vector<juce::Component*> behind);
    void close();
    bool isOpen() const noexcept { return isVisible(); }

    const juce::Image& getGlassBackdrop() override { return backdrop; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    /** The scrolled text: guide sections, the licence line, the credits.
        Laid out once per width and kept, so a scroll step only draws. */
    struct Sections final : juce::Component
    {
        void paint (juce::Graphics&) override;
        void resized() override { layOut(); }
        static int idealHeight (int width);

    private:
        void layOut();

        struct Item { juce::TextLayout layout; juce::Rectangle<float> area; };
        std::vector<Item> items;
        int ruleY = 0;
    };

    /** The glass panel: paints the glass, the pinned title and the close
        cross. The glass is rendered once per open (the scene behind it is a
        still snapshot) and kept as an image at the screen's pixel scale. */
    struct Panel final : juce::Component
    {
        explicit Panel (GuideOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void resized() override { glass = {}; }
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint (closeBounds().getSmallestIntegerContainer()); }
        void mouseExit (const juce::MouseEvent&) override { repaint (closeBounds().getSmallestIntegerContainer()); }
        juce::Rectangle<float> closeBounds() const;
        GuideOverlay& owner;
        juce::Image glass;
        float glassScale = 0.0f;
    };

    /** Wheel scrolling without JUCE's whole-pixel steps: a trackpad moves the
        text by exactly what it reports (fractions carried over), a notched
        wheel glides to where it points instead of jumping. */
    struct SmoothViewport final : juce::Viewport
    {
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void visibleAreaChanged (const juce::Rectangle<int>&) override;
        void jumpToTop();

    private:
        void moveTo (double y);
        double maxY() const;

        double target = 0.0, current = 0.0;
        bool moving = false, gliding = false;
        juce::VBlankAttachment vblank { this, [this] {
            if (! gliding) return;
            current += (target - current) * 0.3;
            if (std::abs (target - current) < 0.5) { current = target; gliding = false; }
            moveTo (current);
        } };
    };

    juce::Rectangle<int> panelBounds() const;
    void takeSnapshot();

    std::vector<juce::Component::SafePointer<juce::Component>> covered;
    juce::Image backdrop;

    Panel panel { *this };
    SmoothViewport viewport;
    Sections sections;
    juce::HyperlinkButton siteLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuideOverlay)
};
