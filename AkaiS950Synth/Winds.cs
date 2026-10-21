using System;
using System.Collections.Generic;

namespace AkaiS950Synth
{
    /// <summary>
    /// Wind instruments, drawn rather than recorded: the woodwinds (flute, piccolo, recorder)
    /// and the brass (trumpet, trombone, tuba, French horn), each a multisample of held notes.
    ///
    /// A wind instrument is a tube driven at one end - by a jet of breath across an edge, or by
    /// the player's lips buzzing in a cup - and almost all of what tells them apart is in four
    /// things, so that is what each instrument here is described by:
    ///
    ///   THE SPECTRUM  The harmonics of the note, rising to a peak and falling away beyond
    ///                 it. A flute's peak is at its fundamental and its harmonics die fast,
    ///                 so it is nearly a sine; a recorder's die faster still. A trumpet's peak
    ///                 is up at a kilohertz and more, which is its brilliance; a trombone's
    ///                 near 550 Hz, a horn's lower and falling steeply - the hand in the bell -
    ///                 and a tuba's lowest of all.
    ///
    ///   BRIGHTENING   A brass note gets brighter as it gets louder, and it gets louder as it
    ///                 starts: the fundamental speaks first and the upper harmonics follow, a
    ///                 few milliseconds later the higher they are. That lag, and the darkness
    ///                 at the very start, are most of what a brass attack is.
    ///
    ///   BREATH        Air that is not tone: a lot of it on a flute, some on a recorder, a
    ///                 little on brass. And at the start of a recorder or flute note, a chiff -
    ///                 a puff of noise as the jet finds the edge, before the tube takes over.
    ///
    ///   VIBRATO       From the breath on the woodwinds (a flute's is as much in level as in
    ///                 pitch), from the lips on the brass, and none to speak of on a recorder.
    ///
    /// Each sample is an attack that plays once and a loop over its tail. Everything in the
    /// tail repeats exactly in the loop's length - each harmonic a whole number of cycles, the
    /// vibrato and a slow wander whole turns, the breath noise a loop of its own - so the
    /// machine can hold a note as long as the key is down with no join to hear.
    /// </summary>
    internal static class Winds
    {
        /// <summary>One instrument: how it sounds, and the notes it is sampled at.</summary>
        internal sealed class Instrument
        {
            public string Prefix;          // the samples' names, before the note: "FLUTE "
            public int Rate = 25000;

            // the spectrum: a peak, and dB an octave below it and above it
            public double PeakHz, Rise, Fall;
            public double FallAtStart;     // extra dB an octave at the very start - brass only
            public double EvenDb;          // the even harmonics, against the odd
            public double Bandwidth = 9000;

            // the start
            public double Attack;          // seconds, to the steady tone
            public double Lag;             // how much later the top harmonics speak, 0..1
            public double ScoopCents;      // starting flat, rising to the note
            public double ChiffDb = -60;   // a puff of noise at the start, against the tone; none below -59

            // the breath, against the tone, and the band it sits in
            public double BreathDb = -60, BreathLow = 1500, BreathHigh = 8000;

            //
            // How much the breath - on a string, the bow's noise - comes in bursts in time with the
            // note, 0 to 1: the bow lets go of the string once a cycle, and each slip is a burst
            // of rasp. 0 is a steady hiss, as on the woodwinds and brass.
            //
            public double NoisePulse;

            // the vibrato, in hertz and cents, and how much of it is in the level
            public double VibHz = 5, VibCents, Tremolo;

            //
            // A body, for the strings: resonances on top of the spectrum, each its centre in
            // hertz, its height in dB and its width in octaves. Where a body is given, the
            // vibrato moves each harmonic across it, so each swells and dips a little with the
            // vibrato as it does on a real violin - which is most of what makes one sound alive.
            //
            public double[] Body;

            public double LoopSeconds = 0.5;

            // a section rather than a soloist: how many players, and how far apart, in cents
            public int Players = 1;
            public double SpreadCents = 6;

            /// <summary>The same instrument as a section: its samples under another name.</summary>
            public Instrument Section(string prefix, int players, int rate, double loopSeconds)
            {
                var s = (Instrument)MemberwiseClone();
                s.Prefix = prefix;
                s.Players = players;
                s.Rate = rate;
                s.LoopSeconds = loopSeconds;
                return s;
            }
            public int[] Roots;            // MIDI notes, C3 = 60 as the machine counts
            public int[] Lows, Highs;      // the keys each answers
        }

