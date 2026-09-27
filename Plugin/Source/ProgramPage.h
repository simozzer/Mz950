#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

#include <functional>
#include <memory>
#include <vector>

/*
 * The programme's keygroups across the keyboard, one bar each, clicked to choose one.
 *
 * Overlapping keygroups - the crossfaded pianos, the layered ARP2600s - are stacked in lanes
 * rather than drawn over each other, so every one can be seen and reached. The axis is the
 * whole MIDI range: a keygroup's position on it is the answer to "which keys is this?", which
 * a list of numbers makes you work out.
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

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> barFor (int keygroup) const;
    int keygroupAt (juce::Point<float>) const;

    std::vector<Range> ranges;
    std::vector<int>   lane;
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
                    private juce::ChangeListener
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

    void showPage (int page);
    void choose (int keygroup);
    void write (Control& c, int value);
    void saveDisk();

    /// Which keygroup the controls show: the chosen one, or the first when "All" is on.
    int shown() const { return selected >= 0 ? selected : 0; }

    VirtualS950Processor& processor;

    std::vector<std::unique_ptr<Control>> controls;

    KeygroupStrip        strip;
    juce::TextButton     allButton  { "All keygroups" };
    juce::TextButton     saveButton { "Save disk as..." };
    juce::Label          heading, detail, blankNote, emptyNote;
    juce::TabbedButtonBar pages { juce::TabbedButtonBar::TabsAtTop };

    int  selected = 0;           // -1 is every keygroup at once
    int  page     = envelopes;
    int  count    = 0;
    bool updating = false;       // filling the controls must not look like turning them

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramPage)
};
