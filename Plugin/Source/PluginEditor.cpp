#include "PluginEditor.h"
#include "NativeFileDialog.h"

#include <algorithm>

/*
 * Where the file browser was last pointed, kept between one disk and the next.
 *
 * WHY THIS IS NOT IN THE PLUGIN'S STATE
 *
 * A plugin's state travels with the song. A folder saved there would come back weeks
 * later, in somebody else's session, as an answer to a question nobody had asked - and an
 * instance freshly added to a new project would have no answer at all, which is exactly
 * the case where being sent to the right shelf matters most. So it lives in the plugin's
 * own settings file instead, beside the machine's other application data, where every
 * instance in every host shares the one answer.
 *
 * The file is opened for each read and each write rather than held. This happens when
 * somebody clicks a button, not in any loop, and two instances writing it at once means
 * the later one wins - which for "the last folder" is the right answer anyway.
 */
namespace
{
    const char* const lastFolderKey = "lastDiskFolder";

    std::unique_ptr<juce::PropertiesFile> pluginSettings()
    {
        juce::PropertiesFile::Options options;
        // Renamed with the product. All it holds is the last folder a disk came from, so
        // starting afresh after the rename costs one extra click, once.
        options.applicationName     = "Mz950";
        options.filenameSuffix      = "settings";
        options.folderName          = "Mz950";
        options.osxLibrarySubFolder = "Application Support";

        return std::make_unique<juce::PropertiesFile> (options);
    }

    juce::File rememberedDiskFolder()
    {
        const auto path = pluginSettings()->getValue (lastFolderKey);
        if (path.isEmpty()) return {};

        const juce::File folder (path);
        return folder.isDirectory() ? folder : juce::File();
    }

    void rememberDiskFolder (const juce::File& folder)
    {
        if (! folder.isDirectory()) return;

        auto settings = pluginSettings();
        settings->setValue (lastFolderKey, folder.getFullPathName());
        settings->saveIfNeeded();
    }

    /*
     * The sound library the installer left on this machine, if it did.
     *
     * First use is the case that matters: somebody has just installed this, has never owned
     * an S950 floppy in their life, and presses Load disk. Sending them to an empty Documents
     * folder invites the conclusion that the plugin is broken. Sending them to the disks that
     * were installed alongside it means the first thing they do makes a noise.
     *
     * The path comes from the installer rather than being guessed at, because {app} is the
     * user's choice and Program Files is only the default.
     */
    juce::File installedLibrary()
    {
       #if JUCE_WINDOWS
        // Per-user install first, then machine-wide - the order Inno's HKA resolves in. The
        // Mz950 keys first; the VirtualS950 ones are what an install from before the rename
        // wrote, and its library is still just as much where it said.
        const char* const keys[] =
        {
            "HKEY_CURRENT_USER\\Software\\Mz950\\DiskLibrary",
            "HKEY_LOCAL_MACHINE\\Software\\Mz950\\DiskLibrary",
            "HKEY_CURRENT_USER\\Software\\VirtualS950\\DiskLibrary",
            "HKEY_LOCAL_MACHINE\\Software\\VirtualS950\\DiskLibrary"
        };

        for (const auto* key : keys)
        {
            const auto path = juce::WindowsRegistry::getValue (key);
            if (path.isEmpty()) continue;

            const juce::File folder (path);
            if (folder.isDirectory()) return folder;
        }
       #endif

        return {};
    }
}

// ============================================================================ envelopes

namespace
{
    // The shape's own colours, kept together so the two graphs cannot drift apart.
    // The graph is an OFFSET control, so it draws in cyan like the knobs beside it; amber
    // is kept for what is on the disk, which this is not. See Look.h.
    const juce::Colour envBack   { 0xff17191d };
    const juce::Colour envGrid   { 0xff2b2f36 };
    const juce::Colour envLine   = look::perform;
    const juce::Colour envHandle = look::text;
    const juce::Colour envFaint  = look::dim;

    /*
     * The readout has to be READ. It was the same grey as the hints around it, on a dark
     * panel, at eleven points - which is fine for something you glance past and no use at
     * all for the four numbers the graph exists to tell you.
     */
    const juce::Colour envText { 0xffd6dde8 };

    /*
     * How much of the width each stage may take.
     *
     * The sustain is a level, not a time, so it gets a fixed plateau to be drawn along
     * rather than a share of the width that grows and shrinks.
     */
    constexpr float stageWidth = 0.26f;
    constexpr float holdWidth  = 0.16f;

    constexpr float handleSize = 7.0f;

    /*
     * The shortest a stage is ever DRAWN, as a fraction of its own width.
     *
     * A stage of zero is the common case - most of the library has an instant attack and no
     * decay - and drawn honestly it puts that stage's corner exactly on top of the one
     * before it, where it cannot be grabbed. That is not a small blemish: with decay at 0,
     * decay AND sustain both hid under the attack handle, so three of the four stages were
     * unreachable and the only way back was a controller.
     *
     * So every stage keeps a sliver of width whatever it holds. The shape then tells a small
     * lie about a stage of zero, which is why the numbers are printed above it, and it is the
     * same lie every hardware editor tells for the same reason.
     */
    constexpr float minStage = 0.18f;
}

EnvelopeEditor::EnvelopeEditor (VirtualS950Processor& p, bool isFilter)
    : processor (p), filter (isFilter)
{
    base = processor.getEnvelope (filter);
    startTimerHz (20);
}

