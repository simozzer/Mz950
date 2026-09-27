#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr double Pi = 3.14159265358979323846;
}

// ------------------------------------------------------------------------ parameters

juce::AudioProcessorValueTreeState::ParameterLayout
VirtualS950Processor::describeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    /*
     * One parameter for now, and it is the one that matters: eight voices of a hot sample
     * with a zone trim will clip, which is what the machine's own master level is for.
     *
     * Everything else about the sound comes off the disk rather than out of a slider, which
     * is the whole point of the instrument - so the automatable surface stays small until
     * there is a reason for it to grow.
     */
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 },
        "Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.0f, 0.5f),
        0.7f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2); })));

    /*
     * How far the pitch wheel bends, in semitones.
     *
     * The machine has this on its MIDI page with a range of 1 to 12, and it belongs to the
     * MACHINE rather than to a programme - so it is not on any disk we could read it from.
     * The OVERALL SETTINGS file that would hold it is written only when somebody saves it
     * deliberately, and none of the calibration disks has one.
     *
     * An integer parameter because the panel's is: the machine offers whole semitones and
     * nothing between, so a continuous control would invite settings the hardware cannot
     * hold. Two is the default here because it is what almost everything defaults to; the
     * one real unit to hand is set to seven, and 80 of the 99 OVERALL SETTINGS files in the
     * library read 7 at the byte that is most likely to be this - which is suggestive but
     * not yet measured, so it does not get to pick the default.
     */
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "bendRange", 1 },
        "Bend Range",
        1, 12, 2,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return juce::String (v) + (v == 1 ? " semitone" : " semitones"); })));

    /*
     * The two filter trims, which apply to every keygroup of whatever programme is playing.
     *
     * Offsets rather than absolute settings, and centred on zero. A programme carries its
     * own cutoff and envelope amount per keygroup - often quite different ones across the
     * keyboard - and an absolute control would flatten all of that to a single value the
     * moment it was touched. An offset keeps the programme's shape and moves the whole of
     * it, which is what "brighter" means on an instrument like this.
     *
     * They are in the panel's own 0..99 and -50..+50 units, so the numbers on screen are the
     * numbers the machine shows, and the measured cutoff curve is applied after the offset
     * rather than before.
     */
    /*
     * Every trim reads as a signed whole number: "+12", "-40", "0".
     *
     * Said here rather than on the slider, because the host shows these too - in an
     * automation lane, on a control surface, in a generic editor - and a lane reading
     * "12.4179993" is no use to anyone. A slider attachment overwrites whatever the slider
     * itself was told, so the parameter is the only place this actually sticks.
     */
    auto addRange = [&layout] (const char* id, const char* name, float lo, float hi)
    {
        auto asOffset = [] (float v, int)
        {
            return juce::String (v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v));
        };

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float> (lo, hi, 0.0f),
            0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (asOffset)));
    };

    /// Symmetric: an offset that can go either way from what the disk says.
    auto addTrim = [&addRange] (const char* id, const char* name, float span)
    {
        addRange (id, name, -span, span);
    };

    /*
     * "Filter", not "Cutoff" - the S950 has no control by that name.
     *
     * What this shifts is the per-SAMPLE filter value, byte 44 for the soft zone and 66 for
     * the hard one, and a keygroup can hold two different ones: 54% of the library's 168
     * two-zone keygroups set them apart, usually by a few units and sometimes by a lot.
     * Shifting both by the same amount keeps whatever relationship the programmer chose,
     * which is why this is one control rather than two.
     *
     * The parameter ID stays vcfCutoff. Changing it would silently drop the setting out of
     * every session already saved with this plugin in it.
     */
    addTrim ("vcfCutoff", "VCF Filter", 99.0f);

    /*
     * The amount reads -50..+50, which is the range printed on the machine.
     *
     * It briefly reached +-100, on the reasoning that an offset has to cover twice a range
     * to reach either end of it from anywhere - which is true, and is why the envelope
     * stages do. It is the wrong trade here. Across the library's 250 multi-keygroup
     * programmes the VCF amount is the SAME in every keygroup of every one of them bar a
     * single drum kit, so there is no spread for an offset to preserve: shifting them all
     * by n and setting them all to n differ only in where the knob starts.
     *
     * Which leaves the range free to match the panel, and it should - a control that reads
     * +70 when the machine only goes to 50 is a control that has to be explained.
     *
     *     VCF amount   100% of programmes use one value      cutoff  74%
     *     VCF sustain  100%                                  VCA decay  62%
     *     VCF decay     99%                                  VCA release 56%
     *
     * The one thing given up is inverting a keygroup that already sits at +50, which needs
     * an offset of -100. Thirty-four keygroups in the library are there.
     */
    addTrim ("vcfAmount", "VCF Amnt", 50.0f);

    /*
     * The two envelopes, as offsets on the panel's 0..99 for each stage.
     *
     * A span of 99 either way means a control can reach the whole of its travel whatever
     * the programme started at: a keygroup with a decay of 80 can still be taken to 0, and
     * one at 5 can still be taken to 99. The cost is that the middle of the knob is not the
     * middle of the range, which is the right way round - the middle is "as recorded", and
     * that is the value anyone wants to find again.
     */
    addTrim ("vcaAttack",  "VCA Attack",  99.0f);
    addTrim ("vcaDecay",   "VCA Decay",   99.0f);
    addTrim ("vcaSustain", "VCA Sustain", 99.0f);
    addTrim ("vcaRelease", "VCA Release", 99.0f);

    addTrim ("vcfAttack",  "VCF Attack",  99.0f);
    addTrim ("vcfDecay",   "VCF Decay",   99.0f);
    addTrim ("vcfSustain", "VCF Sustain", 99.0f);
    addTrim ("vcfRelease", "VCF Release", 99.0f);

    /*
     * The LFO, in the same 0..99 the panel uses, and as offsets like everything else here.
     *
     * Unlike the filter and the envelopes, the LFO on this machine is genuinely per-keygroup
     * and programmes do vary it across the keyboard - so an offset is doing real work rather
     * than standing in for an absolute control.
     *
     * Rate is linear in hertz, not exponential: 1.79 Hz at zero and about 0.089 Hz a unit,
     * measured on eight rungs with an r2 of 0.99998. That makes the knob feel unlike an
     * envelope knob, and it should - a unit is a fixed number of hertz wherever you are.
     *
     * Delay is a fade-in rather than a wait: at 0 the wobble is at full depth within a
     * twentieth of a second, and at 99 it climbs for seven and a half. Turning it up on a
     * held note does not restart anything, because the fade keeps its place.
     *
     * THESE THREE ONLY GO UP, 0 TO 99, WHERE THE OTHERS GO BOTH WAYS
     *
     * The envelope and filter trims are symmetric because a programme always has an envelope
     * and always has a cutoff, so there is something to move in either direction. The LFO is
     * different: almost every programme in the library leaves it switched off, at a rate of
     * 0 and a depth of 0. A symmetric knob on those spends its whole lower half asking for
     * less than nothing, and the result clamps at 0 and does not move - half a control doing
     * nothing at all, which is exactly how it felt.
     *
     * So they run 0..99 and add. Zero still means "as the disk has it", every knob here still
     * reads zero until it is touched, and a continuous controller now spreads its 128 steps
     * across the useful range instead of half of them over a dead zone.
     *
     * What this gives up is turning DOWN a programme that already has vibrato dialled in.
     * That is rare enough to be worth the trade, and reaching for it is a keygroup edit.
     */
    addRange ("lfoRate",  "LFO Rate",  0.0f, 99.0f);
    addRange ("lfoDepth", "LFO Depth", 0.0f, 99.0f);
    addRange ("lfoDelay", "LFO Delay", 0.0f, 99.0f);

    /*
     * How hard you play, and what it reaches: the filter's frequency and the loudness.
     *
     * 0..99 and adding, for the same reason as the LFO - these are depths, and a depth below
     * zero is nothing. Across the six-disk library velocity reaches loudness in all 181
     * keygroups and the filter in 95 of them, so unlike the LFO there usually IS something
     * here already; the knob adds sensitivity to whatever the programme chose.
     *
     * Only these two. The machine's keygroup also carries velocity to attack (byte 9) and to
     * release (byte 10), and nothing in the library sets either - 0 in all 181 - so no engine
     * has ever modelled them and there is no measurement of what they would do. Controls for
     * those would be controls over invented behaviour.
     */
    addRange ("velToFilter",   "Vel Freq",     0.0f, 99.0f);
    addRange ("velToLoudness", "Vel Loudness", 0.0f, 99.0f);

    /*
     * Portamento - the one control here the S950 never had. See Engine::glide.
     *
     * Absolute settings rather than offsets, because there is nothing on the disk for them
     * to be offset from. Off by default, so a programme plays as the disk has it until
     * somebody asks for more.
     *
     * The time is in milliseconds rather than a 0..99, since there is no panel value it
     * would be pretending to be. Zero is no glide at all and the top is three seconds, so
     * CC 5 reads 0 as "off" and 127 as the longest swoop. Skewed so a controller's middle
     * lands near half a second: most of the travel goes on the short glides that get
     * played, and the last stretch reaches the long ones.
     */
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "glide", 1 }, "Glide", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "glideTime", 1 },
        "Glide Time",
        juce::NormalisableRange<float> (0.0f, 3000.0f, 0.0f, 0.4f),
        120.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int)
            {
                // Under half a millisecond Voice::start does not glide at all, so say so.
                if (v <= 0.5f) return juce::String ("no glide");

                return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms"
                                   : juce::String (v / 1000.0f, 2) + " s";
            })));

    /*
     * How many voices, 1 to 8 - also not the machine's, which always has eight. One is
     * mono, which is where glide plays like a monosynth's: see Engine::setVoiceLimit.
     */
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "voices", 1 },
        "Polyphony",        // not "Voices": on a synth that reads as unison, which this is not
        1, s950::Engine::Polyphony, s950::Engine::Polyphony,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return v == 1 ? juce::String ("mono") : juce::String (v); })));

    /*
     * Wide - not the S950's either. See Engine::wide. Two voices a note, detuned apart by the
     * same amount either way and spread across the stereo field; four notes at most, being
     * the machine's eight voices two at a time.
     *
     * Detune is per half, so 10 puts the two 20 cents apart. Fifty is a quarter-tone each
     * way and past where anything but an effect wants to be; five to fifteen is a chorus.
     */
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "wide", 1 }, "Wide", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wideDetune", 1 },
        "Wide Detune",
        juce::NormalisableRange<float> (0.0f, 50.0f, 0.0f),
        10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int)
            {
                return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (v, 1) + " ct";
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wideSpread", 1 },
        "Wide Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.0f),
        70.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "wideOffset", 1 }, "Wide Offset", true));

    return layout;
}

