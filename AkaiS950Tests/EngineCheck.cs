using System;
using AkaiS950Engine;

/// <summary>
/// The engine, against the machine - and against the web version.
///
/// Two things are being checked and they are not the same thing. The first is that the
/// mappings agree with audio.js to the digit, so the two implementations cannot drift: the
/// expected values below were printed by the JavaScript, not by this. The second is that
/// what actually comes out of the renderer is what those mappings asked for - a filter that
/// is 3 dB down where it says it is, an envelope that falls at the measured rate, a vibrato
/// of the measured depth. A mapping can be right while the renderer using it is wrong, and
/// only rendering and measuring catches that.
///
/// Build and run:
///
///   $csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
///   $src = @("C:\Users\simon\AkaiS950Tests\EngineCheck.cs")
///   $src += (Get-ChildItem "C:\Users\simon\AkaiS950Engine\*.cs" | % { $_.FullName })
///   & $csc /nologo /target:exe /main:EngineCheck /out:"$env:TEMP\EngineCheck.exe" `
///       /r:System.dll /r:System.Core.dll $src
///   & "$env:TEMP\EngineCheck.exe"
/// </summary>
static class EngineCheck
{
    static int _fails;

    static void Check(string what, bool ok, string detail)
    {
        Console.WriteLine((ok ? "  ok   " : "  FAIL ") + what +
                          (string.IsNullOrEmpty(detail) ? "" : "   " + detail));
        if (!ok) _fails++;
    }

    static bool Near(double got, double want, double tol) { return Math.Abs(got - want) <= tol; }

    static string F(double v, int dp) { return v.ToString("F" + dp); }