juce::RangedAudioParameter* EnvelopeEditor::parameterFor (const char* stage) const
{
    return processor.parameters.getParameter (juce::String (filter ? "vcf" : "vca") + stage);
}

EnvelopeEditor::Shown EnvelopeEditor::shown() const
{
    auto valueOf = [this] (const char* stage, int keygroupValue)
    {
        auto* p = parameterFor (stage);
        const double trim = p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0;
        return juce::jlimit (0.0, 99.0, keygroupValue + trim);
    };

    return { valueOf ("Attack",  base.attack),
             valueOf ("Decay",   base.decay),
             valueOf ("Sustain", base.sustain),
             base.hasRelease ? valueOf ("Release", base.release) : 0.0 };
}

void EnvelopeEditor::cornerPoints (juce::Point<float>* into) const
{
    auto r = getLocalBounds().toFloat().reduced (handleSize + 2.0f);
    const auto s = shown();

    const float top    = r.getY();
    const float bottom = r.getBottom();
    const float height = r.getHeight();

    auto along = [&r] (double stored)
    {
        return r.getWidth() * stageWidth
             * juce::jmax (minStage, (float) (stored / 99.0));
    };

    const float xAttack = r.getX() + along (s.attack);
    const float xDecay  = xAttack  + along (s.decay);

    /*
     * With no release stage there is nothing after the sustain, so the plateau runs to the
     * edge instead of stopping a sixth of the way across and leaving the graph looking
     * broken. The filter envelope simply holds where it is until the note ends, and that is
     * what a line to the edge says.
     */
    const float xHold    = base.hasRelease ? xDecay + r.getWidth() * holdWidth : r.getRight();
    const float xRelease = xHold + (base.hasRelease ? along (s.release) : 0.0f);

    const float ySustain = bottom - height * (float) (s.sustain / 99.0);

    into[0] = { xAttack,  top };
    into[1] = { xDecay,   ySustain };
    into[2] = { xRelease, base.hasRelease ? bottom : ySustain };
    into[3] = { xHold,    ySustain };          // where the plateau ends; not draggable
}

EnvelopeEditor::Drags EnvelopeEditor::dragsFor (Corner c) const
{
    switch (c)
    {
        case attackCorner:  return { "Attack",  nullptr };
        case decayCorner:   return { "Decay",   "Sustain" };   // sideways and up, together
        case releaseCorner: return { "Release", nullptr };
        default:            return { nullptr,   nullptr };
    }
}

/*
 * The NEAREST corner within reach, not the first one found.
 *
 * First-found is what made three stages ungrabbable: with a decay of 0 the decay corner sits
 * exactly on the attack corner, and attack was simply earlier in the list, so it won every
 * time. Minimum stage widths keep them apart now, and taking the nearest means a tie can
 * never be settled by declaration order again.
 */
