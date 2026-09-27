#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
 * How the window looks, in one place.
 *
 * Three ideas, borrowed from the plugins people find pleasant to look at:
 *
 *   - Every group of controls sits in a PANEL: a card with a small uppercase title, so the
 *     window reads as a handful of named things rather than forty knobs on a plain.
 *   - One knob, everywhere: a dark disc, a bright arc that shows the value, the value as
 *     plain text under it and the name under that. No edit boxes, no outlines.
 *   - Colour MEANS something. Amber is what is on the disk. Cyan is an offset laid on top of
 *     it while you play. Violet is one of the extras the S950 never had. The same three
 *     colours on the tabs, the panels, the knobs and the readouts, so a control's colour
 *     answers "which kind is this?" before its label is read - which is the question the two
 *     tabs were made to answer.
 */
namespace look
{
    // ------------------------------------------------------------------ the palette

    const juce::Colour window     { 0xff1a1d22 };
    const juce::Colour panel      { 0xff22262c };
    const juce::Colour panelEdge  { 0xff2e333b };
    const juce::Colour raised     { 0xff2b3038 };   // knob bodies, buttons, boxes
    const juce::Colour raisedEdge { 0xff3a404a };
    const juce::Colour track      { 0xff353b45 };   // the unlit part of a knob's arc

    const juce::Colour text       { 0xffd6dde8 };
    const juce::Colour dim        { 0xff8a93a3 };
    const juce::Colour faint      { 0xff5c6472 };

    const juce::Colour program    { 0xffe9a33a };   // amber: on the disk
    const juce::Colour perform    { 0xff45c8dc };   // cyan: an offset on top of it
    const juce::Colour extra      { 0xffa78bfa };   // violet: not on the S950
    const juce::Colour neutral    { 0xffc9d1dc };   // gain, and anything that is none of those

    // -------------------------------------------------------------------- the fonts

    juce::Font font (float size);
    juce::Font bold (float size);

    /// The name under a knob or beside a switch: small, dim, uppercase.
    void styleCaption (juce::Label&);

    /// A knob's colour, which its arc and pointer take.
    void accent (juce::Slider&, juce::Colour);

    /// A switch's or button's colour, which it lights up in.
    void accent (juce::Button&, juce::Colour);

    // -------------------------------------------------------------------- the panel

    /*
     * A titled card. Purely decoration: the controls it frames are siblings placed inside
     * content(), which is the area under the title in the PARENT's coordinates - so a layout
     * sets the panel's bounds and then lays its controls out inside content(), and nothing
     * has to be re-parented.
     */
    class Panel : public juce::Component
    {
    public:
        Panel (juce::String title, juce::Colour accent, juce::String note = {});

        juce::Rectangle<int> content() const;

        void setTitle (juce::String title);
        void setNote (juce::String note);

        void paint (juce::Graphics&) override;

        static constexpr int headerHeight = 24;
        static constexpr int pad = 8;

    private:
        juce::String title, note;
        juce::Colour accent;
    };

    // ------------------------------------------------------------- the look and feel

    class Mz950LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        Mz950LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;

        juce::Font getLabelFont (juce::Label&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int height) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                           juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;

        void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
        int  getTabButtonBestWidth (juce::TabBarButton&, int depth) override;
        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

        juce::Font getPopupMenuFont() override;
    };
}