    static int Main()
    {
        Console.WriteLine();
        Console.WriteLine("the mappings, against the numbers audio.js prints:");

        // cutoffHz(stored, rate) - the measured curve, interpolated in log frequency
        CheckCutoff(0, 44100, 311.000);
        CheckCutoff(20, 44100, 311.000);
        CheckCutoff(40, 44100, 1138.000);
        CheckCutoff(50, 44100, 2210.000);
        CheckCutoff(60, 44100, 4779.000);
        CheckCutoff(99, 44100, 16311.000);
        // The top is a fixed frequency, not a fraction of the playback rate - measured, see
        // Cal.TopHz. The only rate that caps it is the one being rendered at: at 20000 nothing
        // above 0.45 of it can be drawn.
        CheckCutoff(50, 20000, 2210.000);
        CheckCutoff(99, 20000, 9000.000);
        CheckCutoff(99, 96000, 16311.000);

        // Past the machine's fastest playback a note drops by octaves (Cal.MaxPlaybackHz).
        // The cases are the ones measured: ESQ BASS 1, 30 kHz, root 60, and VLA W VLN, 25 kHz.
        CheckFold(30000, 24, 0);    // note 84: 120 kHz, played as it is
        CheckFold(30000, 30, 1);    // note 90: 170 kHz, an octave down
        CheckFold(30000, 40, 1);    // note 100: 302 kHz, down once to 151
        CheckFold(30000, 41, 2);    // note 101: 322 kHz, down twice
        CheckFold(30000, 67, 4);    // note 127
        CheckFold(25000, 30, 0);    // VLA note 90: 141 kHz - a rate limit, not a key limit

        // The measured points of Cal.EnvTime. 20 and 50 USED to be interpolation across the
        // long unmeasured gap at the bottom, and this test pinned those interpolated values
        // - which is how it should have behaved, and it duly failed when run 19 measured
        // the gap and found them 26% and 14% slow. They are measurements now. 25 is here as
        // well because it is where the old guess was furthest out, by 31%.
        CheckEnv(0, 0.010400);
        CheckEnv(20, 0.031660);
        CheckEnv(25, 0.041820);
        CheckEnv(50, 0.303820);

        // The low bit of the byte is ignored - run 23 read every value from 45 to 56 and
        // found the pairs identical. 51 must play what 50 plays, not something between 50
        // and 52, which is what the table used to interpolate.
        CheckEnv(51, 0.303820);
        CheckEnv(47, 0.221760);
        Check("odd envelope settings play the even one below, not an interpolation",
              Near(Cal.EnvSeconds(53), Cal.EnvSeconds(52), 1e-12) &&
              !Near(Cal.EnvSeconds(53), Cal.EnvSeconds(54), 1e-6),
              F(Cal.EnvSeconds(53), 6) + " against 52's " + F(Cal.EnvSeconds(52), 6));
        CheckEnv(70, 1.403700);
        CheckEnv(80, 2.813600);
        CheckEnv(90, 4.117200);
        CheckEnv(95, 8.094700);
        CheckEnv(99, 10.740100);

        // The two rules run 19 changed. A stored sustain of 0 is SILENCE, not the bottom of
        // the line the rest sit on; and the decay is a RATE, so its length scales with the
        // drop it has to cover rather than being the same at every depth.
        Check("sustain 99 rests at full level",
              Near(Cal.SustainDbFor(99), 0.0, 1e-9), F(Cal.SustainDbFor(99), 2));
        Check("sustain 50 rests 19.6 dB down",
              Near(Cal.SustainDbFor(50), -19.6, 0.05), F(Cal.SustainDbFor(50), 2));
        Check("sustain 0 is silence, not 39.6 dB down",
              Cal.SustainDbFor(0) < -90.0, F(Cal.SustainDbFor(0), 2));

        double deep = Cal.VcaDecaySeconds(65, Cal.SustainDbFor(0));
        double half = Cal.VcaDecaySeconds(65, Cal.SustainDbFor(50));
        Check("a decay to sustain 50 is shorter than one to sustain 0",
              half < deep * 0.5, F(half, 3) + "s against " + F(deep, 3) + "s");
        Check("and both fall at the same rate, which is what a RATE means",
              Near(-Cal.SustainDbFor(0) / deep, -Cal.SustainDbFor(50) / half, 1e-6),
              F(-Cal.SustainDbFor(0) / deep, 1) + " dB/s");

        // The VCA attack is a counter, not a curve: 5.4/n for whole n. These are the measured
        // settings, and the pairs that share an n are the point - 70 and 75, 80 and 85, and
        // 90 through 99, all of which the hardware returns identical.
        // 24 and 14, not 26 and 15: run 23 set the byte directly where the table had
        // reached these sideways through the velocity rule.
        CheckAttack(8, 5.4 / 300);
        CheckAttack(20, 5.4 / 56);
        CheckAttack(30, 5.4 / 24);
        CheckAttack(40, 5.4 / 14);
        CheckAttack(50, 5.4 / 9);
        CheckAttack(60, 5.4 / 6);
        CheckAttack(70, 5.4 / 4);
        CheckAttack(75, 5.4 / 4);
        CheckAttack(80, 5.4 / 3);
        CheckAttack(85, 5.4 / 3);
        CheckAttack(90, 5.4 / 2);
        CheckAttack(99, 5.4 / 2);

        // Attack 0 is a hard gate, which is what gating a drum means - not measured, but how
        // envelope generators are built. See Cal.VcaAttackGate.
        Check("attack 0 is a hard gate", Cal.VcaAttackSeconds(0) == 0.0,
              F(Cal.VcaAttackSeconds(0) * 1000, 1) + " ms");
        Check("and the gate does not swallow settings that should ramp",
              Cal.VcaAttackSeconds(5) > 0.001 && Cal.VcaAttackSeconds(30) > 0.2,
              F(Cal.VcaAttackSeconds(5) * 1000, 1) + " ms at 5");

        int backwards = 0, shared = 0;
        double last = -1;
        for (int v = 0; v <= 99; v++)
        {
            double t = Cal.VcaAttackSeconds(v);
            if (t < last - 1e-9) backwards++;
            else if (Math.Abs(t - last) < 1e-9) shared++;

            if (t > 0)
            {
                double n = Cal.VcaAttackSpan / t;
                if (Math.Abs(n - Math.Round(n)) > 1e-9)
                    Check("attack " + v + " is a whole number of steps", false, F(n, 4));
            }
            last = t;
        }

        Check("the attack never shortens as the byte rises", backwards == 0,
              backwards + " went backwards");
        Check("and it steps rather than sliding, being a counter", shared > 40,
              shared + " of 99 share an attack with the one below");

        Check("sustain 50 is 19.8 dB down, not half",
              Near(Cal.DbToGain(-(1 - 50 / 99.0) * Cal.SustainDb), 0.104713, 0.00002),
              F(Cal.DbToGain(-(1 - 50 / 99.0) * Cal.SustainDb), 6) + ", audio.js says 0.104713");

        // 8.0 dB, not the 5.8 this pinned before run 20 walked the whole ladder and found
        // 0.401 dB a unit where one reading on a limited take had said 0.29.
        Check("zone loudness +20 is 8.0 dB up",
              Near(Cal.DbToGain(20 * Cal.LoudnessDbPerUnit), 2.517677, 0.00002),
              F(Cal.DbToGain(20 * Cal.LoudnessDbPerUnit), 6) + ", audio.js says 2.517677");

        // ------------------------------------------------------------ the renderer

        Console.WriteLine();
        Console.WriteLine("and what actually comes out of it:");

        FilterCheck();
        EnvelopeCheck();
        LfoCheck();
        HoldCheck();
        VelocityZoneCheck();
        PolyphonyCheck();

        Console.WriteLine();
        Console.WriteLine(_fails == 0 ? "all good" : _fails + " FAILED");
        return _fails == 0 ? 0 : 1;
    }