EnvelopeEditor::Corner EnvelopeEditor::cornerAt (juce::Point<float> where) const
{
    juce::Point<float> p[4];
    cornerPoints (p);

    Corner best = none;
    float  nearest = handleSize * 2.2f;

    for (int i = 0; i <= releaseCorner; ++i)
    {
        if (i == releaseCorner && ! base.hasRelease) continue;

        const float d = where.getDistanceFrom (p[i]);
        if (d <= nearest) { nearest = d; best = (Corner) i; }
    }

    return best;
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();

    g.setColour (envBack);
    g.fillRoundedRectangle (all, 3.0f);

    juce::Point<float> p[4];
    cornerPoints (p);

    auto r = getLocalBounds().toFloat().reduced (handleSize + 2.0f);

    // a floor and a ceiling to read the shape against
    g.setColour (envGrid);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());

    juce::Path shape;
    shape.startNewSubPath (r.getX(), r.getBottom());
    shape.lineTo (p[0]);                       // up the attack
    shape.lineTo (p[1]);                       // down the decay to the sustain
    shape.lineTo (p[3]);                       // along the sustain
    shape.lineTo (p[2]);                       // and away

    g.setColour (envLine.withAlpha (0.14f));
    {
        juce::Path filled (shape);
        filled.lineTo (p[2].x, r.getBottom());
        filled.lineTo (r.getX(), r.getBottom());
        filled.closeSubPath();
        g.fillPath (filled);
    }

    g.setColour (envLine);
    g.strokePath (shape, juce::PathStrokeType (2.0f));

    for (int i = 0; i <= releaseCorner; ++i)
    {
        if (i == releaseCorner && ! base.hasRelease)
            continue;

        const bool lit = (dragging == i) || (dragging == none && hovering == i);
        const float size = handleSize * (lit ? 1.25f : 1.0f);

        g.setColour (envHandle);
        g.fillRect (juce::Rectangle<float> (size, size).withCentre (p[i]));

        if (lit)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (juce::Rectangle<float> (size, size).withCentre (p[i]), 1.0f);
        }
    }

    /*
     * An S900 programme never wrote its four filter bytes, so it has no envelope of its own.
     * It is still shown and still editable - starting from a flat one that moves nothing -
     * because refusing to draw it left those programmes with four controls that did nothing
     * and no way to tell why. What it does say is that the shape came from nowhere.
     */
    if (! base.written)
    {
        g.setColour (envFaint);
        g.setFont (look::font (11.0f));
        g.drawText ("none on the disk - dial in an amount",
                    r.removeFromBottom (16.0f), juce::Justification::centredRight);
    }

    /*
     * What the corners read, so the graph is not only a picture.
     *
     * On a chip of its own, because the shape goes wherever the envelope says and any fixed
     * corner of this component is somewhere the line sometimes is - an instant attack with
     * full sustain puts it straight through the top-left, which is where this used to sit.
     */
    const auto s = shown();

    // roundToInt, not String(v, 0): with zero decimal places JUCE prints the shortest form
    // that round-trips, which put "A 76.1539" on screen where "A 76" was meant.
    juce::String text = "A " + juce::String (juce::roundToInt (s.attack))
                      + "   D " + juce::String (juce::roundToInt (s.decay))
                      + "   S " + juce::String (juce::roundToInt (s.sustain));
    if (base.hasRelease)
        text += "   R " + juce::String (juce::roundToInt (s.release));

    g.setFont (look::bold (11.5f));
    const int wide = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
    auto chip = juce::Rectangle<int> (wide + 14, 18)
                    .withPosition (getLocalBounds().getRight() - wide - 20, 5);

    g.setColour (envBack.withAlpha (0.92f));
    g.fillRoundedRectangle (chip.toFloat(), 3.0f);
    g.setColour (envGrid);
    g.drawRoundedRectangle (chip.toFloat(), 3.0f, 1.0f);

    g.setColour (envText);
    g.drawText (text, chip, juce::Justification::centred);
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto was = hovering;
    hovering = cornerAt (e.position);
    if (hovering != was) repaint();
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hovering != none) { hovering = none; repaint(); }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = cornerAt (e.position);
    if (dragging == none) return;

    // One gesture for the whole drag, so a host records it as one move rather than as a
    // hundred, and an undo step is the drag rather than the last pixel of it.
    gestureOpen = true;

    const auto d = dragsFor (dragging);
    if (d.alongX) if (auto* p = parameterFor (d.alongX)) p->beginChangeGesture();
    if (d.upY)    if (auto* p = parameterFor (d.upY))    p->beginChangeGesture();

    repaint();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == none) return;

    auto r = getLocalBounds().toFloat().reduced (handleSize + 2.0f);
    if (r.getWidth() <= 0 || r.getHeight() <= 0) return;

    juce::Point<float> p[4];
    cornerPoints (p);

    const float perStage = r.getWidth() * stageWidth;
    const auto  d = dragsFor (dragging);

    // Each corner measures from where the one before it ended, so dragging decay does not
    // fight attack - which is what makes the shape feel jointed rather than elastic.
    if (d.alongX != nullptr)
    {
        const float from = dragging == attackCorner  ? r.getX()
                         : dragging == decayCorner   ? p[0].x
                                                     : p[3].x;

        const int which = dragging == attackCorner ? base.attack
                        : dragging == decayCorner  ? base.decay
                                                   : base.release;

        trimTo (d.alongX, which, (e.position.x - from) / perStage * 99.0, true);
    }

    if (d.upY != nullptr)
        trimTo (d.upY, base.sustain,
                (r.getBottom() - e.position.y) / r.getHeight() * 99.0, true);

    repaint();
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    if (! gestureOpen) { dragging = none; return; }

    const auto d = dragsFor (dragging);
    if (d.alongX) if (auto* p = parameterFor (d.alongX)) p->endChangeGesture();
    if (d.upY)    if (auto* p = parameterFor (d.upY))    p->endChangeGesture();

    gestureOpen = false;
    dragging = none;
    repaint();
}

/// Double-click a corner to put its stage back to whatever the disk says.
void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto corner = cornerAt (e.position);
    if (corner == none) return;

    auto zero = [this] (const char* stage)
    {
        if (auto* p = parameterFor (stage))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            p->endChangeGesture();
        }
    };

    const auto d = dragsFor (corner);
    if (d.alongX) zero (d.alongX);
    if (d.upY)    zero (d.upY);

    repaint();
}

void EnvelopeEditor::trimTo (const char* stage, int keygroupValue, double value, bool)
{
    auto* p = parameterFor (stage);
    if (p == nullptr) return;

    // The control is an offset, so what goes in is the distance from where the programme
    // already is - and it cannot ask for more than the range allows.
    const double wanted = juce::jlimit (0.0, 99.0, value);
    const float  trim   = (float) juce::jlimit (-99.0, 99.0, wanted - keygroupValue);

    p->setValueNotifyingHost (p->convertTo0to1 (trim));
}

void EnvelopeEditor::timerCallback()
{
    // The programme can change under this window, and so can the parameters - from a
    // controller, an automation lane, or the host restoring a session.
    const auto now = processor.getEnvelope (filter);

    const bool moved = now.attack != base.attack || now.decay != base.decay
                    || now.sustain != base.sustain || now.release != base.release
                    || now.written != base.written;

    base = now;
    if (moved) { repaint(); return; }

    // Otherwise repaint anyway, cheaply: the trims move without anything telling us.
    repaint();
}

// ============================================================================== the window

