#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Look.h"

#include <functional>
#include <memory>
#include <vector>

/*
 * The programme's keygroups over a piano keyboard, one bar each, clicked to choose one.
 *
 * The bars sit in lanes above the keys, each spanning exactly the keys it covers - the
 * overlapping keygroups of the crossfaded pianos and layered ARP2600s stacked one above the
 * other rather than drawn over each other, so every one can be seen and reached. Below them
 * the keyboard itself, C0 to G8: the selected keygroup's keys shaded in the Program colour,
 * the keys of the others faintly, keys nobody plays left bare - so "which keys is this?" is
 * answered on the keys. A key held over MIDI carries a blue dot. Clicking a key chooses the
 * keygroup that plays it.
 */
class KeygroupStrip : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    struct Range { int low, high; juce::String sample; };

    /// A keygroup was clicked: its index.
    std::function<void (int)> onSelect;

    void setKeygroups (std::vector<Range> ranges);

    /// The one to highlight, or -1 for all of them.
    void setSelected (int keygroup);

    /*
     * Which keygroups are playing right now, and the key each last answered, so the strip
     * lights up as notes arrive and says which keygroup took each one. A lit keygroup gets
     * brighter and gains a light outline and a marker at the key - never a change of hue,
     * so it reads the same whatever someone's colour vision.
     */
    void setActivity (std::vector<bool> lit, std::vector<int> notes);

    /// The keys held over MIDI, 128 of them - each drawn with a dot.
    void setHeld (std::vector<bool> held);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int firstKey = 24, lastKey = 127;       // C0 to G8

private:
    // the areas: bars along the top, the keyboard under them, the octave names at the bottom
    juce::Rectangle<float> barsArea() const;
    juce::Rectangle<float> keysArea() const;

    // the piano's geometry - white keys all the same width, black keys over their joins
    static bool isBlack (int note);
    int   whiteCount() const;
    float whiteWidth() const;
    juce::Rectangle<float> keyRect (int note) const;          // within keysArea
    float keyLeft (int note) const;                            // the key's span along the axis
    float keyRight (int note) const;

    juce::Rectangle<float> barFor (int keygroup) const;
    int keygroupAt (juce::Point<float>) const;
    int keyAt (juce::Point<float>) const;

    std::vector<Range> ranges;
    std::vector<int>   lane;
    std::vector<bool>  lit;
    std::vector<int>   notes;
    std::vector<bool>  held = std::vector<bool> (128, false);
    int lanes = 1;
    int selected = -1;
};

/*
 * THE PROGRAM TAB: what is on the disk.
 *
 * Every control here is an ABSOLUTE setting of one keygroup, in the panel's own units and
 * ranges, read from and written to the disk image - so it reads like the machine's front
 * panel, is saved with the project, and goes out with the disk when it is saved as a file.
 * The Perform tab's controls, by contrast, are offsets on top of this and never touch the
 * disk. Where one of those offsets is moving a value shown here, the control says what is
 * actually sounding underneath it: "-> 52".
 *
 * Six small pages rather than one crowded one, in the order a sound is usually built:
 * envelopes, filter, LFO, velocity, tuning, and the keys and output a keygroup covers.
 */
class ProgramPage : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ProgramPage (VirtualS950Processor&);
    ~ProgramPage() override;

    /// Everything re-read from the processor: a new disk, programme, or edit.
    void refresh();

    /// Only the "-> sounding" readouts, against the offsets as they are now. Cheap.
    void refreshSounding();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Page { envelopes, filter, lfo, velocity, tuning, keys, pageCount };

    struct Control
    {
        s950::KeygroupParam param;
        int         page;
        const char* name;
        const char* trimId;      // the Perform offset that moves it, or nullptr
        bool        zone2;       // only means anything if the keygroup has a second zone

        std::unique_ptr<juce::Slider>       slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox>     combo;
        std::unique_ptr<juce::Label>        label, sounding;

        int  value  = 0;         // as last read, for the sounding readout
        bool varies = false;     // "All" shown, and the keygroups disagree
    };

    void add (s950::KeygroupParam p, int page, const char* name,
              const char* trimId = nullptr, bool zone2 = false);

    /// The sub-tab bar changed page.
    void changeListenerCallback (juce::ChangeBroadcaster*) override { showPage (pages.getCurrentTabIndex()); }

    /*
     * Lighting the strip as notes play. Its own timer, faster than the editor's: a
     * keygroup that flashes 100 ms late has already stopped being useful.
     *
     * A keygroup stays lit while a voice plays it, and for a moment after its hit count
     * moved - so a hit too short to be caught sounding still shows.
     */
    void timerCallback() override;
    std::vector<unsigned> seenHits;
    std::vector<double>   flashUntil;    // milliseconds, on Time::getMillisecondCounterHiRes

    void showPage (int page);
    void choose (int keygroup);
    void write (Control& c, int value);
    void saveDisk();

    /// Which keygroup the controls show: the chosen one, or the first when "All" is on.
    int shown() const { return selected >= 0 ? selected : 0; }

    VirtualS950Processor& processor;

    std::vector<std::unique_ptr<Control>> controls;

    /// Amber throughout: everything on this page is what is on the disk. See Look.h.
    look::Panel keygroupPanel { "KEYGROUPS", look::program, "click one to edit it" };
    look::Panel pagePanel     { "", look::program };

    KeygroupStrip        strip;
    juce::TextButton     allButton  { "All keygroups" };
    juce::TextButton     saveButton { "Save disk as..." };
    juce::Label          heading, detail, blankNote, emptyNote;
    juce::TabbedButtonBar pages { juce::TabbedButtonBar::TabsAtTop };

    int  selected = 0;           // -1 is every keygroup at once
    int  page     = envelopes;
    int  count    = 0;
    bool updating = false;       // filling the controls must not look like turning them


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramPage)
};
