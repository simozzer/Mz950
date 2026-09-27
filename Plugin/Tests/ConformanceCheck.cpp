/*
 * Does the port agree with the engine it was ported from?
 *
 * Reference.h holds what the C# computes for a spread of inputs, generated from the working
 * engine by AkaiS950Tests/ReferenceDump.cs. This runs the same inputs through the C++ and
 * insists on the same answers.
 *
 * It exists because of how this port was written: on a machine with no C++ compiler, so not
 * one line of it had ever been run at the point it was committed. Every previous piece of
 * this project was checked by measuring rather than by reading - the LFO against a
 * recording, the engine against the web version, the editor against a rendered screenshot -
 * and a port that was only ever read would be the one thing taken on trust. This is how it
 * stops being taken on trust.
 *
 * Build it with nothing but a compiler:
 *
 *     cl /std:c++17 /EHsc /I..\Source\S950 ConformanceCheck.cpp ..\Source\S950\*.cpp
 *
 * It needs no JUCE, no SDK and no audio device, which is the point - if this fails, nothing
 * built on top of it is worth debugging.
 */

#include "Reference.h"

#include "Cal.h"
#include "Filter.h"
#include "Patch.h"
#include "Engine.h"

#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>

namespace
{
    int failures = 0;
    int checks   = 0;

    bool close (double a, double b, double tolerance)
    {
        const double diff = std::fabs (a - b);
        if (diff <= tolerance) return true;

        // Relative, for the large ones: 16317 Hz does not need to match to a millionth.
        const double scale = std::max (std::fabs (a), std::fabs (b));
        return scale > 0 && diff / scale <= tolerance;
    }

    void check (bool ok, const char* what, double got, double want)
    {
        ++checks;

        if (ok)
            return;

        ++failures;
        std::printf ("  FAIL %-34s got %.6f, want %.6f\n", what, got, want);
    }

    void same (const char* what, double got, double want, double tolerance = 1e-9)
    {
        check (close (got, want, tolerance), what, got, want);
    }

    // ------------------------------------------------------------------ the mappings

    void checkConstants()
    {
        std::printf ("\n  the measured constants\n");

        same ("TopHz",                s950::cal::TopHz,                reference::TopHz);
        same ("MaxPlaybackHz",        s950::cal::MaxPlaybackHz,        reference::MaxPlaybackHz);
        same ("FloorHz",              s950::cal::FloorHz,              reference::FloorHz);
        same ("KeyFull",              s950::cal::KeyFull,              reference::KeyFull);
        same ("VelOctaves",           s950::cal::VelOctaves,           reference::VelOctaves);
        same ("VelPivot",             s950::cal::VelPivot,             reference::VelPivot);
        same ("EnvOctaves",           s950::cal::EnvOctaves,           reference::EnvOctaves);
        same ("VcaAttackSpan",        s950::cal::VcaAttackSpan,        reference::VcaAttackSpan);
        same ("VcfTimeScale",         s950::cal::VcfTimeScale,         reference::VcfTimeScale);
        same ("SustainDb",            s950::cal::SustainDb,            reference::SustainDb);
        same ("LoudnessDbPerUnit",    s950::cal::LoudnessDbPerUnit,    reference::LoudnessDbPerUnit);
        same ("VelDbPerStep",         s950::cal::VelDbPerStep,         reference::VelDbPerStep);
        same ("LfoDepthCentsPerUnit", s950::cal::LfoDepthCentsPerUnit, reference::LfoDepthCentsPerUnit);
        same ("LfoWheelCentsAtFull",  s950::cal::LfoWheelCentsAtFull,  reference::LfoWheelCentsAtFull);
    }

    void checkCutoffs()
    {
        std::printf ("\n  the filter's cutoff, %d points\n",
                     static_cast<int> (std::size (reference::cutoffs)));

        double worst = 0.0;

        for (const auto& c : reference::cutoffs)
        {
            const double got = s950::cal::cutoffHz (c.stored, c.rate);
            worst = std::max (worst, std::fabs (got - c.hz) / std::max (1.0, c.hz));

            char what[64];
            std::snprintf (what, sizeof (what), "cutoff %d at %.0f", c.stored, c.rate);
            same (what, got, c.hz, 1e-9);
        }

        std::printf ("    worst relative difference %.3g\n", worst);

        /*
         * Past the fastest the machine will play a sample, a note drops by octaves until it
         * is under - measured on ESQ BASS 1 (30 kHz) up to note 127, and not a key limit:
         * VLA W VLN (25 kHz) played note 90 as it is. See cal::MaxPlaybackHz.
         */
        std::printf ("\n  the fastest playback, and the octaves past it\n");
        struct { double rate; int semitones, octavesDown; const char* what; } folds[] =
        {
            { 30000, 24, 0, "note 84 of a 30 kHz sample plays as it is" },
            { 30000, 30, 1, "note 90 of it drops an octave" },
            { 30000, 40, 1, "note 100 drops once, to 151 kHz" },
            { 30000, 41, 2, "note 101 drops twice" },
            { 30000, 67, 4, "note 127 drops four times" },
            { 25000, 30, 0, "a 25 kHz sample's note 90 plays as it is" },
        };
        for (const auto& f : folds)
        {
            const double asked = std::pow (2.0, f.semitones / 12.0);
            same (f.what, s950::cal::foldedRatio (f.rate, asked), asked / std::pow (2.0, f.octavesDown), 1e-12);
        }
    }

    void checkEnvelopes()
    {
        std::printf ("\n  the envelope times, %d points\n",
                     static_cast<int> (std::size (reference::envTimes)));

        for (const auto& e : reference::envTimes)
        {
            char what[64];
            std::snprintf (what, sizeof (what), "envSeconds %d", e.stored);
            same (what, s950::cal::envSeconds (e.stored), e.seconds, 1e-9);
        }

        std::printf ("\n  the VCA attack counter, all %d settings\n",
                     static_cast<int> (std::size (reference::vcaAttacks)));

        for (const auto& a : reference::vcaAttacks)
        {
            char what[64];
            std::snprintf (what, sizeof (what), "vcaAttackSeconds %d", a.stored);
            same (what, s950::cal::vcaAttackSeconds (a.stored), a.seconds, 1e-9);
        }

        std::printf ("\n  the pitch wheel, %d combinations\n",
                     static_cast<int> (std::size (reference::bends)));

        for (const auto& b : reference::bends)
        {
            char what[80];
            std::snprintf (what, sizeof (what), "bend %d at range %g", b.wheel, b.range);
            same (what, s950::cal::bendRatio (b.wheel, b.range), b.ratio, 1e-12);
        }

        std::printf ("\n  the sustain plateau, %d settings\n",
                     static_cast<int> (std::size (reference::sustains)));

        for (const auto& p : reference::sustains)
        {
            char what[80];
            std::snprintf (what, sizeof (what), "sustain %d", p.stored);
            same (what, s950::cal::sustainDbFor (p.stored), p.db, 1e-9);
        }

        std::printf ("\n  the amplitude decay, %d combinations\n",
                     static_cast<int> (std::size (reference::decays)));

        for (const auto& d : reference::decays)
        {
            char what[96];
            std::snprintf (what, sizeof (what), "decay %d to sustain %d", d.decay, d.sustain);
            same (what,
                  s950::cal::vcaDecaySeconds (d.decay, s950::cal::sustainDbFor (d.sustain)),
                  d.seconds, 1e-9);
        }

        std::printf ("\n  the filter decay, %d combinations\n",
                     static_cast<int> (std::size (reference::vcfDecays)));

        for (const auto& d : reference::vcfDecays)
        {
            char what[96];
            std::snprintf (what, sizeof (what), "VCF decay %d to sustain %d",
                           d.stored, d.sustain);
            same (what,
                  s950::cal::vcfDecaySeconds (d.stored, d.sustain / 99.0),
                  d.seconds, 1e-9);
        }

        std::printf ("\n  the positional crossfade table, %d points\n",
                     static_cast<int> (std::size (reference::xfadePoints)));

        for (const auto& p : reference::xfadePoints)
        {
            char what[80];
            std::snprintf (what, sizeof (what), "crossfade at x = %.3f", p.x);
            same (what, s950::cal::crossfadeDb (p.x), p.db, 1e-9);
        }

        std::printf ("\n  the positional crossfade, %d cases\n",
                     static_cast<int> (std::size (reference::xfades)));

        for (const auto& x : reference::xfades)
        {
            char what[128];
            std::snprintf (what, sizeof (what),
                           "crossfade note %d, keygroup %d of %d (%d-%d)",
                           x.note, x.self, x.count, x.lows[x.self], x.highs[x.self]);
            same (what,
                  s950::cal::crossfadeGain (x.note, x.lows, x.highs, x.count, x.self),
                  x.gain, 1e-12);
        }

        std::printf ("\n  warp, %d combinations\n",
                     static_cast<int> (std::size (reference::warps)));

        for (const auto& w : reference::warps)
        {
            char what[112];
            std::snprintf (what, sizeof (what),
                           "warp v%d d%d t%d at velocity %d, %.2fs",
                           w.velWarp, w.depth, w.time, static_cast<int> (w.velocity), w.t);
            same (what,
                  s950::cal::warpRatio (w.velWarp, w.depth, w.time, w.velocity, w.t),
                  w.ratio, 1e-9);
        }

        std::printf ("\n  velocity to release, %d combinations\n",
                     static_cast<int> (std::size (reference::velReleases)));

        for (const auto& r : reference::velReleases)
        {
            char what[96];
            std::snprintf (what, sizeof (what), "release %d depth %d at velocity %d, %s",
                           r.stored, r.depth, r.velocity, r.on ? "on" : "off");
            same (what,
                  s950::cal::envSeconds (
                      s950::cal::velocityReleaseByte (r.stored, r.depth, r.velocity, r.on)),
                  r.seconds, 1e-9);
        }

        std::printf ("\n  velocity to attack, %d combinations\n",
                     static_cast<int> (std::size (reference::velAttacks)));

        for (const auto& a : reference::velAttacks)
        {
            char what[80];
            std::snprintf (what, sizeof (what), "attack %d depth %d at velocity %d",
                           a.stored, a.depth, a.velocity);
            same (what,
                  s950::cal::vcaAttackSeconds (
                      s950::cal::velocityAttackByte (a.stored, a.depth, a.velocity)),
                  a.seconds, 1e-9);
        }

        /*
         * And that it really is a counter, which the ladder above would not notice on its
         * own: a port that interpolated smoothly between the same measured points would match
         * every third value and be wrong everywhere else. The property is what matters -
         * whole steps, never going backwards, and stopping at 5.4/2.
         */
        int backwards = 0, shared = 0;
        double last = -1.0;

        for (int v = 0; v <= 99; ++v)
        {
            const double t = s950::cal::vcaAttackSeconds (v);

            if (t < last - 1e-9)                          ++backwards;
            else if (std::abs (t - last) < 1e-9)          ++shared;

            if (t > 0.0)                                  // 0 is the gate, not a ramp
            {
                const double n = s950::cal::VcaAttackSpan / t;
                if (std::abs (n - std::round (n)) > 1e-9)
                    same ("a whole number of steps", n, std::round (n), 1e-9);
            }

            last = t;
        }

        // Attack 0 is a hard gate - see cal::VcaAttackGate. Not measured; it is what the
        // bottom of an attack range means.
        same ("attack 0 is a hard gate", s950::cal::vcaAttackSeconds (0), 0.0, 1e-12);
        check (s950::cal::vcaAttackSeconds (5) > 0.001 &&
               s950::cal::vcaAttackSeconds (30) > 0.2,
               "and the gate does not swallow settings that should ramp",
               s950::cal::vcaAttackSeconds (5), 0.001);

        same ("the attack never shortens as the byte rises", backwards, 0);
        check (shared > 40, "and it steps rather than sliding", shared, 40);
        same ("the slowest attack is the span over two",
              s950::cal::vcaAttackSeconds (99), s950::cal::VcaAttackSpan / 2.0, 1e-9);
    }