// ---------------------------------------------------------------------- the placeholder

s950::PatchPtr VirtualS950Processor::makePlaceholderPatch()
{
    auto sound = std::make_shared<s950::Sound>();
    sound->name       = "SAW";
    sound->sourceRate = 48000;
    sound->rootPitch  = 60.0;

    /*
     * Eight cycles rather than one, so the loop length is a whole number of samples AND a
     * whole number of cycles. 48000 / 261.626 is 183.49 samples for middle C: rounding that
     * to 183 would put the placeholder five cents flat, where eight cycles rounded to 1468
     * is out by three hundredths of one.
     */
    const int cycles = 8;
    const int words  = 1468;

    sound->audio.resize (static_cast<size_t> (words));

    for (int i = 0; i < words; ++i)
    {
        const double phase = std::fmod (i * cycles / static_cast<double> (words), 1.0);
        sound->audio[static_cast<size_t> (i)] = static_cast<float> (phase * 2.0 - 1.0);
    }

    sound->loops    = true;
    sound->loopFrom = 0;
    sound->loopTo   = words;

    auto p = std::make_shared<s950::Patch>();
    p->name = "Placeholder saw";

    s950::KeygroupPatch kg;
    kg.lowKey        = 0;
    kg.highKey       = 127;
    kg.keygroupIndex = 0;
    kg.sound         = sound;

    kg.vcaAttack  = 0;
    kg.vcaDecay   = 0;
    kg.vcaSustain = 99;
    kg.vcaRelease = 25;

    kg.zoneFilter  = 70;        // something to hear the filter doing its job
    kg.keyToFilter = 50;        // measured: 50 is one-for-one tracking
    kg.lfoDesync   = true;

    p->keygroups.push_back (kg);
    return p;
}

