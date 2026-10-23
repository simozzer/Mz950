using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// Birdsong, drawn rather than recorded.
    ///
    /// A songbird sings with its syrinx: two membranes, one in each bronchus, each making a
    /// nearly pure tone that the bird sweeps faster than any instrument - a whistle gliding
    /// an octave in a twentieth of a second, a trill of thirty notes a second. So most calls
    /// here are a sine, or two, whose frequency follows a drawn path, with a little second
    /// harmonic and an envelope note by note. What makes a robin a robin and not a canary is
    /// the path: its range, its speed, and how the notes are strung together.
    ///
    /// The rest are not songbirds and are made accordingly. A crow's caw and a gull's cry
    /// are harsh, so they are a pulse-like voice rich in harmonics, roughened with noise. An
    /// owl or a dove hoots low through a closed beak, breathy and soft. A woodpecker is not
    /// a voice at all but a beak on wood: short knocks, faster and quieter as they go.
    ///
    /// Every call is a one-shot at 22,050 Hz, a rate birdsong fits under. Each is seeded
    /// from its name, so the disk comes out the same every time it is made.
    ///
    /// THE PROGRAMMES. Each bird gets an octave of the keyboard, twelve keys, playing at
    /// its own pitch on the bottom key and an octave above it at the top. Twelve octaves
    /// will not fit on a keyboard, so the birds are in two programmes of six, both from C1
    /// to B6. AVIARY has every bird on a key of its own at its own pitch, as a kit; DAWN
    /// SONG is a long chorus of them, together and far off.
    /// </summary>
    internal static class Birds
    {
        const int Rate = 22050;

        sealed class Bird
        {
            public string Name;
            public double Seconds;
            public Func<int, Random, double[]> Voice;   // samples, generator
        }

        // ------------------------------------------------------------------ the parts

        /// <summary>
        /// A tone along a path: hz(t) the frequency, amp(t) the level, with `second` of the
        /// second harmonic for the syrinx's slight edge.
        /// </summary>
        static void Tone(double[] outp, int from, int length, Func<double, double> hz,
                         Func<double, double> amp, double second, double gain)
        {
            double phase = 0;
            for (int i = 0; i < length && from + i < outp.Length; i++)
            {
                double t = i / (double)length;
                phase += 2 * Math.PI * hz(t) / Rate;
                double s = Math.Sin(phase) + second * Math.Sin(2 * phase);
                outp[from + i] += gain * amp(t) * s;
            }
        }

        /// <summary>A note's envelope: up quickly, down smoothly.</summary>
        static double Bell(double t)
        {
            double a = t < 0.15 ? t / 0.15 : 1.0;
            double d = t > 0.6 ? Math.Cos((t - 0.6) / 0.4 * Math.PI / 2) : 1.0;
            return a * a * d;
        }

        static double Glide(double a, double b, double t) { return a * Math.Pow(b / a, t); }

        static int Samples(double seconds) { return (int)Math.Round(seconds * Rate); }

        /// <summary>A harsh voice: a buzzing pulse train through a resonance, with noise.</summary>
        static void Harsh(double[] outp, int from, int length, Func<double, double> hz,
                          double formant, double rough, double gain, Random rng)
        {
            double phase = 0, lp = 0, bp = 0, hp;
            double f = 2 * Math.Sin(Math.PI * formant / Rate), q = 0.35;
            for (int i = 0; i < length && from + i < outp.Length; i++)
            {
                double t = i / (double)length;
                phase += hz(t) / Rate;
                phase -= Math.Floor(phase);
                double pulse = phase < 0.18 ? 1.0 : -0.22;         // narrow pulse: many harmonics
                double x = pulse + rough * (rng.NextDouble() * 2 - 1);

                // a state-variable band-pass, the bird's throat
                hp = x - lp - q * bp;
                bp += f * hp;
                lp += f * bp;

                double env = Math.Min(1.0, t / 0.08) * Math.Pow(1 - t, 0.6);
                outp[from + i] += gain * env * bp;
            }
        }

        // ------------------------------------------------------------------ the birds

        static List<Bird> Flock()
        {
            return new List<Bird>
            {
                // ROBIN: a fluting warble, notes rising and falling across 2-4 kHz, each one
                // a short whistle bent at either end
                new Bird { Name = "ROBIN", Seconds = 1.4, Voice = (n, r) =>
                {
                    var o = new double[n];
                    double[] notes = { 2900, 3600, 2500, 3300, 4100, 2700, 3500, 3000 };
                    int at = Samples(0.04);
                    foreach (double hz in notes)
                    {
                        int len = Samples(0.09 + 0.06 * r.NextDouble());
                        double bend = 0.85 + 0.3 * r.NextDouble();
                        double f0 = hz;
                        Tone(o, at, len, t => Glide(f0 * bend, f0, Math.Min(1, t * 2.5)), Bell, 0.08, 0.5);
                        at += len + Samples(0.03 + 0.04 * r.NextDouble());
                    }
                    return o;
                } },

                // BLACKBIRD: lower and mellower, a melodic phrase with glides between the
                // notes and a quiet twitter at the end, as a blackbird finishes its song
                new Bird { Name = "BLACKBIRD", Seconds = 1.8, Voice = (n, r) =>
                {
                    var o = new double[n];
                    double[] notes = { 1650, 1950, 1750, 2200, 1850 };
                    int at = Samples(0.03);
                    for (int k = 0; k < notes.Length; k++)
                    {
                        int len = Samples(0.18);
                        double a = notes[k], b = notes[Math.Min(k + 1, notes.Length - 1)];
                        Tone(o, at, len, t => t < 0.75 ? a * (1 + 0.02 * Math.Sin(t * 40)) : Glide(a, b, (t - 0.75) * 4), Bell, 0.15, 0.55);
                        at += len + Samples(0.04);
                    }
                    for (int k = 0; k < 9; k++)
                    {
                        int len = Samples(0.025);
                        double top = 5000 + 1500 * r.NextDouble();
                        Tone(o, at, len, t => Glide(top, top * 0.7, t), Bell, 0.05, 0.25);
                        at += len + Samples(0.012);
                    }
                    return o;
                } },

                // CANARY: a rolling trill, short identical notes many times a second, the
                // trill climbing and then a second, faster one falling
                new Bird { Name = "CANARY", Seconds = 1.4, Voice = (n, r) =>
                {
                    var o = new double[n];
                    int at = Samples(0.02);
                    for (int k = 0; k < 14; k++)
                    {
                        int len = Samples(0.035);
                        double f = 3200 + 60 * k;
                        Tone(o, at, len, t => Glide(f * 1.25, f, t), Bell, 0.06, 0.5);
                        at += len + Samples(0.018);
                    }
                    at += Samples(0.06);
                    for (int k = 0; k < 16; k++)
                    {
                        int len = Samples(0.022);
                        double f = 4600 - 70 * k;
                        Tone(o, at, len, t => Glide(f, f * 1.15, t), Bell, 0.05, 0.45);
                        at += len + Samples(0.012);
                    }
                    return o;
                } },

                // SPARROW: three bright "cheeps", falling sweeps with plenty of edge
                new Bird { Name = "SPARROW", Seconds = 0.75, Voice = (n, r) =>
                {
                    var o = new double[n];
                    int at = Samples(0.02);
                    for (int k = 0; k < 3; k++)
                    {
                        int len = Samples(0.11);
                        double top = 4300 + 300 * r.NextDouble();
                        Tone(o, at, len, t => Glide(top, top * 0.62, Math.Pow(t, 0.7)), Bell, 0.35, 0.5);
                        at += len + Samples(0.09 + 0.05 * r.NextDouble());
                    }
                    return o;
                } },

                // CUCKOO: its name - two soft hooted notes a third apart, falling
                new Bird { Name = "CUCKOO", Seconds = 0.95, Voice = (n, r) =>
                {
                    var o = new double[n];
                    Tone(o, Samples(0.02), Samples(0.26), t => 690 * (1 - 0.02 * t), Bell, 0.12, 0.7);
                    Tone(o, Samples(0.38), Samples(0.42), t => 575 * (1 - 0.03 * t), Bell, 0.12, 0.7);
                    return o;
                } },

                // OWL: a tawny owl's "hoo... hoo-hoo-hooo", low, breathy and wavering
                new Bird { Name = "OWL", Seconds = 1.8, Voice = (n, r) =>
                {
                    var o = new double[n];
                    Tone(o, Samples(0.02), Samples(0.45), t => 420 * (1 + 0.03 * Math.Sin(t * 9)), Bell, 0.05, 0.75);
                    Tone(o, Samples(0.75), Samples(0.12), t => 400, Bell, 0.05, 0.55);
                    Tone(o, Samples(0.92), Samples(0.80), t => 410 * (1 + 0.025 * Math.Sin(t * 60)), Bell, 0.05, 0.7);
                    for (int i = 0; i < n; i++) o[i] *= 1 + 0.06 * (r.NextDouble() * 2 - 1);   // breath
                    return o;
                } },

                // DOVE: a wood pigeon's lazy "coo-COO-coo, coo-coo", hollow and low
                new Bird { Name = "DOVE", Seconds = 1.8, Voice = (n, r) =>
                {
                    var o = new double[n];
                    double[] at = { 0.02, 0.30, 0.72, 1.12, 1.38 };
                    double[] len = { 0.20, 0.34, 0.24, 0.18, 0.30 };
                    double[] hz = { 360, 420, 365, 380, 350 };
                    for (int k = 0; k < at.Length; k++)
                    {
                        double f = hz[k];
                        Tone(o, Samples(at[k]), Samples(len[k]), t => f * (1 + 0.04 * Math.Sin(Math.PI * t)), Bell, 0.18, 0.65);
                    }
                    return o;
                } },

                // SWALLOW: a fast, scratchy twitter, chirps at all heights in no order
                new Bird { Name = "SWALLOW", Seconds = 1.0, Voice = (n, r) =>
                {
                    var o = new double[n];
                    int at = Samples(0.02);
                    while (at < n - Samples(0.05))
                    {
                        int len = Samples(0.015 + 0.03 * r.NextDouble());
                        double a = 2800 + 3500 * r.NextDouble(), b = 2800 + 3500 * r.NextDouble();
                        Tone(o, at, len, t => Glide(a, b, t), Bell, 0.25, 0.35 + 0.15 * r.NextDouble());
                        at += len + Samples(0.01 + 0.04 * r.NextDouble());
                    }
                    return o;
                } },

                // CROW: three harsh caws, each sagging at the end
                new Bird { Name = "CROW", Seconds = 1.4, Voice = (n, r) =>
                {
                    var o = new double[n];
                    for (int k = 0; k < 3; k++)
                    {
                        double f = 520 + 40 * r.NextDouble();
                        Harsh(o, Samples(0.03 + 0.42 * k), Samples(0.30), t => f * (1 - 0.18 * t * t), 1400, 0.6, 0.5, r);
                    }
                    return o;
                } },

                // GULL: a herring gull's cry, a long falling wail and a laughing tail
                new Bird { Name = "GULL", Seconds = 1.4, Voice = (n, r) =>
                {
                    var o = new double[n];
                    Harsh(o, Samples(0.02), Samples(0.55), t => Glide(1150, 820, t), 2400, 0.25, 0.5, r);
                    for (int k = 0; k < 5; k++)
                        Harsh(o, Samples(0.66 + 0.13 * k), Samples(0.11), t => Glide(950, 780, t), 2200, 0.25, 0.4, r);
                    return o;
                } },

                // WOODPECKER: a drum roll on a branch - knocks twenty a second, fading
                new Bird { Name = "WOODPECKER", Seconds = 0.9, Voice = (n, r) =>
                {
                    var o = new double[n];
                    for (int k = 0; k < 16; k++)
                    {
                        int at = Samples(0.02 + k * 0.05);
                        double level = 0.8 * Math.Pow(0.93, k);
                        double hz = 1100 + 120 * r.NextDouble();
                        int len = Samples(0.03);
                        for (int i = 0; i < len && at + i < n; i++)
                        {
                            double e = Math.Exp(-i / (0.004 * Rate));
                            o[at + i] += level * e * (Math.Sin(2 * Math.PI * hz * i / Rate) + 0.5 * (r.NextDouble() * 2 - 1));
                        }
                    }
                    return o;
                } },

                // WREN: tiny and astonishingly loud, a long tumbling song of trills
                new Bird { Name = "WREN", Seconds = 1.3, Voice = (n, r) =>
                {
                    var o = new double[n];
                    int at = Samples(0.02);
                    double[] heights = { 5200, 4300, 6100, 4800, 5600, 3900 };
                    foreach (double h in heights)
                    {
                        int count = 4 + r.Next(5);
                        for (int k = 0; k < count; k++)
                        {
                            int len = Samples(0.02);
                            double f = h;
                            Tone(o, at, len, t => Glide(f, f * 0.8, t), Bell, 0.1, 0.45);
                            at += len + Samples(0.01);
                        }
                        at += Samples(0.03);
                    }
                    return o;
                } },
            };
        }

        /// <summary>
        /// The dawn chorus: the flock, singing at once at different distances - a far bird
        /// quieter and duller - over a soft wash of the morning, the calls overlapping as
        /// they do in a wood at five in the morning.
        /// </summary>
        static double[] Dawn(int n)
        {
            var o = new double[n];
            var rng = new Random(Seed("DAWN"));
            var flock = Flock();
            string[] singers = { "ROBIN", "BLACKBIRD", "WREN", "CANARY", "SWALLOW", "CUCKOO", "SPARROW", "ROBIN", "BLACKBIRD", "WREN" };

            double t = 0.05;
            foreach (string name in singers)
            {
                var b = flock.Find(x => x.Name == name);
                var call = b.Voice(Samples(b.Seconds), new Random(Seed(name) + (int)(t * 1000)));
                double distance = 0.3 + 0.7 * rng.NextDouble();         // 1 near, 0.3 far
                double lp = 0, a = 0.15 + 0.8 * distance;
                int at = Samples(t);
                for (int i = 0; i < call.Length && at + i < n; i++)
                {
                    lp += a * (call[i] - lp);                          // the far ones duller
                    o[at + i] += lp * distance * 0.6;
                }
                t += 0.25 + 0.45 * rng.NextDouble();
            }

            // a quiet wood: two echoes off the trees
            var wet = (double[])o.Clone();
            foreach (var tap in new[] { new[] { 0.083, 0.25 }, new[] { 0.137, 0.16 } })
            {
                int d = Samples(tap[0]);
                for (int i = d; i < n; i++) wet[i] += o[i - d] * tap[1];
            }
            return wet;
        }

        // ----------------------------------------------------------- waves and programmes

        static readonly string[] FirstSix = { "ROBIN", "BLACKBIRD", "CANARY", "SPARROW", "WREN", "SWALLOW" };
        static readonly string[] SecondSix = { "CUCKOO", "OWL", "DOVE", "CROW", "GULL", "WOODPECKER" };

        /// <summary>Its octave within its programme: 0 to 5, from C1.</summary>
        static int Octave(string name)
        {
            int i = Array.IndexOf(FirstSix, name);
            return i >= 0 ? i : Array.IndexOf(SecondSix, name);
        }

        public static List<Patches.Wave> Waves()
        {
            var outp = new List<Patches.Wave>();
            foreach (var bird in Flock())
            {
                var b = bird;
                outp.Add(new Patches.Wave
                {
                    Name = b.Name,
                    Rate = Rate,
                    // its own pitch on the bottom key of its octave
                    Root = 36 + 12 * Octave(b.Name),
                    Oneshot = () => Finish(b.Voice(Samples(b.Seconds), new Random(Seed(b.Name))))
                });
            }

            outp.Add(new Patches.Wave
            {
                Name = "DAWN",
                Rate = Rate,
                Root = 60,
                Oneshot = () => Finish(Dawn(Samples(4.5)))
            });
            return outp;
        }

        public static List<Patch> Programmes()
        {
            // an octave a bird, playing up from its own pitch, as a sampler player lays out calls
            Func<string, string[], Patch> octaves = (name, birds) =>
            {
                var layers = new List<Layer>();
                for (int k = 0; k < birds.Length; k++)
                    layers.Add(new Layer { Sample = birds[k], LowKey = 36 + 12 * k, HighKey = 47 + 12 * k });
                return new Patch
                {
                    Name = name, A = 0, D = 99, S = 99, R = 25,
                    Filter = 99, KeyToFilter = 0, VelToFilter = 0, VelToLoudness = 25,
                    OneShot = true, Layers = layers.ToArray()
                };
            };

            // every bird on a key of its own, from C3, at its own pitch
            var aviary = new List<Layer>();
            var all = new List<string>(FirstSix);
            all.AddRange(SecondSix);
            for (int k = 0; k < all.Count; k++)
                aviary.Add(new Layer { Sample = all[k], LowKey = 60 + k, HighKey = 60 + k });

            return new List<Patch>
            {
                octaves("SONGBIRDS", FirstSix),
                octaves("BIRDS", SecondSix),
                new Patch
                {
                    Name = "AVIARY", A = 0, D = 99, S = 99, R = 25,
                    Filter = 99, KeyToFilter = 0, VelToFilter = 0, VelToLoudness = 25,
                    OneShot = true, ConstantPitch = true, Layers = aviary.ToArray()
                },
                new Patch
                {
                    Name = "DAWN SONG", A = 0, D = 99, S = 99, R = 40,
                    Filter = 99, KeyToFilter = 0, VelToFilter = 0, VelToLoudness = 20,
                    OneShot = true, Layers = new[] { new Layer { Sample = "DAWN", LowKey = 36, HighKey = 96 } }
                }
            };
        }

        /// <summary>Peak to just under full scale, a soft start and a raised-cosine close.</summary>
        static short[] Finish(double[] w)
        {
            double peak = 1e-9;
            foreach (double v in w) peak = Math.Max(peak, Math.Abs(v));
            int rise = Samples(0.002), fall = Math.Min(w.Length / 4, Samples(0.08));
            for (int i = 0; i < w.Length; i++)
            {
                double g = 0.89 / peak;
                if (i < rise) g *= i / (double)rise;
                if (i >= w.Length - fall) g *= 0.5 * (1 + Math.Cos(Math.PI * (i - (w.Length - fall)) / (fall - 1.0)));
                w[i] *= g;
            }
            return Waveforms.Quantise(w);
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