    void checkLfo()
    {
        std::printf ("\n  the LFO\n");

        for (const auto& r : reference::lfoRates)
        {
            const double got = s950::cal::LfoRateHzAtZero
                             + r.stored * s950::cal::LfoRateHzPerUnit;

            char what[64];
            std::snprintf (what, sizeof (what), "rate %d", r.stored);
            same (what, got, r.hz, 1e-9);
        }

        for (const auto& f : reference::lfoFades)
        {
            const double got = s950::cal::LfoDelayFadeConstant
                             / std::max (1, 100 - f.stored);

            char what[64];
            std::snprintf (what, sizeof (what), "delay fade %d", f.stored);
            same (what, got, f.seconds, 1e-9);
        }
    }

    // ------------------------------------------------------- the engine, end to end

    /// A sawtooth, so there is something with harmonics for the filter to work on.
    std::shared_ptr<s950::Sound> makeSaw (int words, int rate)
    {
        auto s = std::make_shared<s950::Sound>();
        s->name       = "SAW";
        s->sourceRate = rate;
        s->rootPitch  = 60.0;
        s->audio.resize (static_cast<size_t> (words));

        const int period = 100;
        for (int i = 0; i < words; ++i)
            s->audio[static_cast<size_t> (i)] =
                static_cast<float> ((i % period) / static_cast<double> (period) * 2.0 - 1.0);

        s->loops    = true;
        s->loopFrom = 0;
        s->loopTo   = words;
        return s;
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (float v : x) sum += static_cast<double> (v) * v;
        return x.empty() ? 0.0 : std::sqrt (sum / x.size());
    }

    void checkEngine()
    {
        std::printf ("\n  the engine, driven end to end\n");

        auto patch = std::make_shared<s950::Patch>();
        patch->name = "TEST";

        s950::KeygroupPatch kg;
        kg.lowKey        = 0;
        kg.highKey       = 127;
        kg.keygroupIndex = 0;
        kg.sound         = makeSaw (48000, 48000);
        kg.vcaSustain    = 99;
        kg.zoneFilter    = 99;
        patch->keygroups.push_back (kg);

        s950::Engine engine (48000.0);
        engine.setPatch (patch);

        std::vector<float> buffer (4800);

        // Silence before anything is asked for.
        engine.render (buffer.data(), static_cast<int> (buffer.size()));
        check (rms (buffer) == 0.0, "silent before any note", rms (buffer), 0.0);

        // A note sounds, and the note-on survives the ring.
        engine.noteOn (60, 100);
        engine.render (buffer.data(), static_cast<int> (buffer.size()));

        const double sounding = rms (buffer);
        check (sounding > 0.01, "a note on sounds", sounding, 0.01);
        check (engine.getActiveVoices() == 1, "one voice", engine.getActiveVoices(), 1);

        // Eight at once, and no more.
        for (int n = 0; n < 12; ++n)
            engine.noteOn (48 + n, 100);

        engine.render (buffer.data(), static_cast<int> (buffer.size()));
        check (engine.getActiveVoices() <= s950::Engine::Polyphony,
               "never more than eight voices", engine.getActiveVoices(), s950::Engine::Polyphony);

        // Everything off, and it goes quiet - release is 0, so one block is enough.
        engine.allNotesOff();
        for (int i = 0; i < 20; ++i)
            engine.render (buffer.data(), static_cast<int> (buffer.size()));

        check (rms (buffer) < 1e-4, "all notes off falls silent", rms (buffer), 0.0);

        // The patch hand-off: the audio thread takes it, the message thread frees it.
        engine.setPatch (nullptr);
        engine.render (buffer.data(), static_cast<int> (buffer.size()));
        engine.collectRetiredPatch();

        engine.noteOn (60, 100);
        engine.render (buffer.data(), static_cast<int> (buffer.size()));
        check (rms (buffer) == 0.0, "no patch, no sound", rms (buffer), 0.0);
    }

    /*
     * A note asked for part way through a block has to start there.
     *
     * The thing this is really guarding is a silent one: applying every event at the top of
     * the block still sounds like a working instrument, just one that quantises everything
     * it is sent to the buffer size. At 512 samples that is 11 ms, which nobody hears as a
     * fault - they hear a drum machine that does not quite swing.
     *
     * So the check is where the sound starts, not whether there is any.
     */
    void checkEventTiming()
    {
        std::printf ("\n  when a note starts inside a block\n");

        auto patch = std::make_shared<s950::Patch>();

        s950::KeygroupPatch kg;
        kg.keygroupIndex = 0;
        kg.sound         = makeSaw (48000, 48000);
        kg.zoneFilter    = 99;
        patch->keygroups.push_back (kg);

        const int block = 512;

        for (const int offset : { 0, 1, 100, 200, 411, 511 })
        {
            s950::Engine engine (48000.0);
            engine.gain.store (0.25f);          // well clear of clipping, so a peak means something
            engine.setPatch (patch);

            /*
             * Two blocks, not one.
             *
             * A note has an attack even when its attack byte is zero - the shortest the
             * machine does is 1.68 ms, which is 80 samples at 48 kHz - and it starts at
             * silence and climbs. So a note placed at sample 511 of a 512-sample block has
             * exactly one sample in which to be audible, and in that one sample it is not:
             * its gain is still about a hundredth of the way up and the filter has not moved
             * off zero. Rendering the block after it as well is what makes the question
             * answerable at all, and costs nothing.
             */
            std::vector<float> buffer (static_cast<size_t> (block) * 2);
            engine.noteOn (60, 127, offset);
            engine.render (buffer.data(), block);
            engine.render (buffer.data() + block, block);

            // The first sample that is not silence. Before the note there is nothing at all
            // in the buffer - it is memset and no voice is running - so this cannot be early.
            int first = -1;
            for (int i = 0; i < static_cast<int> (buffer.size()); ++i)
            {
                if (std::fabs (static_cast<double> (buffer[static_cast<size_t> (i)])) > 1e-7)
                {
                    first = i;
                    break;
                }
            }

            char what[64];
            std::snprintf (what, sizeof (what), "note at sample %d", offset);

            /*
             * Within a control block of where it was asked for.
             *
             * Not to the sample: a voice recomputes its modulators every 32 samples and the
             * envelope climbs from nothing, so the first few samples of a note can be too
             * quiet to see. Landing inside one control block is the difference that matters
             * - the failure being guarded against is a whole buffer out.
             */
            const bool ok = first >= offset && first < offset + 64;
            check (ok, what, first, offset);
        }

        // Two notes in one block, each joining where it belongs.
        {
            s950::Engine engine (48000.0);
            engine.gain.store (0.25f);
            engine.setPatch (patch);

            std::vector<float> buffer (static_cast<size_t> (block));
            engine.noteOn (60, 127, 100);
            engine.noteOn (67, 127, 300);
            engine.render (buffer.data(), block);

            check (engine.getActiveVoices() == 2, "two notes in one block",
                   engine.getActiveVoices(), 2);

            /*
             * Louder where both are sounding than where only one is.
             *
             * Measured as energy rather than as a peak: at full gain two sawtooths sum past
             * +-1 and both halves clip to exactly 1.0, which compares equal and says
             * nothing. That is what the first version of this check did.
             */
            double one = 0.0, both = 0.0;
            for (int i = 150; i < 290; ++i) one  += std::pow (buffer[(size_t) i], 2.0);
            for (int i = 350; i < 490; ++i) both += std::pow (buffer[(size_t) i], 2.0);

            one  = std::sqrt (one  / 140.0);
            both = std::sqrt (both / 140.0);

            check (both > one * 1.1, "the second note joins part way through", both, one);
        }
    }

