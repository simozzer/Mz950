#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ProgramPage.h"

#include <memory>
#include <vector>

/*
 * The plugin's window.
 *
 * Deliberately almost nothing: a gain slider, what is loaded, and how many voices are
 * sounding. The editor that matters is AkaiS950Studio, which is where disks are opened and
 * keygroups are edited; this is what a host needs to show while the instrument plays.
 *
 * The voice count is here because it answers the first question anyone asks of a plugin
 * that is not making a sound - is it getting the notes? - without a debugger.
 */
/*
 * An ADSR drawn as its own shape, with the corners dragged.
 *
 * Four numbers on four knobs is a poor way to see an envelope: the thing you are actually
 * adjusting is a shape, and a shape is what this shows. Drag the first corner for attack,
 * the second for decay and sustain together, the last for release.
 *
 * WHAT IT DRAWS, GIVEN THAT THE CONTROLS ARE OFFSETS
 *
 * The parameters behind this are trims - they shift every keygroup in the programme at
 * once - so there is no single envelope they describe. What is drawn is therefore the
 * RESULT for one representative keygroup: the programme's own values with the trim added.
 * Dragging moves the trim by however much that keygroup would have to move, and every other
 * keygroup goes with it. So the shape is honest about what you will hear from that keygroup
 * and approximate for the rest, which is the best a single picture can be.
 *
 * The horizontal axis is the panel's 0..99 for each stage, not seconds. The machine's
 * envelope times are exponential - a stored 80 is nearly two seconds where 40 is thirty
 * milliseconds - so drawing them to scale would leave every short envelope invisible in the
 * corner. This way each stage's corner moves evenly under the mouse, which is what a control
 * should do.
 */
class EnvelopeEditor : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    EnvelopeEditor (VirtualS950Processor&, bool filter);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    /*
     * Which corner is being pointed at. Three, not four.
     *
     * Sustain has no handle of its own: it is the HEIGHT of the decay corner, which drags
     * decay sideways and sustain up and down at the same time. That is one corner doing what
     * the shape says it does - where the decay ends is also where the sustain sits - and it
     * is the arrangement every ADSR display uses.
     *
     * It briefly had a fourth, on the plateau, because a decay of zero put the decay corner
     * exactly on top of the attack corner and left sustain unreachable. Minimum stage widths
     * fixed that properly, at which point the extra handle was a second way to one value and
     * one more thing on screen.
     */
    enum Corner { none = -1, attackCorner = 0, decayCorner = 1, releaseCorner = 2 };

    /// The stages a corner drags: horizontal, then vertical. Either may be null.
    struct Drags { const char* alongX; const char* upY; };
    Drags dragsFor (Corner c) const;

    /// The stage values as they will actually sound - the keygroup's own plus the trim.
    struct Shown { double attack, decay, sustain, release; };
    Shown shown() const;

    /// Where the corners sit, in this component.
    void cornerPoints (juce::Point<float>* into) const;

    Corner cornerAt (juce::Point<float> where) const;

    /// Move one stage so the representative keygroup lands on `value`, by trimming.
    void trimTo (const char* id, int keygroupValue, double value, bool gesture);

    juce::RangedAudioParameter* parameterFor (const char* stage) const;

    VirtualS950Processor& processor;
    const bool filter;

    VirtualS950Processor::EnvelopeShape base;    // the keygroup's own values
    Corner dragging = none, hovering = none;
    bool   gestureOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};