        internal sealed class Note
        {
            public Instrument Of;
            public string Name;
            public int Root, Low, High;
        }

        static readonly string[] Pitch = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        public static List<Note> Notes(Instrument ins)
        {
            var outp = new List<Note>();
            for (int i = 0; i < ins.Roots.Length; i++)
            {
                int r = ins.Roots[i];
                outp.Add(new Note
                {
                    Of = ins,
                    Name = ins.Prefix + Pitch[r % 12] + (r / 12 - 2),
                    Root = r,
                    Low = ins.Lows[i],
                    High = ins.Highs[i]
                });
            }
            return outp;
        }

        // ------------------------------------------------------------------ render

        /// <summary>
        /// One note: the words, and how many of them at the end are the loop.
        /// Seeded from its name, so the same disk comes out byte for byte every time.
        /// </summary>
        public static Tuple<short[], int> Render(Note note)
        {
            var ins = note.Of;
            var rng = new Random(Seed(note.Name));
            int rate = ins.Rate;
            double f0 = 440.0 * Math.Pow(2, (note.Root - 69) / 12.0);

            // The loop: a whole number of cycles, as many words as put them on the note.
            int cycles = Math.Max(1, (int)Math.Round(f0 * ins.LoopSeconds));
            int loop = (int)Math.Round(cycles * rate / f0);
            int attack = (int)Math.Round(ins.Attack * rate);
            if (((attack + loop) & 1) != 0) attack++;
            int n = attack + loop;
            int vibTurns = Math.Max(1, (int)Math.Round(ins.VibHz * loop / rate));

            if (ins.Players <= 1)
                return Tuple.Create(Dithered(Voice(ins, f0, rate, attack, loop, cycles, 0, vibTurns, 1, rng), rng), loop);

            //
            // A section: each player a few cents off the note - as whole cycles more or fewer in
            // the loop, in pairs, one sharp and one flat, so the section sits on the note - where
            // the loop is long enough for a cycle to be only a few cents; each with a vibrato of
            // their own, a turn faster or not, deeper or shallower; and each up to 20 ms late to
            // the note, inside the attack.
            //
            double centsPerCycle = 1200 * Math.Log((cycles + 1.0) / cycles, 2);
            var mix = new double[n];
            int pair = 0;
            for (int p = 0; p < ins.Players; p++)
            {
                var prng = new Random(Seed(note.Name) + 7919 * (p + 1));
                int offset = 0;
                if (centsPerCycle <= 8)
                {
                    if (p % 2 == 0)
                    {
                        pair = (int)Math.Round(Math.Abs(Gauss(prng)) * ins.SpreadCents / centsPerCycle);
                        pair = Math.Max(0, Math.Min(3, pair));
                    }
                    offset = p % 2 == 0 ? pair : -pair;
                }
                int delay = Math.Min((int)(prng.NextDouble() * 0.02 * rate), attack / 2);
                int turns = vibTurns + (prng.NextDouble() < 0.5 ? 0 : 1);
                double depth = 0.75 + 0.5 * prng.NextDouble();
                var v = Voice(ins, f0, rate, attack, loop, cycles + offset, delay, turns, depth, prng);
                for (int i = 0; i < n; i++) mix[i] += v[i];
            }
            return Tuple.Create(Dithered(mix, rng), loop);
        }

        static double Gauss(Random rng)
        {
            double u = 1 - rng.NextDouble(), v = rng.NextDouble();
            return Math.Sqrt(-2 * Math.Log(u)) * Math.Cos(2 * Math.PI * v);
        }