VirtualS950Editor::VirtualS950Editor (VirtualS950Processor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Before any child is made, so every one of them is born wearing it.
    setLookAndFeel (&lookAndFeel);

    /*
     * The panels go in first, so they sit BEHIND the controls laid out on top of them -
     * z-order is the order of adding, and a panel that arrived after its knobs would cover
     * them. They are only decoration and let the mouse through anyway.
     */
    for (auto* panel : { &filterPanel, &lfoPanel, &velocityPanel, &vcaPanel, &vcfPanel,
                         &glidePanel, &widePanel })
        addAndMakeVisible (*panel);

    gain.setTextValueSuffix ("");
    gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 14);
    look::accent (gain, look::neutral);
    addAndMakeVisible (gain);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "gain", gain);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    look::styleCaption (gainLabel);
    addAndMakeVisible (gainLabel);

    /*
     * The player's controls, in two rows: what the level does, and what the filter does.
     *
     * Zero is the middle of every one of them and is where the programme plays as the disk
     * describes it, so each says so: double-click returns to it, and the value carries a
     * sign so a glance tells you which way you have gone.
     */
    const struct { const char* id; const char* name; const char* group; int cc; } wanted[] =
    {
        /*
         * Filter belongs to the SAMPLES, not to the envelope, so it gets its own row.
         *
         * It sets where each sample's filter sits - byte 44 for the soft one, 66 for the
         * hard - and the envelope then moves the cutoff away from there by Amnt. Standing
         * the two side by side under one heading made it look like a second envelope
         * control, which it is not.
         */
        { "vcfCutoff",  "Filter",  "SAMPLE", 74 },
        { "vcfAmount",  "Amnt",    "VCF",    70 },

        /*
         * The LFO, in its own row.
         *
         * Three knobs rather than a shape, because there is no shape to draw: it is a sine
         * at a rate, at a depth, fading in over a delay. Rate is linear in hertz where every
         * other time on this panel is exponential, so the knob deliberately feels different
         * under the hand - a unit is 0.089 Hz wherever you are on it.
         */
        { "lfoRate",    "Rate",    "LFO",    76 },
        { "lfoDepth",   "Depth",   "LFO",    77 },
        { "lfoDelay",   "Delay",   "LFO",    78 },

        /*
         * How hard you play, and what it reaches.
         *
         * Two, not four. The keygroup also carries velocity to attack and to release, but
         * nothing in the library sets either and no engine models them, so knobs for those
         * would be knobs over invented behaviour.
         */
        { "velToFilter",   "Freq",     "VELOCITY", 109 },
        { "velToLoudness", "Loudness", "VELOCITY", 112 },
    };

    for (const auto& w : wanted)
    {
        Knob k;
        k.id = w.id; k.name = w.name; k.group = w.group; k.cc = w.cc;

        k.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                   juce::Slider::TextBoxBelow);

        const juce::String tip =
            juce::String (w.group) + " " + w.name +
            ", offset from what the disk says, across every keygroup in the programme. "
            "Zero plays it as written. MIDI CC " + juce::String (w.cc) + "."
            + (juce::String (w.group) == "LFO"
                 ? juce::String (" Adds only: nearly every programme leaves the LFO switched"
                                 " off, so below zero there is nothing to take away.")
                 : juce::String())
            + (juce::String (w.group) == "VELOCITY"
                 ? juce::String (" Adds only. Loudness reaches the next note you play rather"
                                 " than one already sounding, because how hard a key was"
                                 " struck is settled when it goes down.")
                 : juce::String());

        k.slider->setDoubleClickReturnValue (true, 0.0);
        k.slider->setTooltip (tip);
        k.slider->setTitle (juce::String (w.group) + " " + w.name + " offset");
        k.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 14);
        look::accent (*k.slider, look::perform);           // cyan: an offset
        addAndMakeVisible (*k.slider);

        k.label = std::make_unique<juce::Label>();
        k.label->setText (w.name, juce::dontSendNotification);
        k.label->setTooltip (tip);
        look::styleCaption (*k.label);
        addAndMakeVisible (*k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, w.id, *k.slider);

        knobs.push_back (std::move (k));
    }

    // Glide. Not a machine feature, and its panel's note is where that gets said once.
    const juce::String glideTip =
        "Portamento: each note slides in from the last note played in the same keygroup. "
        "Crossing into another keygroup does not glide. An addition - the S950 has no "
        "glide. MIDI CC 65 switches it (64 and up is on), CC 5 sets the time.";

    glideButton.setButtonText ("Glide");
    glideButton.setTooltip (glideTip);
    look::accent (glideButton, look::extra);
    addAndMakeVisible (glideButton);
    glideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, "glide", glideButton);

    glideTime.setTooltip ("How long a glide takes, whatever the interval. MIDI CC 5: "
                          "0 is no glide, 127 is three seconds.");
    glideTime.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
    look::accent (glideTime, look::extra);
    addAndMakeVisible (glideTime);
    glideTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "glideTime", glideTime);

    // After the attachment: it sets the range, and the double-click value must lie inside it.
    glideTime.setDoubleClickReturnValue (true, 120.0);

    glideTimeLabel.setText ("Time", juce::dontSendNotification);
    glideTimeLabel.setTooltip (glideTime.getTooltip());
    look::styleCaption (glideTimeLabel);
    addAndMakeVisible (glideTimeLabel);

    voiceCount.setTooltip ("Polyphony: the most notes that can sound at once, 1 to 8 - "
                           "a limit, not a unison stack. At 1 (mono) no chords; a key struck "
                           "while another is held moves the note there without restarting "
                           "it, gliding if glide is on, and letting go goes back to the key "
                           "still held. MIDI CC 106: 0-15 is mono, 112-127 all eight.");
    voiceCount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 14);
    look::accent (voiceCount, look::extra);
    addAndMakeVisible (voiceCount);
    voiceCountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "voices", voiceCount);
    voiceCount.setDoubleClickReturnValue (true, 8.0);

    // --- wide
    wideButton.setButtonText ("Wide");
    look::accent (wideButton, look::extra);
    look::accent (offsetButton, look::extra);
    wideButton.setTooltip ("Wide: every note as two voices, one detuned flat and leaning left, "
                           "one sharp by the same amount and leaning right. At most four notes "
                           "- the S950's eight voices, two to a note. An addition: the S950 "
                           "has no such thing. MIDI CC 107 (64 and up is on).");
    addAndMakeVisible (wideButton);
    wideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, "wide", wideButton);

    offsetButton.setTooltip ("Start the sharp half 7 ms into the sample, so the pair is not in "
                             "phase at the attack and does not flange. Off keeps the sharpest "
                             "attack. MIDI CC 111.");
    addAndMakeVisible (offsetButton);
    offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, "wideOffset", offsetButton);

    auto setUpWideKnob = [this] (juce::Slider& s, juce::Label& l, const char* id,
                                 const char* name, const char* tip, double reset,
                                 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& a)
    {
        s.setTooltip (tip);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        look::accent (s, look::extra);
        addAndMakeVisible (s);
        a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, id, s);
        s.setDoubleClickReturnValue (true, reset);

        l.setText (name, juce::dontSendNotification);
        l.setTooltip (tip);
        look::styleCaption (l);
        addAndMakeVisible (l);
    };

    setUpWideKnob (wideDetune, wideDetuneLabel, "wideDetune", "Detune",
                   "How far each half is pulled off the note, in cents, one flat and one "
                   "sharp. 5-15 is a chorus; 25 and up goes sour. MIDI CC 108.",
                   10.0, wideDetuneAttachment);

    setUpWideKnob (wideSpread, wideSpreadLabel, "wideSpread", "Spread",
                   "How far apart the halves sit in stereo: 0 both centred, 100 hard left "
                   "and right. As loud either way. MIDI CC 110.",
                   70.0, wideSpreadAttachment);

    voiceCountLabel.setText ("Poly", juce::dontSendNotification);
    voiceCountLabel.setTooltip (voiceCount.getTooltip());
    look::styleCaption (voiceCountLabel);
    addAndMakeVisible (voiceCountLabel);

    vcaEnvelope.setTooltip ("The amplitude envelope, across every keygroup in the programme. "
                            "Drag the corners; double-click one to put that stage back to what "
                            "the disk says. MIDI CC 73 attack, 75 decay, 79 sustain, 72 release.");

    vcfEnvelope.setTooltip ("The filter envelope, across every keygroup in the programme. "
                            "Drag the corners; double-click one to put that stage back to what "
                            "the disk says. MIDI CC 102 attack, 103 decay, 104 sustain, "
                            "105 release.");

    addAndMakeVisible (vcaEnvelope);
    addAndMakeVisible (vcfEnvelope);

    patchLabel.setJustificationType (juce::Justification::centredLeft);
    patchLabel.setFont (look::bold (12.0f));
    addAndMakeVisible (patchLabel);

    voicesLabel.setJustificationType (juce::Justification::centredLeft);
    voicesLabel.setFont (look::font (11.5f));
    voicesLabel.setColour (juce::Label::textColourId, look::dim);
    addAndMakeVisible (voicesLabel);

    loadButton.onClick = [this] { openDisk(); };
    addAndMakeVisible (loadButton);

    programs.setTextWhenNoChoicesAvailable ("no disk loaded");
    programs.onChange = [this]
    {
        // The combo is filled with 1-based ids, which is JUCE's convention because 0 means
        // "nothing selected".
        processor.selectProgram (programs.getSelectedId() - 1);
    };
    addAndMakeVisible (programs);

    // Whatever the processor is already holding - this editor may well not be the first.
    refreshPrograms();

    // --- the two tabs, each in the colour its controls wear
    mainTabs.addTab ("Program", look::program, -1);
    mainTabs.addTab ("Perform", look::perform, -1);
    mainTabs.addTab ("Synth",   look::extra,   -1);     // violet: not on the S950
    mainTabs.addChangeListener (this);
    addAndMakeVisible (mainTabs);

    addChildComponent (programPage);
    addChildComponent (synthPage);

    performHeading.setText ("Offsets and extras on top of the programme, played live and from "
                            "MIDI CCs. Never written to the disk.",
                            juce::dontSendNotification);
    performHeading.setFont (look::font (11.5f));
    performHeading.setColour (juce::Label::textColourId, look::dim);
    addAndMakeVisible (performHeading);

    mainTabs.setCurrentTabIndex (juce::jlimit (0, 2, processor.editorTab), false);
    showTab (mainTabs.getCurrentTabIndex());

    /*
     * Now that every child is in place, tell them all. setLookAndFeel only reaches the
     * children a component has at the time, and most of these were built as members before
     * the constructor body ran - so a Slider had already copied JUCE's default outline
     * colour into its value box, and drew a frame round it that the look says not to.
     */
    sendLookAndFeelChange();

    // Sized for the controls. It used to be 720 x 720 so the file browser could open inside
    // it; the system dialog now opens outside, on top of it - see openDisk.
    setSize (640, 640);
    startTimerHz (10);
}

