using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// A grand piano, drawn rather than recorded.
    ///
    /// A multisample like the factory ones: one one-shot every fourth or fifth, each played
    /// across the few keys around it, with the machine's filter and envelope doing what they
    /// did to the recorded pianos of the day. What is different is where the samples come
    /// from. Each is a sum of the string's partials worked out from the physics of a struck
    /// string, so the disk carries nobody's recording.
    ///
    /// WHAT MAKES IT A PIANO AND NOT AN ORGAN
    ///
    /// Four things, each measured off a sampled grand of the period and fitted as a smooth
    /// curve across the keyboard rather than copied partial by partial:
    ///
    ///   STRETCH   A piano string is stiff, so its partials run sharp of the harmonic
    ///             series by fn = n f0 sqrt(1 + B n^2). B is about 4.5e-4 through the
    ///             bass and middle and climbs tenfold to the top, where the strings are
    ///             short and thick for their length. It is the stretch that makes a low
    ///             note sound like a piano rather than a reed.
    ///
    ///   STRIKE    The hammer hits about an eighth of the way along the string, so the
    ///             partials with a node there are barely excited: |sin(pi n beta)|. That is
    ///             the hollow in the spectrum around the eighth partial.
    ///
    ///   DECAY     Every partial dies at its own rate, faster the higher it is - the top
    ///             of the sound goes first, which is why a held note darkens. And each
    ///             dies in two stages: a fast prompt sound and a slow aftersound, from the
    ///             strings of a unison handing energy to each other through the bridge.
    ///
    ///   UNISON    Two or three strings to a note, never quite in tune, beating slowly.
    ///             It is the beating that keeps a sustained piano note alive, and why one
    ///             sine per partial sounds like a music box.
    ///
    /// And a knock at the front: the hammer and the frame, a few milliseconds of filtered
    /// noise and some low thud, well under the strings.
    ///
    /// WHY ONLY NINE
    ///
    /// The disk would take more, but the machine would not: a stock S950 holds about half a
    /// million words of sample, and a piano is a memory hog because it is long. Nine at
    /// 25 kHz, between a second and a quarter and two and a quarter long, is what fits with
    /// room to spare - the machine's own decay envelope finishes each note off well before
    /// the sample would have, so a longer sample would be memory spent on silence.
    /// </summary>
    internal static class Piano
    {
        /// <summary>One sampled note: where it sits, and how much of it the disk keeps.</summary>
        internal sealed class Note
        {
            public string Name;
            public int Root;           // MIDI note, C3 = 60 as the machine counts
            public int Low, High;      // the keys its keygroup answers
            public double Seconds;
            public int Rate = 25000;
            public int Filter;         // the keygroup's cutoff, before key tracking
            public int Loudness;       // the zone's trim, to level the keyboard
        }

        /// <summary>
        /// The notes, and the keys each one covers.
        ///
        /// A sample stretched more than four or five semitones either way starts to sound
        /// like what it is, the low end going woolly and the top thin. So the ranges are
        /// kept within that, and an extra sample at E1 carries the bottom octave rather
        /// than C2 being dragged down a whole octave to reach it.
        ///
        /// The cutoffs fall as the samples rise, because the filter also tracks the key one
        /// for one: without the step down, the top of the keyboard would be wide open and
        /// glassy beside a middle that has been tamed.
        /// </summary>
        public static List<Note> Notes()
        {
            return new List<Note>
            {
                new Note { Name = "PIANO E1", Root = 40, Low = 21, High = 44, Seconds = 2.30, Rate = 20000, Filter = 74, Loudness = 4 },
                new Note { Name = "PIANO C2", Root = 48, Low = 45, High = 51, Seconds = 2.30, Rate = 20000, Filter = 71, Loudness = 1 },
                new Note { Name = "PIANO G2", Root = 55, Low = 52, High = 57, Seconds = 2.20, Filter = 70, Loudness = 2 },
                new Note { Name = "PIANO C3", Root = 60, Low = 58, High = 63, Seconds = 2.20, Filter = 66, Loudness = 13 },
                new Note { Name = "PIANO G3", Root = 67, Low = 64, High = 69, Seconds = 2.10, Filter = 64, Loudness = 10 },
                new Note { Name = "PIANO C4", Root = 72, Low = 70, High = 75, Seconds = 2.00, Filter = 59, Loudness = 10 },
                new Note { Name = "PIANO G4", Root = 79, Low = 76, High = 81, Seconds = 1.80, Filter = 53, Loudness = 10 },
                new Note { Name = "PIANO C5", Root = 84, Low = 82, High = 87, Seconds = 1.60, Filter = 47, Loudness = 5 },
                new Note { Name = "PIANO G5", Root = 91, Low = 88, High = 108, Seconds = 1.40, Filter = 43, Loudness = -1 },
            };
        }

        // --------------------------------------------------------------- the string

        /// <summary>Stiffness: how far the partials run sharp. See STRETCH above.</summary>
        static double Inharmonicity(int note)
        {
            if (note > 58) return 4.5e-4 * Math.Pow(10, 0.03 * (note - 58));
            return 4.5e-4 * (1 + 0.03 * Math.Max(0, 48 - note));
        }

        /// <summary>Where along the string the hammer lands, as a fraction of its length.</summary>
        static double StrikePoint(int note)
        {
            return 0.122 - 0.0006 * (note - 40);
        }

        /// <summary>
        /// How loud the partials above the fundamental sit beside it, in dB.
        ///
        /// The fundamental leads everywhere on a grand heard from the player's seat, and
        /// by most in the low-middle; the treble is close to level across what it has.
        /// </summary>
        static double Overtones(int note)
        {
            return Lerp(note, new double[,] { { 40, -20 }, { 48, -23 }, { 55, -20 }, { 60, -15 },
                                              { 72, -12 }, { 79, -8 }, { 84, -10 }, { 91, -13 } });
        }

        /// <summary>
        /// How steeply the overtones fall away, as a power of the partial number.
        ///
        /// Steep in the bass, where a long string's upper partials are many and faint, and
        /// flat from the middle up, where the few partials under the bandwidth are close to
        /// level and the strike point and the bandwidth do the shaping between them.
        /// </summary>
        static double Tilt(int note)
        {
            return Lerp(note, new double[,] { { 40, 0.50 }, { 55, 0.40 }, { 60, 0.15 }, { 72, 0.0 } });
        }

        /// <summary>
        /// How fast a partial falls in the prompt sound, in dB per second.
        ///
        /// A floor for the note, higher up the keyboard, plus a term in the partial's own
        /// frequency - the losses in a string and its bridge grow with frequency, which is
        /// what turns a struck note mellow as it rings.
        /// </summary>
        static double DecayDb(int note, double hz)
        {
            double floor = 8.0 + 4.0 * Math.Max(0, note - 48) / 43.0;
            return floor + 3.2 * hz / 1000.0;
        }

        /// <summary>The aftersound: how loud it is under the prompt sound, and how much slower.</summary>
        const double AfterLevel = 0.28, AfterRate = 0.40;

        /// <summary>
        /// Where the bandwidth ends, in hertz.
        ///
        /// Sampled pianos of the era were recorded through anti-alias filters well inside
        /// the sample rate, and the top octave of hiss and hammer they took away is a lot of
        /// what makes them sit in a mix rather than on top of it. So this one is too.
        /// </summary>
        const double Bandwidth = 8500;

        /// <summary>The level of the hammer knock, against the strings.</summary>
        const double Knock = 0.07;

        // ------------------------------------------------------------------ render

        /// <summary>
        /// One note, as 12-bit words.
        ///
        /// Seeded from the note's name, so the same disk comes out byte for byte every time
        /// it is made - which is what lets it be checked against a rebuild.
        /// </summary>
        public static short[] Render(Note note)
        {
            int rate = note.Rate;
            int n = (int)Math.Round(note.Seconds * rate) & ~1;
            var rng = new Random(Seed(note.Name));
            var wave = new double[n];

            double f0 = 440.0 * Math.Pow(2, (note.Root - 69) / 12.0);
            double B = Inharmonicity(note.Root);
            double beta = StrikePoint(note.Root);
            double over = Math.Pow(10, Overtones(note.Root) / 20.0);
            double top = Math.Min(0.45 * rate, Bandwidth * 1.35);

            //
            // The unison: one string in the bottom octave, two through the bass, three from
            // there up, each a cent or so off its neighbours. Never exactly in tune, and
            // never by the same amount twice - the seed sees to both.
            //
            // And not equally loud, either. Three strings of one level can cancel almost
            // completely as they drift through each other's phase, and a note that dips by
            // fifteen dB every second is a tremolo rather than a unison.
            //
            int strings = note.Root < 35 ? 1 : note.Root < 50 ? 2 : 3;
            var cents = new double[strings];
            var weight = new double[strings];
            double weights = 0;
            for (int s = 0; s < strings; s++)
            {
                cents[s] = strings == 1 ? 0 : (s - (strings - 1) / 2.0) * (0.3 + 0.7 * rng.NextDouble());
                weight[s] = s == 0 ? 1.0 : 0.45 + 0.25 * rng.NextDouble();
                weights += weight[s];
            }

            double tilt = Tilt(note.Root);

            // The hammer's contact time: longer for the soft heavy hammers of the bass.
            double contact = 0.0025 - 0.0018 * (note.Root - 40) / 51.0;
            int rise = Math.Max(2, (int)(contact * rate));

            for (int k = 1; ; k++)
            {
                double hz = k * f0 * Math.Sqrt(1 + B * k * k) / Math.Sqrt(1 + B);
                if (hz >= top) break;

                //
                // The level: the strike point's comb, the overtone balance, a gentle tilt
                // downward, the recording's bandwidth, and a couple of dB of irregularity -
                // no real soundboard is flat, and a perfectly smooth series sounds drawn.
                //
                double strike = Math.Abs(Math.Sin(Math.PI * k * beta));
                double level = k == 1 ? 1.0
                             : over * (0.15 + strike) / 0.79 * Math.Pow(k / 2.0, -tilt);
                level *= 1.0 / Math.Sqrt(1 + Math.Pow(hz / Bandwidth, 12));
                level *= Math.Pow(10, (rng.NextDouble() - 0.5) * 4.0 / 20.0);

                double prompt = DecayDb(note.Root, hz) / 8.686;          // nepers per second
                double gPrompt = Math.Exp(-prompt / rate);
                double gAfter = Math.Exp(-prompt * AfterRate / rate);

                for (int s = 0; s < strings; s++)
                {
                    double f = hz * Math.Pow(2, cents[s] / 1200.0);
                    double w = 2 * Math.PI * f / rate;
                    double phase = rng.NextDouble() * 0.3;                // nearly together
                    double a = level * weight[s] / weights;

                    // A rotating phasor, renormalised now and again so it cannot drift.
                    double c = Math.Cos(w), sn = Math.Sin(w);
                    double re = Math.Cos(phase), im = Math.Sin(phase);
                    double ep = 1 - AfterLevel, ea = AfterLevel;

                    for (int i = 0; i < n; i++)
                    {
                        wave[i] += a * (ep + ea) * im;
                        double r2 = re * c - im * sn;
                        im = re * sn + im * c;
                        re = r2;
                        ep *= gPrompt; ea *= gAfter;

                        if ((i & 1023) == 1023)
                        {
                            double m = 1.0 / Math.Sqrt(re * re + im * im);
                            re *= m; im *= m;
                        }
                    }
                }
            }

            // The strings start together when the hammer lets go, not with a step.
            for (int i = 0; i < rise && i < n; i++)
                wave[i] *= 0.5 - 0.5 * Math.Cos(Math.PI * i / rise);

            AddKnock(wave, rate, f0, rng);

            //
            // Finish the tail by hand. A sampled piano note is cut off while still ringing,
            // and a cut at -30 dB is a click; the last fifth taken down by a raised cosine
            // reads only as the note dying a little quicker, as in Drums.
            //
            int fall = (int)(0.2 * n);
            for (int i = 0; i < fall; i++)
                wave[n - fall + i] *= 0.5 * (1 + Math.Cos(Math.PI * i / (fall - 1.0)));

            return Dithered(wave, rng);
        }

        /// <summary>
        /// The hammer and the frame: a short band of noise somewhere above the note and a
        /// low thud under it. Neither is a pitch; together they are the difference between
        /// a key struck and a note switched on.
        /// </summary>
        static void AddKnock(double[] wave, int rate, double f0, Random rng)
        {
            double peak = 0;
            foreach (double v in wave) peak = Math.Max(peak, Math.Abs(v));

            int len = Math.Min(wave.Length, rate / 10);

            // Noise through a two-pole band-pass, falling in a few milliseconds.
            double centre = Math.Min(3000, Math.Max(500, 3 * f0));
            double r = Math.Exp(-Math.PI * centre / 1.2 / rate);
            double b1 = 2 * r * Math.Cos(2 * Math.PI * centre / rate), b2 = -r * r;
            double y1 = 0, y2 = 0, noise = 0;
            var burst = new double[len];
            for (int i = 0; i < len; i++)
            {
                double x = rng.NextDouble() * 2 - 1;
                double y = (1 - r) * x + b1 * y1 + b2 * y2;
                y2 = y1; y1 = y;
                burst[i] = y * Math.Exp(-i / (0.006 * rate));
                noise = Math.Max(noise, Math.Abs(burst[i]));
            }

            // And the thud: three damped low modes, for the frame and the board.
            double[] modes = { 72, 118, 196 };
            for (int i = 0; i < len; i++)
            {
                double t = i / (double)rate, thud = 0;
                for (int m = 0; m < modes.Length; m++)
                    thud += Math.Sin(2 * Math.PI * modes[m] * t) * Math.Exp(-t / (0.035 - 0.008 * m)) / (m + 1);

                wave[i] += peak * Knock * (burst[i] / (noise > 0 ? noise : 1) + 0.8 * thud);
            }
        }

        /// <summary>
        /// Scaled to the 12 bits the machine stores, with a bit of triangular dither.
        ///
        /// Without it the last couple of seconds of a quiet decay are a dozen steps high and
        /// the rounding is audible as grit on the tail. With it they are a smooth fade into a
        /// very faint hiss, which is what the machine's own converters would have added.
        /// </summary>
        static short[] Dithered(double[] wave, Random rng)
        {
            double peak = 0;
            foreach (double v in wave) peak = Math.Max(peak, Math.Abs(v));
            double scale = 2040.0 / (peak > 0 ? peak : 1);

            var outp = new short[wave.Length];
            for (int i = 0; i < wave.Length; i++)
            {
                double d = rng.NextDouble() - rng.NextDouble();
                int v = (int)Math.Round(wave[i] * scale + d);
                outp[i] = (short)(v > 2047 ? 2047 : (v < -2048 ? -2048 : v));
            }
            return outp;
        }

        // ------------------------------------------------------------ the disk

        public static List<Patches.Wave> Waves()
        {
            var outp = new List<Patches.Wave>();

            foreach (var note in Notes())
            {
                var p = note;
                outp.Add(new Patches.Wave
                {
                    Name = p.Name,
                    Rate = p.Rate,
                    Root = p.Root,
                    Oneshot = () => Render(p)
                });
            }

            return outp;
        }

        /// <summary>
        /// The programmes: the piano, and three more ways of playing the same nine samples.
        ///
        /// Each is a keygroup per sample, with the envelope a piano wants from this machine:
        /// no attack, a long decay to nothing, and a release that lets a note ring a moment
        /// past the key. The sample's own decay is the sound; the machine's finishes it.
        /// </summary>
        public static List<Patch> Programmes()
        {
            var grand = new Patch
            {
                Name = "GRAND PNO",
                A = 0, D = 90, S = 0, R = 50,
                KeyToFilter = 50, VelToFilter = 12, VelToLoudness = 30,
                LfoModwheel = 0
            };
            grand.Layers = Keys(0, 0, 0);

            //
            // Brighter and harder, for a band: the filter opened and pushed further by the
            // velocity, so it cuts through when played hard and still sits back when not.
            //
            var bright = grand.With("BRIGHT PNO", Keys(12, 0, 0));
            bright.VelToFilter = 24;

            // Rounder, for under a vocal: the filter down, and a longer release.
            var mellow = grand.With("MELLOW PNO", Keys(-14, 0, 0));
            mellow.R = 58;

            //
            // And out of tune on purpose: each sample twice, an eighth of a semitone apart.
            // Two keygroups to a key, so it plays four notes at once rather than eight.
            //
            // Two layers sum louder than one, so each is trimmed by the three dB it gains.
            var honky = grand.With("HONKYTONK", Join(Keys(4, 0, -7), Keys(4, 2, -7)));
            honky.R = 40;

            return new List<Patch> { grand, bright, mellow, honky };
        }

        /// <summary>A keygroup for each note, its filter and fine tune moved by as much as asked.</summary>
        static Layer[] Keys(int filter, int sixteenths, int loudness)
        {
            var outp = new List<Layer>();

            foreach (var note in Notes())
            {
                outp.Add(new Layer
                {
                    Sample = note.Name,
                    LowKey = note.Low,
                    HighKey = note.High,
                    Fine = sixteenths * 16,
                    Filter = Math.Max(0, Math.Min(99, note.Filter + filter)),
                    Loudness = note.Loudness + loudness
                });
            }

            return outp.ToArray();
        }

        static Layer[] Join(Layer[] a, Layer[] b)
        {
            var outp = new Layer[a.Length + b.Length];
            a.CopyTo(outp, 0);
            b.CopyTo(outp, a.Length);
            return outp;
        }

        static double Lerp(int note, double[,] table)
        {
            int last = table.GetLength(0) - 1;
            if (note <= table[0, 0]) return table[0, 1];
            if (note >= table[last, 0]) return table[last, 1];

            for (int i = 1; i <= last; i++)
            {
                if (note > table[i, 0]) continue;
                double t = (note - table[i - 1, 0]) / (table[i, 0] - table[i - 1, 0]);
                return table[i - 1, 1] + t * (table[i, 1] - table[i - 1, 1]);
            }

            return table[last, 1];
        }

        /// <summary>A name, as a number - FNV-1a, as in Drums.</summary>
        static int Seed(string name)
        {
            unchecked
            {
                uint h = 2166136261;
                foreach (char ch in name) { h ^= ch; h *= 16777619; }
                return (int)(h & 0x7FFFFFFF);
            }
        }
    }
}