        /// <summary>
        /// One player: `cycles` cycles in the loop, starting `delay` words into the attack, with
        /// a vibrato of `vibTurns` turns in the loop at `vibDepth` of the instrument's depth.
        /// </summary>
        static double[] Voice(Instrument ins, double f0, int rate, int attack, int loop, int cycles,
                              int delay, int vibTurns, double vibDepth, Random rng)
        {
            int n = attack + loop;
            Func<int, double> grow = i => i >= attack ? 1 : i < delay ? 0
                                        : Smooth((i - delay) / (double)Math.Max(1, attack - delay));

            // ---- the pitch: vibrato and a slow wander that turn a whole number of times in
            // the loop, scaled so the loop holds exactly `cycles` cycles of the note
            double vibPhase = rng.NextDouble() * 2 * Math.PI, wanderPhase = rng.NextDouble() * 2 * Math.PI;

            var cents = new double[n];      // the pitch, in cents off the note
            var trem = new double[n];       // the vibrato's share of the level
            for (int i = 0; i < n; i++)
            {
                double t = (i - attack) / (double)loop;                  // whole turns in the loop
                double g = grow(i);
                double vib = Math.Sin(2 * Math.PI * vibTurns * t + vibPhase);
                cents[i] = ins.VibCents * vibDepth * vib * g
                         + 1.5 * Math.Sin(2 * Math.PI * t + wanderPhase)
                         - ins.ScoopCents * (1 - g);
                trem[i] = 1 + ins.Tremolo * vibDepth * vib * g;
            }

            var inc = new double[n];
            double loopSum = 0;
            for (int i = 0; i < n; i++)
            {
                inc[i] = Math.Pow(2, cents[i] / 1200.0);
                if (i >= attack) loopSum += inc[i];
            }
            double scale = 2 * Math.PI * cycles / loopSum;

            // ---- the harmonics, as far as the bandwidth and the sample rate allow
            int top = (int)(Math.Min(ins.Bandwidth, 0.45 * rate) / f0);
            top = Math.Max(1, top);
            var level = new double[top + 1];
            var phase0 = new double[top + 1];
            //
            // Across the body: for each harmonic, how much louder or softer it is at every cent
            // the vibrato takes it to - a table, from -50 to +50 cents, so a harmonic sliding
            // over a narrow resonance swells and falls by what the resonance says, not by a
            // straight line through it.
            //
            double[][] across = null;
            if (ins.Body != null)
            {
                across = new double[top + 1][];
                for (int k = 1; k <= top; k++)
                {
                    across[k] = new double[101];
                    double at = BodyDb(ins, k * f0);
                    for (int c = -50; c <= 50; c++)
                        across[k][c + 50] = Math.Pow(10, (BodyDb(ins, k * f0 * Math.Pow(2, c / 1200.0)) - at) / 20.0);
                }
            }
            for (int k = 1; k <= top; k++)
            {
                level[k] = Math.Pow(10, Spectrum(ins, k * f0, f0, k, 0) / 20.0);
                level[k] *= Math.Pow(10, (rng.NextDouble() - 0.5) * 2.0 / 20.0);   // no instrument is smooth
                phase0[k] = rng.NextDouble() * 2 * Math.PI;
            }

            // ---- the tone, harmonic by harmonic from one running phase
            var tone = new double[n];
            var slip = new double[n];       // where in its cycle the note is, for the bow's bursts
            double phase = 0;
            for (int i = 0; i < n; i++)
            {
                double c = Math.Cos(phase), s = Math.Sin(phase);
                double zr = 1, zi = 0, y = 0;
                for (int k = 1; k <= top; k++)
                {
                    double r2 = zr * c - zi * s;
                    zi = zr * s + zi * c;
                    zr = r2;

                    double a = level[k];
                    if (across != null)
                    {
                        double cc = Math.Max(-50, Math.Min(50, cents[i])) + 50;
                        int j = Math.Min(99, (int)cc);
                        a *= across[k][j] + (across[k][j + 1] - across[k][j]) * (cc - j);
                    }
                    if (i < attack)
                    {
                        // the start: each harmonic speaks a little later the higher it is,
                        // and darker - the slope steeper - until the tone has bloomed
                        double into = i < delay ? 0 : (i - delay) / (double)Math.Max(1, attack - delay);
                        double when = into * (1 + ins.Lag) - ins.Lag * (k - 1) / Math.Max(1.0, top - 1.0);
                        double e = Smooth(Math.Max(0, Math.Min(1, when)));
                        double dark = ins.FallAtStart * (1 - Smooth(into));
                        if (dark > 0 && k > 1) a *= Math.Pow(10, -dark * Math.Log(k, 2) / 20.0);
                        a *= e;
                    }

                    y += a * (zi * Math.Cos(phase0[k]) + zr * Math.Sin(phase0[k]));
                }
                tone[i] = y * trem[i];
                slip[i] = phase;
                phase += inc[i] * scale;
            }

            double toneRms = Rms(tone, attack, n);

            // ---- breath, a loop of its own, band-passed round the loop twice
            if (ins.BreathDb > -59)
            {
                var air = Band(Noise(loop, rng), ins.BreathLow, ins.BreathHigh, rate);

                // On a string, the bow's noise rings in the body: the same resonances the tone
                // goes through, as a bank of narrow filters, joined to the hiss.
                if (ins.Body != null) air = Woody(air, ins, rate, rng);
                double airRms = Rms(air, 0, loop);
                double g = airRms > 0 ? toneRms * Math.Pow(10, ins.BreathDb / 20.0) / airRms : 0;
                for (int i = 0; i < n; i++)
                {
                    int j = ((i - attack) % loop + loop) % loop;
                    tone[i] += air[j] * g * grow(i) * trem[i] * Burst(ins, slip[i]);
                }
            }

            // ---- the chiff: a puff of noise at the start, gone by the end of the attack
            if (ins.ChiffDb > -59 && attack > 0)
            {
                var puff = Band(Noise(attack, rng), 1200, 9000, rate);
                double puffRms = Rms(puff, 0, attack);
                double g = puffRms > 0 ? toneRms * Math.Pow(10, ins.ChiffDb / 20.0) / puffRms : 0;
                for (int i = delay; i < attack; i++)
                {
                    double t = (i - delay) / (double)Math.Max(1, attack - delay);
                    tone[i] += puff[i - delay] * g * Math.Sin(Math.PI * Math.Min(1, t * 1.6)) * (1 - t) * Burst(ins, slip[i]);
                }
            }

            // the very first sample out of silence, not a step
            int rise = Math.Min(attack - delay, rate / 500);
            for (int i = 0; i < rise; i++) tone[delay + i] *= i / (double)rise;

            return tone;
        }