/*
 * Pick an image, in the Windows file dialog - owned by this plugin window.
 *
 * Not JUCE's native chooser, which puts itself on the primary display:
 *
 *     auto mainMon = Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds;
 *     setBounds (mainMon.getX() + mainMon.getWidth() / 4, ...)
 *
 * On a machine with two monitors six thousand pixels apart, a plugin on the second one
 * opened its file dialog on the first, where nobody was looking - and a host that keeps
 * plugin windows always on top could cover it. For a while the answer was a JUCE browser
 * inside this window, which could do neither but was small and unfamiliar.
 *
 * nativeDialog owns the system dialog to this window's top-level ancestor, so Windows keeps
 * it above the plugin window, topmost or not, and centres it on whichever monitor that is.
 *
 * Either container: a plain sector image, or the .hfe that archived floppies come in.
 */
void VirtualS950Editor::openDisk()
{
    /*
     * Somewhere useful to start, in the order somebody would guess:
     *
     *   - beside the disk this instance already has open, that being the shelf the next
     *     one is nearly always on;
     *   - otherwise wherever a disk was last chosen from, in any instance and any host;
     *   - otherwise the sound library the installer put on this machine, which is the
     *     first-use answer: somebody who has never held an S950 floppy still has disks;
     *   - otherwise the folder the Studio writes its images to;
     *   - otherwise Documents, which is always there.
     *
     * Each is taken only if it still exists, so a folder on a drive that has since been
     * unplugged falls through to the next answer rather than opening the browser on
     * nothing.
     */
    juce::File start;

    const auto openNow = processor.getDiskPath();
    if (openNow.isNotEmpty())
    {
        const auto beside = juce::File (openNow).getParentDirectory();
        if (beside.isDirectory()) start = beside;
    }

    if (! start.isDirectory()) start = rememberedDiskFolder();

    if (! start.isDirectory()) start = installedLibrary();

    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("S950 images");

    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    nativeDialog::chooseFileToOpen (*this, "Open an Akai S900/S950 disk image", start,
                                    { "Akai S900/S950 disk images (*.hfe, *.img)", "*.hfe;*.img" },
                                    [this] (const juce::File& file)
    {
        if (file == juce::File()) return;

        //
        // Remembered before the load is attempted rather than after it.
        //
        // A file that turns out not to be a disk this can read is still where the person
        // was looking, and sending them back through six folders to try the one next to it
        // is the opposite of helpful. Cancelling leaves no file and so changes nothing.
        //
        rememberDiskFolder (file.getParentDirectory());

        juce::String error;

        if (! processor.loadDisk (file, error))
        {
            // Owned by this window, like the dialog, so it lands where the plugin is.
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not open that disk",
                error,
                this);
            return;
        }

        refreshPrograms();
    });
}

