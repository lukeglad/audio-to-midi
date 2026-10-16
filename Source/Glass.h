#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Implemented by the component the glass surfaces float over (the canvas):
    an image of what it draws, without its children, in its own coordinates.
    Refreshed by the source whenever it repaints.
*/
class GlassBackdrop
{
public:
    virtual ~GlassBackdrop() = default;
    virtual const juce::Image& getGlassBackdrop() = 0;
};

//==============================================================================
/**
    Liquid glass for the floating surfaces: the layer chips, the mute / solo
    strip, the mix and grid pills, the quantize fader, the play and record
    pills, the action chip, the menus and the quick guide. The evolution of
    EQ's frost recipe.

    What's behind is blurred lightly, bent near the rim like the edge of a
    droplet, shown through a light tint of the surface shade, and finished
    with a specular rim that is brightest along the top. Surfaces in an "on"
    state (a lit chip, an engaged toggle) keep their solid ink fill instead,
    so state never depends on reading through glass.
*/
namespace Glass
{
    // The mockup's defaults, in design-space px.
    inline constexpr float kLens = 5.0f;     // how far content shifts at the very rim
    inline constexpr int   kBlur = 3;        // box-blur radius of what's behind
    inline constexpr float kTint = 0.18f;    // surface shade over the glass
    inline constexpr float kRim  = 0.55f;    // strength of the specular rim and sheen

    /**
        Paints a glass stadium filling `bounds` (in `component`'s coordinates).
        `surface` is the surfaceAt() shade at the component's height.
        `rimColour`, if set, replaces the specular rim with a plain hairline
        in that colour (the selected chip's copper). `cornerRadius` makes it a
        rounded rectangle instead of a stadium (the guide panel); negative
        means a stadium.

        @return false when no backdrop could be found; the caller then draws
                its flat fill as before.
    */
    bool paint (juce::Graphics&, juce::Component& component, juce::Rectangle<float> bounds,
                juce::Colour surface, std::optional<juce::Colour> rimColour = std::nullopt,
                float cornerRadius = -1.0f);
}