        /// <summary>A harmonic's level, in dB: up to the peak by Rise an octave, down past it by Fall.</summary>
        static double Spectrum(Instrument ins, double hz, double f0, int k, double extraFall)
        {
            double db = hz < ins.PeakHz ? -ins.Rise * Math.Log(ins.PeakHz / hz, 2)
                                        : -(ins.Fall + extraFall) * Math.Log(hz / ins.PeakHz, 2);
            if (k % 2 == 0) db += ins.EvenDb;
            db += BodyDb(ins, hz);
            // and the bandwidth's own edge, so nothing arrives at it
            db -= 40 * Math.Max(0, Math.Log(hz / ins.Bandwidth, 2) + 0.3);
            return db;
        }

        /// <summary>
        /// Noise rung through the body: a resonator for each of its resonances, each run round
        /// the loop twice so it joins, at the resonance's own height, added to the hiss given.
        /// </summary>
        static double[] Woody(double[] hiss, Instrument ins, int rate, Random rng)
        {
            int n = hiss.Length;
            var src = Noise(n, rng);
            var wood = new double[n];
            for (int m = 0; m + 2 < ins.Body.Length; m += 3)
            {
                double hz = ins.Body[m], gainDb = ins.Body[m + 1], width = ins.Body[m + 2];
                if (gainDb <= 0 || hz >= 0.45 * rate) continue;
                double bw = Math.Max(15, hz * width * 0.7);
                double r = Math.Exp(-Math.PI * bw / rate);
                double b1 = 2 * r * Math.Cos(2 * Math.PI * hz / rate), b2 = -r * r;
                double g = (1 - r) * Math.Pow(10, gainDb / 20.0);
                double y1 = 0, y2 = 0;
                for (int pass = 0; pass < 2; pass++)
                    for (int i = 0; i < n; i++)
                    {
                        double y = g * src[i] + b1 * y1 + b2 * y2;
                        y2 = y1; y1 = y;
                        if (pass == 1) wood[i] += y;
                    }
            }
            double hr = Rms(hiss, 0, n), wr = Rms(wood, 0, n);
            var outp = new double[n];
            for (int i = 0; i < n; i++) outp[i] = hiss[i] * 0.5 + (wr > 0 ? wood[i] * hr / wr : 0);
            return outp;
        }

