using System;
using System.Collections.Generic;
using System.IO;
using AkaiS950Synth;

/// <summary>
/// The C# half of synthcheck.ps1: the same list of renders as Plugin/Tests/SynthDump.cpp,
/// in the same order, from the same seeds. See that file.
/// </summary>
internal static class SynthDump
{
    static string folder;

    static void Dump(string name, short[] w)
    {
        uint h = 2166136261;
        unchecked
        {
            foreach (short v in w)
            {
                h ^= (byte)(v & 0xFF);        h *= 16777619;
                h ^= (byte)((v >> 8) & 0xFF); h *= 16777619;
            }
        }

        var line = string.Format("{0,-10} {1,7} {2:x8}", name, w.Length, h);
        for (int i = 0; i < 8 && i < w.Length; i++) line += " " + w[i];
        Console.WriteLine(line);

        var bytes = new byte[w.Length * 2];
        Buffer.BlockCopy(w, 0, bytes, 0, bytes.Length);
        File.WriteAllBytes(Path.Combine(folder, name.Replace(' ', '_') + ".s16"), bytes);
    }

    static Spectrum[] Mirror(Spectrum[] s)
    {
        if (s.Length < 3) return s;
        var outp = new List<Spectrum>(s);
        for (int i = s.Length - 2; i >= 1; i--) outp.Add(s[i]);
        return outp.ToArray();
    }

    static Spectrum[] Steps(int count, Func<double, Spectrum> at)
    {
        var s = new Spectrum[count];
        for (int i = 0; i < count; i++) s[i] = at(i / (double)(count - 1));
        return s;
    }

    static int Main(string[] args)
    {
        if (args.Length < 1) { Console.Error.WriteLine("usage: SynthDump <folder>"); return 2; }
        folder = args[0];

        double hz = Patches.RootHz;
        int h = Waveforms.HighestHarmonic(20000, hz);

        Action<string, Waveforms.Harmonic> plain = (name, shape) => Dump(name, Waveforms.Render(shape, 40000, hz));

        plain("SAW",      Waveforms.Saw);
        plain("SQUARE",   Waveforms.Square);
        plain("TRIANGLE", Waveforms.Triangle);
        plain("SINE",     Waveforms.Sine);
        plain("ORGAN",    Waveforms.Organ);
        plain("HOLLOW",   Waveforms.Hollow);
        plain("GLASS",    Waveforms.Glass);
        plain("BUZZ",     Waveforms.Buzz);
        plain("PULSE25",  Waveforms.Pulse(0.25));
        plain("FIFTHS",   Waveforms.Lift(Waveforms.Saw, 0.55, 3, 6));
        plain("REED",     Waveforms.Partials(1.0, 0.5, 0.7, 0.25, 0.3, 0.2, 0.45));

        Action<string, int, Spectrum[], Shake> sweep = (name, cycles, table, shake) =>
            Dump(name, Waveforms.Render(table, cycles, 20000, hz, shake));

        sweep("MORPH SS", 96, new[] { Spectrum.FromShape(Waveforms.Sine, h), Spectrum.FromShape(Waveforms.Saw, h) }, null);
        sweep("PWM",      96, Mirror(Steps(5, t => Modulation.Pulse(0.5 - 0.4 * t, h))), null);
        sweep("FM 1-1",   96, Mirror(Steps(5, t => Modulation.Fm(1, 6.0 * t, h))), null);
        sweep("FM BELL",  96, Mirror(Steps(5, t => Modulation.Fm(7, 4.0 * t, h))), null);
        sweep("FM STACK", 128, Mirror(Steps(5, t => Modulation.Fm2(1, 4.0 * t, 3, 2.0 * t, h))), null);
        sweep("RING 2",   96, Mirror(Steps(4, t => Modulation.Ring(Waveforms.Saw, 24, 2 + 1 * t, h))), null);
        sweep("BENT",     96, Mirror(Steps(5, t => Modulation.Bent(Waveforms.Saw, 24, 2.0 * t, h))), null);
        sweep("PD SAW",   96, Mirror(Steps(5, t => Modulation.PhaseDistort(Waveforms.Saw, 16, 0.5 - 0.40 * t, h))), null);
        sweep("WHITE",    32, new[] { Noise.Spectrum(Noise.Colour.White, 101, h) }, null);
        sweep("PINK",     32, new[] { Noise.Spectrum(Noise.Colour.Pink,  202, h) }, null);
        sweep("BROWN",    32, new[] { Noise.Spectrum(Noise.Colour.Brown, 303, h) }, null);
        sweep("DISSOLVE", 96, Mirror(Steps(4, t => Noise.Dusted(Waveforms.Saw, Noise.Colour.Pink, 1.2 * t, 404, h))), null);

        sweep("DRIFT", 96, new[] { Spectrum.FromShape(Waveforms.Saw, h) },
              new Shake { PitchDepth = 0.06, Colour = Noise.Colour.Brown, Points = 18, Seed = 11 });
        sweep("GRIT", 96, new[] { Spectrum.FromShape(Waveforms.Square, h) },
              new Shake { AmDepth = 0.45, Colour = Noise.Colour.White, Points = 64, Seed = 22 });
        sweep("UNSTABLE", 128, Mirror(Steps(4, t => Modulation.Fm(2, 4.0 * t, h))),
              new Shake { PitchDepth = 0.12, AmDepth = 0.35, Colour = Noise.Colour.Pink, Points = 40, Seed = 33 });

        Dump("HISS", Waveforms.Hiss(2000, 12345));

        foreach (var drum in Drums.Kit())
            Dump(drum.Name, Drums.Render(drum));

        return 0;
    }
}