    static void CheckFold(double sourceRate, int semitones, int octavesDown)
    {
        double asked = Math.Pow(2.0, semitones / 12.0);
        double got = Cal.FoldedRatio(sourceRate, asked);
        double want = asked / Math.Pow(2.0, octavesDown);
        Check(F(sourceRate, 0) + " Hz up " + semitones + " semitones plays " + octavesDown + " octave(s) down",
              Near(got, want, 1e-9), F(got, 4));
    }

    static void CheckCutoff(int stored, double rate, double want)
    {
        double got = Cal.CutoffHz(stored, rate);
        Check("cutoff " + stored + " at " + rate + " Hz is " + F(want, 0) + " Hz",
              Near(got, want, 0.5), F(got, 3));
    }

    static void CheckAttack(int stored, double want)
    {
        double got = Cal.VcaAttackSeconds(stored);
        Check("VCA attack " + stored + " is " + F(want, 4) + "s (5.4/" +
              F(Cal.VcaAttackSpan / want, 0) + ")", Near(got, want, 1e-9), F(got, 4));
    }

    static void CheckEnv(int stored, double want)
    {
        double got = Cal.EnvSeconds(stored);
        Check("envelope time " + stored + " is " + F(want, 6) + "s",
              Near(got, want, 1e-6), F(got, 6));
    }

    // ------------------------------------------------------------------ the tests

    const double Rate = 48000;

    /// <summary>A tone to play: a sine, exactly periodic so a loop of it never clicks.</summary>
    static Sound Tone(double hz, double seconds)
    {
        int rate = 48000;
        int period = (int)Math.Round(rate / hz);
        int cycles = Math.Max(1, (int)(seconds * hz));
        int n = period * cycles;

        var a = new float[n];
        for (int i = 0; i < n; i++) a[i] = (float)Math.Sin(2 * Math.PI * (i % period) / period);

        return new Sound
        {
            Name = "TONE", Audio = a, SourceRate = rate, RootPitch = 60,
            Loops = true, LoopFrom = 0, LoopTo = n
        };
    }

    /// <summary>White noise, for measuring a filter.</summary>
    static Sound Noise(double seconds)
    {
        int n = (int)(48000 * seconds);
        var a = new float[n];
        var rng = new Random(12345);
        for (int i = 0; i < n; i++) a[i] = (float)(rng.NextDouble() * 2 - 1);
        return new Sound
        {
            Name = "NOISE", Audio = a, SourceRate = 48000, RootPitch = 60,
            Loops = true, LoopFrom = 0, LoopTo = n
        };
    }

    static KeygroupPatch Flat(Sound s)
    {
        // a flat gate and a wide-open filter: nothing shapes the sound but what a test asks
        return new KeygroupPatch
        {
            LowKey = 0, HighKey = 127, Sound = s,
            VcaAttack = 0, VcaDecay = 0, VcaSustain = 99, VcaRelease = 0,
            VcfWritten = true, VcfAttack = 0, VcfDecay = 0, VcfSustain = 99, VcfRelease = 0,
            VcfAmount = 0, VelToFilter = 0, KeyToFilter = 0, VelToLoudness = 0,
            LfoDelay = 0, LfoRate = 0, LfoDepth = 0, LfoModwheelDepth = 0, LfoDesync = true,
            ZoneFilter = 99, ZoneLoudness = 0, ZoneTranspose = 0
        };
    }

