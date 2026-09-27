#pragma once

#include "Disk.h"

#include <map>
#include <string>
#include <vector>

/*
 * A sound designed on the Synth page, and how it becomes a disk.
 *
 * The recipe is a handful of numbers a window can show as knobs. Rendering it is:
 *
 *   - one SAMPLE per oscillator that is on, a few looped cycles drawn from harmonics
 *     (Synth.h), and one per drum that is on, a one-shot;
 *   - one PROGRAMME, with a keygroup per oscillator layered across the keyboard - which
 *     is how a sampler does detune: two layers a few cents apart, not two oscillators in
 *     one loop, since two pitches in one loop would not join - and a keygroup per drum on
 *     its General MIDI note, constant pitch and one-shot;
 *   - all of it on a blank disk, built by Disk::addSample / addProgram, so the result is
 *     an S950 disk like any other: the Program tab edits it, the set saves it, and it goes
 *     to a real machine.
 *
 * WHAT A KNOB ON THE SYNTH PAGE OWNS, AND WHAT THE PROGRAM TAB OWNS
 *
 * Re-rendering rewrites the disk. If that reset every keygroup setting, the Program tab
 * would be a place edits went to die. So render() takes the disk it is replacing and
 * copies each keygroup's settings across from the keygroup playing the same sample - the
 * envelopes, the filter, the LFO, everything - and writes only what the recipe's own knobs
 * own: the zone's tune and level, and for a drum its decay, tone and level. A sound built
 * up over both tabs survives a change to either.
 */
namespace s950::synth
{
    enum class OscKind
    {
        off,
        sine, triangle, saw, square, pulse, organ, glass, buzz, hollow,
        fm1, fm2, fmBell,          // FM at ratios 1, 2 and 7
        ring2, ring3,              // a saw ring-modulated at 2 and 3
        bent, phaseDistort,        // an oscillator wound up; the Casio trick
        noiseWhite, noisePink, noiseBrown,
        count
    };

    const char* oscKindName (OscKind k);

    /// What the two shaping knobs mean for a kind, for the window's labels.
    struct OscKindLabels { const char* intensity; const char* sweep; bool hasIntensity; bool hasSweep; };
    OscKindLabels oscKindLabels (OscKind k);

    struct OscSettings
    {
        OscKind kind   = OscKind::off;
        int level      = 99;     // 0..99, into the zone's loudness trim
        int octave     = 0;      // -2..2
        int fine       = 0;      // cents, -50..50
        int phase      = 0;      // 0..99, where in the cycle the wave starts
        int intensity  = 50;     // 0..99: pulse width, FM index, bend, skew, or a morph toward a sine
        int sweep      = 0;      // 0..99: how much of the intensity travels across the loop and back
    };

    /// The nine drums the page offers, in this order, on General MIDI's notes.
    enum class DrumSlot { kick, snare, clap, hatClosed, hatOpen, ride, tomLo, tomMid, tomHi, count };

    const char* drumSlotName (DrumSlot d);
    int         drumSlotNote (DrumSlot d);

    struct DrumSettings
    {
        bool on    = true;
        int  tune  = 0;      // semitones, -12..12, as a change of the sample's rate
        int  decay = 99;     // the keygroup's VCA decay; 99 plays the whole one-shot
        int  tone  = 99;     // the zone's filter
        int  level = 0;      // -50..50 on top of the kit's own balance
    };

    struct Recipe
    {
        std::string  name = "SYNTH";
        OscSettings  osc[3];
        bool         drumsOn = false;
        DrumSettings drums[static_cast<int> (DrumSlot::count)];

        /*
         * The keygroup settings a fresh render starts from - what a preset carries. Once
         * rendered these belong to the Program tab, and render() keeps whatever it finds.
         */
        int filter = 78, keyTrack = 50, velFilter = 40;
        int a = 0, d = 30, s = 88, r = 28;
        int vcfA = 0, vcfD = 45, vcfS = 40, vcfR = 0, vcfAmount = 12;
        int velLoudness = 30;
        int lfoRate = 45, lfoDepth = 4, lfoDelay = 70;

        /// Where the synth starts when the drums are on: E2, the first key above the ride,
        /// the drums taking C1 to D#2 (36 to 51) below it. Without drums, C0: the bottom
        /// of the machine's keyboard.
        static constexpr int SynthLowKeyWithDrums = 52;
        static constexpr int SynthLowKey          = 24;

        bool anyOscillator() const;
    };

    /// Named starting points.
    struct Preset { const char* name; Recipe recipe; };
    const std::vector<Preset>& presets();

    /*
     * Rendered waves, kept between renders so a knob that does not change a wave - a
     * level, an octave, a drum's decay - costs a repack of the disk and not a resynthesis.
     * Keyed by everything that goes into the wave.
     */
    struct WaveCache
    {
        std::map<std::string, std::vector<short>> waves;
    };

    /// The words for one oscillator, cached.
    const std::vector<short>& oscillatorWave (const OscSettings& o, WaveCache& cache);

    /// The recipe as a disk. `previous` is the disk this replaces, for the settings it keeps.
    bool render (const Recipe& recipe, WaveCache& cache, const Disk* previous, Disk& out, std::string& error);

    /// The recipe as text and back, for the plugin's saved state. One line, key=value pairs.
    std::string toText (const Recipe& r);
    Recipe      fromText (const std::string& text);
}