VirtualS950Editor::~VirtualS950Editor()
{
    stopTimer();
    mainTabs.removeChangeListener (this);

    // Before lookAndFeel goes: a child asked to repaint during teardown must not find a
    // dangling one.
    setLookAndFeel (nullptr);
}

std::vector<juce::Component*> VirtualS950Editor::performParts()
{
    std::vector<juce::Component*> parts {
        &performHeading, &vcaEnvelope, &vcfEnvelope,
        &filterPanel, &lfoPanel, &velocityPanel, &vcaPanel, &vcfPanel, &glidePanel, &widePanel,
        &glideButton, &glideTime, &glideTimeLabel,
        &voiceCount, &voiceCountLabel,
        &wideButton, &offsetButton, &wideDetune, &wideDetuneLabel,
        &wideSpread, &wideSpreadLabel };

    for (auto& k : knobs)
    {
        parts.push_back (k.slider.get());
        parts.push_back (k.label.get());
    }

    return parts;
}

void VirtualS950Editor::showTab (int tab)
{
    processor.editorTab = tab;

    for (auto* c : performParts())
        c->setVisible (tab == 1);

    programPage.setVisible (tab == 0);
    synthPage.setVisible (tab == 2);

    if (tab == 0)
        programPage.refresh();
}

void VirtualS950Editor::refreshPrograms()
{
    /*
     * dontSendNotification throughout: filling the box must not look like someone choosing
     * from it. Without that, rebuilding the list would call selectProgram and rebuild the
     * patch - and putting the selection back would do it a second time, under whatever is
     * currently sounding.
     */
    programs.clear (juce::dontSendNotification);

    const auto names = processor.getProgramNames();

    for (int i = 0; i < names.size(); ++i)
        programs.addItem (names[i], i + 1);

    const int chosen = processor.getSelectedProgram();

    if (chosen >= 0 && chosen < names.size())
        programs.setSelectedId (chosen + 1, juce::dontSendNotification);
}

