#include "PluginEditor.h"

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
    const juce::Colour envBack   { 0xff17191d };
    const juce::Colour envGrid   { 0xff2b2f36 };
    const juce::Colour envLine   { 0xff45c8dc };
    const juce::Colour envHandle { 0xffe9a33a };
    const juce::Colour envFaint  { 0xff707888 };

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
        g.setFont (11.0f);
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

    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
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
    gain.setTextValueSuffix ("");
    addAndMakeVisible (gain);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "gain", gain);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
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
        k.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 13);
        addAndMakeVisible (*k.slider);

        k.label = std::make_unique<juce::Label>();
        k.label->setText (w.name, juce::dontSendNotification);
        k.label->setJustificationType (juce::Justification::centred);
        k.label->setTooltip (tip);
        addAndMakeVisible (*k.label);

        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, w.id, *k.slider);

        knobs.push_back (std::move (k));
    }

    for (auto* h : { &vcaHeading, &vcfHeading, &sampleHeading, &lfoHeading,
                     &velocityHeading })
    {
        h->setJustificationType (juce::Justification::centredLeft);
        h->setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (*h);
    }

    // Short, because each now has half the width. The controller numbers are on the
    // tooltip of the graph itself, which is where someone looking for them would point.
    vcaHeading.setText ("VCA envelope", juce::dontSendNotification);
    vcfHeading.setText ("VCF envelope", juce::dontSendNotification);
    // No heading over this row: the knob is already labelled Filter, and a heading saying the
    // same word above it is just the word twice. The label stays empty rather than being
    // deleted so the row keeps its spacing, and so there is somewhere to put a heading if
    // this row ever holds enough controls to need one.
    sampleHeading.setText ("", juce::dontSendNotification);

    // This one does need a heading: three knobs called Rate, Depth and Delay could belong to
    // half a dozen things, and the machine calls it the LFO.
    lfoHeading.setText ("LFO", juce::dontSendNotification);
    velocityHeading.setText ("VELOCITY", juce::dontSendNotification);

    // Glide. Not a machine feature, and the heading is where that gets said once.
    glideHeading.setText ("GLIDE & POLYPHONY  (not on the S950)", juce::dontSendNotification);
    glideHeading.setJustificationType (juce::Justification::centredLeft);
    glideHeading.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (glideHeading);

    const juce::String glideTip =
        "Portamento: each note slides in from the last note played in the same keygroup. "
        "Crossing into another keygroup does not glide. An addition - the S950 has no "
        "glide. MIDI CC 65 switches it (64 and up is on), CC 5 sets the time.";

    glideButton.setTooltip (glideTip);
    addAndMakeVisible (glideButton);
    glideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, "glide", glideButton);

    glideTime.setTooltip ("How long a glide takes, whatever the interval. MIDI CC 5: "
                          "0 is no glide, 127 is three seconds.");
    glideTime.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 13);
    addAndMakeVisible (glideTime);
    glideTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "glideTime", glideTime);

    // After the attachment: it sets the range, and the double-click value must lie inside it.
    glideTime.setDoubleClickReturnValue (true, 120.0);

    glideTimeLabel.setText ("Time", juce::dontSendNotification);
    glideTimeLabel.setJustificationType (juce::Justification::centred);
    glideTimeLabel.setTooltip (glideTime.getTooltip());
    addAndMakeVisible (glideTimeLabel);

    voiceCount.setTooltip ("Polyphony: the most notes that can sound at once, 1 to 8 - "
                           "a limit, not a unison stack. At 1 (mono) no chords; a key struck "
                           "while another is held moves the note there without restarting "
                           "it, gliding if glide is on, and letting go goes back to the key "
                           "still held. MIDI CC 106: 0-15 is mono, 112-127 all eight.");
    voiceCount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 13);
    addAndMakeVisible (voiceCount);
    voiceCountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, "voices", voiceCount);
    voiceCount.setDoubleClickReturnValue (true, 8.0);

    // --- wide
    wideHeading.setText ("WIDE  (not on the S950)", juce::dontSendNotification);
    wideHeading.setJustificationType (juce::Justification::centredLeft);
    wideHeading.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (wideHeading);

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
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 13);
        addAndMakeVisible (s);
        a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, id, s);
        s.setDoubleClickReturnValue (true, reset);

        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setTooltip (tip);
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
    voiceCountLabel.setJustificationType (juce::Justification::centred);
    voiceCountLabel.setTooltip (voiceCount.getTooltip());
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
    addAndMakeVisible (patchLabel);

    voicesLabel.setJustificationType (juce::Justification::centredLeft);
    voicesLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
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

    // --- the two tabs
    mainTabs.addTab ("PROGRAM", juce::Colours::transparentBlack, -1);
    mainTabs.addTab ("PERFORM", juce::Colours::transparentBlack, -1);
    mainTabs.addChangeListener (this);
    addAndMakeVisible (mainTabs);

    addChildComponent (programPage);

    performHeading.setText ("PERFORM   -   offsets and extras on top of the programme. Played "
                            "live and from MIDI CCs; never written to the disk.",
                            juce::dontSendNotification);
    performHeading.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (performHeading);

    mainTabs.setCurrentTabIndex (juce::jlimit (0, 1, processor.editorTab), false);
    showTab (mainTabs.getCurrentTabIndex());

    // Big enough to hold the file browser, which opens inside this window rather than as a
    // dialog of its own - see openDisk.
    // 120 taller than it was, for the LFO row: 108 for the knobs and 12 to stand them off
    // the row above.
    setSize (720, 840);
    startTimerHz (10);
}

