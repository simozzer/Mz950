using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// The sea, from under the water: for ambient music, and for sleep.
    ///
    /// Four seconds of low, rolling underwater noise - a dark rumble with a little hiss above
    /// it - looped whole so a held key sustains for as long as anyone wants. Noise cannot be
    /// made periodic, so the loop is crossfaded into its own start at equal power, and the
    /// slow swell is laid on afterwards with a period of the loop exactly, so the waves keep
    /// coming without a seam.
    ///
    /// It was made for a whale disk that did not survive listening; the whales were too
    /// plainly synthetic. The sea was kept.
    /// </summary>
    internal static class Ocean
    {
        const int Rate = 16000;

        internal static short[] Sea()
        {
            int n = 4 * Rate, x = Rate / 2;
            var rng = new Random(Seed("OCEAN"));
            var raw = new double[n + x];

            // noise through two lowpasses: a dark rumble, with a little hiss above it
            double lp1 = 0, lp2 = 0, hiss = 0;
            double a1 = 1 - Math.Exp(-2 * Math.PI * 180.0 / Rate), a2 = 1 - Math.Exp(-2 * Math.PI * 900.0 / Rate);
            for (int i = 0; i < raw.Length; i++)
            {
                double w = rng.NextDouble() * 2 - 1;
                lp1 += a1 * (w - lp1);
                lp2 += a1 * (lp1 - lp2);
                hiss += a2 * (w - hiss);
                raw[i] = 6.0 * lp2 + 0.25 * hiss;
            }

            // the loop: the tail past n faded into the head, equal power
            var o = new double[n];
            for (int i = 0; i < n; i++) o[i] = raw[i];
            for (int i = 0; i < x; i++)
            {
                double t = i / (double)x;
                o[i] = raw[i] * Math.Sin(t * Math.PI / 2) + raw[n + i] * Math.Cos(t * Math.PI / 2);
            }

            // the swell: once round the loop
            for (int i = 0; i < n; i++)
                o[i] *= 0.65 + 0.35 * Math.Cos(2 * Math.PI * i / n);
            return Waveforms.Quantise(o);
        }

        public static List<Patches.Wave> Waves()
        {
            return new List<Patches.Wave>
            {
                new Patches.Wave { Name = "OCEAN", Rate = Rate, Root = 60, Looped = () => Sea() }
            };
        }

        public static List<Patch> Programmes()
        {
            return new List<Patch>
            {
                // the sea as it was made, on every key, fading in and out slowly
                new Patch
                {
                    Name = "OCEAN BED", A = 60, D = 99, S = 99, R = 65,
                    Filter = 99, KeyToFilter = 0, VelToFilter = 0, VelToLoudness = 10,
                    ConstantPitch = true,
                    Layers = new[] { new Layer { Sample = "OCEAN" } }
                },

                // played as a pitch: lower keys a deeper, slower sea, higher ones lighter
                new Patch
                {
                    Name = "DEEP SEA", A = 60, D = 99, S = 99, R = 65,
                    Filter = 99, KeyToFilter = 0, VelToFilter = 0, VelToLoudness = 10,
                    Layers = new[] { new Layer { Sample = "OCEAN" } }
                }
            };
        }

        static int Seed(string name)
        {
            unchecked
            {
                uint h = 2166136261;
                foreach (char c in name) { h ^= c; h *= 16777619; }
                return (int)(h & 0x7FFFFFFF);
            }
        }
    }
}