    void checkFilter()
    {
        std::printf ("\n  the filter\n");

        s950::Butterworth f;
        f.setCutoff (1000.0, 48000.0);

        // A steady input settles to a steady output: the sections are normalised to unity
        // at DC, and if a Q or a coefficient is wrong this is where it shows first.
        double y = 0.0;
        for (int i = 0; i < 20000; ++i)
            y = f.process (1.0);

        same ("unity gain at DC", y, 1.0, 1e-6);

        // Well above the cutoff, very little should come through.
        f.reset();
        f.setCutoff (500.0, 48000.0);

        double peak = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            const double x = std::sin (2.0 * 3.14159265358979323846 * 8000.0 * i / 48000.0);
            const double out = f.process (x);
            if (i > 24000) peak = std::max (peak, std::fabs (out));
        }

        check (peak < 0.02, "8 kHz is stopped by a 500 Hz cutoff", peak, 0.02);
    }

    // --------------------------------------------------------------- the player's trims

    /*
     * The two controls that sit on top of a programme: cutoff and envelope amount, applied
     * to every keygroup at once.
     *
     * Nothing else here covers them, because everything else here is the port against the
     * C# and the trims exist only in the plugin. They are checked by what they do to the
     * sound rather than by reading the numbers back: a sawtooth is full of harmonics, so
     * closing the filter takes energy out of it and the level falls. That is the property
     * worth holding - a trim that moved a variable without moving the audio would pass any
     * check that asked the variable.
     */
    void checkTrims()
    {
        std::printf ("\n  the player's filter trims\n");

        auto patchWith = [] (int zoneFilter, int amount, bool vcfWritten)
        {
            auto patch = std::make_shared<s950::Patch>();
            patch->name = "TRIM";

            s950::KeygroupPatch kg;
            kg.lowKey        = 0;
            kg.highKey       = 127;
            kg.keygroupIndex = 0;
            kg.sound         = makeSaw (48000, 48000);
            kg.vcaSustain    = 99;
            kg.zoneFilter    = zoneFilter;
            kg.vcfAmount     = amount;
            kg.vcfWritten    = vcfWritten;
            kg.vcfDecay      = 80;
            kg.vcfSustain    = 0;
            patch->keygroups.push_back (kg);
            return patch;
        };

        // One note held from the start, rendered once, at whatever trims are set.
        auto levelAt = [&] (const s950::PatchPtr& patch, double cutoffTrim, double amountTrim)
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.trims.cutoff.store (static_cast<float> (cutoffTrim));
            engine.trims.amount.store (static_cast<float> (amountTrim));

            std::vector<float> buffer (2400);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), static_cast<int> (buffer.size()));
            return rms (buffer);
        };

        const auto mid = patchWith (60, 0, true);

        const double flat  = levelAt (mid,   0.0, 0.0);
        const double shut  = levelAt (mid, -40.0, 0.0);
        const double open  = levelAt (mid, +39.0, 0.0);

        check (shut < flat * 0.9, "closing the cutoff trim takes energy out", shut, flat);
        check (open > flat,       "opening it puts energy back",              open, flat);

        // Zero has to mean "exactly as the disk says", or the instrument lies about its own
        // programmes the moment the control exists.
        const double again = levelAt (mid, 0.0, 0.0);
        same ("a zero trim changes nothing", again, flat, 1e-12);

        // The trim reaches a note that is ALREADY sounding, which is the whole difference
        // between a control and a setting that waits for the next key.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (mid);

            std::vector<float> buffer (2400);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), static_cast<int> (buffer.size()));
            const double before = rms (buffer);

            engine.trims.cutoff.store (-40.0f);
            engine.render (buffer.data(), static_cast<int> (buffer.size()));
            const double after = rms (buffer);

            check (after < before * 0.9, "a held note follows the trim", after, before);
        }

        /*
         * Amount deepens an envelope the programme has. The envelope here starts open and
         * falls, so a positive amount puts more through in the first moments.
         *
         * Measured from a nearly shut base rather than the mid one, because a keygroup that
         * is already open has barely any room left to be opened further - at stored 60 the
         * same trim moved the level 3%, which is a true result and a poor test.
         */
        const auto low = patchWith (30, 0, true);
        const double amountFlat = levelAt (low, 0.0,  0.0);
        const double amountUp   = levelAt (low, 0.0, 40.0);
        check (amountUp > amountFlat * 1.05, "the amount trim deepens the envelope",
               amountUp, amountFlat);

        /*
         * An S900 programme left the four VCF bytes blank, so it has no filter envelope of
         * its own - and the player can still build one.
         *
         * Two things have to hold at once. Untouched, such a keygroup must sound exactly as
         * it always did, because a programme with no envelope moves its cutoff by nothing.
         * Touched, it must respond, or a whole class of programmes has four controls that do
         * nothing and no way to tell why. The engine gives it a flat envelope - no attack,
         * no decay, full sustain - which satisfies both: an amount of zero is no movement
         * whatever shape it is applied to.
         */
        const auto s900 = patchWith (60, 0, false);

        const double blankFlat  = levelAt (s900, 0.0,  0.0);
        const double writtenSame = levelAt (patchWith (60, 0, true), 0.0, 0.0);
        same ("an unwritten envelope untouched is an envelope that does nothing",
              blankFlat, writtenSame, 1e-12);

        const double blankUp = levelAt (s900, 0.0, 40.0);
        check (blankUp > blankFlat * 1.02, "and the amount trim can still build one",
               blankUp, blankFlat);

        /*
         * A negative amount inverts the envelope: it pulls the cutoff BELOW the keygroup's
         * own setting instead of above it, so the same envelope that would have opened the
         * filter closes it. That is what the signed byte means, and three keygroups in the
         * library use it - all at -50.
         *
         * Measured from a bright base, because the cutoff floor is 311 Hz and inverting a
         * keygroup that already sits near it has nowhere to go. The library's own three do
         * exactly that and move a tenth of an octave.
         */
        const auto bright = patchWith (80, 0, true);
        const double upright  = levelAt (bright, 0.0,   0.0);
        const double inverted = levelAt (bright, 0.0, -40.0);
        check (inverted < upright * 0.9, "a negative amount inverts the envelope",
               inverted, upright);

        /*
         * The control's range is the panel's, -50..+50, so the engine has to hold the sum
         * inside it rather than trusting the caller. A keygroup at +50 taken down by a
         * further 50 lands at 0 - no envelope - and not at some depth off the bottom of what
         * the machine can express.
         */
        /*
         * The control reads -50..+50, the panel's own range, so it has to be able to cancel
         * whatever the keygroup brought - a programme at +15 taken down by 15 is a programme
         * with no filter envelope.
         *
         * From stored 20, because that is where the difference is worth measuring: with the
         * base at the floor the envelope has the whole range above it, and the saw's energy
         * moves properly. From the middle of the range the same change moves a few per cent,
         * because a sawtooth keeps most of its power in the first few harmonics.
         *
         * There is deliberately no check that the sum is clamped at +-50. It is clamped, but
         * the clamp cannot be heard: at 8.3 octaves a full amount runs the cutoff into a stop
         * from any base, so clamped and unclamped land in the same place and a check would
         * pass whether the code did it or not.
         */
        const auto deep = patchWith (20, 15, true);
        const double asWas  = levelAt (deep, 0.0,   0.0);
        const double zeroed = levelAt (deep, 0.0, -15.0);

        check (zeroed < asWas * 0.9, "the trim can cancel a keygroup's own amount",
               zeroed, asWas);

        /*
         * One Filter knob, reaching BOTH samples of a velocity-switched keygroup.
         *
         * A keygroup holds up to two zones - a soft sample and a hard one - each with its own
         * filter value, and 54% of the library's two-zone keygroups set them apart. They are
         * separate voice entries by the time the engine sees them, so the question is whether
         * one control reaches both, and whether it leaves the gap between them alone.
         *
         * It must also change nothing at rest. A control that has not been touched has no
         * business altering a programme.
         */
        {
            auto split = std::make_shared<s950::Patch>();

            s950::KeygroupPatch soft;
            soft.lowKey = 0; soft.highKey = 127; soft.keygroupIndex = 0;
            soft.sound = makeSaw (48000, 48000);
            soft.vcaSustain = 99;
            soft.velocityFrom = 0; soft.velocityTo = 63;
            soft.zoneFilter = 30;

            auto hard = soft;
            hard.velocityFrom = 64; hard.velocityTo = 127;
            hard.zoneFilter = 45;              // the hard sample is brighter, as they often are

            split->keygroups.push_back (soft);
            split->keygroups.push_back (hard);

            auto atVelocity = [&] (int velocity, double filterTrim)
            {
                s950::Engine engine (48000.0);
                engine.setPatch (split);
                engine.trims.cutoff.store (static_cast<float> (filterTrim));

                std::vector<float> buffer (2400);
                engine.noteOn (60, velocity);
                engine.render (buffer.data(), static_cast<int> (buffer.size()));
                return rms (buffer);
            };

            const double softFlat = atVelocity (40, 0.0);
            const double hardFlat = atVelocity (100, 0.0);

            check (hardFlat > softFlat * 1.05,
                   "the hard sample keeps its own brighter filter", hardFlat, softFlat);

            const double softOpen = atVelocity (40, 25.0);
            const double hardOpen = atVelocity (100, 25.0);

            check (softOpen > softFlat * 1.05, "one Filter knob opens the soft sample",
                   softOpen, softFlat);
            check (hardOpen > hardFlat * 1.05, "and the hard one too", hardOpen, hardFlat);

            // untouched, it has to leave both exactly as the disk describes them
            same ("at rest it changes the soft sample not at all",
                  atVelocity (40, 0.0), softFlat, 1e-12);
            same ("nor the hard one", atVelocity (100, 0.0), hardFlat, 1e-12);
        }

        // The stops still hold: a trim cannot open the filter past the top of its travel.
        const double wideOpen = levelAt (patchWith (99, 0, true), 0.0, 0.0);
        const double shoved   = levelAt (patchWith (99, 0, true), 99.0, 0.0);
        same ("the trim cannot open past the stop", shoved, wideOpen, 1e-12);
    }

    // ------------------------------------------------------------ the envelope trims

    /*
     * The four VCA stages and the three VCF ones, as offsets on the programme's own values.
     *
     * Checked by what they do to the shape of a note rather than by reading a variable back,
     * for the same reason as the filter trims: a control that moved a number without moving
     * the audio would pass any test that asked the number.
     */
    void checkEnvelopeTrims()
    {
        std::printf ("\n  the envelope trims\n");

        // A flat gate - instant attack, no decay, full sustain - so anything that changes
        // the shape is the trim and not the programme.
        auto flatGate = []
        {
            auto patch = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
            kg.sound      = makeSaw (48000, 48000);
            kg.vcaAttack  = 0;
            kg.vcaDecay   = 0;
            kg.vcaSustain = 99;
            kg.vcaRelease = 0;
            kg.zoneFilter = 99;
            patch->keygroups.push_back (kg);
            return patch;
        };

        const auto patch = flatGate();

        /// Render one note, with one trim set, and say how loud it was.
        auto withTrim = [&] (std::atomic<float> s950::Engine::AtomicTrims::* field,
                             double value, int samples)
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            (engine.trims.*field).store (static_cast<float> (value));

            std::vector<float> buffer ((size_t) samples);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), samples);
            return rms (buffer);
        };

        using AT = s950::Engine::AtomicTrims;

        // Attack: a long one means the first tenth of a second is quiet.
        const double fast = withTrim (&AT::vcaAttack,  0.0, 4800);
        const double slow = withTrim (&AT::vcaAttack, 70.0, 4800);
        check (slow < fast * 0.5, "an attack trim slows the attack", slow, fast);

        // Sustain: pulling it down takes level out of a gate that otherwise holds flat.
        const double full  = withTrim (&AT::vcaSustain,   0.0, 4800);
        const double lower = withTrim (&AT::vcaSustain, -50.0, 4800);
        check (lower < full * 0.9, "a sustain trim lowers the level", lower, full);

        // Decay: with sustain pulled down, a slower decay takes longer to get there, so
        // more level survives the window.
        {
            s950::Engine quick (48000.0), slowly (48000.0);
            std::vector<float> a (4800), b (4800);

            quick.setPatch (patch);
            quick.trims.vcaSustain.store (-99.0f);
            quick.trims.vcaDecay.store (0.0f);
            quick.noteOn (60, 100);
            quick.render (a.data(), 4800);

            slowly.setPatch (patch);
            slowly.trims.vcaSustain.store (-99.0f);
            slowly.trims.vcaDecay.store (70.0f);
            slowly.noteOn (60, 100);
            slowly.render (b.data(), 4800);

            check (rms (b) > rms (a) * 1.1, "a decay trim slows the decay", rms (b), rms (a));
        }

        // Release: a longer one rings on after the key is let go.
        {
            auto ringing = [&] (double trim)
            {
                s950::Engine engine (48000.0);
                engine.setPatch (patch);
                engine.trims.vcaRelease.store (static_cast<float> (trim));

                std::vector<float> buffer (2400);
                engine.noteOn (60, 100);
                engine.render (buffer.data(), 2400);      // held
                engine.noteOff (60);

                // the machine's note-off latency, before the release begins
                const int latency = static_cast<int> (std::lround (s950::cal::NoteOffLatencySeconds * 48000.0));
                std::vector<float> wait (static_cast<std::size_t> (latency));
                engine.render (wait.data(), latency);

                engine.render (buffer.data(), 2400);      // let go
                return rms (buffer);
            };

            const double curt = ringing (0.0);
            const double rings = ringing (70.0);
            check (rings > curt * 2.0, "a release trim rings on", rings, curt);
        }

        // Nothing moved means nothing changed, which is the promise the whole set makes.
        const double asWritten = withTrim (&AT::vcaAttack, 0.0, 4800);
        same ("every trim at zero plays the disk", asWritten, fast, 1e-12);

        /*
         * The VCF stages reach the filter, not the level. A slower filter decay holds the
         * sweep open for longer, so more of the sawtooth's harmonics survive the window.
         */
        {
            auto swept = [&] (double decayTrim)
            {
                auto p = std::make_shared<s950::Patch>();
                s950::KeygroupPatch kg;
                kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
                kg.sound      = makeSaw (48000, 48000);
                kg.vcaSustain = 99;
                kg.zoneFilter = 20;          // nearly shut, so the envelope has somewhere to go
                kg.vcfAmount  = 40;
                kg.vcfWritten = true;
                kg.vcfDecay   = 10;
                kg.vcfSustain = 0;
                p->keygroups.push_back (kg);

                s950::Engine engine (48000.0);
                engine.setPatch (p);
                engine.trims.vcfDecay.store (static_cast<float> (decayTrim));

                std::vector<float> buffer (9600);
                engine.noteOn (60, 100);
                engine.render (buffer.data(), 9600);
                return rms (buffer);
            };

            const double brief = swept (0.0);
            const double held  = swept (60.0);
            check (held > brief * 1.1, "a VCF decay trim holds the sweep open", held, brief);
        }

        /*
         * The filter's release. Let go of a note with the filter envelope wide open and the
         * cutoff should fall back to the keygroup's own, so what rings out is duller than
         * what was held - which it could not be while there was no release stage at all.
         */
        {
            auto ringing = [&] (int releaseByte)
            {
                auto p = std::make_shared<s950::Patch>();
                s950::KeygroupPatch kg;
                kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
                kg.sound      = makeSaw (48000, 48000);
                kg.vcaSustain = 99;
                kg.vcaRelease = 60;          // long enough to hear the filter close
                kg.zoneFilter = 20;          // nearly shut, so the envelope has somewhere to go
                kg.vcfAmount  = 50;
                kg.vcfWritten = true;
                kg.vcfDecay   = 99;          // still open when the key comes up
                kg.vcfSustain = 99;
                kg.vcfRelease = releaseByte;
                p->keygroups.push_back (kg);

                s950::Engine engine (48000.0);
                engine.setPatch (p);

                std::vector<float> buffer (4800);
                engine.noteOn (60, 100);
                engine.render (buffer.data(), 4800);       // held, bright
                const double bright = rms (buffer);

                engine.noteOff (60);
                engine.render (buffer.data(), 4800);       // let go
                return std::pair<double, double> (bright, rms (buffer));
            };

            const auto instant = ringing (0);
            const auto slow    = ringing (70);

            // With no release time the envelope drops at once, so the tail is darker than
            // one that closes slowly and keeps some brightness on the way down.
            check (slow.second > instant.second * 1.05,
                   "a filter release closes over its own time", slow.second, instant.second);

            // And it does close: the tail is not simply the held sound fading.
            check (instant.second < instant.first,
                   "the filter falls back when the key is let go",
                   instant.second, instant.first);
        }


        /*
         * Letting go can only take energy away.
         *
         * A stored release of 0 drops the cutoff five and a half octaves in about a
         * millisecond, and a sixth-order cascade retuned that hard - keeping the state the
         * old coefficients left it with - can ring instead of closing. Stepped in isolation
         * this filter peaks at 17.85 against a signal of 1.0 when retuned only every 64
         * samples, though the engine at its own 32 has not been made to do it.
         *
         * So this holds the invariant rather than a number: whatever the cascade does on the
         * way down, the note must not get LOUDER for having been let go. It is honest about
         * what it is - a guard that would catch a bad regression, not a reproduction of the
         * measurement above, which was taken on the filter directly and on the web build.
         */
        {
            auto p = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
            kg.sound      = makeSaw (48000, 48000);
            kg.vcaSustain = 99;
            kg.vcaRelease = 70;
            kg.zoneFilter = 20;          // nearly shut, so the fall is as far as it goes
            kg.vcfAmount  = 50;
            kg.vcfWritten = true;
            kg.vcfDecay   = 99;          // still wide open when the key comes up
            kg.vcfSustain = 99;
            kg.vcfRelease = 0;           // and shut in a millisecond
            p->keygroups.push_back (kg);

            s950::Engine engine (48000.0);
            engine.setPatch (p);

            /*
             * Quietly, so the ring has room to show. The master stage hard-clamps at +-1, and
             * a sawtooth wide open already sits there - so at full gain this measures the
             * clamp and reports 1.0 whether the filter rang or not.
             */
            engine.gain.store (0.25f);

            std::vector<float> buffer (4800);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), 4800);

            double held = 0.0;
            for (float v : buffer) held = std::max (held, (double) std::fabs (v));

            engine.noteOff (60);
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            engine.render (buffer.data(), 4800);

            double letGo = 0.0;
            for (float v : buffer) letGo = std::max (letGo, (double) std::fabs (v));

            // Letting go can only take energy away. Anything louder than the held note is the
            // filter ringing, and at a 32-sample block this was three and a half times it.
            check (letGo < held * 1.2, "closing fast does not make the filter ring",
                   letGo, held);
        }
    }

    // ----------------------------------------------------------------- the LFO trims

    /*
     * Rate, depth and delay, checked by what they do to the audio.
     *
     * The LFO bends pitch, and pitch is awkward to assert directly - so each is read as how
     * far the rendered note has been pushed away from the same note with the LFO still. That
     * distance is zero when nothing is asked for, grows with depth, and - this is what makes
     * rate and delay separable - arrives at a different TIME for each of them.
     */
    void checkLfoTrims()
    {
        std::printf ("\n  the LFO trims\n");

        auto patch = []
        {
            auto p = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
            kg.sound      = makeSaw (48000, 48000);
            kg.vcaAttack  = 0;
            kg.vcaDecay   = 0;
            kg.vcaSustain = 99;
            kg.vcaRelease = 0;
            kg.zoneFilter = 99;
            kg.lfoRate = 0; kg.lfoDepth = 0; kg.lfoDelay = 0;
            kg.lfoDesync = true;
            p->keygroups.push_back (kg);
            return p;
        }();

        auto render = [&] (double rate, double depth, double delay, int samples)
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.trims.lfoRate .store (static_cast<float> (rate));
            engine.trims.lfoDepth.store (static_cast<float> (depth));
            engine.trims.lfoDelay.store (static_cast<float> (delay));

            std::vector<float> buffer ((size_t) samples);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), samples);
            return buffer;
        };

        /// How far apart two renders are, in the same units as rms.
        auto apart = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            const size_t n = std::min (a.size(), b.size());
            double sum = 0.0;
            for (size_t i = 0; i < n; ++i) { const double d = a[i] - b[i]; sum += d * d; }
            return n ? std::sqrt (sum / (double) n) : 0.0;
        };

        const auto still = render (0.0, 0.0, 0.0, 24000);

        // Nothing asked for is nothing done, which is the promise every trim here makes.
        same ("every LFO trim at zero plays the disk",
              apart (render (0.0, 0.0, 0.0, 24000), still), 0.0, 1e-12);

        const auto wobbling = render (0.0, 99.0, 0.0, 24000);
        check (apart (wobbling, still) > rms (still) * 0.2,
               "a depth trim bends the pitch", apart (wobbling, still), rms (still) * 0.2);

        /*
         * Rate, read as how soon the bending starts.
         *
         * A faster LFO has travelled further from the middle of its swing by any given early
         * moment, so over the first twentieth of a second it has pushed the note further off.
         * Comparing the whole note instead would say only that two different wobbles differ.
         */
        const std::vector<float> stillEarly (still.begin(), still.begin() + 2400);
        const auto slowEarly = render ( 0.0, 99.0, 0.0, 2400);
        const auto fastEarly = render (99.0, 99.0, 0.0, 2400);

        check (apart (fastEarly, stillEarly) > apart (slowEarly, stillEarly) * 1.5,
               "a rate trim makes it wobble sooner",
               apart (fastEarly, stillEarly), apart (slowEarly, stillEarly) * 1.5);

        /*
         * Delay, which is a fade-in rather than a wait: at the top of its range the depth
         * climbs for seven and a half seconds, so the start of the note is barely bent at all
         * where the same note without it is bent hard.
         */
        const auto faded = render (99.0, 99.0, 99.0, 2400);
        check (apart (faded, stillEarly) < apart (fastEarly, stillEarly) * 0.5,
               "a delay trim holds the wobble off the start",
               apart (faded, stillEarly), apart (fastEarly, stillEarly) * 0.5);
    }

    // ------------------------------------------------------- the velocity sensitivities

    /*
     * How hard you play, and how much of that reaches the filter and the level.
     *
     * Both are depths on something the keygroup already sets, so both are read the same way:
     * play the same programme at two velocities and see how far apart the results are, with
     * the trim down and then up. A depth that works widens that gap.
     *
     * They differ in WHEN they take effect, and the test holds that too. The filter trim
     * reaches a note already sounding, because a filter control you cannot play with is not
     * a control; the loudness trim does not, because how hard a key was struck is settled
     * when it goes down, and no knob reaches back to change it.
     */
    void checkVelocityTrims()
    {
        std::printf ("\n  the velocity sensitivities\n");

        auto patch = []
        {
            auto p = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
            kg.sound      = makeSaw (48000, 48000);
            kg.vcaAttack  = 0;
            kg.vcaDecay   = 0;
            kg.vcaSustain = 99;
            kg.vcaRelease = 0;
            kg.zoneFilter = 60;          // room to move either way
            kg.velToFilter   = 0;
            kg.velToLoudness = 0;
            p->keygroups.push_back (kg);
            return p;
        }();

        auto at = [&] (std::atomic<float> s950::Engine::AtomicTrims::* field,
                       double trim, int velocity)
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            (engine.trims.*field).store (static_cast<float> (trim));

            std::vector<float> buffer (4800);
            engine.noteOn (60, velocity);
            engine.render (buffer.data(), 4800);
            return rms (buffer);
        };

        using AT = s950::Engine::AtomicTrims;

        // Loudness: with no depth a soft note is as loud as a hard one; with depth it is not.
        const double flatSoft = at (&AT::velToLoudness,  0.0,  20);
        const double flatHard = at (&AT::velToLoudness,  0.0, 127);
        const double deepSoft = at (&AT::velToLoudness, 99.0,  20);
        const double deepHard = at (&AT::velToLoudness, 99.0, 127);

        same ("no loudness depth makes velocity irrelevant", flatSoft, flatHard, 1e-9);
        check (deepSoft < deepHard * 0.5,
               "a loudness depth makes a soft note softer", deepSoft, deepHard * 0.5);

        /*
         * Filter: measured through the level of a sawtooth after filtering, which falls as
         * the cutoff closes. Velocity 20 is well below the measured pivot of 65, so depth
         * takes the cutoff DOWN there, where at 127 it takes it up.
         */
        const double dullSoft = at (&AT::velToFilter, 99.0,  20);
        const double brightHard = at (&AT::velToFilter, 99.0, 127);
        check (dullSoft < brightHard * 0.9,
               "a filter depth makes a soft note darker", dullSoft, brightHard * 0.9);

        same ("no filter depth makes velocity irrelevant",
              at (&AT::velToFilter, 0.0, 20), at (&AT::velToFilter, 0.0, 127), 1e-9);

        /*
         * And the difference in when they land. The same note is started untrimmed, then the
         * trim is turned up under it: the filter follows, the loudness does not.
         */
        auto turnedUpMidNote = [&] (std::atomic<float> s950::Engine::AtomicTrims::* field)
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);

            std::vector<float> before (2400), after (2400);
            engine.noteOn (60, 20);
            engine.render (before.data(), 2400);
            if (field != nullptr) (engine.trims.*field).store (99.0f);
            engine.render (after.data(), 2400);

            return rms (before) > 0.0 ? rms (after) / rms (before) : 0.0;
        };

        /*
         * Against a run where nothing is touched, not against 1.
         *
         * The second half of a note is not the same stretch of sawtooth as the first, so
         * even with no control moved the two halves differ slightly in level - here by a
         * quarter of a per cent. Asking for exactly 1 was asking the sample to repeat.
         */
        const double untouched     = turnedUpMidNote (nullptr);
        const double filterMoved   = turnedUpMidNote (&AT::velToFilter);
        const double loudnessMoved = turnedUpMidNote (&AT::velToLoudness);

        check (std::abs (filterMoved - untouched) > 0.1,
               "the filter depth reaches a note already sounding", filterMoved, untouched);
        same ("the loudness depth waits for the next note", loudnessMoved, untouched, 1e-9);
    }

    /*
     * The filter envelope runs on its own clock.
     *
     * It used to be driven by the amplitude envelope's stage timer, which is reset to zero
     * every time that envelope changes stage - so a programme with a slow amplitude decay
     * restarted its filter sweep when the decay ended. Two renders that differ only in the
     * amplitude decay must give the same filter movement, and that is what this holds.
     */
    void checkFilterClock()
    {
        std::printf ("\n  the filter envelope's own clock\n");

        auto sweep = [] (int vcaDecay)
        {
            auto p = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.lowKey = 0; kg.highKey = 127; kg.keygroupIndex = 0;
            kg.sound      = makeSaw (48000, 48000);
            kg.vcaDecay   = vcaDecay;
            kg.vcaSustain = 99;          // no level change, so only the filter can differ
            kg.zoneFilter = 20;
            kg.vcfAmount  = 50;
            kg.vcfWritten = true;
            kg.vcfDecay   = 80;
            kg.vcfSustain = 0;
            p->keygroups.push_back (kg);

            s950::Engine engine (48000.0);
            engine.setPatch (p);

            std::vector<float> buffer (48000);
            engine.noteOn (60, 100);
            engine.render (buffer.data(), 48000);
            return rms (buffer);
        };

        // A sustain of 99 means the amplitude decay changes nothing about the level; only a
        // filter that restarted with it could make these two differ.
        const double quickDecay = sweep (0);
        const double slowDecay  = sweep (70);

        check (std::fabs (quickDecay - slowDecay) < quickDecay * 0.02,
               "the amplitude decay does not restart the filter", slowDecay, quickDecay);
    }

    // --------------------------------------------------------------------- glide

    /// The pitch the newest voice on `note` is sounding at, or -1.
    double pitchOf (const s950::Engine& engine, int note)
    {
        const s950::Voice* newest = nullptr;

        for (int i = 0; i < s950::Engine::Polyphony; ++i)
        {
            const auto& v = engine.getVoice (i);
            if (v.isActive() && v.getNote() == note
                && (newest == nullptr || v.getStartedAt() > newest->getStartedAt()))
                newest = &v;
        }

        return newest != nullptr ? newest->getPitchNow() : -1.0;
    }

    /// Cycles of the saw in a stretch of output: it climbs through zero once a cycle.
    int upwardCrossings (const std::vector<float>& x)
    {
        int n = 0;
        for (size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;

        return n;
    }

    /*
     * Portamento, which is not the machine's and so has no C# number to agree with. These
     * pin down what it was built to do instead - and, first, that switched off it changes
     * nothing, which every check above already shows by passing untouched.
     */
    void checkGlide()
    {
        std::printf ("\n  glide\n");

        auto makePatch = [] (bool constantPitch)
        {
            auto p = std::make_shared<s950::Patch>();
            s950::KeygroupPatch kg;
            kg.keygroupIndex = 0;
            kg.sound         = makeSaw (48000, 48000);     // 480 Hz at note 60
            kg.zoneFilter    = 99;
            kg.vcaSustain    = 99;
            kg.constantPitch = constantPitch;
            p->keygroups.push_back (kg);
            return p;
        };

        auto patch = makePatch (false);
        std::vector<float> buffer;

        auto run = [&buffer] (s950::Engine& e, int samples)
        {
            buffer.assign (static_cast<size_t> (samples), 0.0f);
            e.render (buffer.data(), samples);
        };

        // Off: the second note is where it was struck at once.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.noteOn (48, 127);
            run (engine, 4800);
            engine.noteOn (60, 127);
            run (engine, 32);
            same ("off: no glide", pitchOf (engine, 60), 60.0, 1e-12);
        }

        // On, 100 ms: halfway at 50, arrived by 100, and the first note has nowhere to come from.
        {
            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (patch);
            engine.glide (true);

            engine.noteOn (48, 127);
            run (engine, 32);
            same ("the first note does not glide", pitchOf (engine, 48), 48.0, 1e-12);

            run (engine, 4800);
            engine.noteOn (60, 127);
            run (engine, 2400);
            same ("halfway there at half the time", pitchOf (engine, 60), 54.0, 0.1);

            run (engine, 2432);
            same ("arrived by the glide time", pitchOf (engine, 60), 60.0, 1e-12);
        }

        // The audio, not only the bookkeeping: the first 50 ms of a glide up from an octave
        // below sound about three quarters as many cycles as the note held level.
        auto cyclesIn50ms = [&] (bool glideOn, const s950::PatchPtr& p)
        {
            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (p);
            engine.glide (glideOn);
            engine.noteOn (48, 127);
            run (engine, 4800);
            engine.noteOff (48);
            engine.noteOn (60, 127);
            run (engine, 2400);
            return upwardCrossings (buffer);
        };

        const int level  = cyclesIn50ms (false, patch);
        const int glided = cyclesIn50ms (true,  patch);

        // Straight in semitones from 48 to 54 over the window: 240 * 2^(u/2) Hz averaged over
        // u in 0..1 is 240 * (sqrt 2 - 1) / (ln 2 / 2) = 287 Hz, or 0.60 of 480.
        check (glided <= level * 0.7 && glided >= level * 0.5,
               "a glide is heard coming up to pitch", glided, level * 0.6);

        // A constant-pitch keygroup ignores the key, and so the glide between keys.
        auto drum = makePatch (true);
        check (cyclesIn50ms (true, drum) == cyclesIn50ms (false, drum),
               "a constant-pitch keygroup does not glide",
               cyclesIn50ms (true, drum), cyclesIn50ms (false, drum));

        // Struck mid-glide: the new note starts from where the last had got to.
        {
            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.2);
            engine.setPatch (patch);
            engine.glide (true);

            engine.noteOn (48, 127);
            run (engine, 4800);
            engine.noteOn (72, 127);
            run (engine, 4800);                            // halfway: about 60

            const double reached = pitchOf (engine, 72);
            engine.noteOn (66, 127);
            run (engine, 32);

            same ("a glide carries on from where the last one got to",
                  pitchOf (engine, 66) + (66.0 - reached) * (32.0 / 9600.0),
                  reached, 0.05);
        }

        // Only inside a keygroup. On a split, crossing into the other half is a different
        // sample and a different instrument, and must not slide; within it, it does.
        {
            auto split = std::make_shared<s950::Patch>();

            for (int k = 0; k < 2; ++k)
            {
                s950::KeygroupPatch kg;
                kg.keygroupIndex = k;
                kg.lowKey        = k == 0 ? 0  : 60;
                kg.highKey       = k == 0 ? 59 : 127;
                kg.sound         = makeSaw (48000, 48000);
                kg.zoneFilter    = 99;
                split->keygroups.push_back (kg);
            }

            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (split);
            engine.glide (true);

            engine.noteOn (48, 127);
            run (engine, 4800);
            engine.noteOn (72, 127);
            run (engine, 32);
            same ("across keygroups: no glide", pitchOf (engine, 72), 72.0, 1e-12);

            run (engine, 4800);
            engine.noteOn (67, 127);
            run (engine, 32);
            check (pitchOf (engine, 67) > 71.0, "within one: it glides",
                   pitchOf (engine, 67), 72.0);

            engine.noteOn (50, 127);
            run (engine, 32);
            check (pitchOf (engine, 50) < 49.0, "and the other half remembers its own",
                   pitchOf (engine, 50), 48.0);
        }

        /*
         * Let go mid-glide: the slide stops where it stands, and the next note plays at its
         * own pitch. A release long enough to keep the voice sounding, so there is a pitch
         * left to look at.
         */
        {
            auto ringing = makePatch (false);
            ringing->keygroups[0].vcaRelease = 60;

            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (ringing);
            engine.glide (true);

            engine.noteOn (48, 127);
            run (engine, 4800);
            engine.noteOn (60, 127);
            run (engine, 2400);                            // about 54

            const double reached = pitchOf (engine, 60);
            engine.noteOff (60);
            run (engine, 2400);
            same ("let go mid-glide: it stops there", pitchOf (engine, 60), reached, 1e-12);

            engine.noteOn (66, 127);
            run (engine, 32);
            same ("and the next note is at its own pitch", pitchOf (engine, 66), 66.0, 1e-12);

            // Let go AFTER arriving: a finished note, and the next one still slides from it.
            run (engine, 9600);
            engine.noteOff (66);
            engine.noteOn (72, 127);
            run (engine, 32);
            check (pitchOf (engine, 72) < 71.0, "a glide that had arrived still leads on",
                   pitchOf (engine, 72), 66.0);
        }

        // The switch lands on its own sample, either side of a note in the same block. Posted
        // in time order, which is how a host hands a block's messages over.
        auto glidesWhenSwitchedAt = [&] (int switchAt)
        {
            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (patch);
            engine.glide (true);
            engine.noteOn (48, 127);
            run (engine, 4800);

            if (switchAt < 200) engine.glide (false, switchAt);
            engine.noteOn (60, 127, 200);
            if (switchAt >= 200) engine.glide (false, switchAt);
            run (engine, 512);
            return pitchOf (engine, 60) < 59.9;
        };

        check (! glidesWhenSwitchedAt (100), "switched off before the note: none", 1, 0);
        check (glidesWhenSwitchedAt (300),   "switched off after it: it glides",   0, 1);

        /*
         * The unison that was heard: single detached notes, polyphony above one, a long
         * release. A glide away from a released note takes it over; without glide the tail
         * rings on beside the next note, as the machine's does.
         */
        auto voicesAfterLine = [&] (bool glideOn, bool holdFirst)
        {
            auto ringing = makePatch (false);
            ringing->keygroups[0].vcaRelease = 70;

            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.2);
            engine.setPatch (ringing);
            engine.glide (glideOn);

            engine.noteOn (48, 127);
            run (engine, 4800);
            if (! holdFirst) engine.noteOff (48);
            run (engine, 480);
            engine.noteOn (60, 127);
            run (engine, 960);                             // 20 ms: past the 10 ms hand-over

            return engine.getActiveVoices();
        };

        check (voicesAfterLine (true, false) == 1, "a glide takes over a released note",
               voicesAfterLine (true, false), 1);
        check (voicesAfterLine (false, false) == 2, "without glide its tail rings on",
               voicesAfterLine (false, false), 2);
        check (voicesAfterLine (true, true) == 2, "a note still held is left sounding",
               voicesAfterLine (true, true), 2);

        // ------------------------------------------------------------- the voice count

        std::printf ("\n  the voice count, and mono\n");

        // Three voices: five held keys sound three.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.setVoiceLimit (3);

            for (int n = 60; n < 65; ++n) engine.noteOn (n, 127);
            run (engine, 64);

            check (engine.getActiveVoices() == 3, "a limit of three sounds three",
                   engine.getActiveVoices(), 3);
        }

        // Mono legato: the note moves, gliding, without being struck again.
        {
            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (patch);
            engine.glide (true);
            engine.setVoiceLimit (1);

            engine.noteOn (48, 127);
            run (engine, 4800);
            const long long struck = engine.getVoice (0).getStartedAt();

            engine.noteOn (60, 127);                       // 48 still down
            run (engine, 2400);

            check (engine.getActiveVoices() == 1, "mono: one voice", engine.getActiveVoices(), 1);
            check (engine.getVoice (0).getStartedAt() == struck, "legato: not struck again",
                   static_cast<double> (engine.getVoice (0).getStartedAt()),
                   static_cast<double> (struck));
            same ("legato glides", pitchOf (engine, 60), 54.0, 0.1);

            // Let go of the top key: back to the one still down, gliding, still held.
            run (engine, 4800);
            engine.noteOff (60);
            run (engine, 32);

            check (engine.getVoice (0).getNote() == 48 && engine.getVoice (0).isHeld(),
                   "letting go returns to the held key", engine.getVoice (0).getNote(), 48);
            check (pitchOf (engine, 48) > 59.0, "gliding back down to it",
                   pitchOf (engine, 48), 60.0);

            // And letting go of that one too releases the note.
            engine.noteOff (48);
            run (engine, 32);
            check (! engine.getVoice (0).isHeld(), "the last key up releases it", 1, 0);
        }

        // Mono, detached: a note struck with no key down is struck afresh.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.setVoiceLimit (1);

            engine.noteOn (48, 127);
            run (engine, 480);
            const long long struck = engine.getVoice (0).getStartedAt();

            engine.noteOff (48);
            engine.noteOn (60, 127);
            run (engine, 32);

            check (engine.getVoice (0).getStartedAt() != struck, "detached: struck afresh",
                   static_cast<double> (engine.getVoice (0).getStartedAt()),
                   static_cast<double> (struck + 1));
        }

        // ------------------------------------------------------------------- wide

        std::printf ("\n  wide\n");

        // Every active voice that is half of a pair has its twin active and pointing back.
        auto pairsWhole = [] (const s950::Engine& e)
        {
            for (int i = 0; i < s950::Engine::Polyphony; ++i)
            {
                const auto& v = e.getVoice (i);
                if (! v.isActive() || v.getPartner() < 0) continue;

                const auto& twin = e.getVoice (v.getPartner());
                if (! twin.isActive() || twin.getPartner() != i) return false;
            }
            return true;
        };

        // Flat on the left, sharp on the right, by the same amount: 50 cents either way of
        // 480 Hz is 466.2 and 494.2, counted over a second of each channel.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.wide (true);
            engine.wideCents.store (50.0);
            engine.wideSpread.store (1.0);
            engine.gain.store (0.25f);
            engine.noteOn (60, 127);

            std::vector<float> l (48000), r (48000);
            engine.render (l.data(), r.data(), 48000);

            check (engine.getActiveVoices() == 2, "a wide note is two voices",
                   engine.getActiveVoices(), 2);
            check (std::abs (upwardCrossings (l) - 466) <= 2, "the left half is flat",
                   upwardCrossings (l), 466);
            check (std::abs (upwardCrossings (r) - 494) <= 2, "the right half as far sharp",
                   upwardCrossings (r), 494);
        }

        // As loud as one voice: centred, the pair's power matches a plain note's.
        {
            auto loudness = [&] (bool wideOn)
            {
                s950::Engine engine (48000.0);
                engine.setPatch (patch);
                engine.wide (wideOn);
                engine.wideCents.store (10.0);
                engine.wideSpread.store (0.0);
                engine.gain.store (0.25f);
                engine.noteOn (60, 127);

                std::vector<float> l (96000), r (96000);
                engine.render (l.data(), r.data(), 96000);
                return rms (l);
            };

            const double plain = loudness (false), wide = loudness (true);
            check (std::fabs (wide - plain) < plain * 0.1, "a wide note is as loud as one",
                   wide, plain);
        }

        // Four notes at most, and six held keys never leave half a pair behind.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.wide (true);

            for (int n = 60; n < 66; ++n) { engine.noteOn (n, 127); run (engine, 64); }

            check (engine.getActiveVoices() == 8, "four wide notes fill eight voices",
                   engine.getActiveVoices(), 8);
            check (engine.getNoteLimit() == 4, "wide caps the notes at four",
                   engine.getNoteLimit(), 4);
            check (pairsWhole (engine), "stealing takes both halves", 0, 1);
        }

        // Polyphony below four still rules: three notes, six voices.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.wide (true);
            engine.setVoiceLimit (3);

            for (int n = 60; n < 65; ++n) { engine.noteOn (n, 127); run (engine, 64); }

            check (engine.getActiveVoices() == 6, "three notes wide are six voices",
                   engine.getActiveVoices(), 6);
            check (pairsWhole (engine), "and every pair whole", 0, 1);
        }

        // Mono and wide: legato moves both halves together.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.wide (true);
            engine.setVoiceLimit (1);

            engine.noteOn (48, 127);
            run (engine, 480);
            engine.noteOn (60, 127);
            run (engine, 64);

            check (engine.getActiveVoices() == 2, "mono wide is two voices",
                   engine.getActiveVoices(), 2);
            check (engine.getVoice (0).getNote() == 60 && engine.getVoice (1).getNote() == 60,
                   "legato moves both halves", engine.getVoice (1).getNote(), 60);
        }

        // A drum is not doubled.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (makePatch (true));
            engine.wide (true);
            engine.noteOn (60, 127);
            run (engine, 64);

            check (engine.getActiveVoices() == 1, "a constant-pitch keygroup stays single",
                   engine.getActiveVoices(), 1);
        }

        // ----------------------------------------------------- handing a patch over

        std::printf ("\n  handing a patch over\n");

        /*
         * A note held through a programme change to a DIFFERENT sample keeps sounding on
         * the sample it started with, after the old programme has been freed. It used to
         * keep a pointer into that programme; this renders a good while after the free,
         * which is where freed memory would have shown up.
         */
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.noteOn (60, 127);
            run (engine, 480);
            const auto sounding = engine.getVoice (0).getSound();

            auto other = makePatch (false);              // same shape, a different sample object
            engine.setPatch (other);
            run (engine, 32);                            // taken; the old one is retired
            engine.collectRetiredPatch();                // and freed

            for (int i = 0; i < 20; ++i) run (engine, 4800);
            check (engine.getVoice (0).isActive() && engine.getVoice (0).getSound() == sounding,
                   "a held note outlives the programme it came from", 0, 1);
            check (engine.getActiveVoices() == 1, "and is still the one voice", engine.getActiveVoices(), 1);
        }

        // The same patch object, offered again: a held note keeps its sample and follows
        // the settings - the Synth-tab case, where an unchanged sample is reused by fingerprint.
        {
            s950::Engine engine (48000.0);
            engine.setPatch (patch);
            engine.noteOn (60, 127);
            run (engine, 480);

            auto edited = std::make_shared<s950::Patch> (*patch);      // same sounds, new settings
            edited->keygroups[0].vcaSustain = 5;
            engine.setPatch (edited);
            run (engine, 32);
            engine.collectRetiredPatch();
            run (engine, 48000);

            check (engine.getVoice (0).isActive(), "a held note follows an edit that kept its sample", 0, 1);
        }

        // A second patch is refused until the audio thread has taken the first: writing
        // `pending` while it might be mid-take was the race. Once taken, it goes.
        {
            s950::Engine engine (48000.0);
            check (engine.trySetPatch (patch), "the first patch is taken", 0, 1);
            check (! engine.trySetPatch (makePatch (true)), "a second waits for the first", 1, 0);

            run (engine, 32);                          // the audio thread takes it
            engine.collectRetiredPatch();
            check (engine.trySetPatch (makePatch (true)), "and then goes", 0, 1);
        }

        // ------------------------------------------------------- drums under mono

        std::printf ("\n  drums outside mono and the limit\n");

        {
            // A kit under a lead: a saw across the keyboard and two drums, constant pitch
            // and one-shot, on 36 and 38.
            auto kit = std::make_shared<s950::Patch>();

            s950::KeygroupPatch lead;
            lead.keygroupIndex = 0; lead.lowKey = 52; lead.highKey = 127;
            lead.sound = makeSaw (48000, 48000); lead.zoneFilter = 99;
            kit->keygroups.push_back (lead);

            for (int k = 1; k <= 2; ++k)
            {
                auto hit = makeSaw (4800, 24000);
                hit->loops = false;

                s950::KeygroupPatch d;
                d.keygroupIndex = k; d.lowKey = d.highKey = (k == 1 ? 36 : 38);
                d.sound = hit;
                d.zoneFilter = 99; d.constantPitch = true; d.oneShot = true;
                kit->keygroups.push_back (d);
            }

            s950::Engine engine (48000.0);
            engine.setPatch (kit);
            engine.glideSeconds.store (0.1);
            engine.glide (true);
            engine.setVoiceLimit (1);

            // the lead in voice 0, held
            engine.noteOn (60, 127);
            run (engine, 480);
            check (engine.getVoice (0).getNote() == 60 && engine.getVoice (0).isHeld(),
                   "mono: the lead is in voice 0", engine.getVoice (0).getNote(), 60);

            // a kick and a snare: they play, above the limit, and the lead is untouched
            engine.noteOn (36, 127);
            engine.noteOn (38, 127);
            run (engine, 480);
            check (engine.getActiveVoices() == 3, "two drums sound beside the mono lead",
                   engine.getActiveVoices(), 3);
            check (engine.getVoice (0).getNote() == 60 && engine.getVoice (0).isHeld(),
                   "and did not steal it", engine.getVoice (0).getNote(), 60);

            // the drum keys held down are not "held keys": the next lead note is legato
            // from the lead, not from a drum, and does not glide from a drum's key
            engine.noteOn (67, 127);
            run (engine, 32);
            check (engine.getVoice (0).getNote() == 67, "a lead note moves the lead voice legato",
                   engine.getVoice (0).getNote(), 67);
            check (pitchOf (engine, 67) < 61.0 && pitchOf (engine, 67) > 59.0,
                   "and glides from the lead's key, not a drum's", pitchOf (engine, 67), 60.0);

            // letting go of a drum key changes nothing for the lead
            engine.noteOff (36);
            run (engine, 32);
            check (engine.getVoice (0).getNote() == 67 && engine.getVoice (0).isHeld(),
                   "a drum key up leaves the lead where it is", engine.getVoice (0).getNote(), 67);

            // and letting go of the lead's top key returns to the lead's held key, 60
            engine.noteOff (67);
            run (engine, 32);
            check (engine.getVoice (0).getNote() == 60, "letting go returns to the held LEAD key",
                   engine.getVoice (0).getNote(), 60);
        }

        // --------------------------------------------------- the note-off latency

        std::printf ("\n  the note-off latency\n");

        {
            // A held saw, let go: the key is up at once, the level holds for the latency,
            // and then the release begins. See cal::NoteOffLatencySeconds.
            auto ringing = makePatch (false);
            ringing->keygroups[0].vcaRelease = 30;

            s950::Engine engine (48000.0);
            engine.setPatch (ringing);
            engine.noteOn (60, 127);
            run (engine, 9600);
            const double held = rms (buffer);

            engine.noteOff (60);
            run (engine, 32);
            check (! engine.getVoice (0).isHeld(), "the key is up at once", 1, 0);

            const int latency = static_cast<int> (std::lround (s950::cal::NoteOffLatencySeconds * 48000.0));
            run (engine, latency - 96);                     // just short of the latency
            const double waiting = rms (buffer);
            check (waiting > held * 0.95, "the level holds until the latency is over", waiting, held);

            run (engine, 2400);                             // the release has begun
            check (rms (buffer) < held * 0.8, "and then it falls", rms (buffer), held);
        }

        // All-notes-off is a panic: no latency. Within the first 10 ms - well inside the
        // 15 ms a note-off waits - it is already quieter than the same note left alone.
        {
            auto level = [&] (bool panic)
            {
                s950::Engine engine (48000.0);
                engine.setPatch (patch);
                engine.noteOn (60, 127);
                run (engine, 4800);
                if (panic) engine.allNotesOff();
                run (engine, 480);
                return rms (buffer);
            };

            const double left = level (false), panicked = level (true);
            check (panicked < left * 0.5, "all-notes-off releases at once", panicked, left);
        }

        // ------------------------------------------------ what the window lights up

        std::printf ("\n  keygroup activity, for the window\n");

        {
            auto split = std::make_shared<s950::Patch>();

            for (int k = 0; k < 2; ++k)
            {
                s950::KeygroupPatch kg;
                kg.keygroupIndex = k;
                kg.lowKey        = k == 0 ? 0  : 60;
                kg.highKey       = k == 0 ? 59 : 127;
                kg.sound         = makeSaw (48000, 48000);
                kg.zoneFilter    = 99;
                split->keygroups.push_back (kg);
            }

            s950::Engine engine (48000.0);
            engine.setPatch (split);
            run (engine, 32);

            check (engine.getKeygroupHits (0) == 0 && engine.getKeygroupLastNote (0) == -1,
                   "nothing counted before a note", engine.getKeygroupHits (0), 0);

            engine.noteOn (48, 100);
            run (engine, 32);
            check (engine.getKeygroupHits (0) == 1, "the keygroup that answered counts one",
                   engine.getKeygroupHits (0), 1);
            check (engine.getKeygroupHits (1) == 0, "the other does not",
                   engine.getKeygroupHits (1), 0);
            check (engine.getKeygroupLastNote (0) == 48, "and remembers the key",
                   engine.getKeygroupLastNote (0), 48);

            // Wide doubles the voices, not the count: one note is one hit.
            engine.wide (true);
            engine.noteOn (72, 100);
            run (engine, 32);
            check (engine.getKeygroupHits (1) == 1, "a wide note counts once",
                   engine.getKeygroupHits (1), 1);
            check (engine.getActiveVoices() == 3, "though it is two voices",
                   engine.getActiveVoices(), 3);
        }

        // Mono across a split: a held key in the other keygroup is no reason for legato.
        {
            auto split = std::make_shared<s950::Patch>();

            for (int k = 0; k < 2; ++k)
            {
                s950::KeygroupPatch kg;
                kg.keygroupIndex = k;
                kg.lowKey        = k == 0 ? 0  : 60;
                kg.highKey       = k == 0 ? 59 : 127;
                kg.sound         = makeSaw (48000, 48000);
                kg.zoneFilter    = 99;
                split->keygroups.push_back (kg);
            }

            s950::Engine engine (48000.0);
            engine.glideSeconds.store (0.1);
            engine.setPatch (split);
            engine.glide (true);
            engine.setVoiceLimit (1);

            engine.noteOn (48, 127);
            run (engine, 480);
            engine.noteOn (72, 127);
            run (engine, 32);

            check (engine.getVoice (0).getKeygroupIndex() == 1, "mono across a split: struck",
                   engine.getVoice (0).getKeygroupIndex(), 1);
            same ("and does not glide", pitchOf (engine, 72), 72.0, 1e-12);
        }
    }
}

int main()
{
    std::printf ("\n  the C++ port against the engine it came from\n");

    checkConstants();
    checkCutoffs();
    checkEnvelopes();
    checkLfo();
    checkFilter();
    checkEngine();
    checkEventTiming();
    checkTrims();
    checkEnvelopeTrims();
    checkLfoTrims();
    checkVelocityTrims();
    checkFilterClock();
    checkGlide();

    std::printf ("\n  %d checks, %d failed\n\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
