#include "SynthPage.h"

using namespace s950::synth;

namespace
{
    void knob (juce::Slider& s, juce::Label& l, const char* name, double lo, double hi, double start,
               const juce::String& tip, std::function<juce::String (double)> text = nullptr)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        s.setRange (lo, hi, 1.0);
        s.setValue (start, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, start);
        s.setTooltip (tip);
        s.setTitle (name);
        if (text != nullptr)
        {
            s.textFromValueFunction = [text] (double v) { return text (v); };
            s.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
        }
        look::accent (s, look::extra);

        l.setText (name, juce::dontSendNotification);
        l.setTooltip (tip);
        look::styleCaption (l);
    }

    juce::String signedText (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)); }
}

SynthPage::SynthPage (VirtualS950Processor& p) : processor (p)
{
    for (auto& o : osc) addAndMakeVisible (o.panel);
    addAndMakeVisible (drumPanel);

    // --- the top line
    presets.setTextWhenNothingSelected ("Start from a preset...");
    presets.setTooltip ("A named starting point for the oscillators. It replaces the three oscillators "
                        "and brings its own envelopes and filter to the disk for the Program tab. The "
                        "drums, if they are on, stay exactly as they are.");
    int id = 1;
    for (const auto& pr : s950::synth::presets()) presets.addItem (pr.name, id++);
    presets.onChange = [this]
    {
        const int chosen = presets.getSelectedId() - 1;
        if (chosen < 0) return;

        auto next = s950::synth::presets()[(std::size_t) chosen].recipe;

        // A preset is a sound for the keyboard; the kit is the kit. Drums that are on stay
        // on, with every setting they had, whichever preset the keyboard goes to.
        if (recipe.drumsOn)
        {
            next.drumsOn = true;
            for (int i = 0; i < (int) DrumSlot::count; ++i) next.drums[i] = recipe.drums[i];
        }

        recipe = next;
        presets.setSelectedId (0, juce::dontSendNotification);
        pull();
        push();
    };
    addAndMakeVisible (presets);

    status.setFont (look::font (11.5f));
    status.setColour (juce::Label::textColourId, look::dim);
    status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (status);

    hint.setText ("Each oscillator is drawn as a looped sample and becomes a keygroup; the drums are "
                  "one-shots. Envelopes, filter and LFO live on the Program tab and survive a re-render.",
                  juce::dontSendNotification);
    hint.setFont (look::font (11.0f));
    hint.setColour (juce::Label::textColourId, look::dim);
    addAndMakeVisible (hint);

    // --- the oscillators
    for (int i = 0; i < 3; ++i)
    {
        auto& o = osc[i];

        id = 1;
        for (int k = 0; k < (int) OscKind::count; ++k)
            o.kind.addItem (oscKindName ((OscKind) k), id++);
        o.kind.setTooltip ("What this layer is drawn from. The plain shapes are summed from their "
                           "harmonics; FM, ring and the bends are analysed into harmonics - so nothing "
                           "here aliases, at any key.");
        o.kind.onChange = [this] { relabel(); push(); };
        addAndMakeVisible (o.kind);

        knob (o.level,  o.levelL,  "Level",  0, 99, 99,  "How loud this layer is, as the zone's loudness trim.");
        knob (o.octave, o.octaveL, "Octave", -2, 2, 0,   "Transpose, in octaves.", signedText);
        knob (o.fine,   o.fineL,   "Fine",   -50, 50, 0, "Detune, in cents. Two layers a few cents apart is the classic wide sound.",
              [] (double v) { return signedText (v) + " ct"; });
        knob (o.phase,  o.phaseL,  "Phase",  0, 99, 0,   "Where in its cycle the wave starts. Layers that start apart do not stack "
                                                          "into one spike at the attack.");
        knob (o.intensity, o.intensityL, "Shape", 0, 99, 50, "The wave's own control: pulse width, FM index, ring amount, bend, "
                                                              "skew - or, for a plain shape, how far it softens toward a sine.");
        knob (o.sweep,  o.sweepL,  "Sweep",  0, 99, 0,   "How much of that travels across the loop and back: pulse width "
                                                          "modulation, an FM sweep, a filter-like morph - baked into the sample, "
                                                          "the way a sampler has always done it. Higher notes sweep faster.");

        for (auto* s : { &o.level, &o.octave, &o.fine, &o.phase, &o.intensity, &o.sweep })
        {
            s->onValueChange = [this] { push(); };
            addAndMakeVisible (*s);
        }
        for (auto* l : { &o.levelL, &o.octaveL, &o.fineL, &o.phaseL, &o.intensityL, &o.sweepL })
            addAndMakeVisible (*l);
    }

    // --- the drums
    drumsOn.setTooltip ("Put the kit on the disk. The drums take C1 to D#2 on General MIDI's notes - kick 36, "
                        "snare 38, clap 39, hats 42 and 46, ride 51, toms 41, 43 and 45 - and the oscillators "
                        "move up to start at E2, the first key above them.");
    drumsOn.onClick = [this] { push(); };
    look::accent (drumsOn, look::extra);
    addAndMakeVisible (drumsOn);

    for (int i = 0; i < (int) DrumSlot::count; ++i)
    {
        pads[i].setButtonText (drumSlotName ((DrumSlot) i));
        pads[i].setClickingTogglesState (false);
        pads[i].setTooltip ("Choose this drum to shape it. Its own switch is beside the knobs.");
        pads[i].onClick = [this, i] { chooseDrum (i); };
        look::accent (pads[i], look::extra);
        addAndMakeVisible (pads[i]);
    }

    drumOn.setTooltip ("Whether this drum is on the disk at all.");
    drumOn.onClick = [this] { push(); };
    look::accent (drumOn, look::extra);
    addAndMakeVisible (drumOn);

    knob (tune,  tuneL,  "Tune",  -12, 12, 0, "Semitones. A one-shot is tuned by changing its rate, so it gets shorter as it goes up.", signedText);
    knob (decay, decayL, "Decay", 0, 99, 99,  "The keygroup's VCA decay. 99 plays the whole drum; lower cuts it shorter.");
    knob (tone,  toneL,  "Tone",  0, 99, 99,  "The zone's filter. 99 is wide open.");
    knob (level, levelL, "Level", -50, 50, 0, "On top of the kit's own balance.", signedText);

    for (auto* s : { &tune, &decay, &tone, &level })
    {
        s->onValueChange = [this] { push(); };
        addAndMakeVisible (*s);
    }
    for (auto* l : { &tuneL, &decayL, &toneL, &levelL })
        addAndMakeVisible (*l);

    recipe = processor.getRecipe();
    pull();
    startTimerHz (10);
}

