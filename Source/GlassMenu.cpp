#include "GlassMenu.h"
#include "AudioFlowerLookAndFeel.h"
#include "GuideOverlay.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    // The house popup metrics (AudioFlowerLookAndFeel), on glass.
    constexpr int   kRowH = 22, kSeparatorH = 6, kPadY = 4, kPadX = 4, kGap = 4;
    constexpr float kTextSize = 14.0f;
    constexpr float kCornerRadius = 8.0f;
    constexpr float kVeil = 0.42f;   // the ground over the glass, so rows read over busy notes
}

GlassMenu::GlassMenu()
{
    addAndMakeVisible (panel);
    setVisible (false);
}

GlassMenu* GlassMenu::find (juce::Component& from)
{
    // Up the parents; at each, look among its children for the menu layer.
    for (auto* c = &from; c != nullptr; c = c->getParentComponent())
        for (auto* child : c->getChildren())
            if (auto* menu = dynamic_cast<GlassMenu*> (child))
                return menu;

    return nullptr;
}

void GlassMenu::showBelow (juce::Component& anchorComponent, Items newItems, std::function<void (int)> onResult, bool alignRight)
{
    open (getLocalArea (&anchorComponent, anchorComponent.getLocalBounds()), true, alignRight, std::move (newItems), std::move (onResult));
}

void GlassMenu::showAt (juce::Component& from, juce::Point<int> point, Items newItems, std::function<void (int)> onResult)
{
    const auto here = getLocalPoint (&from, point);
    open ({ here.x, here.y, 0, 0 }, true, false, std::move (newItems), std::move (onResult));
}

void GlassMenu::open (juce::Rectangle<int> anchorHere, bool below, bool alignRight, Items newItems, std::function<void (int)> onResult)
{
    if (isVisible())
        finish (0);

    items = std::move (newItems);
    callback = std::move (onResult);
    anchor = anchorHere;
    placeBelow = below;
    alignToRight = alignRight;
    panel.hovered = -1;

    toFront (false);
    setVisible (true);
    resized();
    takeSnapshot();
    panel.repaint();
}

void GlassMenu::dismiss()
{
    if (isVisible())
        finish (0);
}

void GlassMenu::finish (int result)
{
    setVisible (false);
    backdrop = {};
    auto done = std::move (callback);
    callback = nullptr;
    if (done)
        done (result);
}

void GlassMenu::takeSnapshot()
{
    // What's under the layer, sharp: the glass blurs and bends it itself.
    // Every sibling but the overlays (this, and the guide if it is up).
    backdrop = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    juce::Graphics g (backdrop);
    g.fillAll (LnF::background());

    if (auto* parent = getParentComponent())
        for (auto* sibling : parent->getChildren())
            if (sibling != this && sibling->isVisible() && dynamic_cast<GuideOverlay*> (sibling) == nullptr)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.setOrigin (sibling->getPosition() - getPosition());
                sibling->paintEntireComponent (g, true);
            }
}

int GlassMenu::panelWidth() const
{
    // Text width plus the mark zones either side (the house menu's sizing),
    // the widest row deciding.
    int widest = 0;
    for (const auto& item : items)
        if (item.text.isNotEmpty())
            widest = juce::jmax (widest, juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), item.text)));

    return widest + 16 + LnF::kMenuMarkZone * 2 + kPadX * 2;
}

int GlassMenu::panelHeight() const
{
    int h = kPadY * 2;
    for (const auto& item : items)
        h += item.text.isEmpty() ? kSeparatorH : kRowH;
    return h;
}

juce::Rectangle<int> GlassMenu::rowBounds (int index) const
{
    int y = kPadY;
    for (int i = 0; i < index; ++i)
        y += items[(size_t) i].text.isEmpty() ? kSeparatorH : kRowH;

    return { kPadX, y, panel.getWidth() - kPadX * 2, items[(size_t) index].text.isEmpty() ? kSeparatorH : kRowH };
}

void GlassMenu::resized()
{
    if (items.empty())
        return;

    const int w = panelWidth(), h = panelHeight();
    int x = alignToRight ? anchor.getRight() - w : anchor.getX();
    int y = anchor.getBottom() + kGap;

    // Kept inside the plugin: flipped above the anchor if it would run off
    // the bottom, slid in from the sides.
    if (y + h > getHeight() - kGap)
        y = juce::jmax (kGap, anchor.getY() - kGap - h);
    x = juce::jlimit (kGap, juce::jmax (kGap, getWidth() - kGap - w), x);

    panel.setBounds (x, y, w, h);
}

void GlassMenu::mouseDown (const juce::MouseEvent& e)
{
    // A click anywhere outside the menu closes it, and goes no further.
    if (! panel.getBounds().contains (e.getPosition()))
        finish (0);
}

//==============================================================================
int GlassMenu::Panel::rowAt (int y) const
{
    for (int i = 0; i < (int) owner.items.size(); ++i)
        if (owner.rowBounds (i).getVerticalRange().contains (y))
            return owner.items[(size_t) i].text.isEmpty() || ! owner.items[(size_t) i].enabled ? -1 : i;
    return -1;
}

void GlassMenu::Panel::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row != hovered) { hovered = row; repaint(); }
}

void GlassMenu::Panel::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0) { hovered = -1; repaint(); }
}

void GlassMenu::Panel::mouseUp (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row >= 0)
        owner.finish (owner.items[(size_t) row].id);
}

void GlassMenu::Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float height = (float) juce::jmax (1, owner.getHeight());
    const auto surface = LnF::surfaceAt (LnF::background(), (float) getBounds().getCentreY() / height);

    if (! Glass::paint (g, *this, r, surface, std::nullopt, kCornerRadius))
    {
        g.setColour (LnF::background());
        g.fillRoundedRectangle (r, kCornerRadius);
    }
    g.setColour (LnF::background().withAlpha (kVeil));
    g.fillRoundedRectangle (r.reduced (1.0f), kCornerRadius - 1.0f);

    g.setFont (LnF::uiFont (kTextSize));

    for (int i = 0; i < (int) owner.items.size(); ++i)
    {
        const auto& item = owner.items[(size_t) i];
        const auto row = owner.rowBounds (i);

        if (item.text.isEmpty())
        {
            g.setColour (LnF::ink().withAlpha (0.16f));
            g.fillRect (row.reduced (6, 0).withSizeKeepingCentre (row.getWidth() - 12, 1));
            continue;
        }

        // The house highlight: solid ink, ground text.
        const bool hot = i == hovered;
        if (hot)
        {
            g.setColour (LnF::ink());
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
        }

        g.setColour (hot ? LnF::background() : LnF::ink().withAlpha (item.enabled ? 0.9f : 0.4f));
        g.drawText (item.text, row, juce::Justification::centred, false);

        if (item.ticked)
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xE2\x9C\x93")),
                        row.withWidth (LnF::kMenuMarkZone + 4), juce::Justification::centred, false);
    }
}
