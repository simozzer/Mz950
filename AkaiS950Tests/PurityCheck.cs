using System;
using System.Collections.Generic;
using AkaiS950Engine;

/// <summary>
/// What comes out that should not.
///
/// A sampler's output is its sample's partials, moved in pitch. Anything at a frequency
/// that is NOT one of those is something the engine added, and where it sits says what
/// added it:
///
///   inharmonic, and moving the wrong way when the key goes up
///       aliasing. The interpolator is folding partials back down past Nyquist, and
///       folded partials are not harmonically related to anything, which is exactly the
///       "bells over the top" that prompted this file.
///
///   at multiples of the control-block rate either side of a partial
///       the modulators are stepping rather than sweeping. The block is 32 samples, so
///       1500 Hz at 48 kHz.
///
///   at the loop rate and its harmonics
///       the loop join is still discontinuous.
///
/// MEASURING IT HONESTLY
///
/// The first version of this got it wrong twice and both mistakes are worth keeping in
/// mind. It read the spectrum through a rectangular window, whose sidelobes put a phantom
/// partial 35 Hz from the fundamental at -34 dB - an artefact of the measurement, not of
/// the engine. And it excluded a fixed 30 Hz around each harmonic, which is nothing like
/// enough when a vibrato of 150 cents is sweeping the partial across 70 Hz of its own.
/// </summary>
static class PurityCheck
{
    const double Rate = 48000;
    static int _fails;

    static void Check(string what, bool ok, string detail)
    {
        Console.WriteLine((ok ? "  ok   " : "  FAIL ") + what + (detail == null ? "" : "   " + detail));
        if (!ok) _fails++;
    }

    static int Main()
    {
        Console.WriteLine();
        Console.WriteLine("looking for partials the source never had:");
        Console.WriteLine();

        // A sine cannot alias into anything but itself, so it isolates the modulators.
        Console.WriteLine("  -- a sine, which has one partial and nothing to fold --");
        Probe("plain, at its own pitch", true, 60, 99, 0, 0, 0, 0);
        Probe("filter sweeping", true, 60, 55, 40, 60, 0, 0);
        Probe("vibrato at full depth", true, 60, 99, 0, 0, 60, 99);

        // A sawtooth has partials all the way up, which is what aliases when it is
        // transposed. This is the case a real sample is in.
        Console.WriteLine();
        Console.WriteLine("  -- a sawtooth, whose upper partials can fold --");
        Probe("at its own pitch", false, 60, 99, 0, 0, 0, 0);
        Probe("up a fifth", false, 67, 99, 0, 0, 0, 0);
        // As far up as a 48 kHz sample goes before the machine drops it an octave: two
        // octaves would ask for 192 kHz, past Cal.MaxPlaybackHz, and fold to one.
        Probe("up an octave and a fifth", false, 79, 99, 0, 0, 0, 0);

        Console.WriteLine();
        Console.WriteLine(_fails == 0 ? "all good" : _fails + " FAILED");
        return _fails == 0 ? 0 : 1;
    }

    static void Probe(string what, bool sine, int note, int filter, int vcfDecay,
                      int vcfAmount, int lfoRate, int lfoDepth)
    {
        float[] audio = Render(sine, note, filter, vcfDecay, vcfAmount, lfoRate, lfoDepth);

        double f0 = 400.0 * Math.Pow(2, (note - 60) / 12.0);

        /*
         * How far a partial can wander and still be itself.
         *
         * Its own sweep, and then the sidebands. Frequency modulation does not put the
         * energy inside the sweep and stop: it spreads it into sidebands every LFO rate
         * apart, whose strengths are Bessel functions of the modulation index, and the
         * tail of those runs well past the deviation. At depth 99 the index is about 5,
         * and the ninth sideband is still at -32 dB - which this test dutifully reported
         * as a stray partial at 466 Hz until the arithmetic was done properly.
         *
         * Twelve sidebands of margin covers it to below -80 dB. On a 400 Hz carrier that
         * is 85 Hz, so anything genuinely wrong - aliasing, or the 1500 Hz control block -
         * is still nowhere near being excused.
         */
        double lfoHz = Cal.LfoRateHzAtZero + lfoRate * Cal.LfoRateHzPerUnit;
        double spread = lfoDepth > 0
            ? Math.Pow(2, lfoDepth * Cal.LfoDepthCentsPerUnit / 1200.0) - 1.0 + 0.01
            : 0.01;
        double sidebands = lfoDepth > 0 ? 12 * lfoHz : 0;

        int from = (int)(Rate * 0.4), len = 1 << 15;      // 0.68 s, a power of two
        if (from + len > audio.Length) len = audio.Length - from;

        double loudest = 0;
        for (int k = 1; k * f0 < Rate / 2; k++)
            loudest = Math.Max(loudest, Power(audio, from, len, k * f0));
        if (loudest <= 0) { Check(what, false, "nothing came out"); return; }

        double worst = 0, worstAt = 0;
        for (double f = 60; f < 20000; f += 7)
        {
            bool belongs = false;
            for (int k = 1; k * f0 < Rate / 2 + f0; k++)
            {
                double centre = k * f0;
                if (Math.Abs(f - centre) < centre * spread + sidebands + 25)
                { belongs = true; break; }
            }
            if (belongs) continue;

            double p = Power(audio, from, len, f);
            if (p > worst) { worst = p; worstAt = f; }
        }

        double db = 10 * Math.Log10(worst / loudest);
        Check(what, db < -40, "worst stray partial " + db.ToString("F1") + " dB at " +
              worstAt.ToString("F0") + " Hz");

        if (db >= -40) Blame(worstAt, f0);
    }