/*
 * Pick an image.
 *
 * The browser opens INSIDE this window rather than as a native dialog, and that is the
 * whole point of it. JUCE puts a native chooser on the primary display:
 *
 *     auto mainMon = Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds;
 *     setBounds (mainMon.getX() + mainMon.getWidth() / 4, ...)
 *
 * On a machine with two monitors six thousand pixels apart, a plugin on the second one
 * opened its file dialog on the first, where nobody was looking. From the DAW it was
 * indistinguishable from a button that did nothing.
 *
 * Parenting the browser into the editor removes the whole class of problem rather than that
 * one instance of it: it cannot land on another screen, it cannot hide behind a DAW window
 * that is always on top, and it cannot take focus away from the host. The cost is that it
 * looks like JUCE rather than like Windows, which for a plugin is the right way round.
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

    chooser = std::make_unique<juce::FileChooser> ("Open an Akai S950 disk image",
                                                   start,
                                                   "*.hfe;*.img",
                                                   false,      // not the OS dialog - see above
                                                   false,
                                                   this);      // live in this window

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
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
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not open that disk",
                error);
            return;
        }

        refreshPrograms();
    });
}

VirtualS950Editor::~VirtualS950Editor()
{
    stopTimer();
    mainTabs.removeChangeListener (this);
}

std::vector<juce::Component*> VirtualS950Editor::performParts()
{
    std::vector<juce::Component*> parts {
        &performHeading, &vcaEnvelope, &vcfEnvelope,
        &vcaHeading, &vcfHeading, &sampleHeading, &lfoHeading, &velocityHeading,
        &glideHeading, &glideButton, &glideTime, &glideTimeLabel,
        &voiceCount, &voiceCountLabel,
        &wideHeading, &wideButton, &offsetButton, &wideDetune, &wideDetuneLabel,
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
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f));
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
    g.setColour (juce::Colours::darkgrey);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("built " + juce::String (__DATE__) + "  " + __TIME__,
                16, getHeight() - 22, getWidth() / 2, 16,
                juce::Justification::centredLeft, true);

    // The licence asks an interactive program to say so where it can be seen.
    g.setColour (juce::Colours::darkgrey);
    g.setFont (juce::FontOptions (10.0f));
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
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("plays Akai S900/S950 disks  -  independent, not affiliated with Akai or inMusic",
                100, 22, getWidth() - 100 - 16 - 126, 16,
                juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (13.0f));

    const auto disk = processor.getDiskName();
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
    auto head = r.removeFromTop (118);
    auto gainCell = head.removeFromRight (110);
    gainLabel.setBounds (gainCell.removeFromBottom (18));
    gain.setBounds (gainCell);
    head.removeFromRight (16);

    head.removeFromTop (48);                    // the title painted above
    auto row = head.removeFromTop (28);
    loadButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    programs.setBounds (row);

    head.removeFromTop (8);
    mainTabs.setBounds (head.removeFromTop (30).withWidth (280));

    r.removeFromTop (12);

    // the licence line is painted along the very bottom, and the status line sits above it
    r.removeFromBottom (14);
    auto status = r.removeFromBottom (20);
    patchLabel.setBounds (status.removeFromLeft (status.getWidth() / 3));
    voicesLabel.setBounds (status);
    r.removeFromBottom (14);

    programPage.setBounds (r);

    performHeading.setBounds (r.removeFromTop (18));
    r.removeFromTop (6);

    /*
     * An envelope with its heading above it, and - for the filter - its two knobs stacked in
     * a narrow column on the right.
     *
     * Side by side rather than one above the other. Two full-width graphs ate the whole
     * window for four numbers each, and there is more to put here than envelopes.
     *
     * The knobs belong to the filter's column because that is what they are: cutoff and
     * amount are the filter's other two numbers, and anywhere else means hunting for them.
     */
    auto placeEnvelope = [this] (juce::Rectangle<int> area, juce::Label& heading,
                                 EnvelopeEditor& envelope, const char* group)
    {
        heading.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);

        int count = 0;
        for (const auto& k : knobs)
            if (juce::String (k.group) == group) ++count;

        if (count > 0)
        {
            auto column = area.removeFromRight (78);
            area.removeFromRight (8);

            // Capped, so a single knob sits at the top of the column at a sensible size
            // rather than being stretched down the whole height of the graph beside it.
            const int cell = std::min (100, column.getHeight() / count);

            for (auto& k : knobs)
            {
                if (juce::String (k.group) != group) continue;

                auto one = column.removeFromTop (cell);
                k.label->setBounds (one.removeFromBottom (13));
                k.slider->setBounds (one.reduced (1));
            }
        }

        envelope.setBounds (area);
    };

    // One row across the bottom holding both, so the space above stays free for whatever
    // comes next.
    auto shapes = r.removeFromBottom (196);

    auto half = shapes.removeFromLeft (shapes.getWidth() / 2 - 8);
    shapes.removeFromLeft (16);

    placeEnvelope (half,   vcaHeading, vcaEnvelope, "VCA");
    placeEnvelope (shapes, vcfHeading, vcfEnvelope, "VCF");

    /*
     * The sample controls get a row of their own above the envelopes, with room in it.
     *
     * Filter is not an envelope parameter - it is where each sample's filter sits, which the
     * envelope then moves away from - and beside the graph it read as though it were. The
     * space to its right is deliberate: this is where the rest of the per-sample controls go.
     */
    /*
     * The LFO and the velocity controls share one strip, side by side.
     *
     * Three knobs and two do not fill a row each, and stacking them would have cost another
     * 120 px of window for 430 px of knobs. Side by side they read as the two groups they
     * are - what the instrument does on its own, and what it does in answer to how you play.
     */
    r.removeFromBottom (14);
    auto performance = r.removeFromBottom (108);
    auto lfo         = performance.removeFromLeft (3 * 86);
    performance.removeFromLeft (24);
    auto velocity    = performance;

    r.removeFromBottom (12);
    auto samples = r.removeFromBottom (108);

    // Wide in the space that was left above, so the window did not have to grow for it.
    r.removeFromBottom (12);
    auto wideRow = r.removeFromBottom (108);

    wideHeading.setBounds (wideRow.removeFromTop (16));
    wideRow.removeFromTop (4);

    wideButton.setBounds (wideRow.removeFromLeft (78).withSizeKeepingCentre (64, 24));
    wideRow.removeFromLeft (8);

    for (const auto& pair : { std::make_pair (&wideDetune, &wideDetuneLabel),
                              std::make_pair (&wideSpread, &wideSpreadLabel) })
    {
        auto cell = wideRow.removeFromLeft (78);
        pair.second->setBounds (cell.removeFromBottom (13));
        pair.first->setBounds (cell.reduced (1));
        wideRow.removeFromLeft (8);
    }

    offsetButton.setBounds (wideRow.removeFromLeft (120).withSizeKeepingCentre (116, 24));

    /*
     * A row of knobs under a heading, laid out left to right.
     *
     * Both rows do the same thing and neither fills its width, which is deliberate: they are
     * where the rest of the per-sample and per-LFO controls will go.
     */
    auto placeRow = [this] (juce::Rectangle<int> area, juce::Label& heading, const char* group)
    {
        heading.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);

        for (auto& k : knobs)
        {
            if (juce::String (k.group) != group) continue;

            auto cell = area.removeFromLeft (78);
            k.label->setBounds (cell.removeFromBottom (13));
            k.slider->setBounds (cell.reduced (1));
            area.removeFromLeft (8);
        }
    };

    // Filter keeps its own cell at the left; glide takes the space the row was left with.
    auto filterCell = samples.removeFromLeft (86);
    samples.removeFromLeft (24);

    placeRow (filterCell, sampleHeading, "SAMPLE");

    glideHeading.setBounds (samples.removeFromTop (16));
    samples.removeFromTop (4);

    auto switchCell = samples.removeFromLeft (78);
    glideButton.setBounds (switchCell.withSizeKeepingCentre (64, 24));
    samples.removeFromLeft (8);

    auto timeCell = samples.removeFromLeft (78);
    glideTimeLabel.setBounds (timeCell.removeFromBottom (13));
    glideTime.setBounds (timeCell.reduced (1));
    samples.removeFromLeft (8);

    auto voicesCell = samples.removeFromLeft (78);
    voiceCountLabel.setBounds (voicesCell.removeFromBottom (13));
    voiceCount.setBounds (voicesCell.reduced (1));
    placeRow (lfo,      lfoHeading,      "LFO");
    placeRow (velocity, velocityHeading, "VELOCITY");
}
