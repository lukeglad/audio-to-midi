#pragma once

#include <JuceHeader.h>
#include "Glass.h"

//==============================================================================
/**
    The plugin's menus as liquid glass, to match everything else: the model
    menu, the quantize grid and the chips' right-click menus. A system popup
    is its own window and can't see the plugin behind it, so this draws the
    menu inside the editor instead: an invisible layer over the whole content
    that catches the click outside (which closes it), holding a glass panel
    of house menu rows (14 pt, 22 px rows, centred text, a tick in the left
    margin, ink-filled highlight).

    One lives on top of the editor's content; anything in the editor finds
    it with GlassMenu::find (*this). Message thread only.
*/
class GlassMenu : public juce::Component,
                  public GlassBackdrop
{
public:
    struct Item
    {
        int id = 0;                 // 0 with an empty text: a separator
        juce::String text;
        bool enabled = true, ticked = false;

        static Item separator() { return {}; }
    };

    using Items = std::vector<Item>;

    GlassMenu();

    /** The menu layer in `from`'s editor, or null outside one. */
    static GlassMenu* find (juce::Component& from);

    /** Under `anchor`, its left edge (or right, `alignRight`) lined up with
        the anchor's. `onResult` gets the chosen id, or 0 when dismissed. */
    void showBelow (juce::Component& anchor, Items, std::function<void (int)> onResult, bool alignRight = false);

    void dismiss();

    const juce::Image& getGlassBackdrop() override { return backdrop; }

    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;

private:
    struct Panel final : juce::Component
    {
        explicit Panel (GlassMenu& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int rowAt (int y) const;
        GlassMenu& owner;
        int hovered = -1;
    };

    void open (juce::Rectangle<int> anchorHere, bool alignRight, Items, std::function<void (int)>);
    void finish (int result);
    void takeSnapshot();
    juce::Rectangle<int> rowBounds (int index) const;
    int panelWidth() const;
    int panelHeight() const;

    Items items;
    std::function<void (int)> callback;
    juce::Image backdrop;
    juce::Rectangle<int> anchor;
    bool alignToRight = false;
    Panel panel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlassMenu)
};