// ------------------------------------------------------------------------ the plugin

VirtualS950Processor::VirtualS950Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output",
                                                    juce::AudioChannelSet::stereo(),
                                                    true)),
      parameters (*this, nullptr, "state", describeParameters())
{
    gainParameter = parameters.getRawParameterValue ("gain");
    bendRangeParameter = parameters.getRawParameterValue ("bendRange");

    glideParameter     = parameters.getRawParameterValue ("glide");
    glideTimeParameter = parameters.getRawParameterValue ("glideTime");
    glideControl       = parameters.getParameter ("glide");
    glideTimeControl   = parameters.getParameter ("glideTime");
    jassert (glideParameter != nullptr && glideTimeParameter != nullptr);

    voicesParameter = parameters.getRawParameterValue ("voices");
    voicesControl   = parameters.getParameter ("voices");
    jassert (voicesParameter != nullptr && voicesControl != nullptr);

    wideParameter       = parameters.getRawParameterValue ("wide");
    wideDetuneParameter = parameters.getRawParameterValue ("wideDetune");
    wideSpreadParameter = parameters.getRawParameterValue ("wideSpread");
    wideOffsetParameter = parameters.getRawParameterValue ("wideOffset");
    wideControl         = parameters.getParameter ("wide");
    wideDetuneControl   = parameters.getParameter ("wideDetune");
    wideSpreadControl   = parameters.getParameter ("wideSpread");
    wideOffsetControl   = parameters.getParameter ("wideOffset");
    jassert (wideParameter != nullptr && wideDetuneParameter != nullptr
             && wideSpreadParameter != nullptr && wideOffsetParameter != nullptr);

    using AT = s950::Engine::AtomicTrims;

    trimControls[0] = {  74, "vcfCutoff",  &AT::cutoff     };
    trimControls[1] = {  70, "vcfAmount",  &AT::amount     };
    trimControls[2] = {  73, "vcaAttack",  &AT::vcaAttack  };
    trimControls[3] = {  75, "vcaDecay",   &AT::vcaDecay   };
    trimControls[4] = {  79, "vcaSustain", &AT::vcaSustain };
    trimControls[5] = {  72, "vcaRelease", &AT::vcaRelease };
    trimControls[6] = { 102, "vcfAttack",  &AT::vcfAttack  };
    trimControls[7] = { 103, "vcfDecay",   &AT::vcfDecay   };
    trimControls[8] = { 104, "vcfSustain", &AT::vcfSustain };
    trimControls[9] = { 105, "vcfRelease", &AT::vcfRelease };

    // 76, 77 and 78 are the General MIDI sound controllers for vibrato rate, depth and
    // delay, which is exactly what these are.
    trimControls[10] = { 76, "lfoRate",  &AT::lfoRate  };
    trimControls[11] = { 77, "lfoDepth", &AT::lfoDepth };
    trimControls[12] = { 78, "lfoDelay", &AT::lfoDelay };

    // 109 and 112 are undefined controllers, taken for velocity sensitivity - GM has no
    // numbers for it, the same reason 102-105 were taken for the filter envelope.
    trimControls[13] = { 109, "velToFilter",   &AT::velToFilter   };
    trimControls[14] = { 112, "velToLoudness", &AT::velToLoudness };

    for (auto& t : trimControls)
    {
        t.value   = parameters.getRawParameterValue (t.id);
        t.control = parameters.getParameter (t.id);

        // A row naming a parameter that does not exist is a control that does nothing, and
        // it would do nothing quietly. Better to know here than to wonder later.
        jassert (t.value != nullptr && t.control != nullptr);
    }

    patch = makePlaceholderPatch();

    /*
     * The engine hands a replaced programme back to be freed, and it must be freed on this
     * thread rather than in the audio callback. Nothing else would ever ask.
     */
    startTimer (500);
}