        /// <summary>
        /// A body: its main resonances as given, and many narrower ones - the wood's own modes
        /// - scattered at random from `low` to `high` hertz, the same for every note of the
        /// instrument, so the jagged response that is a real instrument's fingerprint is too.
        /// </summary>
        public static double[] Body(double[] main, int seed, int modes, double low, double high, double depthDb)
        {
            var rng = new Random(seed);
            var outp = new List<double>(main);
            for (int i = 0; i < modes; i++)
            {
                double hz = low * Math.Pow(high / low, rng.NextDouble());
                double db = (rng.NextDouble() * 2 - 0.6) * depthDb;      // more peaks than dips
                double width = 0.03 + 0.05 * rng.NextDouble();             // narrow: a Q of 20 to 50
                outp.Add(hz); outp.Add(db); outp.Add(width);
            }
            return outp.ToArray();
        }

        /// <summary>
        /// The bow's burst at this point in the cycle: a sharp peak where the string slips, as a
        /// gain that averages 1, so the noise keeps its level and only comes in bursts.
        /// </summary>
        static double Burst(Instrument ins, double phase)
        {
            if (ins.NoisePulse <= 0) return 1;
            double c = Math.Cos(phase);
            double pulse = Math.Pow(0.5 + 0.5 * c, 6) / 0.2256;     // 0.2256: its mean over a cycle
            return 1 - ins.NoisePulse + ins.NoisePulse * pulse;
        }

        /// <summary>The body's resonances at a frequency, in dB: none if there is no body.</summary>
        public static double BodyDb(Instrument ins, double hz)
        {
            if (ins.Body == null) return 0;
            double db = 0;
            for (int i = 0; i + 2 < ins.Body.Length; i += 3)
            {
                double oct = Math.Log(hz / ins.Body[i], 2) / ins.Body[i + 2];
                db += ins.Body[i + 1] * Math.Exp(-0.5 * oct * oct);
            }
            return db;
        }

        static double Smooth(double x)
        {
            x = Math.Max(0, Math.Min(1, x));
            return x * x * (3 - 2 * x);
        }

        static double[] Noise(int n, Random rng)
        {
            var x = new double[n];
            for (int i = 0; i < n; i++) x[i] = rng.NextDouble() * 2 - 1;
            return x;
        }

        /// <summary>
        /// A band: two poles of high-pass and two of low-pass, each run round the buffer twice
        /// so the second pass starts where the first finished - a loop that joins itself.
        /// </summary>
        static double[] Band(double[] x, double low, double high, int rate)
        {
            int n = x.Length;
            double hp = Math.Exp(-2 * Math.PI * low / rate);
            double lp = Math.Exp(-2 * Math.PI * high / rate);
            for (int stage = 0; stage < 4; stage++)
            {
                double prevIn = 0, prevOut = 0;
                for (int pass = 0; pass < 2; pass++)
                    for (int i = 0; i < n; i++)
                    {
                        double y = stage < 2 ? hp * (prevOut + x[i] - prevIn)
                                             : (1 - lp) * x[i] + lp * prevOut;
                        prevIn = x[i]; prevOut = y;
                        if (pass == 1) x[i] = y;
                    }
            }
            return x;
        }

        static double Rms(double[] x, int from, int to)
        {
            double s = 0;
            for (int i = from; i < to; i++) s += x[i] * x[i];
            return Math.Sqrt(s / Math.Max(1, to - from));
        }

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

        public static List<Patches.Wave> Waves(params Instrument[] instruments)
        {
            var outp = new List<Patches.Wave>();
            foreach (var ins in instruments)
                foreach (var note in Notes(ins))
                {
                    var p = note;
                    outp.Add(new Patches.Wave
                    {
                        Name = p.Name,
                        Rate = p.Of.Rate,
                        Root = p.Root,
                        Held = () => Render(p)
                    });
                }
            return outp;
        }

        /// <summary>A keygroup for each of an instrument's notes.</summary>
        public static Layer[] Keys(Instrument ins, int filter, int loudness)
        {
            var outp = new List<Layer>();
            foreach (var note in Notes(ins))
                outp.Add(new Layer
                {
                    Sample = note.Name,
                    LowKey = note.Low,
                    HighKey = note.High,
                    Filter = Math.Max(0, Math.Min(99, 99 + filter)),
                    Loudness = loudness
                });
            return outp.ToArray();
        }