void VirtualS950Editor::timerCallback()
{
    /*
     * Follow the processor if it changed underneath us.
     *
     * The disk and the programme can now move without this window doing it: the host's own
     * program chooser, a MIDI program change, or a saved set being restored after the
     * editor was already built. One integer compare ten times a second, against a counter
     * the processor bumps, catches all three without any of them having to know this
     * window exists.
     */
    const int generation = processor.getDiskGeneration();

    if (generation != seenGeneration)
    {
        seenGeneration = generation;
        refreshPrograms();
        programPage.refresh();      // a new disk or programme: every keygroup is different
        repaint();                  // the disk name is painted, not a label
    }

    /*
     * The Program tab re-reads the disk only when something changed it - a new programme,
     * or an edit - since reading every setting of every keygroup is the one expensive thing
     * it does. The blue "sounding" readouts follow the Perform offsets every tick: those move
     * from a controller with no edit at all.
     */
    const int edits = processor.getEditRevision();

    if (programPage.isVisible())
    {
        if (edits != seenEditRevision)
            programPage.refresh();
        else
            programPage.refreshSounding();
    }

    seenEditRevision = edits;

    const int voices = processor.getActiveVoices();

    patchLabel.setText (processor.getPatchName(), juce::dontSendNotification);

    /*
     * The S950 had eight voices, and so does this - see Engine::Polyphony.
     *
     * A disk recovered with bad or missing sectors says so beside them. An archived floppy
     * is thirty years old and some of them do not read cleanly; finding that out from a
     * label beats finding it out from a hole in a take.
     */
    /*
     * Voices sounding, and how many NOTES may: the Polyphony control, capped at four when
     * wide is on because each note is then two of the eight voices.
     */
    const auto* limit  = processor.parameters.getRawParameterValue ("voices");
    const auto* wideOn = processor.parameters.getRawParameterValue ("wide");
    const bool  wide   = wideOn != nullptr && wideOn->load() >= 0.5f;

    int notes = limit != nullptr ? juce::roundToInt (limit->load()) : 8;
    if (wide) notes = juce::jmin (notes, 4);

    juce::String state = juce::String (voices) + " of 8 voices   -   up to "
                       + juce::String (notes) + (notes == 1 ? " note (mono)" : " notes")
                       + (wide ? ", wide" : "");

    const int bad     = processor.getBadSectors();
    const int missing = processor.getMissingSectors();

    if (bad > 0 || missing > 0)
        state << "   -   recovered with " << bad << " bad and " << missing << " missing sectors";

    voicesLabel.setText (state, juce::dontSendNotification);
}

void VirtualS950Editor::paint (juce::Graphics& g)
{
    g.fillAll (look::window);

    g.setColour (look::text);
    g.setFont (look::bold (24.0f));
    g.drawText ("Mz950", 16, 12, getWidth() - 32, 28,
                juce::Justification::centredLeft, true);

    /*
     * When this copy was compiled.
     *
     * A host holds a plugin's binary open for as long as a set using it is loaded, so a
     * rebuild can quietly fail to install and the window looks identical either way. That
     * has now cost three rounds of "it still does not work" on a build that was never the
     * one running. The C# editor carries the same stamp for the same reason.
     */
    // Bottom left, beside the licence: the top-right corner is gain's, since the header was
    // rebuilt around the two tabs, and the stamp drawn there ran underneath the knob.
    g.setColour (look::faint);
    g.setFont (look::font (10.0f));
    g.drawText ("built " + juce::String (__DATE__) + "  " + __TIME__,
                16, getHeight() - 22, getWidth() / 2, 16,
                juce::Justification::centredLeft, true);

    // The licence asks an interactive program to say so where it can be seen.
    g.drawText (juce::CharPointer_UTF8 ("Copyright \xc2\xa9 2026 Simon Moscrop  -  AGPLv3, "
                                        "no warranty  -  github.com/simozzer/Mz950"),
                16, getHeight() - 22, getWidth() - 32, 16,
                juce::Justification::centredRight, true);

    /*
     * What this is, and what it is not, beside the name.
     *
     * Naming Akai to say what the plugin is COMPATIBLE with is the ordinary, permitted kind
     * of mention; the plugin wearing the name was not, which is why it is called Mz950. The
     * disclaimer sits where the name is, rather than in small print nobody reaches.
     */
    g.setColour (look::dim);
    g.setFont (look::font (11.0f));
    g.drawText ("plays Akai S900/S950 disks  -  independent, not affiliated with Akai or inMusic",
                100, 22, getWidth() - 100 - 16 - 126, 16,
                juce::Justification::centredLeft, true);

    const auto disk = processor.getDiskName();
    g.setColour (disk.isEmpty() ? look::dim : look::text);
    g.setFont (look::font (12.5f));
    g.drawText (disk.isEmpty() ? "no disk  -  placeholder sound"
                               : juce::File (disk).getFileName(),
                16, 40, getWidth() - 32, 20,
                juce::Justification::centredLeft, true);
}