VirtualS950Processor::~VirtualS950Processor()
{
    stopTimer();
}

void VirtualS950Processor::timerCallback()
{
    if (engine != nullptr)
        engine->collectRetiredPatch();

    /*
     * The host chose one of the empty slots. Saying what is current instead puts its chooser
     * back on the programme that is actually sounding. See setCurrentProgram for why this
     * waits for the timer rather than answering on the spot.
     */
    if (hostProgramOutOfStep.exchange (false, std::memory_order_relaxed))
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails {}.withProgramChanged (true));
}

void VirtualS950Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    /*
     * Rebuilt rather than retuned: a voice works out its pitch, its filter ceiling and its
     * LFO step from the rate it was started at, so there is no honest way to change the rate
     * under a sounding note. The host only calls this while stopped.
     */
    engine = std::make_unique<s950::Engine> (sampleRate);
    engine->setPatch (patch);

    // A new engine starts with glide off, whatever the parameter says; this makes the next
    // block tell it.
    glidePosted  = -1;
    voicesPosted = -1;
    widePosted   = -1;

    // processBlock must not allocate, so the scratch buffer is sized here. A little over,
    // because some hosts hand over a longer block than they promised.
    // Two channels: a keygroup can be sent to LEFT or RIGHT, so the engine fills both.
    mono.setSize (2, juce::jmax (samplesPerBlock, 1024), false, true, true);
}

bool VirtualS950Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

double VirtualS950Processor::getTailLengthSeconds() const
{
    // The longest release the machine can be told to do, so a host does not cut a note off.
    return s950::cal::envSeconds (99);
}

/*
 * A controller moving a parameter.
 *
 * setValueNotifyingHost rather than writing the value straight into the engine, so that the
 * knob in the window follows the controller, the host sees the move for automation, and
 * there is one control with three ways in rather than three that disagree.
 *
 * The value is normalised, 0..1, which is what the host's side of a parameter always is -
 * so 0 is the bottom of the range, 127 the top, and a controller's centre detent at 64 lands
 * within a step of the middle, where the trim is zero and the programme plays as written.
 *
 * This runs on the audio thread. It is what every plugin with hard-wired CCs does, and it
 * allocates nothing; the host takes the value and gets out of the way.
 */
void VirtualS950Processor::applyController (juce::RangedAudioParameter* p, int value)
{
    if (p == nullptr)
        return;

    const float normalised = juce::jlimit (0.0f, 1.0f, value / 127.0f);

    if (p->getValue() != normalised)
        p->setValueNotifyingHost (normalised);
}

