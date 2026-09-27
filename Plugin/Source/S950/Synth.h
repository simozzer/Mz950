#pragma once

#include <functional>
#include <string>
#include <vector>

/*
 * Sounds drawn rather than recorded: AkaiS950Synth, the C# workshop tool, ported.
 *
 * Nothing in here plays. It makes 12-bit WORDS - the audio a sample file holds - and hands
 * them to Disk::addSample, so a sound designed on the plugin's Synth page is a sample on a
 * disk and goes through the S950's voice like any other, and can go to the real machine.
 *
 * Two kinds of thing come out:
 *
 *   - WAVES: a few cycles of a periodic waveform, looped. Built from harmonics, so they are
 *     band-limited by construction; and where the harmonics change across the sample the
 *     loop is a wavetable, which is how a sampler does pulse-width modulation, FM sweeps and
 *     the rest without a modulator.
 *   - DRUMS: one-shots with the whole envelope baked in, from the recipes the analogue
 *     machines used.
 *
 * Held to the C# number for number, which is why System.Random is ported rather than
 * replaced: a drum seeded from its name comes out the same words here as there.
 */
namespace s950::synth
{
    // -------------------------------------------------------------- .NET's Random

    /// The .NET Framework's System.Random, exactly: Knuth's subtractive generator. Seeds
    /// mean the same here as in the C# tool, so its disks and ours are the same disks.
    class NetRandom
    {
    public:
        explicit NetRandom (int seed);
        double nextDouble();

    private:
        int internalSample();
        int seedArray[56] = {};
        int inext = 0, inextp = 21;
    };

    /// Round half to even, as .NET's Math.Round does.
    double netRound (double v);

    // ------------------------------------------------------------------ the shapes

    /// The amplitude of harmonic n. Zero means absent.
    using Harmonic = std::function<double (int)>;

    namespace shapes
    {
        Harmonic saw();
        Harmonic square();
        Harmonic triangle();
        Harmonic sine();
        Harmonic pulse (double duty);
        Harmonic organ();
        Harmonic hollow();
        Harmonic glass();
        Harmonic buzz();

        Harmonic mix (Harmonic a, Harmonic b, double amount);
        Harmonic partials (std::vector<double> levels);
        Harmonic lift (Harmonic shape, double amount, std::vector<int> which);
        Harmonic plus (Harmonic a, Harmonic b, double level);
    }

    // ---------------------------------------------------------------- the spectrum

    /// One waveform, as what is in it: cosine and sine coefficients per harmonic. Index 0
    /// is unused; harmonic n is at n.
    struct Spectrum
    {
        std::vector<double> cos, sin;

        explicit Spectrum (int harmonics = 0) : cos (static_cast<std::size_t> (harmonics + 1), 0.0),
                                                sin (static_cast<std::size_t> (harmonics + 1), 0.0) {}

        int harmonics() const { return static_cast<int> (cos.size()) - 1; }

        static Spectrum fromShape (const Harmonic& shape, int harmonics);

        /// Measure one cycle of a waveform: `cycle` takes a phase 0..1 and returns the sample.
        static Spectrum analyse (const std::function<double (double)>& cycle, int harmonics);

        static Spectrum blend (const Spectrum& a, const Spectrum& b, double amount);
        Spectrum limitedTo (int highest) const;
    };

    namespace modulation
    {
        Spectrum ring (const Harmonic& carrier, int carrierPartials, double ratio, int harmonics);
        Spectrum fm (double ratio, double index, int harmonics);
        Spectrum fm2 (double ratioA, double indexA, double ratioB, double indexB, int harmonics);
        Spectrum pulse (double duty, int harmonics);
        Spectrum bent (const Harmonic& shape, int shapePartials, double amount, int harmonics);
        Spectrum phaseDistort (const Harmonic& shape, int shapePartials, double skew, int harmonics);
    }

    // --------------------------------------------------------------------- noise

    enum class NoiseColour { white, pink, brown };

    namespace noise
    {
        Spectrum spectrum (NoiseColour colour, int seed, int harmonics);
        Spectrum dusted (const Harmonic& shape, NoiseColour colour, double amount, int seed, int harmonics);
        std::vector<double> wander (int seed, int points, NoiseColour colour, int length);
    }

    /// How a wave is shaken as it is rendered: level, pitch, or both, by a wandering line
    /// that returns to its start so the loop still joins.
    struct Shake
    {
        double amDepth    = 0.0;
        double pitchDepth = 0.0;
        int    points     = 24;
        NoiseColour colour = NoiseColour::pink;
        int    seed       = 1;

        bool any() const { return amDepth > 0 || pitchDepth > 0; }
    };

    // --------------------------------------------------------------------- waves

    constexpr double RootHz   = 261.6255653;   // middle C, where every wave is written
    constexpr int    RootNote = 60;

    namespace waveforms
    {
        constexpr int PlainCycles = 8;

        int wordsFor (int cycles, double rate, double hz);
        int highestHarmonic (double rate, double hz);

        /// A wavetable swept through across the sample, as a round trip, so the loop joins.
        std::vector<short> render (const std::vector<Spectrum>& table, int cycles, double rate, double hz,
                                   const Shake* shake = nullptr);

        /// A plain wave: one shape, eight cycles, no movement.
        std::vector<short> render (const Harmonic& shape, double rate, double hz);

        std::vector<short> hiss (int words, int seed);

        /// Scale to fill the 12 bits and round.
        std::vector<short> quantise (const std::vector<double>& wave);

        /// A table and its reverse, so a sweep returns to where it started.
        std::vector<Spectrum> mirror (std::vector<Spectrum> s);

        /// `count` spectra at positions 0..1 through a sweep.
        std::vector<Spectrum> steps (int count, const std::function<Spectrum (double)>& at);
    }

    // --------------------------------------------------------------------- drums

    namespace drums
    {
        struct Drum
        {
            std::string name;
            int    key;          // the note it answers to, C3 = 60
            int    rate;         // its own sample rate
            double seconds;
            int    loudness;     // the keygroup trim that balances the kit
            int    vel;          // velocity -> loudness

            /// words, rate, generator -> the wave, before quantising
            std::function<std::vector<double> (int, int, NetRandom&)> voice;
        };

        /// The kit, laid out close to General MIDI.
        std::vector<Drum> kit();

        /// One drum, as the 12-bit words the disk carries. Seeded from its name.
        std::vector<short> render (const Drum& drum);

        int seed (const std::string& name);
    }
}