    /// <summary>Name the usual suspects, so a failure points somewhere.</summary>
    static void Blame(double at, double f0)
    {
        double block = Rate / 32;
        for (int k = 1; k <= 6; k++)
        {
            if (Math.Abs(at - (f0 + k * block)) < 80 || Math.Abs(at - Math.Abs(k * block - f0)) < 80)
            {
                Console.WriteLine("         = the partial +/- " + k + " x the 32-sample control " +
                                  "block (" + block.ToString("F0") + " Hz):");
                Console.WriteLine("           the modulators are stepping, not sweeping.");
                return;
            }
        }

        // an aliased partial is at |k.f0 - rate| for some k
        for (int k = 2; k < 200; k++)
        {
            if (Math.Abs(at - Math.Abs(k * f0 - Rate)) < 80)
            {
                Console.WriteLine("         = harmonic " + k + " of the note folded back " +
                                  "round the sample rate:");
                Console.WriteLine("           the interpolator is aliasing.");
                return;
            }
        }
        Console.WriteLine("         not one of the usual shapes - worth looking at directly.");
    }

    static float[] Render(bool sine, int note, int filter, int vcfDecay, int vcfAmount,
                          int lfoRate, int lfoDepth)
    {
        int rate = 48000, period = rate / 400, n = period * 400;
        var a = new float[n];

        for (int i = 0; i < n; i++)
        {
            double ph = 2 * Math.PI * (i % period) / period;
            if (sine) a[i] = (float)(0.8 * Math.Sin(ph));
            else
            {
                // a band-limited sawtooth: everything a 12-bit sample would hold, and
                // nothing above what its own rate could carry
                double v = 0;
                for (int k = 1; k <= 30; k++) v += Math.Sin(k * ph) / k;
                a[i] = (float)(v * 0.45);
            }
        }

        var sound = new Sound
        {
            Name = sine ? "SINE" : "SAW", Audio = a, SourceRate = rate, RootPitch = 60,
            Loops = true, LoopFrom = 0, LoopTo = n
        };

        var kg = new KeygroupPatch
        {
            LowKey = 0, HighKey = 127, Sound = sound,
            VcaAttack = 0, VcaDecay = 0, VcaSustain = 99, VcaRelease = 0,
            VcfWritten = true, VcfAttack = 0, VcfDecay = vcfDecay, VcfSustain = 40,
            VcfRelease = 0, VcfAmount = vcfAmount,
            ZoneFilter = filter,
            LfoRate = lfoRate, LfoDepth = lfoDepth, LfoDelay = 0, LfoDesync = true
        };

        var p = new Patch();
        p.Keygroups.Add(kg);

        var eng = new Engine(Rate);
        eng.Gain = 1f;
        eng.SetPatch(p);
        eng.NoteOn(note, 100);

        var buf = new float[(int)(Rate * 1.5)];
        for (int at = 0; at < buf.Length; at += 480)
            eng.Render(buf, at, Math.Min(480, buf.Length - at));
        return buf;
    }

    /// <summary>
    /// Power at one frequency, through a Hann window.
    ///
    /// The window is the whole point: without it the measurement's own sidelobes are
    /// louder than anything it is looking for.
    /// </summary>
    static double Power(float[] x, int from, int len, double hz)
    {
        double re = 0, im = 0, w = 2 * Math.PI * hz / Rate, norm = 0;
        for (int i = 0; i < len; i++)
        {
            double win = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (len - 1));
            double a = w * i;
            re += x[from + i] * win * Math.Cos(a);
            im -= x[from + i] * win * Math.Sin(a);
            norm += win;
        }
        return (re * re + im * im) / (norm * norm);
    }
}