/// A switch moved by a controller: set only if it differs, so a held pedal does not spam.
void VirtualS950Processor::applySwitch (juce::RangedAudioParameter* p, bool on)
{
    if (p != nullptr && (p->getValue() >= 0.5f) != on)
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

void VirtualS950Processor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int count = buffer.getNumSamples();
    buffer.clear();

    if (engine == nullptr || count <= 0)
        return;

    engine->gain.store (gainParameter != nullptr ? gainParameter->load() : 0.7f,
                        std::memory_order_relaxed);

    // Before the messages, so a bend arriving in this block is read against the range the
    // host has set for it rather than the one from last time.
    engine->bendRange.store (bendRangeParameter != nullptr ? bendRangeParameter->load() : 2.0f,
                             std::memory_order_relaxed);

    /*
     * Glide's switch, from the parameter - which is where the button, the host's automation
     * and a restored session all arrive. Posted at the start of the block and only when it
     * has changed, so it goes ahead of this block's notes and costs nothing when idle. CC 65
     * below posts its own, at the sample it arrived on.
     */
    {
        const int wanted = (glideParameter != nullptr && glideParameter->load() >= 0.5f) ? 1 : 0;

        if (wanted != glidePosted)
        {
            engine->glide (wanted != 0, 0);
            glidePosted = wanted;
        }
    }

    // The voice count, the same way and for the same reasons. CC 106 posts its own below.
    {
        const int wanted = voicesParameter != nullptr
                             ? juce::roundToInt (voicesParameter->load())
                             : s950::Engine::Polyphony;

        if (wanted != voicesPosted)
        {
            engine->setVoiceLimit (wanted, 0);
            voicesPosted = wanted;
        }
    }

    // Wide's switch, the same way again. CC 107 posts its own below.
    {
        const int wanted = (wideParameter != nullptr && wideParameter->load() >= 0.5f) ? 1 : 0;

        if (wanted != widePosted)
        {
            engine->wide (wanted != 0, 0);
            widePosted = wanted;
        }
    }

    /*
     * Every message, with where in this block it happens.
     *
     * This is what the sample offsets in Engine were added for. JUCE hands each message
     * over with its position already worked out, so passing it on costs nothing and a note
     * lands where the host put it rather than on the block boundary.
     */
    for (const auto meta : midi)
    {
        const auto m  = meta.getMessage();
        const int  at = meta.samplePosition;

        if (m.isNoteOn())
            engine->noteOn (m.getNoteNumber(), m.getVelocity(), at);
        else if (m.isNoteOff())
            engine->noteOff (m.getNoteNumber(), at);
        else if (m.isController() && m.getControllerNumber() == 1)
            engine->modwheel (m.getControllerValue(), at);
        else if (m.isPitchWheel())
            engine->pitchBend (m.getPitchWheelValue(), at);

        // Channel pressure drives keygroup byte 21, the LFO's aftertouch depth. The
        // machine has no polyphonic pressure input, so only the channel message counts.
        else if (m.isChannelPressure())
            engine->aftertouch (m.getChannelPressureValue(), at);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine->allNotesOff (at);

        /*
         * 65 and 5 are General MIDI's own portamento switch and portamento time, so a
         * keyboard or a sequencer with a glide control reaches these with no mapping.
         *
         * The switch goes to the engine at its own sample, AND moves the parameter so the
         * button follows and the host can record it. glidePosted is set here too, so the top
         * of the next block sees the parameter already agrees and posts nothing twice.
         */
        else if (m.isController() && m.getControllerNumber() == 65)
        {
            const bool on = m.getControllerValue() >= 64;

            engine->glide (on, at);
            glidePosted = on ? 1 : 0;

            if (glideControl != nullptr && (glideControl->getValue() >= 0.5f) != on)
                glideControl->setValueNotifyingHost (on ? 1.0f : 0.0f);
        }
        else if (m.isController() && m.getControllerNumber() == 5)
            applyController (glideTimeControl, m.getControllerValue());

        /*
         * 106, undefined in General MIDI, for the voice count. Eight equal bands of sixteen
         * - 0-15 is mono, 112-127 all eight - rather than the parameter's own rounding, which
         * would give the two ends half the width of the rest.
         */
        else if (m.isController() && m.getControllerNumber() == 106)
        {
            const int count = 1 + m.getControllerValue() * s950::Engine::Polyphony / 128;

            engine->setVoiceLimit (count, at);
            voicesPosted = count;

            if (voicesControl != nullptr)
            {
                const float normalised = voicesControl->convertTo0to1 (static_cast<float> (count));

                if (voicesControl->getValue() != normalised)
                    voicesControl->setValueNotifyingHost (normalised);
            }
        }

        /*
         * Wide: 107 switches it, 108 is the detune, 110 the spread, 111 the start offset -
         * all undefined in General MIDI. 109 is velocity-to-filter already, hence the gap.
         */
        else if (m.isController() && m.getControllerNumber() == 107)
        {
            const bool on = m.getControllerValue() >= 64;

            engine->wide (on, at);
            widePosted = on ? 1 : 0;
            applySwitch (wideControl, on);
        }
        else if (m.isController() && m.getControllerNumber() == 108)
            applyController (wideDetuneControl, m.getControllerValue());
        else if (m.isController() && m.getControllerNumber() == 110)
            applyController (wideSpreadControl, m.getControllerValue());
        else if (m.isController() && m.getControllerNumber() == 111)
            applySwitch (wideOffsetControl, m.getControllerValue() >= 64);
        else if (m.isController())
        {
            for (const auto& t : trimControls)
                if (t.cc == m.getControllerNumber())
                {
                    applyController (t.control, m.getControllerValue());
                    break;
                }
        }
    }

    /*
     * The trims, after the messages rather than before.
     *
     * The controllers move these parameters in the loop above, and the engine reads them as
     * it renders - which is below. Storing them first would have every controller move heard
     * a block late.
     *
     * A block is the resolution, where notes get sample-accurate placement. That is the
     * right trade: a note in the wrong place is heard as bad timing, whereas a knob arriving
     * 5 ms late is not heard at all, and threading offsets through would mean a second event
     * queue for something nobody could detect.
     */
    for (const auto& t : trimControls)
        if (t.value != nullptr)
            (engine->trims.*(t.target)).store (t.value->load(), std::memory_order_relaxed);

    // After the messages for the same reason as the trims: CC 5 in this block is heard in it.
    if (glideTimeParameter != nullptr)
        engine->glideSeconds.store (glideTimeParameter->load() / 1000.0,
                                    std::memory_order_relaxed);

    // And wide's three continuous settings, likewise.
    if (wideDetuneParameter != nullptr)
        engine->wideCents.store (wideDetuneParameter->load(), std::memory_order_relaxed);
    if (wideSpreadParameter != nullptr)
        engine->wideSpread.store (wideSpreadParameter->load() / 100.0, std::memory_order_relaxed);
    if (wideOffsetParameter != nullptr)
        engine->wideOffset.store (wideOffsetParameter->load() >= 0.5f, std::memory_order_relaxed);

    /*
     * Two channels, because a keygroup can be sent to LEFT or RIGHT.
     *
     * This used to render once and copy the same signal to both, which is right for almost
     * every programme - the machine is mono through its main pair unless a keygroup says
     * otherwise. 38 keygroups across four library programmes do say otherwise: TUBULAR 2
     * spreads its bells L L L L R R R R and came out dead centre here.
     *
     * The scratch buffer is two channels now and the engine fills both. A host that hands us
     * one channel still works: it gets the left side, which is what a mono host would get out
     * of the machine's left socket.
     */
    if (mono.getNumSamples() < count || mono.getNumChannels() < 2)
        mono.setSize (2, count, false, true, true);     // only if a host broke its promise

    float* leftScratch  = mono.getWritePointer (0);
    float* rightScratch = mono.getWritePointer (1);
    engine->render (leftScratch, rightScratch, count);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, ch == 1 ? rightScratch : leftScratch, count);
}

