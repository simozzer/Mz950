using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// The wave a hard strike gets instead: a keygroup's second zone.
    ///
    /// The two zones inside a keygroup are velocity ALTERNATIVES, not a stack. Below the
    /// switch the first one answers and the second is silent; at or above it, the other
    /// way round. That is a real instrument's second voice - a piano hammer meeting the
    /// string hard enough to change what the string does, rather than just doing it
    /// louder - and it costs no sample, because the wave it switches to is already on the
    /// disk being played by something else.
    ///
    /// WHAT A ZONE CAN AND CANNOT CHANGE
    ///
    /// A zone carries its own sample, tuning, filter cutoff and level, so the hard strike
    /// can be a different waveform, brighter and louder. It does NOT carry an envelope:
    /// the VCA and VCF envelopes belong to the keygroup and both zones share them. A hard
    /// layer that wanted its own attack would have to be a keygroup of its own, and a
    /// keygroup has no velocity range - it would sound at every velocity, under the soft
    /// one rather than instead of it.
    /// </summary>
    internal sealed class Struck
    {
        public string Sample;          // the wave a hard strike plays
        public int Transpose;          // semitones, signed
        public int Fine;               // 1/256ths of a semitone
        public int Loudness;           // signed trim - usually up, that being the point
        public int? Filter;            // its own cutoff, or the layer's if left null

        /// <summary>
        /// The velocity it takes over at, 1..127.
        ///
        /// Below this the soft wave answers; at it and above, this one. The panel writes
        /// 128 to mean there is no second zone at all, which is what a layer without one
        /// gets.
        /// </summary>
        public int At = 80;
    }

    /// <summary>
    /// One layer of a patch: a wave, how it is tuned, how loud it sits, and - where it
    /// wants to differ - its own envelope and filter.
    ///
    /// A layer is a keygroup, and a keygroup on this machine owns its envelope, its filter
    /// and its vibrato outright. So layers do not have to share a shape: a bell that decays
    /// in half a second can sit on top of a pad that takes two seconds to arrive, from one
    /// key, and the machine will do it without being asked twice.
    ///
    /// Anything left null falls back to the patch, so a layer says only what makes it
    /// different from its neighbours.
    /// </summary>
    internal sealed class Layer
    {
        public string Sample;          // which generated wave
        public int Transpose;          // semitones, signed
        public int Fine;               // 1/256ths of a semitone - detuning, for width; lands on
                                       // the machine's 1/16 step (Program.ZonePitch)
        public int Loudness;           // signed trim, in the machine's decibel count

        /// <summary>
        /// Which part of the keyboard this layer answers to. The whole of it by default.
        ///
        /// This is what a keygroup IS - a range of keys - so a split costs nothing extra:
        /// give one layer the bottom two octaves and another the rest, and the same
        /// programme is a bass under the left hand and a lead under the right. Overlap them
        /// and the overlap layers instead, which is the only difference between a split and
        /// a stack.
        ///
        /// C3 is 60, so 48 is C2 and 72 is C4.
        /// </summary>
        public int? LowKey, HighKey;

        /// <summary>
        /// What a hard strike plays instead of <see cref="Sample"/>, if anything.
        ///
        /// This is the other zone of the same keygroup, so it is an alternative rather
        /// than an addition - see <see cref="Struck"/> for what it can and cannot change.
        /// </summary>
        public Struck Hard;

        // Its own amplitude envelope, if it wants one.
        public int? A, D, S, R;

        // Its own filter, and its own filter envelope.
        public int? Filter, KeyToFilter, VelToFilter;
        public int? VcfA, VcfD, VcfS, VcfR, VcfAmount;

        public int? VelToLoudness;
        public int? LfoDelay, LfoRate, LfoDepth;
    }

    /// <summary>
    /// A sound: one or more layers, and what the machine does to them.
    ///
    /// Overlapping KEYGROUPS layer - that is how two detuned saws become one fat one. The
    /// two zones inside a keygroup do not; they are velocity alternatives. So each layer
    /// here becomes a keygroup of its own, covering the whole keyboard.
    /// </summary>
    internal sealed class Patch
    {
        public string Name;
        public Layer[] Layers;

        // The filter. 99 is wide open; Cal.CutoffHz says what the numbers are in hertz.
        public int Filter = 99;
        public int KeyToFilter = 50;   // measured: 50 tracks the keyboard one for one
        public int VelToFilter;

        public int A, D, S = 99, R = 20;               // the amplitude envelope, 0..99
        public int VcfA, VcfD, VcfS = 99, VcfR;        // the filter envelope
        public int VcfAmount;                          // how far it reaches, signed -50..50

        public int VelToLoudness = 30;

        // Vibrato. The delay is a fade-in, not a wait - measured off the machine.
        public int LfoDelay, LfoRate, LfoDepth, LfoModwheel = 50;

        public bool OneShot;

        /// <summary>
        /// Play every keygroup at its sample's own pitch, whatever key is pressed.
        ///
        /// Flag bit 0. For a drum kit this is what stops a kick two keys up from being a
        /// kick two semitones sharp - and it is belt and braces beside the root note,
        /// since each drum's root is already the key it sits on.
        /// </summary>
        public bool ConstantPitch;

        /// <summary>Two or three letters naming the treatment, for a generated patch.</summary>
        public string Suffix;

        /// <summary>
        /// The same settings under a new name, around a different wave.
        ///
        /// A programme is a hundred-odd bytes where a sample is tens of kilobytes, so a
        /// disk can carry a great many programmes over the same handful of waves before
        /// it runs out of anything. That is how factory libraries were built, and it is
        /// why a treatment is worth naming once and applying to everything.
        /// </summary>
        public Patch With(string name, params Layer[] layers)
        {
            var p = (Patch)MemberwiseClone();
            p.Name = name;
            p.Layers = layers;
            return p;
        }
    }

    /// <summary>
    /// The library: what to generate, and what to make of it.
    ///
    /// Every wave here is built rather than sampled, which is the point - an S950 with no
    /// sample library is still a synthesiser, and these are the sounds it makes when the
    /// waves are designed to suit its filter instead of being recorded and hoped over.
    /// </summary>
    internal static class Patches
    {
        /// <summary>Middle C, where every wave is written and tuned.</summary>
        public const double RootHz = 261.6255653;
        public const int    RootNote = 60;

        /// <summary>A wave to generate: a name, how to build its table, and how long.</summary>
        public sealed class Wave
        {
            public string Name;
            public Func<int, Spectrum[]> Table;   // given the highest usable harmonic
            public int Cycles = Waveforms.PlainCycles;
            public int Rate = 40000;
            public bool IsNoise;
            public Shake Shake;          // noise shaking the level, the pitch, or both

            /// <summary>
            /// A one-shot, rendered whole, instead of a table to loop.
            ///
            /// Everything else here is a few cycles played round and round with the
            /// machine's envelope shaping them. A percussion sample cannot be that: its
            /// envelope is the sound rather than a setting, so it is generated at full
            /// length, written with loop mode O, and the loop fields are never read.
            /// </summary>
            public Func<short[]> Oneshot;

            /// <summary>
            /// The key it plays at its own rate on, where that is not middle C.
            ///
            /// Every tuned wave here is written at <see cref="RootNote"/> and transposed
            /// from there. A drum is not transposed at all, so its root is the key it
            /// sits on and the varispeed ratio is one.
            /// </summary>
            public int? Root;
        }

        // ------------------------------------------------------------------- helpers

        /// <summary>
        /// Mirror a sweep so it comes home.
        ///
        /// The table wraps around, so a one-way A-B-C jumps from C straight back to A every
        /// time the loop repeats - a lurch you can hear. A-B-C-B arrives back where it
        /// started by the same road it left.
        /// </summary>
        static Spectrum[] Mirror(Spectrum[] s)
        {
            if (s.Length < 3) return s;

            var outp = new List<Spectrum>(s);
            for (int i = s.Length - 2; i >= 1; i--) outp.Add(s[i]);
            return outp.ToArray();
        }

        static Wave Plain(string name, Waveforms.Harmonic shape)
        {
            return new Wave
            {
                Name = name,
                Table = h => new[] { Spectrum.FromShape(shape, h) }
            };
        }

        /// <summary>
        /// A sweep, written at half rate.
        ///
        /// Movement needs cycles and cycles cost words. Half the rate is half the words for
        /// the same sweep, and costs only the top octave of harmonics - which the filter in
        /// most of these patches is taking off anyway.
        /// </summary>
        static Wave Sweep(string name, int cycles, Func<int, Spectrum[]> table)
        {
            return new Wave { Name = name, Table = table, Cycles = cycles, Rate = 20000 };
        }

        /// <summary>A wave shaken by noise as it is rendered.</summary>
        static Wave Shaken(string name, int cycles, Shake shake, Func<int, Spectrum[]> table)
        {
            return new Wave { Name = name, Table = table, Cycles = cycles, Rate = 20000, Shake = shake };
        }

        /// <summary>Several spectra taken at evenly spaced values of one parameter.</summary>
        static Spectrum[] Steps(int count, Func<double, Spectrum> at)
        {
            var s = new Spectrum[count];
            for (int i = 0; i < count; i++) s[i] = at(i / (double)(count - 1));
            return s;
        }

        // ---------------------------------------------------------------- treatments

        /// <summary>
        /// Ways of treating a wave, named once and applied to many.
        ///
        /// The envelope, the filter and the velocity response are what turn one waveform
        /// into a lead, a pad, a bass or a stab - and none of them costs a sample. So the
        /// waves stay few and the programmes multiply, which is both what fits on a disk
        /// and what a player actually wants: the same oscillator, eight ways.
        /// </summary>
        static Patch[] Treatments()
        {
            return new[]
            {
                // Held, bright, a touch of vibrato coming in late.
                new Patch { Suffix = "LD", Filter = 78, A = 0, D = 34, S = 86, R = 26,
                            VcfAmount = 12, VcfD = 44, VcfS = 45, VelToFilter = 40,
                            LfoRate = 44, LfoDepth = 4, LfoDelay = 70 },

                // Slow in, slow out, the filter opening as it arrives.
                new Patch { Suffix = "PD", Filter = 60, A = 42, D = 66, S = 88, R = 62,
                            VcfAmount = 18, VcfA = 48, VcfD = 74, VcfS = 55,
                            LfoRate = 26, LfoDepth = 4, LfoDelay = 80 },

                // Struck and gone, with the filter falling faster than the level.
                new Patch { Suffix = "PLK", Filter = 44, A = 0, D = 30, S = 0, R = 22,
                            VcfAmount = 28, VcfD = 24, VcfS = 0,
                            VelToFilter = 55, VelToLoudness = 55 },

                // Low, short and stiff, with the keyboard barely opening the filter -
                // a bass wants to sound the same at the bottom as in the middle.
                new Patch { Suffix = "BS", Filter = 46, KeyToFilter = 28,
                            A = 0, D = 36, S = 20, R = 16,
                            VcfAmount = 22, VcfD = 28, VcfS = 8,
                            VelToFilter = 48, VelToLoudness = 45 },

                // Short, loud and wide open: a chord you hit rather than hold.
                new Patch { Suffix = "STB", Filter = 88, A = 0, D = 26, S = 0, R = 18,
                            VelToFilter = 60, VelToLoudness = 65 },

                // Arrives slowly and leaves slowly, with no filter movement at all -
                // what changes is only that it is there.
                new Patch { Suffix = "SWL", Filter = 66, A = 62, D = 70, S = 92, R = 72,
                            LfoRate = 18, LfoDepth = 3, LfoDelay = 88 }
            };
        }

        /// <summary>
        /// The S950 shows ten characters, so that is what a name gets - and where something
        /// has to give it is the WAVE, not the treatment.
        ///
        /// Trimming the end instead would turn "MORPH SS PD" and "MORPH SS PLK" both into
        /// "MORPH SS P", and two files of one name on a disk is a directory the machine
        /// cannot read. Cutting the wave keeps the part that tells them apart.
        /// </summary>
        static string Named(string wave, string suffix)
        {
            int room = 10 - (suffix.Length + 1);
            string w = wave.Length <= room ? wave : wave.Substring(0, room);
            return w.TrimEnd() + " " + suffix;
        }

        /// <summary>Every one of these waves, under every one of these treatments.</summary>
        static List<Patch> Cross(string[] waves, params string[] suffixes)
        {
            var all = Treatments();
            var outp = new List<Patch>();

            foreach (string wave in waves)
                foreach (string suffix in suffixes)
                    foreach (var t in all)
                        if (t.Suffix == suffix)
                            outp.Add(t.With(Named(wave, suffix), new Layer { Sample = wave }));

            return outp;
        }

        // --------------------------------------------------------------------- banks

        /// <summary>One disk: a name, and the programmes on it.</summary>
        public sealed class Bank
        {
            public string Name;
            public List<Patch> Programmes = new List<Patch>();
        }

        /// <summary>
        /// The library, split across floppies.
        ///
        /// A disk holds 64 files and a programme and a sample are each one of them, so
        /// everything here will not fit on one however much room the blocks leave - the
        /// directory runs out long before the space does. That is the machine rather than
        /// a limitation of this program, and splitting by technique is what a shelf of
        /// real floppies would have looked like anyway.
        ///
        /// Each disk carries only the waves its own programmes name, worked out from the
        /// layers rather than listed here - so a disk can never be missing a sample it
        /// refers to, or carrying one nothing plays.
        /// </summary>
        public static List<Bank> Banks()
        {
            var named = new Dictionary<string, Patch>(StringComparer.OrdinalIgnoreCase);
            foreach (var p in All()) named[p.Name] = p;

            //
            // Grouped by what a sound is FOR, not by how it was made.
            //
            // Splitting these by technique was the obvious thing and the wrong one: a
            // disk is loaded because a part needs a bass, not because the player
            // fancies some frequency modulation. So the techniques are spread across
            // the shelf and each disk is a job - which is how factory libraries were
            // laid out, and for the same reason.
            //
            // Each disk carries its hand-built programmes first and then the same few
            // waves under every treatment. That second list is nearly free: a programme
            // is a hundred-odd bytes where a sample is tens of kilobytes, so what fills
            // a disk is the waves, and a wave already paid for can carry as many
            // programmes as the directory has room for.
            //
            return new List<Bank>
            {
                Disk ("BASS",
                    Hand (named, "SQ BASS", "FM BASS", "GRIT BASS", "SUB SINE",
                                 "PLUCK", "STACK 57", "RUMBLE", "SPLIT BS",
                                 "VEL BASS"),
                    Cross (new[] { "SQUARE", "FM 1-2", "GRIT", "SINE", "BUZZ" },
                           "BS", "PLK", "STB")),

                Disk ("LEADS",
                    Hand (named, "SAW LEAD", "FAT SAW", "SOUR LEAD", "CZ LEAD",
                                 "RING LEAD", "SYNC LEAD", "DRIFT LD", "FIFTHS",
                                 "SPLIT LD", "VEL LEAD", "VEL RING"),
                    Cross (new[] { "SAW", "SEVENTH", "PD SINE", "RING 2", "BENT", "DRIFT" },
                           "LD", "STB")),

                Disk ("PADS",
                    Hand (named, "SWEEP PAD", "EVOLVER", "PWM STRGS", "SWELL",
                                 "BREATHY", "UNSTABLE", "DISSOLVE", "FIFTHS UP",
                                 "VEL SWEEP"),
                    Cross (new[] { "MORPH SS", "EVOLVE", "PWM", "DISSOLVE", "UNSTABLE" },
                           "PD", "SWL", "LD")),

                Disk ("KEYS",
                    Hand (named, "FM EPIANO", "ORGAN", "REED ORG", "GLASS BEL",
                                 "FM BELL", "BELL PAD", "THREE UP", "SPLIT KEY",
                                 "VEL EP", "VEL BELL"),
                    Cross (new[] { "FM 1-1", "GLASS", "ORGAN", "REED", "FM BELL" },
                           "PLK", "LD", "PD")),

                // The ones that are not notes so much as events and weather.
                Disk ("TEXTURE",
                    Hand (named, "WIND", "NOISE HIT", "HAMMER", "CZ ROCKER",
                                 "CZ BRASS", "FM STACK", "RING CLNG", "SPLIT FX",
                                 "VEL HIT"),
                    Cross (new[] { "PINK", "BROWN", "WHITE", "FM STACK", "PD ROCK" },
                           "PD", "STB", "SWL")),

                //
                // The drums, which are a disk of their own for a reason the other five do
                // not have: their samples are one-shots with the envelope baked in, and a
                // kit is 600-odd KB where a wavetable is a few. Nothing else would fit
                // beside them, and nothing else wants to - a kit is loaded as a kit.
                //
                // No generated programmes here. Those apply a treatment to a wave, and a
                // treatment is an envelope and a filter, which is precisely what a drum
                // does not take from the machine.
                //
                Disk ("DRUMS", Drums.Programmes(), new List<Patch>())
            };
        }

        /// <summary>One disk: the hand-built programmes, then the generated ones.</summary>
        static Bank Disk(string name, List<Patch> hand, List<Patch> generated)
        {
            var b = new Bank { Name = name };
            b.Programmes.AddRange(hand);

            //
            // Never the same name twice. A generated name can land on a hand-built one, or
            // on another generated one once the wave has been cut to fit, and two files of
            // one name in a directory is not something this machine has an answer for.
            //
            foreach (var p in generated)
            {
                bool taken = false;
                foreach (var had in b.Programmes)
                    if (string.Equals(had.Name, p.Name, StringComparison.OrdinalIgnoreCase))
                    { taken = true; break; }

                if (!taken) b.Programmes.Add(p);
            }

            return b;
        }

        /// <summary>The hand-built programmes, by name.</summary>
        static List<Patch> Hand(Dictionary<string, Patch> named, params string[] wanted)
        {
            var outp = new List<Patch>();

            foreach (string name in wanted)
            {
                Patch p;
                if (!named.TryGetValue(name, out p))
                    throw new InvalidOperationException("no programme called '" + name + "'");

                outp.Add(p);
            }

            return outp;
        }

        // --------------------------------------------------------------------- waves

        public static List<Wave> Waves()
        {
            var waves = new List<Wave>
            {
                // ---- the plain shapes
                Plain ("SAW",      Waveforms.Saw),
                Plain ("SQUARE",   Waveforms.Square),
                Plain ("TRIANGLE", Waveforms.Triangle),
                Plain ("SINE",     Waveforms.Sine),
                Plain ("ORGAN",    Waveforms.Organ),
                Plain ("GLASS",    Waveforms.Glass),
                Plain ("BUZZ",     Waveforms.Buzz),

                // ---- shape morphs
                Sweep ("MORPH SS", 96, h => new[]
                {
                    Spectrum.FromShape (Waveforms.Sine, h),
                    Spectrum.FromShape (Waveforms.Saw,  h)
                }),

                Sweep ("EVOLVE", 128, h => Mirror (new[]
                {
                    Spectrum.FromShape (Waveforms.Sine,   h),
                    Spectrum.FromShape (Waveforms.Hollow, h),
                    Spectrum.FromShape (Waveforms.Organ,  h),
                    Spectrum.FromShape (Waveforms.Glass,  h)
                })),

                // ---- pulse width modulation: the duty swept and brought back
                Sweep ("PWM", 96, h =>
                    Mirror (Steps (5, t => Modulation.Pulse (0.5 - 0.4 * t, h)))),

                // ---- frequency modulation
                //
                // The index is what FM calls brightness: at zero it is a sine, and every
                // step up adds another pair of sidebands. Sweeping it is the sound the
                // technique is famous for.
                //
                Sweep ("FM 1-1", 96, h =>
                    Mirror (Steps (5, t => Modulation.Fm (1, 6.0 * t, h)))),

                Sweep ("FM 1-2", 96, h =>
                    Mirror (Steps (5, t => Modulation.Fm (2, 5.0 * t, h)))),

                // A ratio of seven is far enough from the harmonic series to clang, while
                // still landing every sideband on a harmonic - so it is bell-like without
                // being out of tune.
                Sweep ("FM BELL", 96, h =>
                    Mirror (Steps (5, t => Modulation.Fm (7, 4.0 * t, h)))),

                // Two modulators, which is where FM stops demonstrating itself and starts
                // sounding like an instrument.
                Sweep ("FM STACK", 128, h =>
                    Mirror (Steps (5, t => Modulation.Fm2 (1, 4.0 * t, 3, 2.0 * t, h)))),

                // ---- ring modulation
                //
                // Sum and difference of every pair of partials, and none of the originals:
                // hollow, metallic, and nothing like either input.
                //
                Sweep ("RING 2", 96, h =>
                    Mirror (Steps (4, t => Modulation.Ring (Waveforms.Saw, 24, 2 + 1 * t, h)))),

                Sweep ("RING 3", 96, h =>
                    Mirror (Steps (4, t => Modulation.Ring (Waveforms.Square, 24, 3, h)))),

                // ---- phase bending: an oscillator being wound up
                Sweep ("BENT", 96, h =>
                    Mirror (Steps (5, t => Modulation.Bent (Waveforms.Saw, 24, 2.0 * t, h)))),

                // ---- added harmonics: fifths and sevenths
                //
                // Harmonic 3 is an octave and a fifth, 5 is two octaves and a major
                // third, 7 is two octaves and a flat seventh - a third of a semitone
                // below the one a keyboard plays, which is what makes it sour rather
                // than simply bigger.
                //
                Plain ("FIFTHS",  Waveforms.Lift (Waveforms.Saw, 0.55, 3, 6)),
                Plain ("SEVENTH", Waveforms.Lift (Waveforms.Saw, 0.50, 7)),
                Plain ("STACK57", Waveforms.Lift (Waveforms.Square, 0.40, 3, 5, 7)),

                // A drawbar registration written out by hand: the fundamental, its
                // octave, the fifth above that, and a quiet seventh leaning on it.
                Plain ("REED", Waveforms.Partials (1.0, 0.5, 0.7, 0.25, 0.3, 0.2, 0.45)),

                // The added harmonics brought in and taken away again - a chorus of
                // fifths arriving over the note rather than sitting on it from the start.
                Sweep ("FIFTHS UP", 96, h => Mirror (Steps (4, t => Spectrum.FromShape (
                    Waveforms.Lift (Waveforms.Saw, 0.8 * t, 3, 6), h)))),

                Sweep ("SEVENTHS", 96, h => Mirror (Steps (4, t => Spectrum.FromShape (
                    Waveforms.Lift (Waveforms.Saw, 0.7 * t, 5, 7), h)))),

                // ---- phase distortion: half the cycle narrower than the other
                //
                // A skew of a half is the untouched wave; winding it towards either end
                // steepens the join between the two halves and grows harmonics out of it.
                // Swept, it is a filter sweep with no filter - which is exactly how the
                // Casio CZ line worked, and it costs the sample nothing.
                //
                Sweep ("PD SINE", 96, h =>
                    Mirror (Steps (5, t => Modulation.PhaseDistort (Waveforms.Sine, 1, 0.5 - 0.42 * t, h)))),

                Sweep ("PD SAW", 96, h =>
                    Mirror (Steps (5, t => Modulation.PhaseDistort (Waveforms.Saw, 16, 0.5 - 0.40 * t, h)))),

                //
                // Both ways from the middle, so the cycle leans one way and then the other
                // rather than always to the same side.
                //
                // No mirroring here: a sine through the skew is already a round trip, since
                // it leaves the middle, reaches both ends and comes back within one pass.
                //
                Sweep ("PD ROCK", 128, h =>
                    Steps (9, t => Modulation.PhaseDistort (
                        Waveforms.Sine, 1, 0.5 + 0.40 * Math.Sin (2.0 * Math.PI * t), h))),

                // ---- noise, as a sound
                //
                // Harmonics at the colour's slope with random phases: noise to the ear,
                // periodic to the machine, so it loops without a seam and never aliases.
                //
                Sweep ("WHITE", 32, h => new[] { Noise.Spectrum (Noise.Colour.White, 101, h) }),
                Sweep ("PINK",  32, h => new[] { Noise.Spectrum (Noise.Colour.Pink,  202, h) }),
                Sweep ("BROWN", 32, h => new[] { Noise.Spectrum (Noise.Colour.Brown, 303, h) }),

                // A sweep from tone to noise and back - the same wave dissolving.
                Sweep ("DISSOLVE", 96, h => Mirror (Steps (4, t =>
                    Noise.Dusted (Waveforms.Saw, Noise.Colour.Pink, 1.2 * t, 404, h)))),

                // ---- noise, as a modulator
                //
                // Shaking the pitch is an oscillator that will not sit still; shaking the
                // level is one whose amplifier will not. Brown wanders, white jitters.
                //
                Shaken ("DRIFT", 96,
                    new Shake { PitchDepth = 0.06, Colour = Noise.Colour.Brown, Points = 18, Seed = 11 },
                    h => new[] { Spectrum.FromShape (Waveforms.Saw, h) }),

                Shaken ("GRIT", 96,
                    new Shake { AmDepth = 0.45, Colour = Noise.Colour.White, Points = 64, Seed = 22 },
                    h => new[] { Spectrum.FromShape (Waveforms.Square, h) }),

                Shaken ("UNSTABLE", 128,
                    new Shake { PitchDepth = 0.12, AmDepth = 0.35, Colour = Noise.Colour.Pink, Points = 40, Seed = 33 },
                    h => Mirror (Steps (4, t => Modulation.Fm (2, 4.0 * t, h)))),

                new Wave { Name = "NOISE", IsNoise = true, Rate = 20000 }
            };

            // The drum kit, which is generated a different way and says so - see Drums.
            waves.AddRange(Drums.Waves());

            return waves;
        }

        // ------------------------------------------------------------------- patches

        public static List<Patch> All()
        {
            return new List<Patch>
            {
                new Patch
                {
                    Name = "SAW LEAD",
                    Layers = new[] { new Layer { Sample = "SAW" } },
                    Filter = 78, A = 0, D = 30, S = 88, R = 28,
                    VcfAmount = 12, VcfD = 45, VcfS = 40,
                    VelToFilter = 40, LfoRate = 45, LfoDepth = 4, LfoDelay = 70
                },

                //
                // Two saws a whisker apart. The detune is the whole sound. Six 256ths asks for
                // about two cents; the machine's finest step is a sixteenth of a semitone
                // (6.25 cents), so that is what lands on the disk - still slow enough to beat
                // wide rather than sound out of tune.
                //
                new Patch
                {
                    Name = "FAT SAW",
                    Layers = new[]
                    {
                        new Layer { Sample = "SAW", Fine = -6 },
                        new Layer { Sample = "SAW", Fine =  6 }
                    },
                    Filter = 72, A = 2, D = 40, S = 80, R = 35,
                    VcfAmount = 10, VcfD = 50, VcfS = 45, VelToFilter = 35
                },

                new Patch
                {
                    Name = "SQ BASS",
                    Layers = new[] { new Layer { Sample = "SQUARE" } },
                    Filter = 48, KeyToFilter = 30,
                    A = 0, D = 38, S = 20, R = 18,
                    VcfAmount = 22, VcfD = 30, VcfS = 10,
                    VelToFilter = 50, VelToLoudness = 45
                },

                new Patch
                {
                    Name = "SUB SINE",
                    Layers = new[]
                    {
                        new Layer { Sample = "SINE" },
                        new Layer { Sample = "TRIANGLE", Transpose = 12, Loudness = -14 }
                    },
                    Filter = 99, KeyToFilter = 0, A = 1, D = 55, S = 70, R = 30
                },

                new Patch
                {
                    Name = "PLUCK",
                    Layers = new[] { new Layer { Sample = "BUZZ" } },
                    Filter = 40, A = 0, D = 28, S = 0, R = 22,
                    VcfAmount = 30, VcfD = 22, VcfS = 0,
                    VelToFilter = 55, VelToLoudness = 55
                },

                new Patch
                {
                    Name = "PWM STRGS",
                    Layers = new[]
                    {
                        new Layer { Sample = "PWM", Fine = -7 },
                        new Layer { Sample = "PWM", Fine =  7 }
                    },
                    Filter = 68, A = 30, D = 55, S = 88, R = 55,
                    VcfAmount = 12, VcfA = 25, VcfD = 60, VcfS = 60,
                    LfoRate = 38, LfoDepth = 6, LfoDelay = 75
                },

                new Patch
                {
                    Name = "SWEEP PAD",
                    Layers = new[]
                    {
                        new Layer { Sample = "MORPH SS", Fine = -4 },
                        new Layer { Sample = "MORPH SS", Fine = 5, Transpose = 12, Loudness = -8 }
                    },
                    Filter = 62, A = 45, D = 60, S = 85, R = 62,
                    VcfAmount = 18, VcfA = 40, VcfD = 70, VcfS = 55,
                    LfoRate = 30, LfoDepth = 5, LfoDelay = 80
                },

                new Patch
                {
                    Name = "EVOLVER",
                    Layers = new[] { new Layer { Sample = "EVOLVE" } },
                    Filter = 74, A = 25, D = 70, S = 90, R = 70,
                    VcfAmount = 16, VcfA = 55, VcfD = 80, VcfS = 60,
                    LfoRate = 22, LfoDepth = 4, LfoDelay = 85
                },

                // ---- the FM patches

                new Patch
                {
                    Name = "FM EPIANO",
                    Layers = new[] { new Layer { Sample = "FM 1-1" } },
                    Filter = 84, A = 0, D = 48, S = 30, R = 34,
                    VelToLoudness = 55, VelToFilter = 30
                },

                new Patch
                {
                    Name = "FM BELL",
                    Layers = new[]
                    {
                        new Layer { Sample = "FM BELL" },
                        new Layer { Sample = "SINE", Transpose = 12, Loudness = -20 }
                    },
                    Filter = 88, A = 0, D = 62, S = 0, R = 58, VelToLoudness = 60
                },

                new Patch
                {
                    Name = "FM BASS",
                    Layers = new[] { new Layer { Sample = "FM 1-2" } },
                    Filter = 52, KeyToFilter = 30,
                    A = 0, D = 34, S = 18, R = 16,
                    VcfAmount = 18, VcfD = 28, VcfS = 8,
                    VelToFilter = 45, VelToLoudness = 50
                },

                new Patch
                {
                    Name = "FM STACK",
                    Layers = new[] { new Layer { Sample = "FM STACK", Fine = -3 } },
                    Filter = 76, A = 12, D = 60, S = 78, R = 50,
                    VcfAmount = 14, VcfA = 30, VcfD = 65, VcfS = 55,
                    LfoRate = 28, LfoDepth = 4, LfoDelay = 78
                },

                // ---- ring modulation

                new Patch
                {
                    Name = "RING LEAD",
                    Layers = new[] { new Layer { Sample = "RING 2" } },
                    Filter = 74, A = 0, D = 40, S = 70, R = 26,
                    VcfAmount = 16, VcfD = 40, VcfS = 45, VelToFilter = 40
                },

                new Patch
                {
                    Name = "RING CLNG",
                    Layers = new[] { new Layer { Sample = "RING 3" } },
                    Filter = 82, A = 0, D = 55, S = 0, R = 46, VelToLoudness = 55
                },

                new Patch
                {
                    Name = "SYNC LEAD",
                    Layers = new[] { new Layer { Sample = "BENT" } },
                    Filter = 70, A = 0, D = 36, S = 80, R = 24,
                    VcfAmount = 20, VcfD = 44, VcfS = 40,
                    VelToFilter = 50, LfoRate = 42, LfoDepth = 4, LfoDelay = 72
                },

                new Patch
                {
                    Name = "GLASS BEL",
                    Layers = new[]
                    {
                        new Layer { Sample = "GLASS" },
                        new Layer { Sample = "SINE", Transpose = 19, Loudness = -18 }
                    },
                    Filter = 86, A = 0, D = 52, S = 0, R = 48, VelToLoudness = 60
                },

                new Patch
                {
                    Name = "ORGAN",
                    Layers = new[] { new Layer { Sample = "ORGAN" } },
                    Filter = 90, KeyToFilter = 20,
                    A = 0, D = 0, S = 99, R = 8, VelToLoudness = 10
                },

                //
                // One-shot, so it plays through whatever the key does - which is what the
                // flag is for, and what makes a noise hit behave like a drum rather than a
                // held note.
                //
                new Patch
                {
                    Name = "FIFTHS",
                    Layers = new[] { new Layer { Sample = "FIFTHS" } },
                    Filter = 76, A = 0, D = 40, S = 84, R = 28,
                    VcfAmount = 12, VcfD = 46, VcfS = 50, VelToFilter = 35
                },

                new Patch
                {
                    Name = "REED ORG",
                    Layers = new[] { new Layer { Sample = "REED" } },
                    Filter = 88, KeyToFilter = 25,
                    A = 0, D = 0, S = 99, R = 10, VelToLoudness = 15
                },

                new Patch
                {
                    Name = "SOUR LEAD",
                    Layers = new[] { new Layer { Sample = "SEVENTH" } },
                    Filter = 72, A = 0, D = 36, S = 80, R = 26,
                    VcfAmount = 14, VcfD = 42, VcfS = 45,
                    VelToFilter = 45, LfoRate = 46, LfoDepth = 5, LfoDelay = 68
                },

                new Patch
                {
                    Name = "STACK 57",
                    Layers = new[]
                    {
                        new Layer { Sample = "STACK57", Fine = -4 },
                        new Layer { Sample = "STACK57", Fine =  4 }
                    },
                    Filter = 70, A = 6, D = 46, S = 82, R = 34,
                    VcfAmount = 12, VcfD = 50, VcfS = 50
                },

                new Patch
                {
                    Name = "FIFTHS UP",
                    Layers = new[] { new Layer { Sample = "FIFTHS UP" } },
                    Filter = 78, A = 16, D = 54, S = 86, R = 44,
                    VcfAmount = 12, VcfA = 25, VcfD = 58, VcfS = 55,
                    LfoRate = 26, LfoDepth = 4, LfoDelay = 76
                },

                new Patch
                {
                    Name = "CZ LEAD",
                    Layers = new[] { new Layer { Sample = "PD SINE" } },
                    Filter = 88, A = 0, D = 38, S = 82, R = 26,
                    VelToFilter = 30, LfoRate = 44, LfoDepth = 4, LfoDelay = 70
                },

                new Patch
                {
                    Name = "CZ BRASS",
                    Layers = new[]
                    {
                        new Layer { Sample = "PD SAW", Fine = -4 },
                        new Layer { Sample = "PD SAW", Fine =  4 }
                    },
                    Filter = 80, A = 8, D = 44, S = 80, R = 30,
                    VcfAmount = 10, VcfA = 10, VcfD = 50, VcfS = 55, VelToFilter = 40
                },

                new Patch
                {
                    Name = "CZ ROCKER",
                    Layers = new[] { new Layer { Sample = "PD ROCK" } },
                    Filter = 84, A = 14, D = 56, S = 84, R = 44,
                    LfoRate = 24, LfoDepth = 4, LfoDelay = 78
                },

                new Patch
                {
                    Name = "WIND",
                    Layers = new[] { new Layer { Sample = "PINK" } },
                    Filter = 44, KeyToFilter = 25,
                    A = 40, D = 60, S = 75, R = 55,
                    VcfAmount = 20, VcfA = 55, VcfD = 70, VcfS = 45,
                    LfoRate = 12, LfoDepth = 3, LfoDelay = 70
                },

                new Patch
                {
                    Name = "RUMBLE",
                    Layers = new[]
                    {
                        new Layer { Sample = "BROWN" },
                        new Layer { Sample = "SINE", Transpose = -12, Loudness = -6 }
                    },
                    Filter = 34, KeyToFilter = 20, A = 20, D = 70, S = 80, R = 50
                },

                new Patch
                {
                    Name = "DISSOLVE",
                    Layers = new[] { new Layer { Sample = "DISSOLVE" } },
                    Filter = 70, A = 22, D = 65, S = 82, R = 60,
                    VcfAmount = 16, VcfA = 45, VcfD = 70, VcfS = 50
                },

                new Patch
                {
                    Name = "DRIFT LD",
                    Layers = new[]
                    {
                        new Layer { Sample = "DRIFT", Fine = -5 },
                        new Layer { Sample = "DRIFT", Fine =  5 }
                    },
                    Filter = 74, A = 4, D = 42, S = 84, R = 32,
                    VcfAmount = 12, VcfD = 46, VcfS = 45, VelToFilter = 35
                },

                new Patch
                {
                    Name = "GRIT BASS",
                    Layers = new[] { new Layer { Sample = "GRIT" } },
                    Filter = 46, KeyToFilter = 30,
                    A = 0, D = 36, S = 22, R = 18,
                    VcfAmount = 24, VcfD = 30, VcfS = 10,
                    VelToFilter = 50, VelToLoudness = 50
                },

                new Patch
                {
                    Name = "UNSTABLE",
                    Layers = new[] { new Layer { Sample = "UNSTABLE" } },
                    Filter = 68, A = 16, D = 58, S = 76, R = 52,
                    VcfAmount = 18, VcfA = 35, VcfD = 62, VcfS = 50,
                    LfoRate = 20, LfoDepth = 5, LfoDelay = 80
                },

                //
                // STACKS: layers with their own envelopes and their own levels.
                //
                // A struck attack over a slow body is the oldest trick in sampling and
                // the machine does it in one programme, because a keygroup carries its
                // own envelope. The attack layer decays to nothing while the body is
                // still arriving.
                //
                new Patch
                {
                    Name = "BELL PAD",
                    Layers = new[]
                    {
                        // struck, bright, gone in a moment
                        new Layer { Sample = "GLASS", Loudness = -4,
                                    A = 0, D = 34, S = 0, R = 30, Filter = 92 },

                        // and the pad underneath, arriving as the bell leaves
                        new Layer { Sample = "MORPH SS", Loudness = -10, Fine = 4,
                                    A = 48, D = 70, S = 88, R = 68, Filter = 60,
                                    VcfAmount = 16, VcfA = 55, VcfD = 75, VcfS = 55 }
                    },
                    LfoRate = 24, LfoDepth = 4, LfoDelay = 82
                },

                new Patch
                {
                    Name = "HAMMER",
                    Layers = new[]
                    {
                        // the knock: noise, open, and immediately over
                        new Layer { Sample = "NOISE", Loudness = -8,
                                    A = 0, D = 12, S = 0, R = 10, Filter = 72,
                                    VelToLoudness = 70 },

                        // the note it knocked
                        new Layer { Sample = "FM 1-1",
                                    A = 0, D = 50, S = 34, R = 34, Filter = 84 }
                    },
                    VelToLoudness = 55
                },

                new Patch
                {
                    Name = "SWELL",
                    Layers = new[]
                    {
                        new Layer { Sample = "SAW", Fine = -5, Loudness = -6,
                                    A = 0,  D = 44, S = 60, R = 36, Filter = 66 },

                        // the same wave, detuned, taking its time - so the sound opens
                        // out as it is held rather than starting where it ends
                        new Layer { Sample = "SAW", Fine = 6, Loudness = -2,
                                    A = 52, D = 70, S = 90, R = 60, Filter = 80,
                                    VcfAmount = 18, VcfA = 60, VcfD = 80, VcfS = 60 }
                    }
                },

                new Patch
                {
                    Name = "BREATHY",
                    Layers = new[]
                    {
                        // air, quiet and always there
                        new Layer { Sample = "PINK", Loudness = -22,
                                    A = 26, D = 60, S = 70, R = 50, Filter = 52,
                                    KeyToFilter = 20 },

                        new Layer { Sample = "REED", Loudness = -2,
                                    A = 8, D = 48, S = 84, R = 32, Filter = 78 }
                    },
                    LfoRate = 34, LfoDepth = 5, LfoDelay = 70
                },

                //
                // Three layers an octave apart at falling levels - an organ registration
                // built out of keygroups rather than drawbars, each with its own filter so
                // the top one stays bright while the bottom stays round.
                //
                new Patch
                {
                    Name = "THREE UP",
                    Layers = new[]
                    {
                        new Layer { Sample = "SQUARE", Transpose = -12, Loudness = 0,
                                    Filter = 54, A = 0, D = 40, S = 92, R = 24 },

                        new Layer { Sample = "SAW", Loudness = -8,
                                    Filter = 74, A = 2, D = 44, S = 86, R = 28 },

                        new Layer { Sample = "TRIANGLE", Transpose = 12, Loudness = -16,
                                    Filter = 90, A = 6, D = 50, S = 78, R = 34 }
                    },
                    VelToFilter = 35
                },

                new Patch
                {
                    Name = "NOISE HIT",
                    Layers = new[] { new Layer { Sample = "NOISE" } },
                    Filter = 64, KeyToFilter = 40,
                    A = 0, D = 18, S = 0, R = 14,
                    VcfAmount = 28, VcfD = 14, VcfS = 0,
                    VelToFilter = 60, VelToLoudness = 60,
                    OneShot = true
                },

                //
                // SPLITS: one programme, two or three instruments, chosen by where the
                // hands are.
                //
                // Nothing new is needed for this. A keygroup already IS a range of keys -
                // it has been carrying 0..127 all along - so giving one the bottom two
                // octaves and another the rest costs a programme nothing and a sample
                // nothing. Where the ranges meet they simply stop; where they overlap they
                // layer, which is the only difference between a split and a stack.
                //
                new Patch
                {
                    Name = "SPLIT BS",
                    Layers = new[]
                    {
                        // Below middle C: short, closed, and barely tracking the keyboard,
                        // so the bottom octave sounds like the one above it.
                        new Layer { Sample = "SQUARE", HighKey = 59,
                                    Filter = 44, KeyToFilter = 26,
                                    A = 0, D = 34, S = 18, R = 16,
                                    VcfAmount = 24, VcfD = 28, VcfS = 8,
                                    VelToFilter = 50, VelToLoudness = 45 },

                        // From middle C up, the right hand gets a lead instead.
                        new Layer { Sample = "FM 1-2", LowKey = 60, Loudness = -4,
                                    Filter = 82, A = 0, D = 36, S = 84, R = 26,
                                    VcfAmount = 12, VcfD = 44, VcfS = 45,
                                    VelToFilter = 40,
                                    LfoRate = 44, LfoDepth = 4, LfoDelay = 70 }
                    }
                },

                //
                // Three ways up the keyboard, getting brighter as it goes - a zone change
                // you play into rather than reach for.
                //
                new Patch
                {
                    Name = "SPLIT LD",
                    Layers = new[]
                    {
                        new Layer { Sample = "SAW", HighKey = 47,
                                    Filter = 52, KeyToFilter = 30,
                                    A = 0, D = 40, S = 70, R = 22,
                                    VcfAmount = 18, VcfD = 34, VcfS = 30,
                                    VelToFilter = 45 },

                        new Layer { Sample = "BENT", LowKey = 48, HighKey = 83,
                                    Filter = 72, A = 0, D = 38, S = 82, R = 26,
                                    VcfAmount = 16, VcfD = 44, VcfS = 45,
                                    VelToFilter = 45,
                                    LfoRate = 42, LfoDepth = 4, LfoDelay = 72 },

                        new Layer { Sample = "PD SINE", LowKey = 84, Loudness = -4,
                                    Filter = 92, A = 0, D = 34, S = 80, R = 24,
                                    LfoRate = 46, LfoDepth = 5, LfoDelay = 66 }
                    }
                },

                //
                // Left hand holds an organ, right hand strikes a bell. Two instruments that
                // want opposite envelopes, which is exactly what a keygroup can give them.
                //
                new Patch
                {
                    Name = "SPLIT KEY",
                    Layers = new[]
                    {
                        new Layer { Sample = "REED", HighKey = 59,
                                    Filter = 84, KeyToFilter = 25,
                                    A = 0, D = 0, S = 99, R = 10, VelToLoudness = 15 },

                        new Layer { Sample = "GLASS", LowKey = 60, Loudness = -2,
                                    Filter = 88, A = 0, D = 50, S = 0, R = 46,
                                    VelToLoudness = 60 },

                        // The bell's octave, over the top of it and quiet - an overlap
                        // rather than a boundary, so the top of the keyboard is a stack.
                        new Layer { Sample = "SINE", LowKey = 72, Transpose = 12,
                                    Loudness = -18,
                                    Filter = 99, A = 0, D = 44, S = 0, R = 40 }
                    }
                },

                //
                // Weather at the bottom, events at the top: held rumble under the left
                // hand, one-shot cracks under the right.
                //
                new Patch
                {
                    Name = "SPLIT FX",
                    Layers = new[]
                    {
                        new Layer { Sample = "BROWN", HighKey = 47,
                                    Filter = 34, KeyToFilter = 20,
                                    A = 20, D = 70, S = 80, R = 50 },

                        new Layer { Sample = "PINK", LowKey = 48, HighKey = 83,
                                    Filter = 48, KeyToFilter = 25,
                                    A = 30, D = 60, S = 72, R = 48,
                                    VcfAmount = 20, VcfA = 50, VcfD = 70, VcfS = 45,
                                    LfoRate = 12, LfoDepth = 3, LfoDelay = 70 },

                        new Layer { Sample = "WHITE", LowKey = 84, Loudness = -6,
                                    Filter = 70, KeyToFilter = 40,
                                    A = 0, D = 16, S = 0, R = 12,
                                    VcfAmount = 28, VcfD = 14, VcfS = 0,
                                    VelToFilter = 60, VelToLoudness = 60 }
                    }
                },

                //
                // HARD AND SOFT: a different wave when the key is hit, not just a louder
                // one.
                //
                // Velocity opening the filter is the cheap version of this and every patch
                // here already does it. What it cannot do is change what the oscillator IS,
                // and that is what happens on a real instrument: a string struck hard does
                // not play its quiet tone with the treble turned up, it plays a different
                // tone. The second zone of a keygroup gives exactly that, for the price of
                // naming a wave the disk is already carrying.
                //
                // Both zones share the keygroup's envelope, so these differ in tone,
                // tuning and level and not in attack - which is the machine's rule, not a
                // simplification.
                //
                new Patch
                {
                    Name = "VEL BASS",
                    Layers = new[]
                    {
                        new Layer { Sample = "SQUARE",
                                    Hard = new Struck { Sample = "GRIT", At = 76,
                                                        Filter = 62, Loudness = 2 } }
                    },
                    Filter = 44, KeyToFilter = 28,
                    A = 0, D = 36, S = 20, R = 16,
                    VcfAmount = 20, VcfD = 30, VcfS = 10,
                    VelToFilter = 40, VelToLoudness = 45
                },

                //
                // Lean on it and it goes sour: the seventh is a third of a semitone below
                // the note a keyboard would play for it, so the hard strike arrives with a
                // beat in it that the soft one does not have.
                //
                new Patch
                {
                    Name = "VEL LEAD",
                    Layers = new[]
                    {
                        new Layer { Sample = "SAW",
                                    Hard = new Struck { Sample = "SEVENTH", At = 88,
                                                        Filter = 84, Loudness = 2 } }
                    },
                    Filter = 74, A = 0, D = 34, S = 84, R = 26,
                    VcfAmount = 12, VcfD = 44, VcfS = 45, VelToFilter = 30,
                    LfoRate = 44, LfoDepth = 4, LfoDelay = 70
                },

                // Right at the top of the range, so it is something you have to mean.
                new Patch
                {
                    Name = "VEL RING",
                    Layers = new[]
                    {
                        new Layer { Sample = "BENT",
                                    Hard = new Struck { Sample = "RING 2", At = 104,
                                                        Filter = 88, Loudness = 3 } }
                    },
                    Filter = 70, A = 0, D = 36, S = 80, R = 24,
                    VcfAmount = 16, VcfD = 44, VcfS = 40, VelToFilter = 35
                },

                new Patch
                {
                    Name = "VEL SWEEP",
                    Layers = new[]
                    {
                        new Layer { Sample = "PWM", Fine = -6,
                                    Hard = new Struck { Sample = "DISSOLVE", At = 90,
                                                        Fine = -6, Filter = 76 } },

                        new Layer { Sample = "PWM", Fine = 6, Loudness = -4,
                                    Hard = new Struck { Sample = "DISSOLVE", At = 90,
                                                        Fine = 6, Filter = 76, Loudness = -4 } }
                    },
                    Filter = 64, A = 34, D = 58, S = 88, R = 56,
                    VcfAmount = 14, VcfA = 30, VcfD = 62, VcfS = 58,
                    LfoRate = 28, LfoDepth = 5, LfoDelay = 78
                },

                //
                // The oldest velocity switch there is: a sine held quietly, and the FM tone
                // when it is struck. Every digital piano of the era was doing this, and the
                // switch is low because a player expects the tone to arrive early.
                //
                new Patch
                {
                    Name = "VEL EP",
                    Layers = new[]
                    {
                        new Layer { Sample = "SINE",
                                    Hard = new Struck { Sample = "FM 1-1", At = 64,
                                                        Filter = 88, Loudness = 2 } }
                    },
                    Filter = 82, A = 0, D = 48, S = 30, R = 34,
                    VelToLoudness = 55, VelToFilter = 25
                },

                new Patch
                {
                    Name = "VEL BELL",
                    Layers = new[]
                    {
                        new Layer { Sample = "GLASS",
                                    Hard = new Struck { Sample = "FM BELL", At = 96,
                                                        Filter = 92, Loudness = 2 } },

                        new Layer { Sample = "SINE", Transpose = 19, Loudness = -18 }
                    },
                    Filter = 86, A = 0, D = 54, S = 0, R = 48, VelToLoudness = 60
                },

                //
                // A drum, near enough: pink air when it is brushed and a white crack when
                // it is hit, played through whether the key is held or not.
                //
                new Patch
                {
                    Name = "VEL HIT",
                    Layers = new[]
                    {
                        new Layer { Sample = "PINK",
                                    Hard = new Struck { Sample = "WHITE", At = 72,
                                                        Filter = 84, Loudness = 3 } }
                    },
                    Filter = 60, KeyToFilter = 40,
                    A = 0, D = 18, S = 0, R = 14,
                    VcfAmount = 26, VcfD = 14, VcfS = 0,
                    VelToFilter = 50, VelToLoudness = 60,
                    OneShot = true
                }
            };
        }
    }
}
