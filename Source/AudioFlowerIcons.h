#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h" // palette tokens (ink()) for icon recolouring

//==============================================================================
/**
    AudioFlower house icon set: outline-stroke icons from Tabler Icons
    (MIT licensed, github.com/tabler/tabler-icons), all sharing the same
    24x24 viewBox and 1.5 stroke width so they read as one consistent family
    at small sizes. No filled/solid icons anywhere in the house style -- if a
    new icon is ever needed, pick the "outline" variant from Tabler, not
    "filled".

    Usage: makeTablerIcon (kUndo) returns a ready-to-use juce::Drawable, e.g.
      icon = makeTablerIcon (AudioFlowerIcons::kUndo);
      icon->drawWithin (g, bounds, juce::RectanglePlacement::centred, alpha);

    Each constant below is just the inner <path> element(s) copied verbatim
    from Tabler's own SVG source -- no coordinate conversion needed since the
    wrapping viewBox matches. Add new icons the same way: fetch from
    github.com/tabler/tabler-icons/blob/main/icons/outline/<name>.svg and
    paste its <path d="..."/> here.
*/
namespace AudioFlowerIcons
{
    inline std::unique_ptr<juce::Drawable> makeTablerIcon (const juce::String& innerPaths)
    {
        const juce::String svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
            "fill=\"none\" stroke=\"black\" stroke-width=\"1.5\" "
            "stroke-linecap=\"round\" stroke-linejoin=\"round\">" + innerPaths + "</svg>";

        if (auto xml = juce::XmlDocument::parse (svg))
        {
            auto drawable = juce::Drawable::createFromSVG (*xml);

            // The SVG string above bakes in stroke="black", which no palette
            // token can reach -- recolour to the house ink() so every icon
            // follows the theme (a no-op while ink() IS black).
            if (drawable != nullptr)
                drawable->replaceColour (juce::Colours::black, AudioFlowerLookAndFeel::ink());

            return drawable;
        }
        return nullptr;
    }

    // chevron-left / chevron-right -- prev/next (e.g. preset stepping)
    static const juce::String kChevronLeft  = "<path d=\"M15 6l-6 6l6 6\" />";
    static const juce::String kChevronRight = "<path d=\"M9 6l6 6l-6 6\" />";

    // arrow-back-up / arrow-forward-up -- undo/redo
    static const juce::String kUndo = "<path d=\"M9 14l-4 -4l4 -4\" />"
                                       "<path d=\"M5 10h11a4 4 0 1 1 0 8h-1\" />";
    static const juce::String kRedo = "<path d=\"M15 14l4 -4l-4 -4\" />"
                                       "<path d=\"M19 10h-11a4 4 0 1 0 0 8h1\" />";

    // device-floppy -- save
    static const juce::String kSave = "<path d=\"M6 4h10l4 4v10a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2\" />"
                                       "<path d=\"M10 14a2 2 0 1 0 4 0a2 2 0 1 0 -4 0\" />"
                                       "<path d=\"M14 4l0 4l-6 0l0 -4\" />";

    // photo -- skin/theme picker
    static const juce::String kPhoto = "<path d=\"M15 8h.01\" />"
                                        "<path d=\"M3 6a3 3 0 0 1 3 -3h12a3 3 0 0 1 3 3v12a3 3 0 0 1 -3 3h-12a3 3 0 0 1 -3 -3v-12z\" />"
                                        "<path d=\"M3 16l5 -5c.928 -.893 2.072 -.893 3 0l5 5\" />"
                                        "<path d=\"M14 14l1 -1c.928 -.893 2.072 -.893 3 0l3 3\" />";

    // power -- bypass toggle
    static const juce::String kPower = "<path d=\"M7 6a7.75 7.75 0 1 0 10 0\" />"
                                        "<path d=\"M12 4l0 8\" />";

    // headphones -- listen/audition toggle
    static const juce::String kHeadphones = "<path d=\"M4 15a2 2 0 0 1 2 -2h1a2 2 0 0 1 2 2v3a2 2 0 0 1 -2 2h-1a2 2 0 0 1 -2 -2l0 -3\" />"
                                             "<path d=\"M15 15a2 2 0 0 1 2 -2h1a2 2 0 0 1 2 2v3a2 2 0 0 1 -2 2h-1a2 2 0 0 1 -2 -2l0 -3\" />"
                                             "<path d=\"M4 15v-3a8 8 0 0 1 16 0v3\" />";

    // x -- delete/close
    static const juce::String kDelete = "<path d=\"M18 6l-12 12\" />"
                                         "<path d=\"M6 6l12 12\" />";

    // player-play / player-stop -- the preview transport
    static const juce::String kPlay = "<path d=\"M7 4v16l13 -8l-13 -8\" />";
    static const juce::String kStop = "<path d=\"M5 7a2 2 0 0 1 2 -2h10a2 2 0 0 1 2 2v10a2 2 0 0 1 -2 2h-10a2 2 0 0 1 -2 -2l0 -10\" />";
}