// ------------------------------------------------------------------------- the state

/*
 * WHAT A SAVED PROJECT CARRIES
 *
 * The whole disk, not a path to it.
 *
 * A path is smaller and it is what most plugins store, and it breaks: the library gets
 * moved or renamed, the project goes to somebody else, the drive letter changes, and the
 * song opens silent. For a sampler that is worse than for most plugins, because the sound
 * IS the disk - there is nothing to fall back on.
 *
 * An S950 floppy is 800 x 1024 bytes. Compressed and encoded it comes to a few hundred
 * kilobytes inside the project, which is nothing beside the audio a session already holds,
 * and it means a saved song can never lose the sound it was made with - on this machine or
 * any other.
 *
 * The sectors are stored rather than the .hfe they may have arrived in: decoding is
 * deterministic and one way, so keeping the result means reopening does no MFM work and
 * cannot come out differently from the day it was saved.
 */
void VirtualS950Processor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    auto xml   = state.createXml();

    if (xml == nullptr)
        return;

    if (disk != nullptr && ! programNames.isEmpty())
    {
        auto* node = xml->createNewChildElement ("DISK");

        node->setAttribute ("name",    juce::String (disk->getName()));
        node->setAttribute ("path",    diskPath);
        node->setAttribute ("program", selectedProgram);

        // Also by name: if a disk is ever replaced by an edited version with the
        // programmes in a different order, the name is what the musician meant.
        if (selectedProgram >= 0 && selectedProgram < programNames.size())
            node->setAttribute ("programName", programNames[selectedProgram]);

        const auto& image = disk->getImage();

        juce::MemoryOutputStream packed;
        {
            juce::GZIPCompressorOutputStream zip (packed, 9);
            zip.write (image.data(), image.size());
        }

        node->setAttribute ("bytes",  static_cast<int> (image.size()));
        node->addTextElement (juce::Base64::toBase64 (packed.getData(), packed.getDataSize()));
    }

    copyXmlToBinary (*xml, destination);
}