void VirtualS950Editor::resized()
{
    auto r = getLocalBounds().reduced (16);

    /*
     * The header: the title and disk painted, the disk row, and the two tabs - with gain in
     * its corner on the right, on both tabs. It was there first, and it is the one control
     * that is neither the programme nor an offset from it.
     *
     * Only the header is narrowed by it. Everything below gets the whole width, which the
     * Program tab's rows of keygroup controls need.
     */
    auto head = r.removeFromTop (106);
    // The same size as every other knob now, level with the disk row rather than the title.
    auto gainCell = head.removeFromRight (68);
    gainLabel.setBounds (gainCell.removeFromBottom (14));
    gain.setBounds (gainCell.withTrimmedTop (26).withTrimmedBottom (2));
    head.removeFromRight (16);

    head.removeFromTop (44);                    // the title painted above
    auto row = head.removeFromTop (26);
    loadButton.setBounds (row.removeFromLeft (104));
    row.removeFromLeft (8);
    programs.setBounds (row);

    head.removeFromTop (8);
    // The whole width of the disk row, so the tab bar's rule ends where the programme box does.
    mainTabs.setBounds (head.removeFromTop (28));

    r.removeFromTop (10);

    // the licence line is painted along the very bottom, and the status line sits above it
    r.removeFromBottom (14);
    auto status = r.removeFromBottom (20);
    patchLabel.setBounds (status.removeFromLeft (status.getWidth() / 3));
    voicesLabel.setBounds (status);
    r.removeFromBottom (14);

    programPage.setBounds (r);
    synthPage.setBounds (r);

    performHeading.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);

    /*
     * THE PERFORM TAB, AS THREE ROWS OF PANELS
     *
     *   row 1   FILTER | LFO | VELOCITY            the offsets, cyan
     *   row 2   GLIDE & POLYPHONY | WIDE           the extras, violet
     *   row 3   VCA ENVELOPE | VCF ENVELOPE        the offsets again, as shapes
     *
     * One knob cell is 68 wide; a panel is its knobs plus padding, and the rows share the
     * width in proportion to what they hold, so nothing is stretched to fill. The knobs
     * were 84 x 104 while the file browser opened inside this window and needed the room;
     * with the system dialog outside it, they are sized for the controls alone.
     */
    constexpr int cellW = 68, cellH = 80, gap = 10;
    const int headroom = look::Panel::headerHeight + 8;

    /// A knob and its caption in one cell, the caption under the value.
    auto placeKnob = [cellW] (juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l)
    {
        cell = cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), cellW), cell.getHeight());
        l.setBounds (cell.removeFromBottom (13));
        s.setBounds (cell.reduced (1));
    };

    /*
     * `n` equal cells across an area, so a panel's controls sit evenly in it rather than
     * bunching at its left with a blank to their right. A knob keeps its own size inside
     * its cell; only the spacing stretches.
     */
    auto cellsAcross = [] (juce::Rectangle<int> area, int n)
    {
        std::vector<juce::Rectangle<int>> cells;
        for (int i = 0; i < n; ++i)
            cells.push_back (juce::Rectangle<int> (area.getX() + area.getWidth() * i / n, area.getY(),
                                                   area.getWidth() / n, area.getHeight()));
        return cells;
    };

    /// Every offset knob of one group, spread across a panel's content area.
    auto placeGroup = [&] (const look::Panel& panel, const char* group)
    {
        std::vector<Knob*> mine;
        for (auto& k : knobs)
            if (juce::String (k.group) == group) mine.push_back (&k);

        const auto cells = cellsAcross (panel.content(), (int) mine.size());
        for (size_t i = 0; i < mine.size(); ++i)
            placeKnob (cells[i], *mine[i]->slider, *mine[i]->label);
    };

    /*
     * Every row splits at the same centre gutter, so the panels line up down the page:
     * FILTER and VELOCITY (one knob and two) on the left, the LFO's three on the right;
     * glide beside wide; the two envelopes. Before, row 1 split in sixths and the others in
     * half, and no edge lined up with any other.
     */
    const int half = (r.getWidth() - gap) / 2;

    // --- row 1: the offsets - FILTER and VELOCITY on the left, in the ratio of their knobs
    auto row1 = r.removeFromTop (cellH + headroom);
    {
        auto left = row1.removeFromLeft (half);
        row1.removeFromLeft (gap);
        filterPanel.setBounds (left.removeFromLeft ((left.getWidth() - gap) / 3));
        left.removeFromLeft (gap);
        velocityPanel.setBounds (left);
        lfoPanel.setBounds (row1);
    }

    placeGroup (filterPanel,   "SAMPLE");
    placeGroup (lfoPanel,      "LFO");
    placeGroup (velocityPanel, "VELOCITY");

    // --- row 2: the extras, violet, and told apart from the row above by it
    r.removeFromTop (gap);
    auto row2 = r.removeFromTop (cellH + headroom);

    glidePanel.setBounds (row2.removeFromLeft (half));
    row2.removeFromLeft (gap);
    widePanel.setBounds (row2);

    {
        const auto cells = cellsAcross (glidePanel.content(), 3);
        glideButton.setBounds (cells[0].withSizeKeepingCentre (juce::jmin (cells[0].getWidth(), 76), 24));
        placeKnob (cells[1], glideTime,  glideTimeLabel);
        placeKnob (cells[2], voiceCount, voiceCountLabel);
    }
    {
        const auto cells = cellsAcross (widePanel.content(), 4);
        wideButton.setBounds (cells[0].withSizeKeepingCentre (juce::jmin (cells[0].getWidth(), 76), 24));
        placeKnob (cells[1], wideDetune, wideDetuneLabel);
        placeKnob (cells[2], wideSpread, wideSpreadLabel);
        offsetButton.setBounds (cells[3].withSizeKeepingCentre (juce::jmin (cells[3].getWidth(), 112), 24));
    }

    // --- row 3: the two envelopes as shapes, the filter's with its Amount beside it. The
    // row takes what is left, which the window's height was chosen to make about right.
    r.removeFromTop (gap);
    auto row3 = r;

    vcaPanel.setBounds (row3.removeFromLeft (half));
    row3.removeFromLeft (gap);
    vcfPanel.setBounds (row3);

    vcaEnvelope.setBounds (vcaPanel.content());

    {
        auto area = vcfPanel.content();
        auto side = area.removeFromRight (cellW);
        area.removeFromRight (6);

        // the Amount knob halfway down beside its envelope, not stuck to the top
        for (auto& k : knobs)
            if (juce::String (k.group) == "VCF")
                placeKnob (side.withSizeKeepingCentre (cellW, cellH), *k.slider, *k.label);

        vcfEnvelope.setBounds (area);
    }
}
