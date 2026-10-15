#include "Glass.h"
#include "AudioFlowerLookAndFeel.h"

namespace
{
    // Separable box blur, in place, on packed ARGB pixels. Two passes per
    // axis approximate a Gaussian closely enough for glass.
    void boxBlur (std::vector<juce::PixelARGB>& px, int w, int h, int radius)
    {
        if (radius <= 0 || w <= 0 || h <= 0)
            return;

        std::vector<juce::PixelARGB> line ((size_t) juce::jmax (w, h));

        const auto pass = [&] (bool horizontal)
        {
            const int outer = horizontal ? h : w, inner = horizontal ? w : h;
            for (int o = 0; o < outer; ++o)
            {
                const auto at = [&] (int i) -> juce::PixelARGB& { return horizontal ? px[(size_t) (o * w + i)] : px[(size_t) (i * w + o)]; };

                int r = 0, gr = 0, b = 0, n = 0;
                for (int i = -radius; i <= radius; ++i)
                {
                    const auto& p = at (juce::jlimit (0, inner - 1, i));
                    r += p.getRed(); gr += p.getGreen(); b += p.getBlue(); ++n;
                }

                for (int i = 0; i < inner; ++i)
                {
                    line[(size_t) i].setARGB (255, (juce::uint8) (r / n), (juce::uint8) (gr / n), (juce::uint8) (b / n));
                    const auto& out = at (juce::jlimit (0, inner - 1, i - radius));
                    const auto& in  = at (juce::jlimit (0, inner - 1, i + radius + 1));
                    r += in.getRed() - out.getRed(); gr += in.getGreen() - out.getGreen(); b += in.getBlue() - out.getBlue();
                }

                for (int i = 0; i < inner; ++i)
                    at (i) = line[(size_t) i];
            }
        };

        for (int k = 0; k < 2; ++k)
        {
            pass (true);
            pass (false);
        }
    }

    juce::Path roundedShape (juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        return p;
    }
}

bool Glass::paint (juce::Graphics& g, juce::Component& component, juce::Rectangle<float> bounds,
                   juce::Colour surface, std::optional<juce::Colour> rimColour, float cornerRadius)
{
    // A stadium is the rounded rectangle whose radius is half its short side.
    const float radius = cornerRadius >= 0.0f ? juce::jmin (cornerRadius, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f)
                                              : juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    auto* source = component.findParentComponentOfClass<GlassBackdrop>();
    auto* sourceComponent = dynamic_cast<juce::Component*> (source);
    if (source == nullptr || sourceComponent == nullptr)
        return false;

    const auto& backdrop = source->getGlassBackdrop();
    if (! backdrop.isValid())
        return false;

    // The patch of the backdrop behind this surface, with a margin for the
    // blur and the lens to read from.
    const auto offset = sourceComponent->getLocalPoint (&component, juce::Point<int>());
    const int margin = kBlur * 2 + (int) std::ceil (kLens) + 2;
    const auto region = (bounds.getSmallestIntegerContainer() + offset).expanded (margin)
                            .getIntersection (backdrop.getBounds());
    if (region.isEmpty())
        return false;

    const int w = region.getWidth(), h = region.getHeight();

    std::vector<juce::PixelARGB> px ((size_t) (w * h));
    {
        const juce::Image::BitmapData in (backdrop, region.getX(), region.getY(), w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto c = in.getPixelColour (x, y);
                px[(size_t) (y * w + x)].setARGB (255, c.getRed(), c.getGreen(), c.getBlue());
            }
    }

    boxBlur (px, w, h, kBlur);

    // Lens: inside the shape, pixels near the rim read from further in, so
    // what's behind bends at the edge like a droplet's. The shape is the
    // core rectangle (the bounds shrunk by the radius) grown by the radius:
    // a stadium's core is a line, a panel's a rectangle.
    const auto shape = (bounds + offset.toFloat()) - region.getPosition().toFloat();
    const float rad = radius;
    const auto core = shape.reduced (rad);
    const float rimZone = juce::jmax (1.0f, juce::jmin (rad * 0.95f, 10.0f));

    juce::Image glass (juce::Image::ARGB, w, h, true);
    {
        juce::Image::BitmapData out (glass, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const juce::Point<float> p ((float) x + 0.5f, (float) y + 0.5f);
                const juce::Point<float> c (juce::jlimit (core.getX(), core.getRight(), p.x),
                                            juce::jlimit (core.getY(), core.getBottom(), p.y));
                const auto d = p - c;
                const float dist = std::sqrt (d.x * d.x + d.y * d.y);
                if (dist > rad + 1.0f)
                    continue;

                // Inside the core the nearest edge is a side, further than
                // the radius: no bend there.
                auto s = p;
                const float edge = rad - dist;
                if (edge < rimZone && dist > 0.0f)
                {
                    const float k = 1.0f - juce::jmax (0.0f, edge) / rimZone;
                    s -= d / dist * (kLens * k * k);
                }

                const int sx = juce::jlimit (0, w - 1, (int) s.x), sy = juce::jlimit (0, h - 1, (int) s.y);
                const auto& c2 = px[(size_t) (sy * w + sx)];
                out.setPixelColour (x, y, juce::Colour (c2.getRed(), c2.getGreen(), c2.getBlue()));
            }
    }

    const auto body = roundedShape (bounds, radius);

    g.saveState();
    g.reduceClipRegion (body);
    g.drawImageAt (glass, region.getX() - offset.x, region.getY() - offset.y);

    // A light tint of the surface shade, then a sheen: brightest along the
    // top, a faint return along the bottom. The sheen's warm white is an
    // effect colour (it brightens whatever is under it), not a palette token.
    g.setColour (surface.withAlpha (kTint));
    g.fillRect (bounds);

    const auto light = AudioFlowerLookAndFeel::ink().interpolatedWith (juce::Colours::white, 0.6f);
    juce::ColourGradient sheen (light.withAlpha (0.22f * kRim), 0.0f, bounds.getY(),
                                light.withAlpha (0.08f * kRim), 0.0f, bounds.getBottom(), false);
    sheen.addColour (0.35, light.withAlpha (0.0f));
    sheen.addColour (0.80, light.withAlpha (0.0f));
    g.setGradientFill (sheen);
    g.fillRect (bounds);
    g.restoreState();

    // The rim: a specular hairline, bright on top and fading down the
    // sides -- or a plain hairline in `rimColour` (the selected chip).
    const auto rimPath = roundedShape (bounds.reduced (0.5f), juce::jmax (0.0f, radius - 0.5f));
    if (rimColour.has_value())
    {
        g.setColour (*rimColour);
    }
    else
    {
        juce::ColourGradient rim (light.withAlpha (0.75f * kRim), 0.0f, bounds.getY(),
                                  light.withAlpha (0.35f * kRim), 0.0f, bounds.getBottom(), false);
        rim.addColour (0.5, AudioFlowerLookAndFeel::ink().withAlpha (0.18f * kRim));
        g.setGradientFill (rim);
    }
    g.strokePath (rimPath, juce::PathStrokeType (1.0f));
    return true;
}