void VirtualS950Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    /*
     * The disk rides as a child of the parameter tree, so what is wanted is copied out and
     * the element taken away before the rest is handed to the APVTS - which knows nothing
     * about it and would only carry it around inside the parameter state for ever.
     */
    juce::String diskName, savedPath, wantedProgram, encoded;
    int  savedIndex = 0;
    bool haveDisk   = false;

    if (auto* found = xml->getChildByName ("DISK"))
    {
        diskName      = found->getStringAttribute ("name");
        savedPath     = found->getStringAttribute ("path");
        wantedProgram = found->getStringAttribute ("programName");
        savedIndex    = found->getIntAttribute ("program", 0);
        encoded       = found->getAllSubText().trim();
        haveDisk      = true;

        xml->removeChildElement (found, true);
    }

    parameters.replaceState (juce::ValueTree::fromXml (*xml));

    if (! haveDisk || encoded.isEmpty())
        return;

    juce::MemoryOutputStream packed;
    if (! juce::Base64::convertFromBase64 (packed, encoded))
        return;

    juce::MemoryInputStream          source (packed.getData(), packed.getDataSize(), false);
    juce::GZIPDecompressorInputStream unzip (source);

    juce::MemoryOutputStream sectors;
    sectors.writeFromInputStream (unzip, -1);

    if (sectors.getDataSize() == 0)
        return;

    const auto* first = static_cast<const unsigned char*> (sectors.getData());
    std::vector<unsigned char> bytes (first, first + sectors.getDataSize());

    auto restored = std::make_unique<s950::Disk>();
    std::string why;

    if (! restored->loadBytes (diskName.toStdString(), std::move (bytes), why))
        return;

    juce::String error;
    if (! adoptDisk (std::move (restored), error))
        return;

    diskPath = savedPath;

    /*
     * Back to the programme that was playing.
     *
     * By name first: a disk edited since the song was saved can have its programmes in a
     * different order, and the name is what was meant. The index is the fallback, for a
     * programme that has since been renamed.
     */
    const int index = programNames.indexOf (wantedProgram);

    selectProgram (index >= 0 ? index : savedIndex);
}

// ---------------------------------------------------------------------------- disks

bool VirtualS950Processor::loadDisk (const juce::File& file, juce::String& error)
{
    auto opened = std::make_unique<s950::Disk>();

    std::string why;
    if (! opened->loadFile (file.getFullPathName().toStdString(), why))
    {
        error = juce::String (why);
        return false;
    }

    if (! adoptDisk (std::move (opened), error))
        return false;

    diskPath = file.getFullPathName();
    return true;
}

bool VirtualS950Processor::adoptDisk (std::unique_ptr<s950::Disk> opened, juce::String& error)
{
    if (opened == nullptr) { error = "no disk"; return false; }

    juce::StringArray names;
    for (const auto& e : opened->getEntries())
        if (e.type == 'P')
            names.add (juce::String (e.name));

    if (names.isEmpty())
    {
        error = "that disk has no programmes on it";
        return false;
    }

    disk            = std::move (opened);
    diskPath        = {};
    programNames    = names;
    selectedProgram = -1;

    selectProgram (0);
    diskGeneration.fetch_add (1, std::memory_order_relaxed);

    /*
     * A new disk is a new set of names on the same sixty-four slots.
     *
     * The count cannot change - see getNumPrograms for why it must not - so there is no new
     * parameter for the host to find. What it does need telling is that the names it last
     * read are stale: parameterInfoChanged is what makes it read them again, and
     * programChanged says which slot is now current.
     */
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails {}
                           .withProgramChanged (true)
                           .withParameterInfoChanged (true));

    return true;
}

juce::StringArray VirtualS950Processor::getProgramNames() const
{
    return programNames;
}

// --------------------------------------------------- the disk's programmes, as the host's

/*
 * The whole directory, always, whether a disk is loaded or not.
 *
 * This has to be a constant, and the reason is in the VST3 wrapper. The program parameter
 * the host drives is built once, while the plugin is being constructed, and only if this
 * returns more than one. A count that starts at one and grows when a disk is loaded is
 * therefore never seen: the parameter was never made, and no amount of telling the host
 * afterwards can make one. That is why the programmes only ever appeared in this window.
 *
 * The names stay live - the wrapper calls getProgramName for a slot every time it draws it -
 * so a fixed set of slots wearing changing names is exactly the shape a host wants.
 *
 * Sixty-four is the format's own limit rather than a number picked for being large: block 0
 * holds a 64-entry directory, so no disk can have more programmes than there are slots.
 */
int VirtualS950Processor::getNumPrograms()
{
    return s950::Disk::DirEntries;
}

