using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// A choir, singing "ooh" and "aah", drawn rather than recorded.
    ///
    /// A multisample like the piano's, but held rather than struck: each sample is a second
    /// and a half of a section singing one vowel on one note, looped whole, with the machine's
    /// envelope swelling it in and letting it go - the way the choir disks of the day were
    /// made to sustain from a few seconds of tape.
    ///
    /// WHAT MAKES IT VOICES AND NOT AN ORGAN
    ///
    ///   THE SOURCE    The vocal folds: a buzz whose harmonics fall away, steeper for a soft
    ///                 "ooh" than an open "aah", and steeper for the women's voices, which
    ///                 are breathier. A little breath noise rides on it, pulsing with each
    ///                 opening of the folds; and above the vowel, the hiss of the breath
    ///                 itself - air - on an open "aah".
    ///
    ///   THE VOWEL     The throat and mouth: four or five resonances - formants - that the
    ///                 buzz is filtered through, and that say which vowel it is wherever the
    ///                 pitch goes. "Ooh" has its first two low and close (300 and 870 Hz in a
    ///                 man); "aah" has them high and wide (730 and 1090). Women's sit some
    ///                 15% higher, and a soprano singing above her first formant moves it up
    ///                 to her note, as singers do, so the top does not go thin and pinched.
    ///
    ///   THE CHOIR     Not one singer but eight, never quite together: each a few cents
    ///                 off the others, with a vibrato of its own speed and depth, a slow
    ///                 wander of pitch and level, and a throat of slightly different size.
    ///                 It is the eight drifting through each other that makes a choir rather
    ///                 than a voice through a chorus pedal.
    ///
    /// WHY IT LOOPS WITHOUT A JOIN
    ///
    /// The whole sample is the loop, so its end must run straight into its start. Everything
    /// that moves is made to come round exactly in the sample's length: each voice sings a
    /// whole number of cycles in it, its vibrato and wander a whole number of turns, and the
    /// formant filters are run round the sample twice, so the second time they start where
    /// they finished. The sample rate is fixed, so it is the sample's length that is chosen
    /// to make the pitch land exactly on its note.
    /// </summary>
    internal static class Choir
    {
        internal enum Vowel { Ooh, Aah }

        /// <summary>One sampled note: its key, the keys it covers, and how long its loop is.</summary>
        internal sealed class Note
        {
            public string Name;
            public Vowel Vowel;
            public int Root;           // MIDI note, C3 = 60 as the machine counts
            public int Low, High;
            public double Seconds;     // about; the length is trimmed to put the pitch on the note
        }

        /// <summary>The sample rate. Voices have little above 8 kHz worth the words.</summary>
        const int Rate = 20000;

        /// <summary>Singers to a sample.</summary>
        const int Voices = 8;

        //
        // The keys: a sample every seventh semitone, each played no more than three or four
        // either way - a voice stretched further turns into a chipmunk or a giant. The bottom
        // one is the men's, the top two the women's, and the middle ones a mixed section.
        //
        static readonly int[,] Keys =
        {
            // root  low  high
            {  45,   21,  48 },     // A1
            {  52,   49,  55 },     // E2
            {  59,   56,  62 },     // B2
            {  65,   63,  68 },     // F3
            {  72,   69,  75 },     // C4
            {  79,   76, 108 },     // G4
        };

        static readonly string[] KeyNames = { "A1", "E2", "B2", "F3", "C4", "G4" };

        public static List<Note> Notes()
        {
            var outp = new List<Note>();
            foreach (Vowel v in new[] { Vowel.Ooh, Vowel.Aah })
                for (int i = 0; i < Keys.GetLength(0); i++)
                    outp.Add(new Note
                    {
                        Name = (v == Vowel.Ooh ? "OOHS " : "AAHS ") + KeyNames[i],
                        Vowel = v,
                        Root = Keys[i, 0],
                        Low = Keys[i, 1],
                        High = Keys[i, 2],
                        // longer at the bottom, where a cycle more or less is more cents
                        Seconds = Keys[i, 0] < 50 ? 1.8 : 1.5
                    });
            return outp;
        }

        // --------------------------------------------------------------- the vowels

        //
        // Formant frequencies, Hz, for a man and for a woman (Peterson and Barney's averages,
        // with a fourth and fifth from the usual tables), and their bandwidths. A voice is
        // placed between the two by how high its section sings.
        //
        static readonly double[] OohMan   = { 300,  870, 2240, 3300, 4100 };
        static readonly double[] OohWoman = { 370,  950, 2670, 3800, 4700 };
        static readonly double[] AahMan   = { 730, 1090, 2440, 3300, 4100 };
        static readonly double[] AahWoman = { 850, 1220, 2810, 3800, 4700 };
        static readonly double[] Widths   = {  70,   90,  130,  200,  260 };

        /// <summary>How far toward a woman's voice a section singing this note is: 0 to 1.</summary>
        static double Womanly(int root)
        {
            return Math.Max(0, Math.Min(1, (root - 50) / 20.0));
        }

        // ------------------------------------------------------------------ render

        /// <summary>
        /// One note, as 12-bit words, looping.
        ///
        /// Seeded from the note's name, so the same disk comes out byte for byte every time.
        /// </summary>
        public static short[] Render(Note note)
        {
            var rng = new Random(Seed(note.Name));

            double f0 = 440.0 * Math.Pow(2, (note.Root - 69) / 12.0);

            // The loop: a whole number of cycles of the note, and as many samples as make
            // that number of cycles land exactly on the note's pitch.
            int cycles = (int)Math.Round(f0 * note.Seconds);
            int n = (int)Math.Round(cycles * Rate / f0);
            double loop = n / (double)Rate;                 // seconds, exactly

            double womanly = Womanly(note.Root);
            var mix = new double[n];

            //
            // How far off the note each singer is, as whole cycles more or fewer in the loop:
            // in pairs, one sharp and one as flat, so the section as a whole sits on the note
            // however the eight fall.
            //
            double centsPerCycle = 1200 * Math.Log((cycles + 1.0) / cycles, 2);
            var offsets = new int[Voices];
            for (int v = 0; v + 1 < Voices; v += 2)
            {
                int o = (int)Math.Round(Math.Abs(Gauss(rng)) * 9.0 / centsPerCycle);
                o = Math.Max(0, Math.Min(3, o));
                offsets[v] = o;
                offsets[v + 1] = -o;
            }

            for (int v = 0; v < Voices; v++)
            {
                //
                // This singer: that far off the note, a vibrato of their own, a throat a few
                // percent bigger or smaller, and a little more or less woman than the section.
                //
                int mine = cycles + offsets[v];

                double vibHz = 4.6 + 1.2 * rng.NextDouble();
                double vibCents = note.Vowel == Vowel.Ooh ? 8 + 8 * rng.NextDouble()
                                                          : 12 + 10 * rng.NextDouble();
                double size = 1 + 0.04 * Gauss(rng);
                double her = Math.Max(0, Math.Min(1, womanly + 0.15 * Gauss(rng)));

                var source = Source(n, mine, loop, vibHz, vibCents, note.Vowel, her, rng);
                var formants = Formants(note.Vowel, her, size, f0);
                Filter(source, formants);
                if (note.Vowel == Vowel.Aah || AirOoh > 0)
                    AddAir(source, note.Vowel == Vowel.Ooh ? AirOoh : AirAah, rng);

                // A slow swell and fade of its own, so no one voice stays on top.
                double ap = rng.NextDouble() * 2 * Math.PI;
                int at = 1 + rng.Next(2);
                double level = 0.8 + 0.4 * rng.NextDouble();
                for (int i = 0; i < n; i++)
                    mix[i] += source[i] * level * (1 + 0.12 * Math.Sin(2 * Math.PI * at * i / n + ap));
            }

            // Nothing at 0 Hz: the sum of breath noise can sit a little off centre.
            double mean = 0;
            foreach (double x in mix) mean += x;
            mean /= n;
            for (int i = 0; i < n; i++) mix[i] -= mean;

            return Dithered(mix, rng);
        }

        /// <summary>
        /// One singer's vocal folds: a buzz with a vibrato and a wander, and breath on it.
        ///
        /// The pitch moves, so the buzz is built cycle by cycle from its phase rather than as
        /// fixed partials - each harmonic is a multiple of the same phase - and every
        /// movement is a whole number of turns in the loop. The phase is then scaled so the
        /// voice sings exactly `cycles` cycles in it, whatever the vibrato did on the way.
        /// </summary>
        static double[] Source(int n, int cycles, double loop, double vibHz, double vibCents,
                               Vowel vowel, double her, Random rng)
        {
            int vibTurns = Math.Max(1, (int)Math.Round(vibHz * loop));
            double vibPhase = rng.NextDouble() * 2 * Math.PI;

            // The wander: two slow turns of a few cents, under the vibrato.
            double w1 = rng.NextDouble() * 2 * Math.PI, w2 = rng.NextDouble() * 2 * Math.PI;
            double d1 = 3 + 3 * rng.NextDouble(), d2 = 2 + 2 * rng.NextDouble();

            var inc = new double[n];
            double sum = 0;
            for (int i = 0; i < n; i++)
            {
                double t = i / (double)n;               // 0..1 round the loop
                double cents = vibCents * Math.Sin(2 * Math.PI * vibTurns * t + vibPhase)
                             + d1 * Math.Sin(2 * Math.PI * t + w1)
                             + d2 * Math.Sin(2 * Math.PI * 2 * t + w2);
                inc[i] = Math.Pow(2, cents / 1200.0);
                sum += inc[i];
            }
            double scale = 2 * Math.PI * cycles / sum;

            //
            // How fast the harmonics fall: about 6 dB an octave for an open "aah" from a man,
            // steeper for "ooh", steeper again for the breathier women's voices.
            //
            double tilt = (vowel == Vowel.Ooh ? 1.45 : 1.05) + 0.25 * her;
            double f = cycles / loop;
            int harmonics = Math.Max(1, (int)(0.45 * Rate / (f * Math.Pow(2, 40 / 1200.0))));
            var amp = new double[harmonics + 1];
            for (int k = 1; k <= harmonics; k++)
            {
                amp[k] = Math.Pow(k, -tilt);
                // and the top octave of what fits rolled off, so nothing arrives at the edge
                double edge = k / (double)harmonics;
                if (edge > 0.7) amp[k] *= 0.5 + 0.5 * Math.Cos(Math.PI * (edge - 0.7) / 0.3);
            }

            double breath = (vowel == Vowel.Ooh ? 0.05 : 0.035) + 0.05 * her;

            var outp = new double[n];
            double phase = rng.NextDouble() * 2 * Math.PI, rms = 0;
            for (int i = 0; i < n; i++)
            {
                // the harmonics by recurrence: sin((k+1)p) = 2 cos p sin kp - sin (k-1)p
                double c = Math.Cos(phase), s1 = Math.Sin(phase), s0 = 0, y = 0;
                for (int k = 1; k <= harmonics; k++)
                {
                    y += amp[k] * s1;
                    double s2 = 2 * c * s1 - s0;
                    s0 = s1; s1 = s2;
                }
                outp[i] = y;
                rms += y * y;
                phase += inc[i] * scale;
            }
            rms = Math.Sqrt(rms / n);

            // Breath: noise, louder as the folds open.
            double ph = 0;
            for (int i = 0; i < n; i++)
            {
                double open = 0.55 + 0.45 * Math.Cos(ph);
                outp[i] += rms * breath * 3.0 * (rng.NextDouble() * 2 - 1) * open;
                ph += inc[i] * scale;
            }

            return outp;
        }

        /// <summary>
        /// One singer's throat for a vowel: the formants, moved toward a woman's by `her`,
        /// scaled by the size of the throat, and the first lifted to the note where it sings
        /// above it.
        /// </summary>
        static double[,] Formants(Vowel vowel, double her, double size, double f0)
        {
            var man = vowel == Vowel.Ooh ? OohMan : AahMan;
            var woman = vowel == Vowel.Ooh ? OohWoman : AahWoman;

            var outp = new double[man.Length, 2];
            for (int i = 0; i < man.Length; i++)
            {
                double hz = (man[i] + (woman[i] - man[i]) * her) / size;
                if (i == 0 && hz < f0 * 1.1) hz = f0 * 1.1;
                outp[i, 0] = Math.Min(hz, 0.45 * Rate);
                outp[i, 1] = Widths[i] * (1 + 0.3 * her);
            }
            return outp;
        }

        /// <summary>
        /// The formants, one two-pole resonance after another, round the loop twice.
        ///
        /// A resonance remembers its last two outputs, so a single pass would start the
        /// sample from silence and end it ringing - a click at the join. The first pass is
        /// thrown away; the second starts from where the first finished, which is where the
        /// loop will have left it.
        /// </summary>
        static void Filter(double[] x, double[,] formants)
        {
            int n = x.Length;
            for (int f = 0; f < formants.GetLength(0); f++)
            {
                double r = Math.Exp(-Math.PI * formants[f, 1] / Rate);
                double b = 2 * r * Math.Cos(2 * Math.PI * formants[f, 0] / Rate);
                double c = -r * r;
                double a = 1 - b - c;                    // unity at 0 Hz, as Klatt's cascade

                double y1 = 0, y2 = 0;
                for (int pass = 0; pass < 2; pass++)
                    for (int i = 0; i < n; i++)
                    {
                        double y = a * x[i] + b * y1 + c * y2;
                        y2 = y1; y1 = y;
                        if (pass == 1) x[i] = y;
                    }
            }
        }

        //
        // How much air: breath above the vowel, against the voice. An open "aah" lets a lot of
        // it out, and it is what makes a section sound like people breathing in a room rather
        // than reeds; a closed "ooh" none beyond the breath in its source.
        //
        const double AirOoh = 0, AirAah = 0.07;

        /// <summary>
        /// Breath that the vowel does not shape: noise above about 2.5 kHz, added after the
        /// formants, as a level against the voice's own.
        ///
        /// The breath in the source goes through the formants, which take most of its top
        /// away - that is breathiness, not air. This is the hiss of the breath itself, on
        /// top. Its high-pass goes round the loop twice, like the formants, so it joins.
        /// </summary>
        static void AddAir(double[] x, double level, Random rng)
        {
            int n = x.Length;
            double rms = 0;
            foreach (double v in x) rms += v * v;
            rms = Math.Sqrt(rms / n);

            var air = new double[n];
            for (int i = 0; i < n; i++) air[i] = rng.NextDouble() * 2 - 1;

            // Two poles of high-pass at 2.5 kHz, and one of low-pass at 8, round the loop twice.
            double hp = Math.Exp(-2 * Math.PI * 2500.0 / Rate);
            double lp = Math.Exp(-2 * Math.PI * 8000.0 / Rate);
            for (int stage = 0; stage < 3; stage++)
            {
                double prevIn = 0, prevOut = 0;
                for (int pass = 0; pass < 2; pass++)
                    for (int i = 0; i < n; i++)
                    {
                        double y = stage < 2 ? hp * (prevOut + air[i] - prevIn)
                                             : (1 - lp) * air[i] + lp * prevOut;
                        prevIn = air[i]; prevOut = y;
                        if (pass == 1) air[i] = y;
                    }
            }

            double airRms = 0;
            foreach (double v in air) airRms += v * v;
            airRms = Math.Sqrt(airRms / n);
            double scale = airRms > 0 ? level * rms / airRms : 0;
            for (int i = 0; i < n; i++) x[i] += air[i] * scale;
        }

        /// <summary>Scaled to the 12 bits the machine stores, with a bit of triangular dither.</summary>
        static short[] Dithered(double[] wave, Random rng)
        {
            double peak = 0;
            foreach (double v in wave) peak = Math.Max(peak, Math.Abs(v));
            double scale = 2000.0 / (peak > 0 ? peak : 1);

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
                    Rate = Rate,
                    Root = p.Root,
                    Looped = () => Render(p)
                });
            }
            return outp;
        }

        /// <summary>
        /// The programmes: each vowel, the two together, and a darker, slower "ooh" to sit
        /// under a mix. A swell in, a held note, and a release that lets the hall carry it.
        /// The vibrato is the singers' own, so the machine's LFO is left on the mod wheel.
        /// </summary>
        public static List<Patch> Programmes()
        {
            var oohs = new Patch
            {
                Name = "VOICE OOHS",
                A = 45, D = 0, S = 99, R = 60,
                KeyToFilter = 50, VelToLoudness = 20
            };
            oohs.Layers = Layers(Vowel.Ooh, 0, 0);

            var aahs = oohs.With("VOICE AAHS", Layers(Vowel.Aah, 0, 0));
            aahs.A = 38;

            // Both at once: two keygroups to a key, each three dB down for the doubling.
            var choir = oohs.With("CHOIR", Join(Layers(Vowel.Ooh, 0, -7), Layers(Vowel.Aah, 0, -7)));
            choir.A = 42;

            // Further away: the filter down, slower in and longer out.
            var dark = oohs.With("DARK OOHS", Layers(Vowel.Ooh, -30, 0));
            dark.A = 58;
            dark.R = 70;

            return new List<Patch> { oohs, aahs, choir, dark };
        }

        static Layer[] Layers(Vowel vowel, int filter, int loudness)
        {
            var outp = new List<Layer>();
            foreach (var note in Notes())
            {
                if (note.Vowel != vowel) continue;
                outp.Add(new Layer
                {
                    Sample = note.Name,
                    LowKey = note.Low,
                    HighKey = note.High,
                    Filter = Math.Max(0, Math.Min(99, 99 + filter)),
                    Loudness = loudness
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

        /// <summary>A normal deviate, by Box and Muller.</summary>
        static double Gauss(Random rng)
        {
            double u = 1 - rng.NextDouble(), v = rng.NextDouble();
            return Math.Sqrt(-2 * Math.Log(u)) * Math.Cos(2 * Math.PI * v);
        }

        /// <summary>A name, as a number - FNV-1a, as in Drums and Piano.</summary>
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
