#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Look.h"

/*
 * THE SYNTH TAB: sounds with no disk.
 *
 * Three oscillators and a drum kit, each knob a number in a recipe (S950/SynthPatch.h).
 * Nothing here makes a sound directly: a change re-renders the recipe to a disk, which
 * the plugin then plays like any other, and which the Program tab edits, the set keeps,
 * and Save disk as... writes for a real S950. That is why the envelopes, filter and LFO
 * are not on this page - they are on the disk, where the Program tab already edits them,
 * and they survive a re-render.
 *
 * Violet throughout, like Glide and Wide: none of this was on the S950.
 */
class SynthPage : public juce::Component,
                  private juce::Timer
{
public:
    explicit SynthPage (VirtualS950Processor&);
    ~SynthPage() override;

    void resized() override;

private:
    void timerCallback() override;

    /// The recipe into the controls, and the controls into the recipe (and off to render).
    void pull();
    void push();

    void chooseDrum (int slot);
    void relabel();

    VirtualS950Processor& processor;
    s950::synth::Recipe   recipe;
    juce::String          shownText;      // the recipe as last pulled, to notice it changing under us
    bool updating = false;

    // --- the top line
    juce::ComboBox presets;
    juce::Label    status, hint;

    // --- the oscillators
    struct Osc
    {
        look::Panel    panel;
        juce::ComboBox kind;
        juce::Slider   level, octave, fine, phase, intensity, sweep;
        juce::Label    levelL, octaveL, fineL, phaseL, intensityL, sweepL;

        Osc (const juce::String& title) : panel (title, look::extra, "a layer across the keyboard") {}
    };
    Osc osc[3] { Osc ("OSC 1"), Osc ("OSC 2"), Osc ("OSC 3") };

    // --- the drums
    look::Panel        drumPanel { "DRUMS", look::extra, "one-shots on General MIDI's notes" };
    juce::ToggleButton drumsOn { "Drums" };
    juce::TextButton   pads[static_cast<int> (s950::synth::DrumSlot::count)];
    juce::ToggleButton drumOn { "On" };
    juce::Slider       tune, decay, tone, level;
    juce::Label        tuneL, decayL, toneL, levelL;
    int selectedDrum = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPage)
};