int VirtualS950Processor::getCurrentProgram()
{
    return juce::jmax (0, selectedProgram);
}

void VirtualS950Processor::setCurrentProgram (int index)
{
    /*
     * The host can land on any of the sixty-four, including the empty ones past the end of a
     * small disk. selectProgram rightly ignores those - there is nothing to play - but that
     * would leave the host's chooser naming a slot that is not sounding, so have the timer
     * tell it what is and it snaps back.
     *
     * Not from here, though. This is the host's own call into the plugin, and answering
     * inside it means editing a parameter while the host is still in the middle of setting
     * it - which is a re-entrancy worth stepping around rather than finding out about.
     */
    if (! juce::isPositiveAndBelow (index, programNames.size()))
    {
        hostProgramOutOfStep.store (true, std::memory_order_relaxed);
        return;
    }

    const juce::ScopedValueSetter<bool> choosing (hostIsChoosing, true);
    selectProgram (index);
}

const juce::String VirtualS950Processor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, programNames.size()))
        return programNames[index];

    /*
     * A slot past the end of this disk. There are always sixty-four and most disks fill a
     * dozen, so the rest have to read as empty rather than as nothing: a host draws an empty
     * name as a blank row, which looks like a fault rather than like a free slot.
     */
    if (programNames.isEmpty())
        return index == 0 ? "Placeholder saw" : "- no disk -";

    return "-";
}

juce::String VirtualS950Processor::getDiskName() const
{
    return disk != nullptr ? juce::String (disk->getName()) : juce::String();
}

int VirtualS950Processor::getBadSectors() const
{
    return disk != nullptr ? disk->getBadCrcSectors() : 0;
}

int VirtualS950Processor::getMissingSectors() const
{
    return disk != nullptr ? disk->getMissingSectors() : 0;
}

void VirtualS950Processor::selectProgram (int index)
{
    if (disk == nullptr || index < 0 || index >= programNames.size())
        return;

    // The nth programme in directory order, which is the order getProgramNames built.
    const s950::Disk::Entry* entry = nullptr;
    int seen = 0;

    for (const auto& e : disk->getEntries())
    {
        if (e.type != 'P') continue;
        if (seen++ == index) { entry = &e; break; }
    }

    if (entry == nullptr) return;

    auto built = disk->buildPatch (*entry);
    if (built == nullptr || built->keygroups.empty())
        return;                                    // leave what is playing alone

    selectedProgram = index;
    patch = built;

    if (engine != nullptr)
        engine->setPatch (patch);

    diskGeneration.fetch_add (1, std::memory_order_relaxed);

    /*
     * And tell the host, so its own chooser follows the one in this window rather than
     * disagreeing with it. This is also the gesture Ableton listens for in Configure mode,
     * which is how Program gets onto the device panel without hunting for it in a list two
     * thousand long.
     *
     * Unless the host is where the change came from, in which case it already knows, and
     * saying so from inside its own call is the re-entrancy setCurrentProgram avoids.
     */
    if (! hostIsChoosing)
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails {}.withProgramChanged (true));
}

// ------------------------------------------------------------------- for the editor

int VirtualS950Processor::getActiveVoices() const
{
    return engine != nullptr ? engine->getActiveVoices() : 0;
}

juce::String VirtualS950Processor::getPatchName() const
{
    return patch != nullptr ? juce::String (patch->name) : juce::String ("nothing loaded");
}

VirtualS950Processor::EnvelopeShape VirtualS950Processor::getEnvelope (bool filter) const
{
    EnvelopeShape shape;

    if (patch == nullptr)
        return shape;

    for (const auto& kg : patch->keygroups)
    {
        if (kg.sound == nullptr || kg.sound->audio.empty())
            continue;

        if (filter)
        {
            // A programme with no filter envelope of its own is drawn as the flat one the
            // engine gives it - no attack, no decay, full sustain - so the graph shows what
            // the trims are actually shaping. See Voice::applyTrims.
            shape.written = kg.vcfWritten;
            shape.attack  = kg.vcfWritten ? kg.vcfAttack  : 0;
            shape.decay   = kg.vcfWritten ? kg.vcfDecay   : 0;
            shape.sustain = kg.vcfWritten ? kg.vcfSustain : 99;
            shape.release = kg.vcfWritten ? kg.vcfRelease : 0;
        }
        else
        {
            shape.attack  = kg.vcaAttack;
            shape.decay   = kg.vcaDecay;
            shape.sustain = kg.vcaSustain;
            shape.release = kg.vcaRelease;
        }

        break;
    }

    return shape;
}

juce::AudioProcessorEditor* VirtualS950Processor::createEditor()
{
    return new VirtualS950Editor (*this);
}

// -------------------------------------------------------------------------- the hook

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VirtualS950Processor();
}