        public static Layer[] Join(params Layer[][] parts)
        {
            var outp = new List<Layer>();
            foreach (var p in parts) outp.AddRange(p);
            return outp.ToArray();
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

    /// <summary>The woodwind disk: flute, piccolo and recorder.</summary>
    internal static class Woodwind
    {
        // Nearly a sine, its second harmonic a few dB down in the low register and the rest
        // falling fast; a lot of breath, a soft chiff, and a vibrato as much in level as pitch.
        public static readonly Winds.Instrument Flute = new Winds.Instrument
        {
            Prefix = "FLUTE ",
            PeakHz = 520, Rise = 2, Fall = 17, Bandwidth = 9000,
            Attack = 0.11, Lag = 0.2, ScoopCents = 6, ChiffDb = -10,
            BreathDb = -19, BreathLow = 1200, BreathHigh = 7500,
            VibHz = 5.0, VibCents = 9, Tremolo = 0.10,
            Roots = new[] { 62, 69, 76, 83, 89 },
            Lows  = new[] { 52, 66, 73, 80, 87 },
            Highs = new[] { 65, 72, 79, 86, 108 }
        };

        // An octave higher and purer, the breath a touch brighter.
        public static readonly Winds.Instrument Piccolo = new Winds.Instrument
        {
            Prefix = "PICCOLO ",
            PeakHz = 900, Rise = 1, Fall = 20, Bandwidth = 10000,
            Attack = 0.09, Lag = 0.15, ScoopCents = 5, ChiffDb = -12,
            BreathDb = -21, BreathLow = 2000, BreathHigh = 9500,
            VibHz = 5.4, VibCents = 8, Tremolo = 0.08,
            Roots = new[] { 77, 84, 91, 98 },
            Lows  = new[] { 70, 81, 88, 95 },
            Highs = new[] { 80, 87, 94, 108 }
        };

        // Purer still and straight - no vibrato to speak of - with a clear chiff and less breath.
        public static readonly Winds.Instrument Recorder = new Winds.Instrument
        {
            Prefix = "RECDR ",
            PeakHz = 300, Rise = 0, Fall = 22, EvenDb = -3, Bandwidth = 8000,
            Attack = 0.07, Lag = 0.1, ScoopCents = 0, ChiffDb = -12,
            BreathDb = -27, BreathLow = 1500, BreathHigh = 7000,
            VibHz = 4.5, VibCents = 2, Tremolo = 0.02,
            Roots = new[] { 65, 72, 79, 86, 93 },
            Lows  = new[] { 55, 69, 76, 83, 90 },
            Highs = new[] { 68, 75, 82, 89, 108 }
        };

        public static List<Patches.Wave> Waves() { return Winds.Waves(Flute, Piccolo, Recorder); }

        /// <summary>
        /// The attack is in the samples, so the machine's envelope only holds them and lets
        /// go: no attack of its own, sustain full, a short release for the breath to stop.
        /// </summary>
        public static List<Patch> Programmes()
        {
            var flute = new Patch
            {
                Name = "FLUTE",
                A = 0, D = 0, S = 99, R = 30,
                KeyToFilter = 50, VelToLoudness = 25, VelToFilter = 10
            };
            flute.Layers = Winds.Keys(Flute, 0, 0);

            var piccolo = flute.With("PICCOLO", Winds.Keys(Piccolo, 0, 0));
            piccolo.R = 26;

            var recorder = flute.With("RECORDER", Winds.Keys(Recorder, 0, 0));
            recorder.R = 22;
            recorder.VelToLoudness = 15;

            // Breathy and further off: the filter down, a slower start and a longer release.
            var soft = flute.With("SOFT FLUTE", Winds.Keys(Flute, -28, 0));
            soft.A = 12;
            soft.R = 42;

            return new List<Patch> { flute, piccolo, recorder, soft };
        }
    }

    /// <summary>The brass disk: trumpet, trombone, tuba and French horn.</summary>
    internal static class Brass
    {
        // Brilliant: the harmonics rising to a peak above a kilohertz before they fall.
        public static readonly Winds.Instrument Trumpet = new Winds.Instrument
        {
            Prefix = "TRUMPET ",
            PeakHz = 1300, Rise = 3, Fall = 13, FallAtStart = 12, Bandwidth = 9000,
            Attack = 0.09, Lag = 0.6, ScoopCents = 25,
            BreathDb = -36, BreathLow = 1500, BreathHigh = 6000,
            VibHz = 5.6, VibCents = 6, Tremolo = 0.03,
            Roots = new[] { 55, 62, 69, 76, 83 },
            Lows  = new[] { 48, 59, 66, 73, 80 },
            Highs = new[] { 58, 65, 72, 79, 96 }
        };

        // Rounder, its peak an octave and more lower, and a little slower to speak.
        public static readonly Winds.Instrument Trombone = new Winds.Instrument
        {
            Prefix = "TROMB ",
            PeakHz = 560, Rise = 3, Fall = 13, FallAtStart = 12, Bandwidth = 7500,
            Attack = 0.11, Lag = 0.6, ScoopCents = 20,
            BreathDb = -38, BreathLow = 1000, BreathHigh = 5000,
            VibHz = 5.2, VibCents = 4, Tremolo = 0.02,
            Roots = new[] { 41, 48, 55, 62, 69 },
            Lows  = new[] { 34, 45, 52, 59, 66 },
            Highs = new[] { 44, 51, 58, 65, 80 }
        };

        // Dark and round: the fundamental leading, the rest soon gone, and slow to speak.
        public static readonly Winds.Instrument Tuba = new Winds.Instrument
        {
            Prefix = "TUBA ",
            Rate = 20000,
            PeakHz = 230, Rise = 2, Fall = 15, FallAtStart = 10, Bandwidth = 5000,
            Attack = 0.16, Lag = 0.5, ScoopCents = 15,
            BreathDb = -40, BreathLow = 600, BreathHigh = 3000,
            VibHz = 4.8, VibCents = 3, Tremolo = 0.02,
            LoopSeconds = 0.6,
            Roots = new[] { 29, 36, 43, 50, 57 },
            Lows  = new[] { 21, 33, 40, 47, 54 },
            Highs = new[] { 32, 39, 46, 53, 66 }
        };

        // Mellow: the hand in the bell takes the top off steeply above its peak.
        public static readonly Winds.Instrument Horn = new Winds.Instrument
        {
            Prefix = "HORN ",
            PeakHz = 420, Rise = 3, Fall = 21, FallAtStart = 10, Bandwidth = 6000,
            Attack = 0.13, Lag = 0.5, ScoopCents = 15,
            BreathDb = -40, BreathLow = 800, BreathHigh = 4000,
            VibHz = 5.0, VibCents = 4, Tremolo = 0.02,
            Roots = new[] { 41, 48, 55, 62, 69 },
            Lows  = new[] { 34, 45, 52, 59, 66 },
            Highs = new[] { 44, 51, 58, 65, 80 }
        };

        public static List<Patches.Wave> Waves() { return Winds.Waves(Trumpet, Trombone, Tuba, Horn); }

        /// <summary>
        /// Brass gets brighter the harder it is blown, so the velocity opens the filter as well
        /// as raising the level: a soft note is darker as well as quieter.
        /// </summary>
        public static List<Patch> Programmes()
        {
            var trumpet = new Patch
            {
                Name = "TRUMPET",
                A = 0, D = 0, S = 99, R = 30,
                KeyToFilter = 50, VelToLoudness = 30, VelToFilter = 22
            };
            trumpet.Layers = Winds.Keys(Trumpet, -12, 0);

            var trombone = trumpet.With("TROMBONE", Winds.Keys(Trombone, -12, 0));
            trombone.R = 34;

            var tuba = trumpet.With("TUBA", Winds.Keys(Tuba, -10, 0));
            tuba.R = 40;

            var horn = trumpet.With("FRENCH HRN", Winds.Keys(Horn, -14, 0));
            horn.R = 40;
            horn.VelToFilter = 16;

            // A section: trumpets, trombones and horns together where their ranges meet,
            // each a little down for the stacking.
            var section = trumpet.With("BRASS SECT",
                Winds.Join(Winds.Keys(Trumpet, -12, -6), Winds.Keys(Trombone, -12, -6), Winds.Keys(Horn, -14, -6)));
            section.R = 34;

            return new List<Patch> { trumpet, trombone, tuba, horn, section };
        }
    }
}
