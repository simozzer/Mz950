/*
 * Render a fixed list of waves and drums, for holding the C++ synthesis to the C#.
 *
 *     SynthDump <folder>
 *
 * The same list as AkaiS950Tests/SynthDump.cs, in the same order, with the same seeds.
 * Each render goes to <folder>/<NAME>.s16 as raw little-endian 16-bit words, and one line
 * per render goes to stdout: the name, the word count, an FNV-1a hash and the first eight
 * words. synthcheck.ps1 runs both, diffs the lines, and for any render that differs finds
 * how far apart the words are - one 12-bit step from a rounding difference is not the same
 * finding as a drum that came out of a different generator.
 */

#include "Synth.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

using namespace s950::synth;

namespace
{
    std::string folder;

    void dump (const std::string& name, const std::vector<short>& w)
    {
        std::uint32_t h = 2166136261u;
        for (short v : w)
        {
            h ^= static_cast<std::uint8_t> (v & 0xFF);        h *= 16777619u;
            h ^= static_cast<std::uint8_t> ((v >> 8) & 0xFF); h *= 16777619u;
        }

        std::printf ("%-10s %7d %08x", name.c_str(), static_cast<int> (w.size()), h);
        for (std::size_t i = 0; i < 8 && i < w.size(); ++i) std::printf (" %d", w[i]);
        std::printf ("\n");

        std::string file = name;
        for (auto& c : file) if (c == ' ') c = '_';

        std::ofstream out (folder + "/" + file + ".s16", std::ios::binary);
        out.write (reinterpret_cast<const char*> (w.data()), static_cast<std::streamsize> (w.size() * 2));
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: SynthDump <folder>\n"); return 2; }
    folder = argv[1];

    const double hz = RootHz;
    const int hPlain = waveforms::highestHarmonic (40000, hz);
    const int hSweep = waveforms::highestHarmonic (20000, hz);

    auto plain = [&] (const char* name, const Harmonic& shape) { dump (name, waveforms::render (shape, 40000, hz)); };

    plain ("SAW",      shapes::saw());
    plain ("SQUARE",   shapes::square());
    plain ("TRIANGLE", shapes::triangle());
    plain ("SINE",     shapes::sine());
    plain ("ORGAN",    shapes::organ());
    plain ("HOLLOW",   shapes::hollow());
    plain ("GLASS",    shapes::glass());
    plain ("BUZZ",     shapes::buzz());
    plain ("PULSE25",  shapes::pulse (0.25));
    plain ("FIFTHS",   shapes::lift (shapes::saw(), 0.55, { 3, 6 }));
    plain ("REED",     shapes::partials ({ 1.0, 0.5, 0.7, 0.25, 0.3, 0.2, 0.45 }));
    (void) hPlain;

    using waveforms::mirror;
    using waveforms::steps;
    const int h = hSweep;

    auto sweep = [&] (const char* name, int cycles, const std::vector<Spectrum>& table, const Shake* shake = nullptr)
    {
        dump (name, waveforms::render (table, cycles, 20000, hz, shake));
    };

    sweep ("MORPH SS", 96, { Spectrum::fromShape (shapes::sine(), h), Spectrum::fromShape (shapes::saw(), h) });
    sweep ("PWM",      96, mirror (steps (5, [&] (double t) { return modulation::pulse (0.5 - 0.4 * t, h); })));
    sweep ("FM 1-1",   96, mirror (steps (5, [&] (double t) { return modulation::fm (1, 6.0 * t, h); })));
    sweep ("FM BELL",  96, mirror (steps (5, [&] (double t) { return modulation::fm (7, 4.0 * t, h); })));
    sweep ("FM STACK", 128, mirror (steps (5, [&] (double t) { return modulation::fm2 (1, 4.0 * t, 3, 2.0 * t, h); })));
    sweep ("RING 2",   96, mirror (steps (4, [&] (double t) { return modulation::ring (shapes::saw(), 24, 2 + 1 * t, h); })));
    sweep ("BENT",     96, mirror (steps (5, [&] (double t) { return modulation::bent (shapes::saw(), 24, 2.0 * t, h); })));
    sweep ("PD SAW",   96, mirror (steps (5, [&] (double t) { return modulation::phaseDistort (shapes::saw(), 16, 0.5 - 0.40 * t, h); })));
    sweep ("WHITE",    32, { noise::spectrum (NoiseColour::white, 101, h) });
    sweep ("PINK",     32, { noise::spectrum (NoiseColour::pink,  202, h) });
    sweep ("BROWN",    32, { noise::spectrum (NoiseColour::brown, 303, h) });
    sweep ("DISSOLVE", 96, mirror (steps (4, [&] (double t) { return noise::dusted (shapes::saw(), NoiseColour::pink, 1.2 * t, 404, h); })));

    {
        Shake drift; drift.pitchDepth = 0.06; drift.colour = NoiseColour::brown; drift.points = 18; drift.seed = 11;
        sweep ("DRIFT", 96, { Spectrum::fromShape (shapes::saw(), h) }, &drift);

        Shake grit; grit.amDepth = 0.45; grit.colour = NoiseColour::white; grit.points = 64; grit.seed = 22;
        sweep ("GRIT", 96, { Spectrum::fromShape (shapes::square(), h) }, &grit);

        Shake unstable; unstable.pitchDepth = 0.12; unstable.amDepth = 0.35; unstable.colour = NoiseColour::pink; unstable.points = 40; unstable.seed = 33;
        sweep ("UNSTABLE", 128, mirror (steps (4, [&] (double t) { return modulation::fm (2, 4.0 * t, h); })), &unstable);
    }

    dump ("HISS", waveforms::hiss (2000, 12345));

    for (const auto& drum : drums::kit())
        dump (drum.name, drums::render (drum));

    return 0;
}
