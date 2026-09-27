#include "Look.h"

namespace look
{
    // -------------------------------------------------------------------- the fonts

    namespace
    {
        // Segoe UI is on every Windows since Vista; elsewhere JUCE falls back to its own
        // sans, which is close enough that nothing has to be laid out twice.
        const char* const face = "Segoe UI";
    }

    juce::Font font (float size) { return juce::Font (juce::FontOptions (face, size, juce::Font::plain)); }
    juce::Font bold (float size) { return juce::Font (juce::FontOptions (face, size, juce::Font::bold)); }

    void styleCaption (juce::Label& l)
    {
        l.setFont (font (10.5f));
        l.setColour (juce::Label::textColourId, dim);
        l.setJustificationType (juce::Justification::centred);
        l.setText (l.getText().toUpperCase(), juce::dontSendNotification);
    }

    void accent (juce::Slider& s, juce::Colour c)
    {
        s.setColour (juce::Slider::rotarySliderFillColourId, c);
        s.setColour (juce::Slider::thumbColourId, c);
    }

    void accent (juce::Button& b, juce::Colour c)
    {
        b.setColour (juce::ToggleButton::tickColourId, c);
        b.setColour (juce::TextButton::buttonOnColourId, c);
    }

    // -------------------------------------------------------------------- the panel

    Panel::Panel (juce::String t, juce::Colour c, juce::String n)
        : title (std::move (t)), note (std::move (n)), accent (c)
    {
        // Decoration only: the controls in front of it get the mouse.
        setInterceptsMouseClicks (false, false);
    }

    juce::Rectangle<int> Panel::content() const
    {
        return getBounds().withTrimmedTop (headerHeight).reduced (pad, 4);
    }

    void Panel::setTitle (juce::String t) { title = std::move (t); repaint(); }
    void Panel::setNote  (juce::String n) { note  = std::move (n); repaint(); }

    void Panel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        if (title.isEmpty() && note.isEmpty())
            return;

        auto head = getLocalBounds().removeFromTop (headerHeight).reduced (pad + 2, 0);

        // The title, and a short rule under it in the panel's colour: the one mark that says
        // which kind of thing the panel holds, before a word of it is read.
        g.setFont (bold (11.0f));
        g.setColour (accent);
        g.drawText (title, head, juce::Justification::centredLeft, false);

