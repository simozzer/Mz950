#include "SynthPatch.h"
#include "Synth.h"
#include "Cal.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace s950::synth
{
    // ------------------------------------------------------------------ the kinds

    const char* oscKindName (OscKind k)
    {
        switch (k)
        {
            case OscKind::off:          return "Off";
            case OscKind::sine:         return "Sine";
            case OscKind::triangle:     return "Triangle";
            case OscKind::saw:          return "Saw";
            case OscKind::square:       return "Square";
            case OscKind::pulse:        return "Pulse";
            case OscKind::organ:        return "Organ";
            case OscKind::glass:        return "Glass";
            case OscKind::buzz:         return "Buzz";
            case OscKind::hollow:       return "Hollow";
            case OscKind::fm1:          return "FM 1:1";
            case OscKind::fm2:          return "FM 1:2";
            case OscKind::fmBell:       return "FM bell";
            case OscKind::ring2:        return "Ring x2";
            case OscKind::ring3:        return "Ring x3";
            case OscKind::bent:         return "Bent";
            case OscKind::phaseDistort: return "Phase dist";
            case OscKind::noiseWhite:   return "White noise";
            case OscKind::noisePink:    return "Pink noise";
            case OscKind::noiseBrown:   return "Brown noise";
            default:                    return "?";
        }
    }

    OscKindLabels oscKindLabels (OscKind k)
    {
        switch (k)
        {
            case OscKind::off:          return { "", "", false, false };
            case OscKind::pulse:        return { "Width",  "PWM",   true, true };
            case OscKind::fm1:
            case OscKind::fm2:
            case OscKind::fmBell:       return { "Index",  "Sweep", true, true };
            case OscKind::ring2:
            case OscKind::ring3:        return { "Ring",   "Sweep", true, true };
            case OscKind::bent:         return { "Bend",   "Sweep", true, true };
            case OscKind::phaseDistort: return { "Skew",   "Sweep", true, true };
            case OscKind::noiseWhite:
            case OscKind::noisePink:
            case OscKind::noiseBrown:   return { "", "", false, false };
            default:                    return { "Soften", "Sweep", true, true };   // a morph toward a sine
        }
    }

    // ------------------------------------------------------------------ the drums

    namespace
    {
        struct DrumRow { const char* kitName; int note; const char* label; };

        // Which drum of the kit each slot plays, on General MIDI's note for it.
        const DrumRow DrumRows[] =
        {
            { "KICK 1",     36, "Kick"     },
            { "SNARE 1",    38, "Snare"    },
            { "CLAP",       39, "Clap"     },
            { "HAT CLOSED", 42, "Hat cl"   },
            { "HAT OPEN",   46, "Hat op"   },
            { "RIDE",       51, "Ride"     },
            { "TOM LO",     41, "Tom lo"   },
            { "TOM MID",    43, "Tom mid"  },
            { "TOM HI",     45, "Tom hi"   },
        };

        const drums::Drum* kitDrum (const std::vector<drums::Drum>& kit, const char* name)
        {
            for (const auto& d : kit) if (d.name == name) return &d;
            return nullptr;
        }
    }

    const char* drumSlotName (DrumSlot d) { return DrumRows[static_cast<int> (d)].label; }
    int         drumSlotNote (DrumSlot d) { return DrumRows[static_cast<int> (d)].note; }

    bool Recipe::anyOscillator() const
    {
        for (const auto& o : osc) if (o.kind != OscKind::off) return true;
        return false;
    }

    // ------------------------------------------------------------------ the waves

    namespace
    {
        Harmonic plainShape (OscKind k)
        {
            switch (k)
            {
                case OscKind::sine:     return shapes::sine();
                case OscKind::triangle: return shapes::triangle();
                case OscKind::square:   return shapes::square();
                case OscKind::organ:    return shapes::organ();
                case OscKind::glass:    return shapes::glass();
                case OscKind::buzz:     return shapes::buzz();
                case OscKind::hollow:   return shapes::hollow();
                default:                return shapes::saw();
            }
        }

        /*
         * One spectrum of a kind at an intensity 0..1. The sweep is a table of these at
         * intensities from `full` down toward nothing and back, so "sweep" means the same
         * thing whatever the kind: how far the wave travels from what the intensity knob set.
         */
        Spectrum spectrumFor (OscKind k, double x, int h)
        {
            switch (k)
            {
                case OscKind::pulse:        return modulation::pulse (0.5 - 0.45 * x, h);
                case OscKind::fm1:          return modulation::fm (1, 8.0 * x, h);
                case OscKind::fm2:          return modulation::fm (2, 6.0 * x, h);
                case OscKind::fmBell:       return modulation::fm (7, 5.0 * x, h);
                case OscKind::ring2:        return Spectrum::blend (Spectrum::fromShape (shapes::saw(), h),
                                                                    modulation::ring (shapes::saw(), 24, 2, h), x);
                case OscKind::ring3:        return Spectrum::blend (Spectrum::fromShape (shapes::saw(), h),
                                                                    modulation::ring (shapes::saw(), 24, 3, h), x);
                case OscKind::bent:         return modulation::bent (shapes::saw(), 24, 2.5 * x, h);
                case OscKind::phaseDistort: return modulation::phaseDistort (shapes::sine(), 1, 0.5 - 0.44 * x, h);
                default:
                    // the plain shapes soften toward a sine as the intensity rises
                    return Spectrum::blend (Spectrum::fromShape (plainShape (k), h),
                                            Spectrum::fromShape (shapes::sine(), h), x);
            }
        }

        bool isNoise (OscKind k)
        {
            return k == OscKind::noiseWhite || k == OscKind::noisePink || k == OscKind::noiseBrown;
        }

        /// The rate a kind is drawn at: the plain shapes at 40 kHz, anything that sweeps or
        /// is analysed at 20 kHz over more cycles, as the C# tool's library does.
        int rateFor (OscKind k, bool sweeping)
        {
            if (isNoise (k)) return 20000;
            if (sweeping)    return 20000;
            switch (k)
            {
                case OscKind::pulse: case OscKind::fm1: case OscKind::fm2: case OscKind::fmBell:
                case OscKind::ring2: case OscKind::ring3: case OscKind::bent: case OscKind::phaseDistort:
                    return 20000;
                default:
                    return 40000;
            }
        }

        bool sweeps (const OscSettings& o) { return o.sweep > 0 && oscKindLabels (o.kind).hasSweep; }

        /*
         * What an oscillator is made of, as a table of spectra up to harmonic `h`: one for a
         * still wave, a round trip through five for one that sweeps. Shared by the single
         * wave below and the mix, so the two cannot come to mean different things.
         */
        std::vector<Spectrum> tableFor (const OscSettings& o, int h)
        {
            const double x = o.intensity / 99.0;

            if (isNoise (o.kind))
            {
                const auto colour = o.kind == OscKind::noiseWhite ? NoiseColour::white
                                  : o.kind == OscKind::noisePink  ? NoiseColour::pink : NoiseColour::brown;
                return { noise::spectrum (colour, 101, h) };
            }

            if (sweeps (o))
            {
                // From the intensity set down toward nothing, by the sweep amount, and back.
                const double depth = o.sweep / 99.0;
                return waveforms::mirror (waveforms::steps (5, [&] (double t)
                {
                    return spectrumFor (o.kind, x * (1.0 - depth * t), h);
                }));
            }

            return { spectrumFor (o.kind, x, h) };
        }

        std::vector<short> drawWave (const OscSettings& o)
        {
            const bool sweeping = sweeps (o);
            const int  rate     = rateFor (o.kind, sweeping);
            const int  h        = waveforms::highestHarmonic (rate, RootHz);
            const auto table    = tableFor (o, h);

            const int cycles = isNoise (o.kind)            ? 32
                             : sweeping || rate != 40000  ? 96
                             :                              waveforms::PlainCycles;

            std::vector<short> words = waveforms::render (table, cycles, rate, RootHz);

            // The start phase: a looped wave rotated is the same wave started elsewhere.
            if (o.phase > 0 && ! words.empty())
            {
                const auto by = static_cast<std::size_t> (std::llround (o.phase / 99.0 * (double) words.size()))
                              % words.size();
                std::rotate (words.begin(), words.begin() + static_cast<long> (by), words.end());
            }

            return words;
        }

        std::string waveKey (const OscSettings& o)
        {
            return std::to_string (static_cast<int> (o.kind)) + "|" + std::to_string (o.intensity) + "|"
                 + std::to_string (o.sweep) + "|" + std::to_string (o.phase);
        }

        std::string drumKey (const char* kitName) { return std::string ("drum|") + kitName; }
    }

    const std::vector<short>& oscillatorWave (const OscSettings& o, WaveCache& cache)
    {
        const auto key = waveKey (o);
        auto found = cache.waves.find (key);
        if (found == cache.waves.end())
            found = cache.waves.emplace (key, drawWave (o)).first;
        return found->second;
    }

    // ------------------------------------------------------------------ the mix

    namespace
    {
        /// How far above middle C an oscillator sounds: its octave and its fine tune.
        double ratioOf (const OscSettings& o)
        {
            return std::pow (2.0, o.octave + std::max (-50, std::min (50, o.fine)) / 1200.0);
        }

        /// A sweep's round trip, in cycles of the oscillator - what the single wave used.
        constexpr int SweepCycles = 96;

        /// The longest loop the mix may take, in cycles of middle C: about 2.5 seconds.
        constexpr int MaxRootCycles = 654;

        /// How close to its setting an oscillator must land, in cents.
        constexpr double CentsTolerance = 1.0;
    }

    MixPlan planMix (const Recipe& recipe)
    {
        MixPlan plan;
        plan.rate = 40000;

        int shortest = 1;
        for (int i = 0; i < 3; ++i)
        {
            const auto& o = recipe.osc[i];
            if (o.kind == OscKind::off) continue;

            plan.oscs.push_back (i);
            plan.rate = std::min (plan.rate, rateFor (o.kind, sweeps (o)));

            // A sweeping oscillator needs the loop to hold at least one round trip of it.
            if (sweeps (o))
                shortest = std::max (shortest, static_cast<int> (std::ceil (SweepCycles / ratioOf (o))));
        }

        if (plan.oscs.empty()) return plan;

        /*
         * The shortest loop in which every oscillator's cycles come out whole and within the
         * tolerance - or, if none does within the limit, the one that comes closest. Four
         * cycles of middle C at least, which is where the two octaves down land whole.
         */
        int    best = -1;
        double bestWorst = 1e9;

        for (int n = std::max (4, shortest); n <= MaxRootCycles; ++n)
        {
            double worst = 0.0;
            for (int i : plan.oscs)
            {
                const double want = n * ratioOf (recipe.osc[i]);
                const double got  = std::max (1.0, std::round (want));
                worst = std::max (worst, std::abs (1200.0 * std::log2 (got / want)));
            }

            if (worst < bestWorst) { bestWorst = worst; best = n; }
            if (worst <= CentsTolerance) break;
        }

        plan.rootCycles = best;
        plan.words      = waveforms::wordsFor (best, plan.rate, RootHz);

        for (int i : plan.oscs)
        {
            const auto& o    = recipe.osc[i];
            const double want = best * ratioOf (o);
            const int    got  = std::max (1, static_cast<int> (std::lround (want)));

            plan.cycles.push_back (got);
            plan.errorCents.push_back (1200.0 * std::log2 (got / want));
            plan.sweepTrips.push_back (sweeps (o) ? std::max (1, static_cast<int> (std::lround (got / (double) SweepCycles))) : 0);
        }

        return plan;
    }

    namespace
    {
        /// Harmonic n of a table at position t (0..1 around the round trip), as the single
        /// wave's renderer reads it: smoothstep between neighbouring spectra, wrapping.
        void harmonicAt (const std::vector<Spectrum>& table, int n, double t, double& c, double& s)
        {
            auto coef = [n] (const Spectrum& sp, double& cc, double& ss)
            {
                cc = n <= sp.harmonics() ? sp.cos[(std::size_t) n] : 0.0;
                ss = n <= sp.harmonics() ? sp.sin[(std::size_t) n] : 0.0;
            };

            if (table.size() == 1) { coef (table[0], c, s); return; }

            const double pos  = t * (double) table.size();
            const int    ix   = static_cast<int> (pos);
            const double frac = pos - ix;
            const double m    = frac * frac * (3.0 - 2.0 * frac);

            double ac, as, bc, bs;
            coef (table[(std::size_t) (ix % (int) table.size())], ac, as);
            coef (table[(std::size_t) ((ix + 1) % (int) table.size())], bc, bs);
            c = ac * (1 - m) + bc * m;
            s = as * (1 - m) + bs * m;
        }

        /*
         * One oscillator across the whole loop, additively: harmonic by harmonic, each turned
         * by a rotating phasor rather than a cosine a sample - a loop of seconds times dozens
         * of harmonics is millions of samples, and a rotation is a multiply. Renormalised
         * every so often so rounding cannot shrink or swell it over the length.
         */
        std::vector<double> drawInMix (const OscSettings& o, int cycles, int trips, int words, int rate)
        {
            // this oscillator's own pitch, with the sample at its root: its cycles in the loop
            // over the loop's length - which sets how many harmonics fit below the Nyquist
            const double hz = cycles * static_cast<double> (rate) / words;
            const int    h  = waveforms::highestHarmonic (rate, hz);
            const auto table = tableFor (o, h);

            const double pi    = 3.14159265358979323846;
            const double start = o.phase / 99.0 * 2.0 * pi;    // where in its cycle it begins

            std::vector<double> wave ((std::size_t) words, 0.0);

            for (int n = 1; n <= h; ++n)
            {
                bool used = false;
                for (const auto& sp : table)
                    if (n <= sp.harmonics() && (sp.cos[(std::size_t) n] != 0.0 || sp.sin[(std::size_t) n] != 0.0))
                        { used = true; break; }
                if (! used) continue;

                const double step = 2.0 * pi * n * cycles / words;
                const double rc = std::cos (step), rs = std::sin (step);
                double pc = std::cos (n * start), ps = std::sin (n * start);

                double c = 0.0, s = 0.0;
                if (table.size() == 1) harmonicAt (table, n, 0.0, c, s);

                for (int i = 0; i < words; ++i)
                {
                    if (table.size() > 1)
                    {
                        double t = trips * (i / (double) words);
                        t -= std::floor (t);
                        harmonicAt (table, n, t, c, s);
                    }

                    wave[(std::size_t) i] += c * pc + s * ps;

                    const double nc = pc * rc - ps * rs;
                    ps = pc * rs + ps * rc;
                    pc = nc;

                    if ((i & 1023) == 1023)
                    {
                        const double len = std::sqrt (pc * pc + ps * ps);
                        pc /= len; ps /= len;
                    }
                }
            }

            // Each oscillator at full scale before its level, as each one's own sample was.
            double peak = 0.0;
            for (double v : wave) peak = std::max (peak, std::abs (v));
            if (peak > 0.0) for (auto& v : wave) v /= peak;

            return wave;
        }

        std::string mixKey (const Recipe& r)
        {
            std::string key = "mix";
            for (int i = 0; i < 3; ++i)
            {
                const auto& o = r.osc[i];
                if (o.kind == OscKind::off) continue;
                key += "|" + std::to_string (i) + ":" + waveKey (o) + ":" + std::to_string (o.octave)
                     + ":" + std::to_string (o.fine) + ":" + std::to_string (o.level);
            }
            return key;
        }
    }

    const std::vector<short>& mixedWave (const Recipe& recipe, WaveCache& cache, MixPlan& plan)
    {
        plan = planMix (recipe);

        const auto key = mixKey (recipe);
        auto found = cache.waves.find (key);
        if (found != cache.waves.end()) return found->second;

        std::vector<double> mix ((std::size_t) std::max (0, plan.words), 0.0);

        for (std::size_t k = 0; k < plan.oscs.size(); ++k)
        {
            const auto& o = recipe.osc[plan.oscs[k]];

            // The level knob as it always sounded: the zone loudness it used to write, in
            // the machine's own decibels a unit - 99 is full, 0 is twenty decibels down.
            const int    units = -50 + (std::max (0, std::min (99, o.level)) * 50) / 99;
            const double gain  = std::pow (10.0, units * cal::LoudnessDbPerUnit / 20.0);

            const auto one = drawInMix (o, plan.cycles[k], plan.sweepTrips[k], plan.words, plan.rate);
            for (std::size_t i = 0; i < mix.size(); ++i) mix[i] += one[i] * gain;
        }

        return cache.waves.emplace (key, waveforms::quantise (mix)).first->second;
    }

    // ----------------------------------------------------------------- rendering

    namespace
    {
        int rateForTune (int rate, int tune)
        {
            const double scaled = rate * std::pow (2.0, tune / 12.0);
            return static_cast<int> (std::llround (std::min (48000.0, std::max (4000.0, scaled))));
        }

        /// The keygroup of `program` whose soft zone plays `sample`, or -1.
        int keygroupPlaying (const Disk& d, const Disk::Entry& program, const std::string& sample)
        {
            const auto groups = d.keygroups (program);
            for (std::size_t k = 0; k < groups.size(); ++k)
                if (groups[k].zone1.name == sample) return static_cast<int> (k);
            return -1;
        }

        void put (Disk& d, const Disk::Entry& p, int k, KeygroupParam param, int value)
        {
            d.setKeygroupParam (p, k, param, value);
        }
    }

    bool render (const Recipe& recipe, WaveCache& cache, const Disk* previous, Disk& out, std::string& error)
    {
        if (! recipe.anyOscillator() && ! recipe.drumsOn)
        {
            error = "nothing to render: every oscillator is off and so are the drums";
            return false;
        }

        Disk d = Disk::blank (recipe.name + " DISK");
        const auto kit = drums::kit();

        // --- the samples: oscillators, then drums
        struct Layer { std::string sample; bool drum; int osc; int slot; };
        std::vector<Layer> layers;

        // The oscillators, mixed into one looped sample - one keygroup, one voice a note.
        if (recipe.anyOscillator())
        {
            MixPlan plan;
            Disk::NewSample s;
            s.name     = MixSampleName;
            s.words12  = mixedWave (recipe, cache, plan);
            s.rate     = plan.rate;
            s.rootNote = RootNote;
            s.loopMode = 'L';

            if (! d.addSample (s, error)) return false;
            layers.push_back ({ s.name, false, -1, -1 });
        }

        if (recipe.drumsOn)
            for (int slot = 0; slot < static_cast<int> (DrumSlot::count); ++slot)
            {
                const auto& ds = recipe.drums[slot];
                if (! ds.on) continue;

                const drums::Drum* drum = kitDrum (kit, DrumRows[slot].kitName);
                if (drum == nullptr) continue;

                const auto key = drumKey (drum->name.c_str());
                auto found = cache.waves.find (key);
                if (found == cache.waves.end())
                    found = cache.waves.emplace (key, drums::render (*drum)).first;

                Disk::NewSample s;
                s.name     = drum->name;
                s.words12  = found->second;
                s.rate     = rateForTune (drum->rate, ds.tune);   // tune is a change of rate: pitch and length together
                s.rootNote = DrumRows[slot].note;
                s.loopMode = 'O';

                if (! d.addSample (s, error)) return false;
                layers.push_back ({ s.name, true, -1, slot });
            }

        if (layers.empty())
        {
            error = "nothing to render";
            return false;
        }

        // --- the programme
        if (! d.addProgram (recipe.name, static_cast<int> (layers.size()), error)) return false;

        const Disk::Entry* program = nullptr;
        for (const auto& e : d.getEntries()) if (e.type == 'P') program = &e;
        if (program == nullptr) { error = "the programme did not read back"; return false; }

        const Disk::Entry* was = nullptr;
        if (previous != nullptr)
            for (const auto& e : previous->getEntries())
                if (e.type == 'P' && e.name == Disk::normaliseNameFor (recipe.name)) was = &e;

        const int synthLow = recipe.drumsOn ? Recipe::SynthLowKeyWithDrums : Recipe::SynthLowKey;

        for (int k = 0; k < static_cast<int> (layers.size()); ++k)
        {
            const auto& layer = layers[static_cast<std::size_t> (k)];
            using P = KeygroupParam;

            // What the recipe starts every keygroup from - then, where the disk being
            // replaced had a keygroup playing this same sample, its settings instead.
            put (d, *program, k, P::VcaAttack,  recipe.a);
            put (d, *program, k, P::VcaDecay,   recipe.d);
            put (d, *program, k, P::VcaSustain, recipe.s);
            put (d, *program, k, P::VcaRelease, recipe.r);
            put (d, *program, k, P::VcfAttack,  recipe.vcfA);
            put (d, *program, k, P::VcfDecay,   recipe.vcfD);
            put (d, *program, k, P::VcfSustain, recipe.vcfS);
            put (d, *program, k, P::VcfRelease, recipe.vcfR);
            put (d, *program, k, P::VcfAmount,  recipe.vcfAmount);
            put (d, *program, k, P::KeyToFilter, recipe.keyTrack);
            put (d, *program, k, P::VelToFilter, recipe.velFilter);
            put (d, *program, k, P::VelToLoudness, recipe.velLoudness);
            put (d, *program, k, P::LfoRate,  recipe.lfoRate);
            put (d, *program, k, P::LfoDepth, recipe.lfoDepth);
            put (d, *program, k, P::LfoDelay, recipe.lfoDelay);
            put (d, *program, k, P::LfoModwheel, 50);
            put (d, *program, k, P::LfoDesync, 1);
            put (d, *program, k, P::Zone1Filter, recipe.filter);

            if (was != nullptr)
            {
                // A disk from before the mix had a keygroup per oscillator: the mix takes its
                // settings from the first of those, so a Program tab edit survives the move.
                int from = keygroupPlaying (*previous, *was, layer.sample);
                if (from < 0 && ! layer.drum)
                    for (const char* old : { "OSC1", "OSC2", "OSC3" })
                        if ((from = keygroupPlaying (*previous, *was, old)) >= 0) break;

                if (from >= 0)
                {
                    static const P kept[] = {
                        P::VelocitySwitch, P::VcaAttack, P::VcaDecay, P::VcaSustain, P::VcaRelease,
                        P::VelToFilter, P::KeyToFilter, P::VelToAttack, P::VelToRelease, P::VelToLoudness,
                        P::WarpVelocity, P::WarpDepth, P::WarpTime,
                        P::LfoDelay, P::LfoRate, P::LfoDepth, P::LfoAftertouch, P::LfoModwheel,
                        P::OutputPort, P::VcfAmount, P::VcfAttack, P::VcfDecay, P::VcfSustain, P::VcfRelease,
                        P::Zone1Filter, P::LfoDesync, P::VelocityReleaseOn
                    };
                    for (P p : kept)
                        put (d, *program, k, p, previous->getKeygroupParam (*was, from, p));
                }
            }

            d.setZoneSample (*program, k, 0, layer.sample);

            if (! layer.drum)
            {
                put (d, *program, k, P::LowKey,  synthLow);
                put (d, *program, k, P::HighKey, 127);
                put (d, *program, k, P::ConstantPitch, 0);
                put (d, *program, k, P::OneShot, 0);

                // Octave, fine and level are in the mix itself now - the zone plays it as is.
                put (d, *program, k, P::Zone1Transpose, 0);
                put (d, *program, k, P::Zone1Fine,      0);
                put (d, *program, k, P::Zone1Loudness,  0);
            }
            else
            {
                const auto& ds   = recipe.drums[layer.slot];
                const auto* drum = kitDrum (kit, DrumRows[layer.slot].kitName);
                const int note   = DrumRows[layer.slot].note;

                put (d, *program, k, P::LowKey,  note);
                put (d, *program, k, P::HighKey, note);
                put (d, *program, k, P::ConstantPitch, 1);
                put (d, *program, k, P::OneShot, 1);
                put (d, *program, k, P::Zone1Transpose, 0);
                put (d, *program, k, P::Zone1Fine, 0);

                // The recipe's drum knobs own these four, whatever the Program tab said.
                put (d, *program, k, P::VcaAttack,  0);
                put (d, *program, k, P::VcaDecay,   ds.decay >= 99 ? 99 : ds.decay);
                put (d, *program, k, P::VcaSustain, ds.decay >= 99 ? 99 : 0);
                put (d, *program, k, P::Zone1Filter,   ds.tone);
                put (d, *program, k, P::Zone1Loudness, (drum != nullptr ? drum->loudness : 0) + ds.level);
                if (was == nullptr || keygroupPlaying (*previous, *was, layer.sample) < 0)
                {
                    put (d, *program, k, P::VcaRelease, 20);
                    put (d, *program, k, P::KeyToFilter, 0);
                    put (d, *program, k, P::VelToFilter, 0);
                    put (d, *program, k, P::VelToLoudness, drum != nullptr ? drum->vel : 30);
                    put (d, *program, k, P::VcfAmount, 0);
                    put (d, *program, k, P::LfoDepth, 0);
                }
            }
        }

        d.rebuildPointers();
        out = std::move (d);
        return true;
    }

    // ------------------------------------------------------------------ presets

    namespace
    {
        Recipe make (const char* name, std::vector<OscSettings> oscs)
        {
            Recipe r;
            r.name = name;
            for (std::size_t i = 0; i < 3 && i < oscs.size(); ++i) r.osc[i] = oscs[i];
            return r;
        }

        OscSettings osc (OscKind kind, int level = 99, int octave = 0, int fine = 0, int intensity = 50, int sweep = 0, int phase = 0)
        {
            OscSettings o;
            o.kind = kind; o.level = level; o.octave = octave; o.fine = fine;
            o.intensity = intensity; o.sweep = sweep; o.phase = phase;
            return o;
        }
    }

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> all = []
        {
            std::vector<Preset> p;

            {
                Recipe r = make ("INIT", { osc (OscKind::saw, 99, 0, 0, 0, 0) });
                p.push_back ({ "Init: one saw", r });
            }
            {
                Recipe r = make ("FAT SAW", { osc (OscKind::saw, 99, 0, -4, 0, 0), osc (OscKind::saw, 99, 0, 4, 0, 0, 30) });
                r.filter = 72; r.a = 2; r.d = 40; r.s = 80; r.r = 35; r.vcfAmount = 10; r.vcfD = 50; r.vcfS = 45; r.velFilter = 35;
                p.push_back ({ "Fat saw lead", r });
            }
            {
                Recipe r = make ("SQ BASS", { osc (OscKind::square, 99, 0, 0, 0, 0), osc (OscKind::sine, 70, -1, 0, 0, 0) });
                r.filter = 48; r.keyTrack = 30; r.a = 0; r.d = 38; r.s = 20; r.r = 18;
                r.vcfAmount = 22; r.vcfD = 30; r.vcfS = 10; r.velFilter = 50; r.velLoudness = 45; r.lfoDepth = 0;
                p.push_back ({ "Square bass", r });
            }
            {
                Recipe r = make ("PWM STRGS", { osc (OscKind::pulse, 99, 0, -5, 40, 70), osc (OscKind::pulse, 99, 0, 5, 40, 70, 50) });
                r.filter = 68; r.a = 30; r.d = 55; r.s = 88; r.r = 55; r.vcfAmount = 12; r.vcfA = 25; r.vcfD = 60; r.vcfS = 60;
                r.lfoRate = 38; r.lfoDepth = 6; r.lfoDelay = 75;
                p.push_back ({ "PWM strings", r });
            }
            {
                Recipe r = make ("SWEEP PAD", { osc (OscKind::saw, 99, 0, -4, 70, 90), osc (OscKind::saw, 85, 1, 5, 70, 90, 40) });
                r.filter = 62; r.a = 45; r.d = 60; r.s = 85; r.r = 62; r.vcfAmount = 8; r.vcfA = 50; r.vcfD = 70; r.vcfS = 70;
                r.lfoRate = 30; r.lfoDepth = 5; r.lfoDelay = 80;
                p.push_back ({ "Sweep pad", r });
            }
            {
                Recipe r = make ("FM EPIANO", { osc (OscKind::fm1, 99, 0, 0, 55, 60) });
                r.filter = 84; r.a = 0; r.d = 48; r.s = 30; r.r = 34; r.vcfAmount = 0; r.velLoudness = 55; r.velFilter = 30; r.lfoDepth = 0;
                p.push_back ({ "FM e-piano", r });
            }
            {
                Recipe r = make ("FM BELL", { osc (OscKind::fmBell, 99, 0, 0, 60, 70), osc (OscKind::sine, 60, 1, 0, 0, 0) });
                r.filter = 88; r.a = 0; r.d = 62; r.s = 0; r.r = 58; r.vcfAmount = 0; r.velLoudness = 60; r.lfoDepth = 0;
                p.push_back ({ "FM bell", r });
            }
            {
                Recipe r = make ("ORGAN", { osc (OscKind::organ, 99, 0, 0, 0, 0) });
                r.filter = 90; r.keyTrack = 20; r.a = 0; r.d = 0; r.s = 99; r.r = 8; r.vcfAmount = 0; r.velLoudness = 10; r.lfoDepth = 0;
                p.push_back ({ "Organ", r });
            }
            {
                Recipe r = make ("GLASS BEL", { osc (OscKind::glass, 99, 0, 0, 0, 0), osc (OscKind::sine, 65, 1, 0, 0, 0) });
                r.filter = 86; r.a = 0; r.d = 52; r.s = 0; r.r = 48; r.vcfAmount = 0; r.velLoudness = 60; r.lfoDepth = 0;
                p.push_back ({ "Glass bell", r });
            }
            {
                Recipe r = make ("KIT", { osc (OscKind::saw, 99, 0, 0, 0, 0) });
                r.drumsOn = true;
                r.filter = 78;
                p.push_back ({ "Drums + saw", r });
            }

            return p;
        }();

        return all;
    }

    bool hasSeparateOscillators (const Recipe& recipe, const Disk& disk)
    {
        for (const auto& e : disk.getEntries())
            if (e.type == 'P' && e.name == Disk::normaliseNameFor (recipe.name))
                for (const auto& k : disk.keygroups (e))
                    if (k.zone1.name == "OSC1" || k.zone1.name == "OSC2" || k.zone1.name == "OSC3")
                        return true;
        return false;
    }

    bool hasOldTuning (const Recipe& recipe, const Disk& disk)
    {
        const Disk::Entry* program = nullptr;
        for (const auto& e : disk.getEntries())
            if (e.type == 'P' && e.name == Disk::normaliseNameFor (recipe.name)) program = &e;
        if (program == nullptr) return false;

        bool sawOld = false;

        for (const auto& k : disk.keygroups (*program))
        {
            const auto& name = k.zone1.name;
            if (name.size() != 4 || name.compare (0, 3, "OSC") != 0) continue;
            const int i = name[3] - '1';
            if (i < 0 || i > 2) continue;

            const auto& o     = recipe.osc[i];
            const int   cents = std::max (-50, std::min (50, o.fine));

            // what the writer put there before v0.5.0: whole semitones, then 256ths upward
            const int fine256  = static_cast<int> (std::llround (std::abs (cents) * 2.56));
            const int oldHigh  = o.octave * 12 - (cents < 0 ? 1 : 0);
            const int oldLow   = cents < 0 ? 256 - fine256 : fine256;

            // what it writes now, and what the machine reads: sixteenths, in one number
            const int sixteenths = o.octave * 12 * 16 + static_cast<int> (std::lround (cents * 16.0 / 100.0));
            const int newHigh    = sixteenths >= 0 ? sixteenths / 256 : -((255 - sixteenths) / 256);
            const int newLow     = sixteenths - newHigh * 256;

            if (oldHigh == newHigh && (oldLow & 0xFF) == newLow) continue;     // the same bytes either way

            if (k.zone1.transpose == oldHigh && k.zone1.fine == (oldLow & 0xFF))
                sawOld = true;
            else if (! (k.zone1.transpose == newHigh && k.zone1.fine == newLow))
                return false;          // tuned by hand: not ours to redo
        }

        return sawOld;
    }

    // ---------------------------------------------------------------- as text

    std::string toText (const Recipe& r)
    {
        // Spaces in the name would split the line, so they travel as underscores; an S950
        // name is upper-case letters, digits and spaces, and never had one.
        std::string name = r.name;
        for (auto& c : name) if (c == ' ') c = '_';

        std::ostringstream s;
        s << "name=" << name;
        for (int i = 0; i < 3; ++i)
        {
            const auto& o = r.osc[i];
            s << " o" << i << "=" << static_cast<int> (o.kind) << "," << o.level << "," << o.octave << ","
              << o.fine << "," << o.phase << "," << o.intensity << "," << o.sweep;
        }
        s << " drums=" << (r.drumsOn ? 1 : 0);
        for (int i = 0; i < static_cast<int> (DrumSlot::count); ++i)
        {
            const auto& ds = r.drums[i];
            s << " d" << i << "=" << (ds.on ? 1 : 0) << "," << ds.tune << "," << ds.decay << "," << ds.tone << "," << ds.level;
        }
        s << " voice=" << r.filter << "," << r.keyTrack << "," << r.velFilter << "," << r.a << "," << r.d << "," << r.s << ","
          << r.r << "," << r.vcfA << "," << r.vcfD << "," << r.vcfS << "," << r.vcfR << "," << r.vcfAmount << ","
          << r.velLoudness << "," << r.lfoRate << "," << r.lfoDepth << "," << r.lfoDelay;
        return s.str();
    }

    Recipe fromText (const std::string& text)
    {
        Recipe r;
        std::istringstream in (text);
        std::string item;

        auto ints = [] (const std::string& v)
        {
            std::vector<int> out;
            std::istringstream s (v);
            std::string part;
            while (std::getline (s, part, ',')) out.push_back (std::atoi (part.c_str()));
            return out;
        };

        while (in >> item)
        {
            const auto eq = item.find ('=');
            if (eq == std::string::npos) continue;
            const std::string key = item.substr (0, eq), value = item.substr (eq + 1);

            if (key == "name")
            {
                r.name = value;
                for (auto& c : r.name) if (c == '_') c = ' ';
                continue;
            }
            if (key == "drums") { r.drumsOn = std::atoi (value.c_str()) != 0; continue; }

            const auto v = ints (value);

            if (key.size() == 2 && key[0] == 'o' && v.size() >= 7)
            {
                const int i = key[1] - '0';
                if (i < 0 || i > 2) continue;
                auto& o = r.osc[i];
                const int kind = std::max (0, std::min (static_cast<int> (OscKind::count) - 1, v[0]));
                o.kind = static_cast<OscKind> (kind);
                o.level = v[1]; o.octave = v[2]; o.fine = v[3]; o.phase = v[4]; o.intensity = v[5]; o.sweep = v[6];
            }
            else if (key.size() == 2 && key[0] == 'd' && v.size() >= 5)
            {
                const int i = key[1] - '0';
                if (i < 0 || i >= static_cast<int> (DrumSlot::count)) continue;
                auto& ds = r.drums[i];
                ds.on = v[0] != 0; ds.tune = v[1]; ds.decay = v[2]; ds.tone = v[3]; ds.level = v[4];
            }
            else if (key == "voice" && v.size() >= 16)
            {
                r.filter = v[0]; r.keyTrack = v[1]; r.velFilter = v[2]; r.a = v[3]; r.d = v[4]; r.s = v[5]; r.r = v[6];
                r.vcfA = v[7]; r.vcfD = v[8]; r.vcfS = v[9]; r.vcfR = v[10]; r.vcfAmount = v[11];
                r.velLoudness = v[12]; r.lfoRate = v[13]; r.lfoDepth = v[14]; r.lfoDelay = v[15];
            }
        }

        return r;
    }
}
