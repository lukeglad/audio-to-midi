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
    /** The scrolled text: guide sections, the licence line, the credits. */
    struct Sections final : juce::Component
    {
        void paint (juce::Graphics&) override;
        static int idealHeight (int width);
    };

    /** The glass panel: paints the glass, the pinned title and the close cross. */
    struct Panel final : juce::Component
    {
        explicit Panel (GuideOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        juce::Rectangle<float> closeBounds() const;
        GuideOverlay& owner;
    };

    juce::Rectangle<int> panelBounds() const;
    void takeSnapshot();

    std::vector<juce::Component::SafePointer<juce::Component>> covered;
    juce::Image backdrop;

    Panel panel { *this };
    juce::Viewport viewport;
    Sections sections;
    juce::HyperlinkButton siteLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuideOverlay)
};