SynthPage::~SynthPage()
{
    stopTimer();
}

void SynthPage::chooseDrum (int slot)
{
    selectedDrum = juce::jlimit (0, (int) DrumSlot::count - 1, slot);
    pull();
}

/// The two shaping knobs mean different things per kind; say which, and dim the ones a
/// kind has no use for.
void SynthPage::relabel()
{
    for (int i = 0; i < 3; ++i)
    {
        auto& o = osc[i];
        const auto kind   = (OscKind) juce::jmax (0, o.kind.getSelectedId() - 1);
        const auto labels = oscKindLabels (kind);
        const bool on     = kind != OscKind::off;

        o.intensityL.setText (juce::String (labels.hasIntensity ? labels.intensity : "Shape").toUpperCase(), juce::dontSendNotification);
        o.sweepL.setText     (juce::String (labels.hasSweep     ? labels.sweep     : "Sweep").toUpperCase(), juce::dontSendNotification);

        o.intensity.setEnabled (on && labels.hasIntensity);
        o.sweep.setEnabled     (on && labels.hasSweep);
        for (auto* s : { &o.level, &o.octave, &o.fine, &o.phase }) s->setEnabled (on);
    }
}

void SynthPage::pull()
{
    const juce::ScopedValueSetter<bool> filling (updating, true);

    for (int i = 0; i < 3; ++i)
    {
        auto& o = osc[i];
        const auto& r = recipe.osc[i];
        o.kind.setSelectedId ((int) r.kind + 1, juce::dontSendNotification);
        o.level.setValue (r.level, juce::dontSendNotification);
        o.octave.setValue (r.octave, juce::dontSendNotification);
        o.fine.setValue (r.fine, juce::dontSendNotification);
        o.phase.setValue (r.phase, juce::dontSendNotification);
        o.intensity.setValue (r.intensity, juce::dontSendNotification);
        o.sweep.setValue (r.sweep, juce::dontSendNotification);
        for (auto* s : { &o.level, &o.octave, &o.fine, &o.phase, &o.intensity, &o.sweep }) s->updateText();
    }
    relabel();

    drumsOn.setToggleState (recipe.drumsOn, juce::dontSendNotification);

    for (int i = 0; i < (int) DrumSlot::count; ++i)
    {
        pads[i].setToggleState (i == selectedDrum, juce::dontSendNotification);
        pads[i].setColour (juce::TextButton::textColourOffId, recipe.drums[i].on ? look::text : look::faint);
        pads[i].setEnabled (recipe.drumsOn);
    }

    const auto& ds = recipe.drums[selectedDrum];
    drumOn.setToggleState (ds.on, juce::dontSendNotification);
    tune.setValue (ds.tune, juce::dontSendNotification);
    decay.setValue (ds.decay, juce::dontSendNotification);
    tone.setValue (ds.tone, juce::dontSendNotification);
    level.setValue (ds.level, juce::dontSendNotification);
    for (auto* s : { &tune, &decay, &tone, &level }) { s->updateText(); s->setEnabled (recipe.drumsOn && ds.on); }
    drumOn.setEnabled (recipe.drumsOn);

    shownText = juce::String (toText (recipe));
}