class VirtualS950Editor : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit VirtualS950Editor (VirtualS950Processor&);
    ~VirtualS950Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VirtualS950Processor& processor;

    /*
     * THE TWO TABS
     *
     * PROGRAM is what is on the disk: every keygroup setting, absolute, saved in the disk.
     * PERFORM is what you do on top of it: the offsets, glide, polyphony and wide - none of
     * which is ever written to the disk. They used to share one screen, looking alike, and
     * nothing said which kind a control was. Now the tab does.
     */
    juce::TabbedButtonBar mainTabs { juce::TabbedButtonBar::TabsAtTop };
    ProgramPage           programPage { processor };
    juce::Label           performHeading;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { showTab (mainTabs.getCurrentTabIndex()); }
    void showTab (int tab);

    /// Everything that belongs to the Perform tab, to be shown and hidden together.
    std::vector<juce::Component*> performParts();

    int seenEditRevision = -1;

    /// The processor generation this window last caught up with. See timerCallback.
    int seenGeneration = -1;

    void openDisk();

    /*
     * Put the loaded disk's programmes in the box.
     *
     * Called when the editor is built as well as when a disk is opened, and that is the
     * point of it being a method. A host destroys and recreates the editor every time the
     * plugin's window is closed and reopened, while the processor - and the disk it is
     * holding - carries on untouched. An editor that only filled this list when a disk was
     * chosen came back empty, so changing programme meant opening the same disk again.
     */
    void refreshPrograms();

    juce::Slider gain { juce::Slider::RotaryHorizontalVerticalDrag,
                        juce::Slider::TextBoxBelow };
    juce::Label  gainLabel;

    /*
     * The player's controls, which move every keygroup of the loaded programme together.
     *
     * All offsets from what the disk says, so all read zero until someone turns them, and
     * all are double-clickable back to it - which matters more here than on most controls,
     * because zero is not merely a default, it is "play what is on the floppy".
     *
     * Held as one array with their labels so that laying them out, building them and
     * attaching them are each one loop rather than nine near-identical lines. The row they
     * belong to and the controller that moves them live here too, because the tooltip says
     * both and nobody should have to look them up in the processor.
     */
    struct Knob
    {
        const char* id;
        const char* name;
        const char* group;        // which row it sits in
        int         cc;

        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::vector<Knob> knobs;

    /*
     * The two envelopes as shapes rather than as eight knobs.
     *
     * Cutoff and amount stay knobs: they are single values, not stages of anything, and a
     * knob is the right picture of a single value.
     */
    EnvelopeEditor vcaEnvelope { processor, false };
    EnvelopeEditor vcfEnvelope { processor, true  };

    juce::Label  vcaHeading, vcfHeading, sampleHeading, lfoHeading, velocityHeading;

    /*
     * Portamento, which the S950 never had.
     *
     * Not in `knobs`: those are all offsets from the disk, with tooltips and double-clicks
     * that say so, and neither of these is. The switch is a button because it is a switch.
     */
    juce::Label        glideHeading, glideTimeLabel;
    juce::ToggleButton glideButton { "On" };
    juce::Slider       glideTime { juce::Slider::RotaryHorizontalVerticalDrag,
                                   juce::Slider::TextBoxBelow };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> glideAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideTimeAttachment;

    /// How many voices, 1 (mono) to 8. In the glide group because mono is what it is for.
    juce::Slider voiceCount { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
    juce::Label  voiceCountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> voiceCountAttachment;

    /*
     * Wide, which the S950 never had: its own row, headed as an addition like glide's.
     */
    juce::Label        wideHeading, wideDetuneLabel, wideSpreadLabel;
    juce::ToggleButton wideButton   { "On" };
    juce::ToggleButton offsetButton { "Offset start" };
    juce::Slider       wideDetune { juce::Slider::RotaryHorizontalVerticalDrag,
                                    juce::Slider::TextBoxBelow };
    juce::Slider       wideSpread { juce::Slider::RotaryHorizontalVerticalDrag,
                                    juce::Slider::TextBoxBelow };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wideAttachment, offsetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wideDetuneAttachment,
                                                                          wideSpreadAttachment;
    juce::Label  patchLabel;
    juce::Label  voicesLabel;

    juce::TextButton loadButton { "Load disk..." };
    juce::ComboBox   programs;

    /// Held for as long as the dialog is open: launchAsync returns at once, and a chooser
    /// that goes out of scope takes its window with it.
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualS950Editor)
};