    static float[] Render(KeygroupPatch kg, int note, int velocity, double seconds,
                          double releaseAt)
    {
        var p = new Patch();
        p.Keygroups.Add(kg);

        var eng = new Engine(Rate);
        eng.Gain = 1f;
        eng.SetPatch(p);
        eng.NoteOn(note, velocity);

        int total = (int)(Rate * seconds);
        var outBuf = new float[total];
        int block = 256;
        int releaseSample = releaseAt > 0 ? (int)(Rate * releaseAt) : int.MaxValue;
        bool released = false;

        for (int at = 0; at < total; at += block)
        {
            int n = Math.Min(block, total - at);
            if (!released && at >= releaseSample) { eng.NoteOff(note); released = true; }
            eng.Render(outBuf, at, n);
        }
        return outBuf;
    }

    /// <summary>Power at one frequency, by Goertzel.</summary>
    /// <summary>
    /// The power at one frequency, through a Hann window.
    ///
    /// The window is not a nicety. Without one this is a rectangular-windowed DFT bin, whose
    /// sidelobes fall off as 1/df - so a bin down in the passband collects leakage from every
    /// frequency in the signal, and collects MORE of it from the unfiltered reference than
    /// from the filtered take simply because the reference has more up there. The passband
    /// then reads several dB down when it is actually flat, and how far down depends on where
    /// the corner happens to be. Moving the measured cutoff from 1878 Hz to 2210 was enough
    /// to turn that from a pass into a failure, which is a test reporting on its own window
    /// rather than on the filter.
    /// </summary>
    static double Power(float[] x, int from, int len, double hz)
    {
        double re = 0, im = 0, w = 2 * Math.PI * hz / Rate, sum = 0;
        for (int i = 0; i < len; i++)
        {
            double win = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (len - 1.0));
            double a = w * i;
            re += x[from + i] * win * Math.Cos(a);
            im -= x[from + i] * win * Math.Sin(a);
            sum += win;
        }
        return (re * re + im * im) / (sum * sum);
    }

    static double Rms(float[] x, int from, int len)
    {
        double s = 0;
        for (int i = 0; i < len; i++) s += x[from + i] * (double)x[from + i];
        return Math.Sqrt(s / len);
    }

    // -------------------------------------------------------------------- filter

    /// <summary>
    /// Noise through the filter at a known cutoff, measured where it should be 3 dB down and
    /// an octave above that, where six poles should have taken 36 dB off it.
    /// </summary>
    static void FilterCheck()
    {
        var kg = Flat(Noise(3));
        kg.ZoneFilter = 50;                      // 2210 Hz, measured
        float[] y = Render(kg, 60, 100, 2.0, 0);

        var flat = Flat(Noise(3));
        float[] reference = Render(flat, 60, 100, 2.0, 0);

        int from = (int)(Rate * 0.5), len = (int)(Rate * 1.0);
        double fc = Cal.CutoffHz(50, 48000);

        double atCorner = 10 * Math.Log10(Power(y, from, len, fc) /
                                          Power(reference, from, len, fc));
        Check("the filter is 3 dB down at its corner", Near(atCorner, -3, 1.5),
              F(atCorner, 1) + " dB at " + F(fc, 0) + " Hz");

        double oct = 10 * Math.Log10(Power(y, from, len, fc * 2) /
                                     Power(reference, from, len, fc * 2));
        Check("  and 36 dB down an octave above it", Near(oct - atCorner, -36, 6),
              F(oct - atCorner, 1) + " dB per octave");

        double below = 10 * Math.Log10(Power(y, from, len, fc / 4) /
                                       Power(reference, from, len, fc / 4));
        Check("  and flat two octaves below", Near(below, 0, 1.5), F(below, 1) + " dB");
    }

    // ------------------------------------------------------------------ envelope

    /// <summary>
    /// The decay is a straight line in decibels, which is the thing most worth checking:
    /// a line in amplitude would hang near the peak and then fall off a cliff.
    /// </summary>
    static void EnvelopeCheck()
    {
        var kg = Flat(Tone(400, 1));
        kg.VcaDecay = 80;                        // 2.868 s, measured
        kg.VcaSustain = 0;
        float[] y = Render(kg, 60, 100, 2.5, 0);

        // how far it has fallen at four points, in decibels
        double[] at = { 0.5, 1.0, 1.5, 2.0 };
        var db = new double[at.Length];
        for (int i = 0; i < at.Length; i++)
        {
            int from = (int)(Rate * at[i]);
            db[i] = 20 * Math.Log10(Math.Max(Rms(y, from, 2000), 1e-9) /
                                    Math.Max(Rms(y, 200, 2000), 1e-9));
        }

        // the steps between them should be equal, which is what "straight in dB" means
        double s1 = db[1] - db[0], s2 = db[2] - db[1], s3 = db[3] - db[2];
        double spread = Math.Max(s1, Math.Max(s2, s3)) - Math.Min(s1, Math.Min(s2, s3));
        Check("the decay falls in a straight line in decibels", spread < 1.5,
              "steps of " + F(s1, 1) + ", " + F(s2, 1) + ", " + F(s3, 1) + " dB");

        // and it should take the measured time to get to the bottom
        double perSecond = (db[3] - db[0]) / (at[3] - at[0]);
        double toFloor = Cal.SustainDb / -perSecond;
        Check("  and reaches the floor in about the measured 2.87s", Near(toFloor, 2.87, 0.6),
              F(toFloor, 2) + "s at " + F(perSecond, 1) + " dB/s");

        // a release has to actually stop the note
        var held = Flat(Tone(400, 1));
        held.VcaRelease = 20;
        float[] r = Render(held, 60, 100, 2.0, 0.5);
        double after = Rms(r, (int)(Rate * 1.5), 4000);
        Check("  and a released note goes quiet", after < 0.002, "rms " + F(after, 6));
    }

    // ----------------------------------------------------------------------- LFO

    /// <summary>
    /// The vibrato, measured back out of the rendered audio the way the calibration run
    /// measures it off the machine: demodulate the tone and look at how its pitch moves.
    /// </summary>
    static void LfoCheck()
    {
        var kg = Flat(Tone(400, 1));
        kg.LfoRate = 60;
        kg.LfoDepth = 99;
        float[] y = Render(kg, 60, 100, 3.0, 0);

        double[] cents = Demodulate(y, (int)(Rate * 0.5), (int)(Rate * 2.0), 400);

        // the rate: scan for the strongest periodicity in the pitch track
        double best = 0, bestAmp = -1;
        for (double f = 1; f <= 20; f += 0.01)
        {
            double a = TrackAmplitude(cents, f);
            if (a > bestAmp) { bestAmp = a; best = f; }
        }

        double wantHz = Cal.LfoRateHzAtZero + 60 * Cal.LfoRateHzPerUnit;
        Check("the vibrato runs at the measured rate", Near(best, wantHz, wantHz * 0.03),
              F(best, 3) + " Hz, the law says " + F(wantHz, 3));

        double wantCents = 99 * Cal.LfoDepthCentsPerUnit;
        Check("  and at the measured depth", Near(bestAmp, wantCents, wantCents * 0.08),
              F(bestAmp, 1) + " cents, the law says " + F(wantCents, 1));

        // depth 0 has to be silent, or the oscillator is running when it should not
        var flat = Flat(Tone(400, 1));
        float[] z = Render(flat, 60, 100, 1.5, 0);
        double[] still = Demodulate(z, (int)(Rate * 0.4), (int)(Rate * 0.8), 400);
        double wobble = 0;
        for (int i = 0; i < still.Length; i++) wobble = Math.Max(wobble, Math.Abs(still[i]));
        Check("  and a keygroup with no depth does not wobble", wobble < 2,
              F(wobble, 2) + " cents");
    }

    /// <summary>
    /// The tone's pitch over time, in cents against its own mean.
    ///
    /// The same trick the calibration analysis uses: multiply by a cosine and a sine at the
    /// carrier, average each product over exactly one period of it - which nulls every
    /// harmonic - and watch the angle turn.
    /// </summary>
    static double[] Demodulate(float[] x, int from, int len, double carrier)
    {
        int L = (int)Math.Round(Rate / carrier);
        var mi = new double[len];
        var mq = new double[len];
        double w = 2 * Math.PI * carrier / Rate;

        for (int i = 0; i < len; i++)
        {
            double a = w * i;
            mi[i] = x[from + i] * Math.Cos(a);
            mq[i] = -x[from + i] * Math.Sin(a);
        }
        Boxcar(mi, L); Boxcar(mq, L);

        int hop = (int)(Rate / 500);
        int frames = (len - 2 * L) / hop;
        var phase = new double[frames];
        double prev = 0, turns = 0;

        for (int f = 0; f < frames; f++)
        {
            int at = 2 * L + f * hop;
            double ph = Math.Atan2(mq[at], mi[at]);
            if (f > 0)
            {
                double d = ph - prev;
                while (d > Math.PI) { d -= 2 * Math.PI; turns--; }
                while (d < -Math.PI) { d += 2 * Math.PI; turns++; }
            }
            prev = ph;
            phase[f] = ph + 2 * Math.PI * turns;
        }

        double frameRate = Rate / hop;
        var hz = new double[frames - 1];
        for (int k = 0; k < hz.Length; k++)
            hz[k] = carrier + (phase[k + 1] - phase[k]) * frameRate / (2 * Math.PI);

        double mean = 0;
        for (int k = 0; k < hz.Length; k++) mean += hz[k];
        mean /= hz.Length;

        var cents = new double[hz.Length];
        for (int k = 0; k < cents.Length; k++)
            cents[k] = 1200 * Cal.Log2(Math.Max(hz[k], 1e-6) / mean);
        return cents;
    }

    static void Boxcar(double[] a, int len)
    {
        var outp = new double[a.Length];
        double sum = 0;
        int half = len / 2;
        for (int i = 0; i < a.Length; i++)
        {
            sum += a[i];
            if (i >= len) sum -= a[i - len];
            outp[Math.Max(0, i - half)] = sum / Math.Min(len, i + 1);
        }
        Array.Copy(outp, a, a.Length);
    }

    /// <summary>The amplitude of a cosine at f in a track sampled 500 times a second.</summary>
    static double TrackAmplitude(double[] a, double f)
    {
        const double FrameRate = 500;
        int whole = (int)(a.Length / (FrameRate / f));
        int n = whole >= 1 ? (int)(whole * FrameRate / f) : a.Length;
        if (n < 4) return 0;

        double re = 0, im = 0, w = 2 * Math.PI * f / FrameRate;
        for (int i = 0; i < n; i++)
        {
            double ang = w * i;
            re += a[i] * Math.Cos(ang);
            im -= a[i] * Math.Sin(ang);
        }
        return Math.Sqrt(re * re + im * im) * 2 / n;
    }

    // --------------------------------------------------------------- holding a note

    /// <summary>
    /// A looped note sustains while it is held and stops when it is not.
    ///
    /// The first half of that is what a loop is for. The second half is the part that
    /// went wrong: the engine released correctly all along, but nothing in the editor ever
    /// called NoteOff - the piano keyboard raised a click and no matching release - so
    /// every looped sample played until the program closed, and after eight of them every
    /// voice was held and the next key stole one.
    /// </summary>
    static void HoldCheck()
    {
        var p2 = new Patch();
        var kg = Flat(Tone(400, 2));
        kg.VcaRelease = 20;                      // about a tenth of a second
        p2.Keygroups.Add(kg);

        var eng = new Engine(Rate);
        eng.Gain = 1f;
        eng.SetPatch(p2);
        eng.NoteOn(60, 100);

        var buf = new float[(int)(Rate * 0.25)];

        // held, and well past the end of the sample itself
        for (int i = 0; i < 24; i++) eng.Render(buf, 0, buf.Length);
        double heldRms = Rms(buf, 0, buf.Length);
        Check("a looped note is still sounding six seconds in", heldRms > 0.05,
              "rms " + F(heldRms, 3) + ", " + eng.ActiveVoices + " voice");

        // let go
        eng.NoteOff(60);
        for (int i = 0; i < 8; i++) eng.Render(buf, 0, buf.Length);

        double afterRms = Rms(buf, 0, buf.Length);
        Check("  and stops when the key is let go", afterRms < 0.002,
              "rms " + F(afterRms, 6));
        Check("  giving its voice back", eng.ActiveVoices == 0,
              eng.ActiveVoices + " still active");

        // and the voices must not leak: press and release the same key many times
        for (int i = 0; i < 40; i++)
        {
            eng.NoteOn(60, 100);
            eng.Render(buf, 0, buf.Length);
            eng.NoteOff(60);
            for (int j = 0; j < 4; j++) eng.Render(buf, 0, buf.Length);
        }
        Check("  and forty presses leave nothing behind", eng.ActiveVoices == 0,
              eng.ActiveVoices + " still active");
    }

    // ------------------------------------------------------------ velocity zones

    /// <summary>
    /// The two zones of a keygroup are alternatives, never both.
    ///
    /// Sounding both was the bug behind "a bell-like tone over every note": 74 of the
    /// library's 168 two-zone keygroups name the SAME sample in both, so playing them
    /// together laid two copies of one sample over each other, and 121 of the 168 set the
    /// switch to 128 - which is the panel saying there is no second zone at all, since no
    /// velocity can reach it.
    /// </summary>
    static void VelocityZoneCheck()
    {
        Sound soft = Tone(400, 1), hard = Tone(300, 1);

        var p2 = new Patch();
        var a2 = Flat(soft); a2.VelocityFrom = 0; a2.VelocityTo = 61;
        var b2 = Flat(hard); b2.VelocityFrom = 62; b2.VelocityTo = 127;
        p2.Keygroups.Add(a2);
        p2.Keygroups.Add(b2);

        var into = new System.Collections.Generic.List<KeygroupPatch>();

        p2.Matching(60, 50, into);
        Check("a soft strike takes the lower zone only",
              into.Count == 1 && into[0].Sound == soft, into.Count + " zone(s)");

        p2.Matching(60, 100, into);
        Check("  a hard one takes the upper zone only",
              into.Count == 1 && into[0].Sound == hard, into.Count + " zone(s)");

        p2.Matching(60, 61, into);
        Check("  and the switch itself is the boundary",
              into.Count == 1 && into[0].Sound == soft, into.Count + " zone(s)");

        // a switch of 128 means there is no second zone: zone 1 answers everything
        var p3 = new Patch();
        var only = Flat(soft); only.VelocityFrom = 0; only.VelocityTo = 127;
        p3.Keygroups.Add(only);
        p3.Matching(60, 127, into);
        Check("  a switch of 128 leaves zone 1 the whole range", into.Count == 1,
              into.Count + " zone(s) at velocity 127");

        // and it really is one VOICE, not one match
        var eng = new Engine(Rate);
        eng.SetPatch(p2);
        var buf = new float[512];
        eng.NoteOn(60, 100);
        eng.Render(buf, 0, buf.Length);
        Check("  so one key makes one voice, not two", eng.ActiveVoices == 1,
              eng.ActiveVoices + " voices");
    }

    // ------------------------------------------------------------------ polyphony

    static void PolyphonyCheck()
    {
        var p = new Patch();
        p.Keygroups.Add(Flat(Tone(400, 1)));

        var eng = new Engine(Rate);
        eng.SetPatch(p);

        var buf = new float[512];
        for (int n = 60; n < 60 + Engine.Polyphony; n++) eng.NoteOn(n, 100);
        eng.Render(buf, 0, buf.Length);
        Check("eight notes make eight voices", eng.ActiveVoices == Engine.Polyphony,
              eng.ActiveVoices + " of " + Engine.Polyphony);

        // a ninth steals rather than being dropped or overflowing
        eng.NoteOn(72, 100);
        eng.Render(buf, 0, buf.Length);
        Check("  and a ninth steals the oldest rather than being lost",
              eng.ActiveVoices == Engine.Polyphony, eng.ActiveVoices + " voices");

        eng.AllNotesOff();
        for (int i = 0; i < 400; i++) eng.Render(buf, 0, buf.Length);
        Check("  and all-notes-off empties it", eng.ActiveVoices == 0,
              eng.ActiveVoices + " left");
    }
}
