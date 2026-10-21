using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// The strings disk: violin, viola and contrabass, bowed and pizzicato, drawn rather than
    /// recorded.
    ///
    /// BOWED   The bow sticks to the string and slips off it once a cycle, so the string moves
    ///         in a sawtooth - every harmonic present, falling about 6 dB an octave - and the
    ///         wooden body colours it: a low air resonance (near 280 Hz on a violin, lower on
    ///         the bigger instruments) and a broad "bridge hill" up around 2.5 kHz that is the
    ///         violin's brilliance. Rendered by the engine in Winds, which moves each harmonic
    ///         across those resonances with the vibrato - the shimmer of a held string note -
    ///         with a little rosin noise from the bow, and a bow's slower start.
    ///
    /// PIZZICATO   The string plucked by a finger: a one-shot. A pluck about a fifth of the
    ///         way along leaves the harmonics falling as one over their number squared, with
    ///         the ones that have a node at the finger missing; each dies at its own rate,
    ///         faster the higher it is, so the note darkens as it rings; and the body colours
    ///         it as it does the bowed notes, with a soft knock as the string is let go.
    /// </summary>
    internal static class Strings
    {
        // ---------------------------------------------------------------- bowed

        public static readonly Winds.Instrument Violin = new Winds.Instrument
        {
            Prefix = "VIOLIN ",
            // little below the air resonance - the body is too small to radiate it - and a
            // harmonic series shaped by dozens of narrow modes of the wood
            PeakHz = 550, Rise = 7, Fall = 5, Bandwidth = 10000,
            Body = Winds.Body(new double[] { 280, 7, 0.25,  460, 4, 0.25,  2900, 8, 0.55,  1400, -4, 0.3 },
                              2801, 40, 250, 5500, 7),
            Attack = 0.13, Lag = 0.3, ScoopCents = 4, ChiffDb = -16,
            BreathDb = -24, NoisePulse = 0.8, BreathLow = 2000, BreathHigh = 8000,
            VibHz = 5.8, VibCents = 16, Tremolo = 0.03,
            LoopSeconds = 0.45,
            Roots = new[] { 57, 64, 71, 79, 86 },
            Lows  = new[] { 52, 61, 68, 76, 83 },
            Highs = new[] { 60, 67, 75, 82, 100 }
        };

        public static readonly Winds.Instrument Viola = new Winds.Instrument
        {
            Prefix = "VIOLA ",
            PeakHz = 420, Rise = 7, Fall = 5.5, Bandwidth = 9000,
            Body = Winds.Body(new double[] { 230, 7, 0.25,  380, 4, 0.25,  2400, 7, 0.55,  1200, -4, 0.3 },
                              3801, 40, 200, 4500, 7),
            Attack = 0.14, Lag = 0.3, ScoopCents = 4, ChiffDb = -17,
            BreathDb = -25, NoisePulse = 0.8, BreathLow = 1800, BreathHigh = 7000,
            VibHz = 5.5, VibCents = 15, Tremolo = 0.03,
            LoopSeconds = 0.45,
            Roots = new[] { 50, 57, 64, 71, 77 },
            Lows  = new[] { 45, 54, 61, 68, 75 },
            Highs = new[] { 53, 60, 67, 74, 90 }
        };

        public static readonly Winds.Instrument Contrabass = new Winds.Instrument
        {
            Prefix = "CBASS ",
            Rate = 20000,
            PeakHz = 50, Rise = 0, Fall = 8.5, Bandwidth = 5000,
            Body = new double[] { 70, 6, 0.3,  125, 4, 0.3,  1000, 3, 0.6 },
            Attack = 0.18, Lag = 0.3, ScoopCents = 4, ChiffDb = -32,
            BreathDb = -36, BreathLow = 1000, BreathHigh = 5000,
            VibHz = 5.0, VibCents = 11, Tremolo = 0.03,
            LoopSeconds = 0.55,
            Roots = new[] { 31, 38, 45, 52, 59 },
            Lows  = new[] { 21, 35, 42, 49, 56 },
            Highs = new[] { 34, 41, 48, 55, 66 }
        };

        // ---------------------------------------------------------------- pizzicato

        internal sealed class Pluck
        {
            public string Name;
            public Winds.Instrument Body;    // whose body colours it
            public int Root, Low, High, Rate = 25000;
            public double Seconds;           // how long the sample is
            public double Ring;              // how long the fundamental rings, seconds to -60 dB
            public int Players = 1;          // a section plucking together, never quite at once
        }

        static readonly string[] Pitch = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        static Pluck P(string prefix, Winds.Instrument body, int root, int low, int high, double seconds, double ring,
                       int rate = 25000, int players = 1)
        {
            return new Pluck
            {
                Name = prefix + Pitch[root % 12] + (root / 12 - 2),
                Body = body, Root = root, Low = low, High = high,
                Seconds = seconds, Ring = ring, Rate = rate, Players = players
            };
        }

        /// <summary>The soloists' pizzicato, on the STRINGS disk.</summary>
        public static List<Pluck> Plucks()
        {
            return new List<Pluck>
            {
                P("VLN PZ ", Violin, 59, 52, 63, 0.60, 0.9),
                P("VLN PZ ", Violin, 69, 64, 73, 0.55, 0.8),
                P("VLN PZ ", Violin, 79, 74, 100, 0.50, 0.6),
                P("VLA PZ ", Viola, 50, 45, 54, 0.75, 1.1),
                P("VLA PZ ", Viola, 60, 55, 64, 0.70, 1.0),
                P("VLA PZ ", Viola, 69, 65, 90, 0.60, 0.8),
                P("CB PZ ", Contrabass, 33, 21, 37, 1.10, 2.2, 20000),
                P("CB PZ ", Contrabass, 43, 38, 47, 1.00, 1.9, 20000),
                P("CB PZ ", Contrabass, 52, 48, 66, 0.90, 1.6, 20000),
            };
        }

        /// <summary>The sections' pizzicato, on the ENSEMBLE disk: eight, six and four players.</summary>
        public static List<Pluck> SectionPlucks()
        {
            return new List<Pluck>
            {
                P("VLNS PZ ", Violin, 59, 52, 63, 0.60, 0.9, 22050, 8),
                P("VLNS PZ ", Violin, 69, 64, 73, 0.55, 0.8, 22050, 8),
                P("VLNS PZ ", Violin, 79, 74, 100, 0.50, 0.6, 22050, 8),
                P("VLAS PZ ", Viola, 50, 45, 54, 0.70, 1.1, 22050, 6),
                P("VLAS PZ ", Viola, 60, 55, 64, 0.65, 1.0, 22050, 6),
                P("VLAS PZ ", Viola, 69, 65, 90, 0.60, 0.8, 22050, 6),
                P("CBS PZ ", Contrabass, 33, 21, 37, 1.00, 2.2, 20000, 4),
                P("CBS PZ ", Contrabass, 43, 38, 47, 0.90, 1.9, 20000, 4),
                P("CBS PZ ", Contrabass, 52, 48, 66, 0.80, 1.6, 20000, 4),
            };
        }

        public static short[] RenderPluck(Pluck p)
        {
            var rng = new Random(Seed(p.Name));
            int n = (int)Math.Round(p.Seconds * p.Rate) & ~1;
            if (p.Players <= 1) return Scaled(PluckWave(p, n, 0, 0, rng), rng);

            // a section: each player a few cents off, and up to 25 ms late
            var mix = new double[n];
            for (int i = 0; i < p.Players; i++)
            {
                var prng = new Random(Seed(p.Name) + 7919 * (i + 1));
                double cents = (prng.NextDouble() - 0.5) * 12;
                int delay = (int)(prng.NextDouble() * 0.025 * p.Rate);
                var w = PluckWave(p, n, cents, delay, prng);
                for (int j = 0; j < n; j++) mix[j] += w[j];
            }
            return Scaled(mix, rng);
        }

        static double[] PluckWave(Pluck p, int n, double cents, int delay, Random rng)
        {
            int rate = p.Rate;
            double f0 = 440.0 * Math.Pow(2, (p.Root - 69) / 12.0 + cents / 1200.0);
            double beta = 0.19 + 0.02 * rng.NextDouble();          // where the finger plucks
            double top = Math.Min(0.45 * rate, p.Body.Bandwidth);
            var wave = new double[n];

            for (int k = 1; k * f0 < top; k++)
            {
                double hz = k * f0 * (1 + 0.00005 * k * k);          // a gut or steel string, barely stiff
                double level = Math.Abs(Math.Sin(Math.PI * k * beta)) / (k * k)
                             * Math.Pow(10, Winds.BodyDb(p.Body, hz) / 20.0)
                             * Math.Pow(10, (rng.NextDouble() - 0.5) * 2.0 / 20.0);
                // the higher, the shorter: -60 dB in Ring seconds at the fundamental
                double ring = p.Ring / (1 + 0.8 * (hz - f0) / 1000.0);
                double decay = Math.Exp(-6.908 / (ring * rate));
                double w = 2 * Math.PI * hz / rate, c = Math.Cos(w), s = Math.Sin(w);
                double ph = rng.NextDouble() * 0.2;
                double re = Math.Cos(ph), im = Math.Sin(ph), amp = level;
                for (int i = delay; i < n; i++)
                {
                    wave[i] += amp * im;
                    double r2 = re * c - im * s;
                    im = re * s + im * c;
                    re = r2;
                    amp *= decay;
                    if ((i & 1023) == 1023) { double m = 1.0 / Math.Sqrt(re * re + im * im); re *= m; im *= m; }
                }
            }

            // the finger letting go: a few milliseconds' rise, and a soft knock in the body
            double peak = 0;
            foreach (double v in wave) peak = Math.Max(peak, Math.Abs(v));
            int rise = Math.Max(2, rate / 600);
            for (int i = 0; i < rise && delay + i < n; i++) wave[delay + i] *= i / (double)rise;
            double bodyHz = p.Body.Body[0];
            for (int i = 0; i < rate / 8 && delay + i < n; i++)
            {
                double t = i / (double)rate;
                wave[delay + i] += peak * 0.12 * Math.Sin(2 * Math.PI * bodyHz * t) * Math.Exp(-t / 0.02);
            }
            return wave;
        }

        /// <summary>The tail taken down, so the cut is not a click; then to 12 bits, dithered.</summary>
        static short[] Scaled(double[] wave, Random rng)
        {
            int n = wave.Length;
            int fall = (int)(0.2 * n);
            for (int i = 0; i < fall; i++)
                wave[n - fall + i] *= 0.5 * (1 + Math.Cos(Math.PI * i / (fall - 1.0)));

            double peak = 0;
            foreach (double v in wave) peak = Math.Max(peak, Math.Abs(v));
            double scale = 2000.0 / (peak > 0 ? peak : 1);
            var outp = new short[n];
            for (int i = 0; i < n; i++)
            {
                double d = rng.NextDouble() - rng.NextDouble();
                int v = (int)Math.Round(wave[i] * scale + d);
                outp[i] = (short)(v > 2047 ? 2047 : (v < -2048 ? -2048 : v));
            }
            return outp;
        }

        // ------------------------------------------------------------ the sections

        // Eight violins, six violas and four basses, each a little apart - see Winds.Render.
        // At a slightly lower rate and with longer loops, so the players can drift through
        // each other before the loop comes round, and all of it still fits on one disk.
        public static readonly Winds.Instrument Violins = Violin.Section("VLNS ", 8, 22050, 0.8);
        public static readonly Winds.Instrument Violas = Viola.Section("VLAS ", 6, 22050, 0.8);
        public static readonly Winds.Instrument Basses = Contrabass.Section("CBS ", 4, 20000, 0.8);

        // ------------------------------------------------------------ the disks

        public static List<Patches.Wave> Waves()
        {
            var outp = Winds.Waves(Violin, Viola, Contrabass, Violins, Violas, Basses);
            var plucks = Plucks();
            plucks.AddRange(SectionPlucks());
            foreach (var pl in plucks)
            {
                var p = pl;
                outp.Add(new Patches.Wave { Name = p.Name, Rate = p.Rate, Root = p.Root, Oneshot = () => RenderPluck(p) });
            }
            return outp;
        }

        static Layer[] PluckKeys(List<Pluck> from, string prefix, int loudness)
        {
            var outp = new List<Layer>();
            foreach (var p in from)
                if (p.Name.StartsWith(prefix))
                    outp.Add(new Layer { Sample = p.Name, LowKey = p.Low, HighKey = p.High, Filter = 99, Loudness = loudness });
            return outp.ToArray();
        }

        static Patch Bowed(string name, Layer[] layers)
        {
            var p = new Patch
            {
                Name = name,
                A = 0, D = 0, S = 99, R = 36,
                KeyToFilter = 50, VelToLoudness = 25, VelToFilter = 14
            };
            p.Layers = layers;
            return p;
        }

        static Patch Plucked(string name, Layer[] layers)
        {
            var p = new Patch
            {
                Name = name,
                A = 0, D = 80, S = 0, R = 40,
                KeyToFilter = 50, VelToLoudness = 30, VelToFilter = 18
            };
            p.Layers = layers;
            return p;
        }

        /// <summary>
        /// The STRINGS disk, the soloists. Bowed: held, the attack in the samples, a release
        /// that lets the note ring on a little. Pizzicato: one-shots, the machine only finishing
        /// each off. And the three together where their ranges meet, as a trio.
        /// </summary>
        public static List<Patch> Programmes()
        {
            var violin = Bowed("VIOLIN", Winds.Keys(Violin, -6, 0));
            var viola = Bowed("VIOLA", Winds.Keys(Viola, -8, 0));
            var bass = Bowed("CONTRABASS", Winds.Keys(Contrabass, -10, 0));
            bass.R = 40;
            var trio = Bowed("STR TRIO", Winds.Join(Winds.Keys(Violin, -6, -5), Winds.Keys(Viola, -8, -5), Winds.Keys(Contrabass, -10, -4)));

            var plucks = Plucks();
            var pizz = Plucked("VLN PIZZ", PluckKeys(plucks, "VLN PZ", 0));
            var vlaPizz = Plucked("VLA PIZZ", PluckKeys(plucks, "VLA PZ", 0));
            var cbPizz = Plucked("CB PIZZ", PluckKeys(plucks, "CB PZ", 0));
            cbPizz.D = 88;
            cbPizz.R = 46;

            return new List<Patch> { violin, viola, bass, trio, pizz, vlaPizz, cbPizz };
        }

        /// <summary>
        /// The ENSEMBLE disk, the sections: each on its own, all three together where their
        /// ranges meet, the same slower and softer to swell under a mix, and their pizzicato.
        /// </summary>
        public static List<Patch> SectionProgrammes()
        {
            var violins = Bowed("VIOLINS", Winds.Keys(Violins, -6, 0));
            var violas = Bowed("VIOLAS", Winds.Keys(Violas, -8, 0));
            var basses = Bowed("BASSES", Winds.Keys(Basses, -10, 0));
            basses.R = 42;

            var strings = Bowed("STRINGS", Winds.Join(Winds.Keys(Violins, -6, -5), Winds.Keys(Violas, -8, -5), Winds.Keys(Basses, -10, -4)));
            strings.R = 44;

            var slow = Bowed("SLOW STRS", Winds.Join(Winds.Keys(Violins, -20, -5), Winds.Keys(Violas, -22, -5), Winds.Keys(Basses, -22, -4)));
            slow.A = 55;
            slow.R = 60;

            var plucks = SectionPlucks();
            var pizz = Plucked("PIZZ VLNS", PluckKeys(plucks, "VLNS PZ", 0));
            var vlaPizz = Plucked("PIZZ VLAS", PluckKeys(plucks, "VLAS PZ", 0));
            var cbPizz = Plucked("PIZZ BASS", PluckKeys(plucks, "CBS PZ", 0));
            cbPizz.D = 88;
            cbPizz.R = 46;
            var pizzAll = Plucked("PIZZ ENSEM", Winds.Join(PluckKeys(plucks, "VLNS PZ", -5), PluckKeys(plucks, "VLAS PZ", -5), PluckKeys(plucks, "CBS PZ", -3)));

            return new List<Patch> { violins, violas, basses, strings, slow, pizz, vlaPizz, cbPizz, pizzAll };
        }

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