        const int wide = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title);
        g.setColour (accent.withAlpha (0.55f));
        g.fillRect (head.getX(), head.getBottom() - 3, juce::jmax (wide, 24), 2);

        // The note only where it fits beside the title: a narrow panel keeps its name and
        // drops the aside, rather than printing the two through each other.
        if (note.isNotEmpty())
        {
            g.setFont (font (10.0f));
            const int noteWide = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), note);

            if (wide + 16 + noteWide <= head.getWidth())
            {
                g.setColour (dim);
                g.drawText (note, head, juce::Justification::centredRight, false);
            }
        }
    }

    // ------------------------------------------------------------- the look and feel

    Mz950LookAndFeel::Mz950LookAndFeel()
    {
        setDefaultSansSerifTypefaceName (face);

        using namespace juce;

        setColour (ResizableWindow::backgroundColourId, window);
        setColour (DocumentWindow::backgroundColourId,  window);

        setColour (Label::textColourId, text);

        // A knob's value is plain text under it - no box, no outline.
        setColour (Slider::rotarySliderFillColourId,    perform);
        setColour (Slider::rotarySliderOutlineColourId, track);
        setColour (Slider::thumbColourId,               text);
        setColour (Slider::textBoxTextColourId,         text);
        setColour (Slider::textBoxBackgroundColourId,   Colours::transparentBlack);
        setColour (Slider::textBoxOutlineColourId,      Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId,    perform.withAlpha (0.4f));

        setColour (TextEditor::backgroundColourId,      raised);
        setColour (TextEditor::textColourId,            text);
        setColour (TextEditor::highlightColourId,       perform.withAlpha (0.4f));
        setColour (TextEditor::outlineColourId,         raisedEdge);
        setColour (TextEditor::focusedOutlineColourId,  perform);

        setColour (TextButton::buttonColourId,   raised);
        setColour (TextButton::buttonOnColourId, perform);
        setColour (TextButton::textColourOffId,  text);
        setColour (TextButton::textColourOnId,   window);

        setColour (ToggleButton::textColourId,         text);
        setColour (ToggleButton::tickColourId,         perform);
        setColour (ToggleButton::tickDisabledColourId, faint);

        setColour (ComboBox::backgroundColourId, raised);
        setColour (ComboBox::outlineColourId,    raisedEdge);
        setColour (ComboBox::textColourId,       text);
        setColour (ComboBox::arrowColourId,      dim);
        setColour (ComboBox::buttonColourId,     raised);
        setColour (ComboBox::focusedOutlineColourId, perform);

        setColour (PopupMenu::backgroundColourId,            raised);
        setColour (PopupMenu::textColourId,                  text);
        setColour (PopupMenu::highlightedBackgroundColourId, perform.withAlpha (0.25f));
        setColour (PopupMenu::highlightedTextColourId,       text);

        setColour (TabbedButtonBar::tabTextColourId,      dim);
        setColour (TabbedButtonBar::frontTextColourId,    text);
        setColour (TabbedButtonBar::tabOutlineColourId,   Colours::transparentBlack);
        setColour (TabbedButtonBar::frontOutlineColourId, Colours::transparentBlack);

        setColour (TooltipWindow::backgroundColourId, raised);
        setColour (TooltipWindow::textColourId,       text);
        setColour (TooltipWindow::outlineColourId,    raisedEdge);

        setColour (ScrollBar::thumbColourId, raisedEdge);

        // The in-window file browser, so opening a disk does not drop into JUCE's default
        // grey in the middle of a dark window.
        setColour (ListBox::backgroundColourId, panel);
        setColour (ListBox::outlineColourId,    panelEdge);
        setColour (DirectoryContentsDisplayComponent::textColourId,            text);
        setColour (DirectoryContentsDisplayComponent::highlightColourId,       perform.withAlpha (0.25f));
        setColour (DirectoryContentsDisplayComponent::highlightedTextColourId, text);
        setColour (FileBrowserComponent::currentPathBoxBackgroundColourId, raised);
        setColour (FileBrowserComponent::currentPathBoxTextColourId,       text);
        setColour (FileBrowserComponent::currentPathBoxArrowColourId,      dim);
        setColour (FileBrowserComponent::filenameBoxBackgroundColourId,    raised);
        setColour (FileBrowserComponent::filenameBoxTextColourId,          text);
        setColour (FileChooserDialogBox::titleTextColourId,                text);
        setColour (AlertWindow::backgroundColourId, panel);
        setColour (AlertWindow::textColourId,       text);
        setColour (AlertWindow::outlineColourId,    panelEdge);
    }

    /*
     * The knob.
     *
     * A dark disc with a thin arc round it. The arc lights from its start to the value in the
     * knob's colour - or, for an OFFSET, from the top: an offset knob sits at zero in the
     * middle, and an arc that grew from the left would show a full half-turn of "something"
     * for a control that is doing nothing at all. A short pointer on the disc says the same
     * thing a second way, for anyone who reads knobs by their pointer.
     */
    void Mz950LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                             float pos, float startAngle, float endAngle,
                                             juce::Slider& s)
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f - 3.0f;
        const auto  centre = bounds.getCentre();

        if (radius < 6.0f) return;

        const auto  fill    = s.findColour (juce::Slider::rotarySliderFillColourId);
        const auto  outline = s.findColour (juce::Slider::rotarySliderOutlineColourId);
        const float angle   = startAngle + pos * (endAngle - startAngle);
        const bool  bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float arcW    = juce::jlimit (2.0f, 3.5f, radius * 0.11f);

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (outline);
        g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        const float from = bipolar ? (startAngle + endAngle) / 2.0f : startAngle;

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path lit;
            lit.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (s.isEnabled() ? fill : fill.withAlpha (0.35f));
            g.strokePath (lit, juce::PathStrokeType (arcW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // A bipolar knob keeps a tick at its rest position, so zero is a place, not a guess.
        if (bipolar)
        {
            const float tickR = radius + arcW * 0.5f + 2.5f;
            const auto  tip   = centre.getPointOnCircumference (tickR, from);
            g.setColour (dim);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
        }

        const float bodyR = radius - arcW - 3.0f;
        g.setColour (raised);
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre));
        g.setColour (raisedEdge);
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre), 1.0f);

        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (bodyR * 0.45f, angle));
        pointer.lineTo (centre.getPointOnCircumference (bodyR * 0.88f, angle));
        g.setColour (s.isEnabled() ? text : dim);
        g.strokePath (pointer, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    juce::Font Mz950LookAndFeel::getLabelFont (juce::Label& l)
    {
        // A knob's value, under the knob: small and plain.
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
            return font (11.5f);

        // Everything else keeps the size it was given, capped at the house size - JUCE's
        // default label is 15 points, which is a headline here - in the house face.
        const auto given = l.getFont();
        return juce::Font (juce::FontOptions (face, juce::jmin (given.getHeight(), 12.5f),
                                              given.getStyleFlags()));
    }

    // ------------------------------------------------------------------ the buttons

    void Mz950LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour&, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const auto lit = b.findColour (juce::TextButton::buttonOnColourId);

        juce::Colour fill = on ? lit.withAlpha (0.22f) : raised;
        if (down)             fill = fill.brighter (0.15f);
        else if (highlighted) fill = fill.brighter (0.07f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? lit.withAlpha (0.8f) : raisedEdge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font Mz950LookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return font (12.5f);
    }

    /*
     * A switch is a pill with a thumb, lit in the control's colour when it is on. The text
     * beside it is the switch's name, so "On" reads as a state and not as a button to press.
     */
    void Mz950LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool highlighted, bool)
    {
        const bool on  = b.getToggleState();
        const auto lit = b.findColour (juce::ToggleButton::tickColourId);

        auto r = b.getLocalBounds();
        const int pillW = 30, pillH = 16;
        auto pill = juce::Rectangle<float> ((float) r.getX(), (float) r.getCentreY() - pillH / 2.0f,
                                            (float) pillW, (float) pillH);

        juce::Colour fill = on ? lit : raisedEdge;
        if (highlighted) fill = fill.brighter (0.1f);

        g.setColour (fill.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (pill, pillH / 2.0f);

        const float thumbR = pillH / 2.0f - 2.5f;
        const float thumbX = on ? pill.getRight() - 2.5f - thumbR : pill.getX() + 2.5f + thumbR;
        g.setColour (on ? window : dim);
        g.fillEllipse (juce::Rectangle<float> (thumbR * 2, thumbR * 2)
                           .withCentre ({ thumbX, pill.getCentreY() }));

        g.setColour (b.isEnabled() ? text : dim);
        g.setFont (font (12.0f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (pillW + 8),
                    juce::Justification::centredLeft, false);
    }

    // ---------------------------------------------------------------- the combo box

    void Mz950LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int,
                                         juce::ComboBox& c)
    {
        auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);

        g.setColour (c.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (c.hasKeyboardFocus (true) ? c.findColour (juce::ComboBox::focusedOutlineColourId)
                                               : c.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        // A small chevron, not an arrow in a box.
        auto arrow = juce::Rectangle<float> ((float) w - 22.0f, 0.0f, 14.0f, (float) h).reduced (3.0f, 0.0f);
        juce::Path p;
        const float cx = arrow.getCentreX(), cy = arrow.getCentreY();
        p.startNewSubPath (cx - 4.0f, cy - 2.0f);
        p.lineTo (cx, cy + 2.5f);
        p.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (c.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    juce::Font Mz950LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.0f); }
    juce::Font Mz950LookAndFeel::getPopupMenuFont()                 { return font (13.0f); }

    // --------------------------------------------------------------------- the tabs

    /*
     * A tab is its name, and a rule under it when it is in front, in the tab's own colour -
     * which for the two main tabs is amber for PROGRAM and cyan for PERFORM, the same two
     * colours the controls on each of them wear.
     */
    void Mz950LookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
    {
        const auto area  = b.getActiveArea();
        const bool front = b.isFrontTab();
        const auto tint  = b.getTabBackgroundColour();
        const auto lit   = tint.isTransparent() ? perform : tint;

        g.setFont (bold (12.0f));
        g.setColour (front ? text : (over ? text.withAlpha (0.8f) : dim));
        g.drawText (b.getButtonText().toUpperCase(), area, juce::Justification::centred, false);

        if (front)
        {
            g.setColour (lit);
            g.fillRect (area.getX() + 6, area.getBottom() - 3, area.getWidth() - 12, 2);
        }
    }

    int Mz950LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
    {
        return juce::GlyphArrangement::getStringWidthInt (bold (12.0f), b.getButtonText().toUpperCase()) + 30;
    }

    void Mz950LookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
    {
        g.setColour (panelEdge);
        g.fillRect (0, h - 1, w, 1);
    }
}