void SynthPage::push()
{
    if (updating) return;

    for (int i = 0; i < 3; ++i)
    {
        auto& o = osc[i];
        auto& r = recipe.osc[i];
        r.kind      = (OscKind) juce::jmax (0, o.kind.getSelectedId() - 1);
        r.level     = juce::roundToInt (o.level.getValue());
        r.octave    = juce::roundToInt (o.octave.getValue());
        r.fine      = juce::roundToInt (o.fine.getValue());
        r.phase     = juce::roundToInt (o.phase.getValue());
        r.intensity = juce::roundToInt (o.intensity.getValue());
        r.sweep     = juce::roundToInt (o.sweep.getValue());
    }

    recipe.drumsOn = drumsOn.getToggleState();

    auto& ds = recipe.drums[selectedDrum];
    ds.on    = drumOn.getToggleState();
    ds.tune  = juce::roundToInt (tune.getValue());
    ds.decay = juce::roundToInt (decay.getValue());
    ds.tone  = juce::roundToInt (tone.getValue());
    ds.level = juce::roundToInt (level.getValue());

    shownText = juce::String (toText (recipe));
    processor.setRecipe (recipe);
    pull();                     // the pads and the enables follow
}

void SynthPage::timerCallback()
{
    // The recipe can change under this page - a set restored by the host - so follow it.
    const auto now = juce::String (toText (processor.getRecipe()));
    if (now != shownText && ! processor.isRendering())
    {
        recipe = processor.getRecipe();
        pull();
    }

    juce::String s;
    if (processor.isRendering())
        s = "rendering...";
    else if (processor.getRenderError().isNotEmpty())
        s = processor.getRenderError();
    else if (processor.diskIsSynth())
        s = "on the disk: " + processor.getDiskName() + "   -   edit it on the Program tab, or Save disk as...";
    else
        s = "turn a knob, or pick a preset, and this becomes the loaded disk";

    status.setText (s, juce::dontSendNotification);
}

void SynthPage::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (26);
    presets.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (12);
    status.setBounds (top);

    r.removeFromTop (6);
    hint.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);

    constexpr int gap = 10;
    const int headroom = look::Panel::headerHeight + 8;

    /*
     * Three rows of knobs on this page - two in each oscillator and one for the drums - and
     * they share whatever height the page has, so the drums panel ends at the bottom rather
     * than leaving a band of nothing under it. At least 76, which is what a knob needs.
     */
    const int fixedH = (headroom + 26 + 6) + gap + (headroom + 28 + 8);
    const int cellH  = juce::jlimit (76, 96, (r.getHeight() - fixedH) / 3);

    // --- three oscillators side by side, each a combo over two rows of three knobs
    auto row = r.removeFromTop (headroom + 26 + 6 + 2 * cellH);
    const int third = (row.getWidth() - 2 * gap) / 3;

    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        l.setBounds (cell.removeFromBottom (13));
        s.setBounds (cell.reduced (1));
    };

    for (int i = 0; i < 3; ++i)
    {
        auto& o = osc[i];
        auto area = row.removeFromLeft (third);
        if (i < 2) row.removeFromLeft (gap);

        o.panel.setBounds (area);
        auto inside = o.panel.content();

        o.kind.setBounds (inside.removeFromTop (26));
        inside.removeFromTop (6);

        auto row1 = inside.removeFromTop (cellH);
        auto row2 = inside.removeFromTop (cellH);
        const int w = row1.getWidth() / 3;

        placeKnob (row1.removeFromLeft (w), o.level,  o.levelL);
        placeKnob (row1.removeFromLeft (w), o.octave, o.octaveL);
        placeKnob (row1,                    o.fine,   o.fineL);
        placeKnob (row2.removeFromLeft (w), o.phase,  o.phaseL);
        placeKnob (row2.removeFromLeft (w), o.intensity, o.intensityL);
        placeKnob (row2,                    o.sweep,  o.sweepL);
    }

    // --- the drums: the switch and the pads on one line, the chosen drum's knobs under
    r.removeFromTop (gap);
    drumPanel.setBounds (r.removeFromTop (headroom + 28 + 8 + cellH));
    auto inside = drumPanel.content();

    auto line = inside.removeFromTop (28);
    drumsOn.setBounds (line.removeFromLeft (84));
    line.removeFromLeft (8);
    const int padW = juce::jmin (74, (line.getWidth() - 8 * 4) / 9);
    for (auto& pad : pads)
    {
        pad.setBounds (line.removeFromLeft (padW).reduced (0, 2));
        line.removeFromLeft (4);
    }

    inside.removeFromTop (8);
    auto knobs = inside.removeFromTop (cellH);

    // The chosen drum's switch and four knobs, spread evenly across the panel like the pads
    // above them rather than bunched at its left - each knob its own size in its cell.
    const int n = 5, cellW = knobs.getWidth() / n;
    auto cellAt = [&] (int i) { return juce::Rectangle<int> (knobs.getX() + i * cellW, knobs.getY(), cellW, knobs.getHeight()); };
    auto knobIn = [] (juce::Rectangle<int> c) { return c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 68), c.getHeight()); };

    drumOn.setBounds (cellAt (0).withSizeKeepingCentre (70, 24));
    placeKnob (knobIn (cellAt (1)), tune,  tuneL);
    placeKnob (knobIn (cellAt (2)), decay, decayL);
    placeKnob (knobIn (cellAt (3)), tone,  toneL);
    placeKnob (knobIn (cellAt (4)), level, levelL);
}
