#include "Synth.h"

#include <algorithm>
#include <cmath>

namespace s950::synth
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
    }

    // -------------------------------------------------------------- .NET's Random

    /*
     * System.Random as the .NET Framework implements it: Knuth's subtractive generator with
     * a 56-entry table. NextDouble() is InternalSample() / Int32.MaxValue. Written from the
     * reference source, and checked by the synth cross-check: a drum rendered here hashes
     * the same as the one the C# renders from the same seed.
     */
    NetRandom::NetRandom (int seed)
    {
        constexpr int MBIG  = 2147483647;
        constexpr int MSEED = 161803398;

        const int subtraction = seed == (-2147483647 - 1) ? 2147483647 : std::abs (seed);
        int mj = MSEED - subtraction;
        seedArray[55] = mj;
        int mk = 1;

        for (int i = 1; i < 55; ++i)
        {
            const int ii = (21 * i) % 55;
            seedArray[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += MBIG;
            mj = seedArray[ii];
        }

        for (int k = 1; k < 5; ++k)
            for (int i = 1; i < 56; ++i)
            {
                seedArray[i] -= seedArray[1 + (i + 30) % 55];
                if (seedArray[i] < 0) seedArray[i] += MBIG;
            }

        inext  = 0;
        inextp = 21;
    }

    int NetRandom::internalSample()
    {
        constexpr int MBIG = 2147483647;

        int locINext = inext, locINextp = inextp;
        if (++locINext  >= 56) locINext  = 1;
        if (++locINextp >= 56) locINextp = 1;

        int retVal = seedArray[locINext] - seedArray[locINextp];
        if (retVal == MBIG) --retVal;
        if (retVal < 0) retVal += MBIG;

        seedArray[locINext] = retVal;
        inext  = locINext;
        inextp = locINextp;
        return retVal;
    }

    double NetRandom::nextDouble()
    {
        return internalSample() * (1.0 / 2147483647.0);
    }

    double netRound (double v)
    {
        // nearbyint rounds half to even under the default rounding mode, as Math.Round does.
        return std::nearbyint (v);
    }

    // ------------------------------------------------------------------ the shapes

    namespace shapes
    {
        Harmonic saw()      { return [] (int n) { return 1.0 / n; }; }
        Harmonic square()   { return [] (int n) { return (n % 2 == 1) ? 1.0 / n : 0.0; }; }
        Harmonic triangle() { return [] (int n) { return (n % 2 == 1) ? (((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (n * (double) n) : 0.0; }; }
        Harmonic sine()     { return [] (int n) { return n == 1 ? 1.0 : 0.0; }; }
        Harmonic pulse (double duty) { return [duty] (int n) { return std::sin (Pi * n * duty) / n; }; }

        Harmonic organ()
        {
            return [] (int n)
            {
                switch (n)
                {
                    case 1:  return 1.00;
                    case 2:  return 0.60;
                    case 3:  return 0.35;
                    case 4:  return 0.45;
                    case 6:  return 0.20;
                    case 8:  return 0.25;
                    case 12: return 0.12;
                    case 16: return 0.10;
                    default: return 0.0;
                }
            };
        }

        Harmonic hollow() { return [] (int n) { return (n % 2 == 1) ? 1.0 / (n * std::sqrt ((double) n)) : 0.0; }; }
        Harmonic glass()  { return [] (int n) { return ((n & (n - 1)) == 0) ? 1.0 / std::sqrt ((double) n) : 0.0; }; }
        Harmonic buzz()   { return [] (int n) { return 1.0 / std::sqrt ((double) n); }; }

        Harmonic mix (Harmonic a, Harmonic b, double amount)
        {
            return [a, b, amount] (int n) { return a (n) * (1.0 - amount) + b (n) * amount; };
        }

        Harmonic partials (std::vector<double> levels)
        {
            return [levels] (int n) { return (n >= 1 && n <= (int) levels.size()) ? levels[(std::size_t) n - 1] : 0.0; };
        }

        Harmonic lift (Harmonic shape, double amount, std::vector<int> which)
        {
            return [shape, amount, which] (int n)
            {
                double v = shape (n);
                for (int w : which) if (w == n) { v += amount; break; }
                return v;
            };
        }

        Harmonic plus (Harmonic a, Harmonic b, double level)
        {
            return [a, b, level] (int n) { return a (n) + b (n) * level; };
        }
    }

    // ---------------------------------------------------------------- the spectrum

    Spectrum Spectrum::fromShape (const Harmonic& shape, int harmonics)
    {
        Spectrum s (harmonics);
        for (int n = 1; n <= harmonics; ++n) s.sin[(std::size_t) n] = shape (n);
        return s;
    }

    /*
     * Exact rather than approximate, because the signal is periodic and the window is a
     * whole number of periods: the correlation over one period IS the Fourier coefficient.
     * Oversampled sixteen points per harmonic, as the C# does.
     */
    Spectrum Spectrum::analyse (const std::function<double (double)>& cycle, int harmonics)
    {
        const int steps = std::max (2048, harmonics * 16);
        Spectrum s (harmonics);

        std::vector<double> sample ((std::size_t) steps);
        for (int i = 0; i < steps; ++i) sample[(std::size_t) i] = cycle (i / (double) steps);

        for (int n = 1; n <= harmonics; ++n)
        {
            double c = 0, sn = 0;
            const double w = 2.0 * Pi * n / steps;

            for (int i = 0; i < steps; ++i)
            {
                c  += sample[(std::size_t) i] * std::cos (w * i);
                sn += sample[(std::size_t) i] * std::sin (w * i);
            }

            s.cos[(std::size_t) n] = 2.0 * c / steps;
            s.sin[(std::size_t) n] = 2.0 * sn / steps;
        }

        return s;
    }

    Spectrum Spectrum::blend (const Spectrum& a, const Spectrum& b, double amount)
    {
        const int n = std::max (a.harmonics(), b.harmonics());
        Spectrum s (n);

        for (int i = 1; i <= n; ++i)
        {
            const double ac = i <= a.harmonics() ? a.cos[(std::size_t) i] : 0, as = i <= a.harmonics() ? a.sin[(std::size_t) i] : 0;
            const double bc = i <= b.harmonics() ? b.cos[(std::size_t) i] : 0, bs = i <= b.harmonics() ? b.sin[(std::size_t) i] : 0;
            s.cos[(std::size_t) i] = ac * (1 - amount) + bc * amount;
            s.sin[(std::size_t) i] = as * (1 - amount) + bs * amount;
        }

        return s;
    }

    Spectrum Spectrum::limitedTo (int highest) const
    {
        Spectrum s (std::min (highest, harmonics()));
        for (int n = 1; n <= s.harmonics(); ++n) { s.cos[(std::size_t) n] = cos[(std::size_t) n]; s.sin[(std::size_t) n] = sin[(std::size_t) n]; }
        return s;
    }

    namespace modulation
    {
        static double partial (double phase, double ratio) { return std::sin (2.0 * Pi * ratio * phase); }

        Spectrum ring (const Harmonic& carrier, int carrierPartials, double ratio, int harmonics)
        {
            return Spectrum::analyse ([&] (double phase)
            {
                double c = 0;
                for (int n = 1; n <= carrierPartials; ++n)
                {
                    const double amp = carrier (n);
                    if (amp != 0) c += amp * partial (phase, n);
                }
                return c * partial (phase, ratio);
            }, harmonics);
        }

        Spectrum fm (double ratio, double index, int harmonics)
        {
            return Spectrum::analyse ([=] (double phase)
            {
                return std::sin (2.0 * Pi * phase + index * partial (phase, ratio));
            }, harmonics);
        }

        Spectrum fm2 (double ratioA, double indexA, double ratioB, double indexB, int harmonics)
        {
            return Spectrum::analyse ([=] (double phase)
            {
                return std::sin (2.0 * Pi * phase + indexA * partial (phase, ratioA) + indexB * partial (phase, ratioB));
            }, harmonics);
        }

        Spectrum pulse (double duty, int harmonics)
        {
            return Spectrum::analyse ([=] (double phase) { return phase < duty ? 1.0 : -1.0; }, harmonics);
        }

        Spectrum bent (const Harmonic& shape, int shapePartials, double amount, int harmonics)
        {
            const double power = std::pow (2.0, amount);

            return Spectrum::analyse ([&, power] (double phase)
            {
                const double b = std::pow (phase, power);
                double v = 0;
                for (int n = 1; n <= shapePartials; ++n)
                {
                    const double amp = shape (n);
                    if (amp != 0) v += amp * partial (b, n);
                }
                return v;
            }, harmonics);
        }

        Spectrum phaseDistort (const Harmonic& shape, int shapePartials, double skew, int harmonics)
        {
            const double d = skew < 0.04 ? 0.04 : (skew > 0.96 ? 0.96 : skew);

            return Spectrum::analyse ([&, d] (double phase)
            {
                const double p = phase < d ? 0.5 * (phase / d) : 0.5 + 0.5 * ((phase - d) / (1.0 - d));
                double v = 0;
                for (int n = 1; n <= shapePartials; ++n)
                {
                    const double amp = shape (n);
                    if (amp != 0) v += amp * partial (p, n);
                }
                return v;
            }, harmonics);
        }
    }

    // --------------------------------------------------------------------- noise

    namespace noise
    {
        static double slope (NoiseColour c, int n)
        {
            switch (c)
            {
                case NoiseColour::pink:  return 1.0 / std::sqrt ((double) n);
                case NoiseColour::brown: return 1.0 / n;
                default:                 return 1.0;
            }
        }

        Spectrum spectrum (NoiseColour colour, int seed, int harmonics)
        {
            NetRandom rng (seed);
            Spectrum s (harmonics);

            for (int n = 1; n <= harmonics; ++n)
            {
                const double amp   = slope (colour, n);
                const double phase = rng.nextDouble() * 2.0 * Pi;
                s.cos[(std::size_t) n] = amp * std::cos (phase);
                s.sin[(std::size_t) n] = amp * std::sin (phase);
            }

            return s;
        }

        Spectrum dusted (const Harmonic& shape, NoiseColour colour, double amount, int seed, int harmonics)
        {
            const auto clean = Spectrum::fromShape (shape, harmonics);
            const auto dirt  = spectrum (colour, seed, harmonics);

            double reference = std::abs (clean.sin[1]) + std::abs (clean.cos[1]);
            if (reference <= 0) reference = 1;

            Spectrum s (harmonics);
            for (int n = 1; n <= harmonics; ++n)
            {
                s.cos[(std::size_t) n] = clean.cos[(std::size_t) n] + dirt.cos[(std::size_t) n] * amount * reference;
                s.sin[(std::size_t) n] = clean.sin[(std::size_t) n] + dirt.sin[(std::size_t) n] * amount * reference;
            }
            return s;
        }

        std::vector<double> wander (int seed, int points, NoiseColour colour, int length)
        {
            NetRandom rng (seed);
            std::vector<double> control ((std::size_t) points);

            double walk = 0;
            for (int i = 0; i < points; ++i)
            {
                const double r = rng.nextDouble() * 2.0 - 1.0;
                if (colour == NoiseColour::brown)     { walk = walk * 0.75 + r * 0.25; control[(std::size_t) i] = walk; }
                else if (colour == NoiseColour::pink) { walk = walk * 0.45 + r * 0.55; control[(std::size_t) i] = walk; }
                else control[(std::size_t) i] = r;
            }

            control[(std::size_t) points - 1] = control[0];        // come home, or the loop steps

            double peak = 0;
            for (double v : control) peak = std::max (peak, std::abs (v));
            if (peak > 0) for (auto& v : control) v /= peak;

            std::vector<double> out ((std::size_t) length);
            for (int i = 0; i < length; ++i)
            {
                const double pos = i / (double) length * (points - 1);
                const int at = (int) pos;
                const double frac = pos - at;
                const double a = control[(std::size_t) at];
                const double b = control[(std::size_t) std::min (at + 1, points - 1)];
                const double m = (1.0 - std::cos (frac * Pi)) * 0.5;
                out[(std::size_t) i] = a * (1.0 - m) + b * m;
            }
            return out;
        }
    }

    // --------------------------------------------------------------------- waves

    namespace waveforms
    {
        int wordsFor (int cycles, double rate, double hz) { return (int) netRound (cycles * rate / hz); }

        int highestHarmonic (double rate, double hz)
        {
            const int h = (int) ((rate / 2.0) / hz);
            return h < 1 ? 1 : h;
        }

        /// Harmonic n at position t through the table, with wraparound and smoothstep legs.
        static void at (const std::vector<Spectrum>& table, int n, double t, double& c, double& s)
        {
            if (table.size() == 1)
            {
                c = n <= table[0].harmonics() ? table[0].cos[(std::size_t) n] : 0;
                s = n <= table[0].harmonics() ? table[0].sin[(std::size_t) n] : 0;
                return;
            }

            const double pos = t * (double) table.size();
            const int    ix  = (int) pos;
            const double frac = pos - ix;

            const Spectrum& a = table[(std::size_t) (ix % (int) table.size())];
            const Spectrum& b = table[(std::size_t) ((ix + 1) % (int) table.size())];
            const double m = frac * frac * (3.0 - 2.0 * frac);

            const double ac = n <= a.harmonics() ? a.cos[(std::size_t) n] : 0, as = n <= a.harmonics() ? a.sin[(std::size_t) n] : 0;
            const double bc = n <= b.harmonics() ? b.cos[(std::size_t) n] : 0, bs = n <= b.harmonics() ? b.sin[(std::size_t) n] : 0;

            c = ac * (1 - m) + bc * m;
            s = as * (1 - m) + bs * m;
        }

        std::vector<short> render (const std::vector<Spectrum>& table, int cycles, double rate, double hz,
                                   const Shake* shake)
        {
            const int words   = wordsFor (cycles, rate, hz);
            const int highest = highestHarmonic (rate, hz);

            std::vector<double> drift, level;
            if (shake != nullptr && shake->pitchDepth > 0) drift = noise::wander (shake->seed, shake->points, shake->colour, words);
            if (shake != nullptr && shake->amDepth > 0)    level = noise::wander (shake->seed + 7919, shake->points, shake->colour, words);

            std::vector<double> wave ((std::size_t) words, 0.0);
            const double baseStep = 2.0 * Pi * cycles / words;

            for (int n = 1; n <= highest; ++n)
            {
                bool used = false;
                for (const auto& s : table)
                    if (n <= s.harmonics() && (s.cos[(std::size_t) n] != 0.0 || s.sin[(std::size_t) n] != 0.0)) { used = true; break; }
                if (! used) continue;

                for (int i = 0; i < words; ++i)
                {
                    double c, s;
                    at (table, n, i / (double) words, c, s);
                    if (c == 0.0 && s == 0.0) continue;

                    double phase = baseStep * i;
                    if (! drift.empty()) phase += shake->pitchDepth * drift[(std::size_t) i];

                    const double angle = n * phase;
                    wave[(std::size_t) i] += c * std::cos (angle) + s * std::sin (angle);
                }
            }

            if (! level.empty())
                for (int i = 0; i < words; ++i)
                {
                    const double g = 1.0 + shake->amDepth * level[(std::size_t) i];
                    wave[(std::size_t) i] *= g < 0 ? 0 : g;
                }

            return quantise (wave);
        }

        std::vector<short> render (const Harmonic& shape, double rate, double hz)
        {
            const std::vector<Spectrum> one { Spectrum::fromShape (shape, highestHarmonic (rate, hz)) };
            return render (one, PlainCycles, rate, hz);
        }

        std::vector<short> hiss (int words, int seed)
        {
            NetRandom rng (seed);
            std::vector<double> wave ((std::size_t) words);

            double last = 0;
            for (int i = 0; i < words; ++i)
            {
                const double white = rng.nextDouble() * 2.0 - 1.0;
                last = 0.65 * white + 0.35 * last;
                wave[(std::size_t) i] = last;
            }

            const int fade = std::min (64, words / 8);
            for (int i = 0; i < fade; ++i)
            {
                const double t = i / (double) fade;
                wave[(std::size_t) i] = wave[(std::size_t) i] * t + wave[(std::size_t) (words - fade + i)] * (1 - t);
            }

            return quantise (wave);
        }

        std::vector<short> quantise (const std::vector<double>& wave)
        {
            double peak = 0;
            for (double v : wave) peak = std::max (peak, std::abs (v));
            if (peak <= 0) peak = 1;

            const double scale = 2047.0 / peak;
            std::vector<short> out (wave.size());

            for (std::size_t i = 0; i < wave.size(); ++i)
            {
                const int v = (int) netRound (wave[i] * scale);
                out[i] = (short) (v > 2047 ? 2047 : (v < -2048 ? -2048 : v));
            }
            return out;
        }

        std::vector<Spectrum> mirror (std::vector<Spectrum> s)
        {
            if (s.size() < 3) return s;
            const int n = (int) s.size();
            for (int i = n - 2; i >= 1; --i) s.push_back (s[(std::size_t) i]);
            return s;
        }

        std::vector<Spectrum> steps (int count, const std::function<Spectrum (double)>& at)
        {
            std::vector<Spectrum> s;
            for (int i = 0; i < count; ++i) s.push_back (at (i / (double) (count - 1)));
            return s;
        }
    }

    // --------------------------------------------------------------------- drums

    namespace drums
    {
        constexpr double FadeIn = 0.0002;
        constexpr double Tail   = 0.25;

        static const std::vector<double> Metal808  { 1, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
        static const std::vector<double> MetalWide { 1, 1.3733, 1.8371, 2.2471, 2.9531, 3.4813, 4.2129 };

        using Wave = std::vector<double>;

        // ------------------------------------------------------------ the workshop

        static Wave sweep (int n, int rate, double startHz, double endHz, double pitchTau, double ampTau)
        {
            Wave wave ((std::size_t) n);
            double phase = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t  = i / (double) rate;
                const double hz = endHz + (startHz - endHz) * std::exp (-t / pitchTau);
                phase += 2 * Pi * hz / rate;
                wave[(std::size_t) i] = std::sin (phase) * std::exp (-t / ampTau);
            }
            return wave;
        }

        /// A square summed from its odd harmonics below Nyquist, each at a random phase.
        static void square (Wave& wave, int n, int rate, double hz, NetRandom& rng)
        {
            const double limit = rate * 0.46;
            for (int k = 1; k * hz < limit; k += 2)
            {
                const double amp   = 1.0 / k;
                const double step  = 2 * Pi * k * hz / rate;
                const double phase = Pi * (2 * rng.nextDouble() - 1);
                for (int i = 0; i < n; ++i) wave[(std::size_t) i] += amp * std::sin (step * i + phase);
            }
        }

        static Wave white (int n, NetRandom& rng)
        {
            Wave wave ((std::size_t) n);
            for (int i = 0; i < n; ++i) wave[(std::size_t) i] = 2 * rng.nextDouble() - 1;
            return wave;
        }

        static Wave highPass (const Wave& x, double hz, int rate)
        {
            const double a = std::exp (-2 * Pi * hz / rate);
            Wave out (x.size());
            double px = 0, py = 0;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                py = a * (py + x[i] - px);
                px = x[i];
                out[i] = py;
            }
            return out;
        }

        static Wave lowPass (const Wave& x, double hz, int rate)
        {
            const double a = 1 - std::exp (-2 * Pi * hz / rate);
            Wave out (x.size());
            double y = 0;
            for (std::size_t i = 0; i < x.size(); ++i) { y += a * (x[i] - y); out[i] = y; }
            return out;
        }

        /// The Chamberlin state variable, band output. Cutoff clamped below a sixth of the rate.
        static Wave bandPass (const Wave& x, double hz, double q, int rate)
        {
            const double f    = 2 * std::sin (Pi * std::min (hz, rate / 6.0) / rate);
            const double damp = std::min (2.0, 1 / std::max (0.25, q));
            Wave out (x.size());
            double low = 0, band = 0;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const double high = x[i] - low - damp * band;
                band += f * high;
                low  += f * band;
                out[i] = band;
            }
            return out;
        }

        static Wave norm (Wave x)
        {
            double peak = 0;
            for (double v : x) peak = std::max (peak, std::abs (v));
            if (peak > 0) for (auto& v : x) v /= peak;
            return x;
        }

        static void saturate (Wave& x, double drive)
        {
            const double k = std::tanh (drive);
            for (auto& v : x) v = std::tanh (drive * v) / k;
        }

        // -------------------------------------------------------------- the voices

        static Wave kick (int n, int rate, NetRandom& rng, double startHz, double endHz, double pitchTau,
                          double ampTau, double click, double clickTau, double drive)
        {
            Wave wave = sweep (n, rate, startHz, endHz, pitchTau, ampTau);
            saturate (wave, drive);

            if (click > 0)
            {
                const Wave hiss = norm (highPass (white (n, rng), 2500, rate));
                for (int i = 0; i < n; ++i)
                    wave[(std::size_t) i] += click * hiss[(std::size_t) i] * std::exp (-(i / (double) rate) / clickTau);
            }
            return wave;
        }

        static Wave tom (int n, int rate, NetRandom& rng, double startHz, double endHz, double pitchTau,
                         double ampTau, double noiseAmt)
        {
            Wave wave = sweep (n, rate, startHz, endHz, pitchTau, ampTau);
            const Wave skin = norm (lowPass (highPass (white (n, rng), 300, rate), 4000, rate));

            for (int i = 0; i < n; ++i)
            {
                const double t = i / (double) rate;
                wave[(std::size_t) i] = (1 - noiseAmt) * wave[(std::size_t) i]
                                      + noiseAmt * skin[(std::size_t) i] * std::exp (-t / (ampTau * 0.35));
            }
            return wave;
        }

        static Wave snare (int n, int rate, NetRandom& rng, double hz1, double hz2, double bend, double bodyTau,
                           double mix, double noiseHz, double noiseQ, double noiseTau, double crack)
        {
            Wave body ((std::size_t) n);
            double p1 = 0, p2 = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t  = i / (double) rate;
                const double up = 1 + bend * std::exp (-t / 0.020);
                p1 += 2 * Pi * hz1 * up / rate;
                p2 += 2 * Pi * hz2 * up / rate;
                body[(std::size_t) i] = (std::sin (p1) + 0.7 * std::sin (p2)) * std::exp (-t / bodyTau);
            }
            body = norm (body);

            const Wave w      = white (n, rng);
            const Wave rattle = norm (highPass (bandPass (w, noiseHz, noiseQ, rate), 400, rate));
            const Wave edge   = norm (highPass (w, 3500, rate));

            Wave wave ((std::size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / (double) rate;
                wave[(std::size_t) i] = (1 - mix) * body[(std::size_t) i]
                                      + mix * rattle[(std::size_t) i] * std::exp (-t / noiseTau)
                                      + crack * edge[(std::size_t) i] * std::exp (-t / 0.0025);
            }
            return wave;
        }

        static Wave clap (int n, int rate, NetRandom& rng, double tapTau, double tailFrom, double tailTau,
                          double tailLevel, double hz, double q)
        {
            const double taps[] = { 0, 0.010, 0.021 };
            const Wave hiss = norm (highPass (bandPass (white (n, rng), hz, q, rate), 500, rate));
            Wave wave ((std::size_t) n);

            for (int i = 0; i < n; ++i)
            {
                const double t = i / (double) rate;
                double gate = 0;
                for (double at : taps) if (t >= at) gate += std::exp (-(t - at) / tapTau);
                if (t >= tailFrom) gate += tailLevel * std::exp (-(t - tailFrom) / tailTau);
                wave[(std::size_t) i] = hiss[(std::size_t) i] * gate;
            }
            return wave;
        }

        static Wave metal (int n, int rate, NetRandom& rng, double bas, const std::vector<double>& ratios,
                           double hp, double tau, double snap, double snapTau,
                           double wash = 0, double washHp = 0, double washTau = 1,
                           double ping = 0, double pingHz = 0, double pingTau = 1, double swell = 0)
        {
            Wave bank ((std::size_t) n, 0.0);
            for (double r : ratios) square (bank, n, rate, bas * r, rng);
            bank = norm (highPass (highPass (bank, hp, rate), hp, rate));

            Wave air;
            if (wash > 0) air = norm (highPass (highPass (white (n, rng), washHp, rate), washHp, rate));

            Wave wave ((std::size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / (double) rate;
                const double opening = swell > 0 ? 1 - std::exp (-t / swell) : 1;
                const double env = std::exp (-t / tau) + snap * std::exp (-t / snapTau);

                wave[(std::size_t) i] = bank[(std::size_t) i] * env * opening;
                if (! air.empty()) wave[(std::size_t) i] += wash * air[(std::size_t) i] * std::exp (-t / washTau) * opening;
                if (ping > 0)
                    wave[(std::size_t) i] += ping * std::sin (2 * Pi * pingHz * i / rate) * std::exp (-t / pingTau);
            }
            return wave;
        }

        static Wave rim (int n, int rate, NetRandom& rng, double pingHz, double pingTau, double thumpHz,
                         double thumpTau, double click)
        {
            Wave wave ((std::size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / (double) rate;
                wave[(std::size_t) i] = std::sin (2 * Pi * pingHz * i / rate) * std::exp (-t / pingTau)
                                      + 0.8 * std::sin (2 * Pi * thumpHz * i / rate) * std::exp (-t / thumpTau);
            }

            const Wave hiss = norm (highPass (white (n, rng), 3000, rate));
            for (int i = 0; i < n; ++i)
                wave[(std::size_t) i] += click * hiss[(std::size_t) i] * std::exp (-(i / (double) rate) / 0.0012);
            return wave;
        }

        static Wave clave (int n, int rate, double hz, double tau)
        {
            Wave wave ((std::size_t) n);
            for (int i = 0; i < n; ++i)
                wave[(std::size_t) i] = std::sin (2 * Pi * hz * i / rate) * std::exp (-(i / (double) rate) / tau);
            return wave;
        }

        // ------------------------------------------------------------------ the kit

        std::vector<Drum> kit()
        {
            using R = NetRandom&;
            return {
                { "SNARE 3", 33, 24000, 0.20, -4, 40, [] (int n, int r, R g) { return snare (n, r, g, 262, 391, 0.50, 0.055, 0.60, 2700, 1.0, 0.065, 0.68); } },
                { "KICK 3",  34, 20000, 0.25, -1, 25, [] (int n, int r, R g) { return kick (n, r, g, 240, 62, 0.007, 0.11, 0.28, 0.0015, 3.6); } },
                { "KICK 2",  35, 20000, 0.90,  0, 20, [] (int n, int r, R g) { return kick (n, r, g, 118, 42, 0.034, 0.60, 0.08, 0.0020, 2.0); } },
                { "KICK 1",  36, 20000, 0.45,  0, 25, [] (int n, int r, R g) { return kick (n, r, g, 190, 49, 0.014, 0.24, 0.16, 0.0018, 2.8); } },
                { "RIMSHOT", 37, 24000, 0.09, -6, 45, [] (int n, int r, R g) { return rim (n, r, g, 1700, 0.012, 420, 0.020, 0.30); } },
                { "SNARE 1", 38, 24000, 0.32, -2, 40, [] (int n, int r, R g) { return snare (n, r, g, 188, 278, 0.35, 0.105, 0.48, 1900, 0.9, 0.120, 0.58); } },
                { "CLAP",    39, 24000, 0.42, -4, 45, [] (int n, int r, R g) { return clap (n, r, g, 0.0035, 0.031, 0.110, 0.62, 1100, 1.1); } },
                { "SNARE 2", 40, 24000, 0.42, -2, 40, [] (int n, int r, R g) { return snare (n, r, g, 152, 229, 0.30, 0.190, 0.42, 1400, 0.8, 0.170, 0.46); } },
                { "TOM LO",  41, 20000, 0.60, -3, 35, [] (int n, int r, R g) { return tom (n, r, g, 140, 88, 0.070, 0.34, 0.09); } },
                { "HAT CLOSED", 42, 32000, 0.08, -9, 55, [] (int n, int r, R g) { return metal (n, r, g, 205.3, Metal808, 5600, 0.026, 1.0, 0.0025); } },
                { "TOM MID", 43, 20000, 0.50, -3, 35, [] (int n, int r, R g) { return tom (n, r, g, 198, 128, 0.060, 0.28, 0.09); } },
                { "HAT PEDAL", 44, 32000, 0.14, -10, 55, [] (int n, int r, R g) { return metal (n, r, g, 205.3, Metal808, 5000, 0.050, 0.9, 0.0030); } },
                { "TOM HI",  45, 20000, 0.42, -3, 35, [] (int n, int r, R g) { return tom (n, r, g, 278, 182, 0.050, 0.22, 0.09); } },
                { "HAT OPEN", 46, 32000, 0.75, -10, 50, [] (int n, int r, R g) { return metal (n, r, g, 205.3, Metal808, 5400, 0.340, 1.0, 0.0080); } },
                { "CRASH",   49, 32000, 2.60, -8, 50, [] (int n, int r, R g) { return metal (n, r, g, 148.0, MetalWide, 3800, 1.10, 0.7, 0.0120, 0.55, 5000, 1.40, 0, 0, 1, 0.006); } },
                { "RIDE",    51, 32000, 1.80, -10, 50, [] (int n, int r, R g) { return metal (n, r, g, 166.0, MetalWide, 3000, 0.900, 1.3, 0.0050, 0.16, 6000, 0.500, 0.45, 3520, 0.060); } },
                { "RIDE BELL", 53, 32000, 1.40, -11, 45, [] (int n, int r, R g) { return metal (n, r, g, 262.0, { 1, 1.4771, 2.1834, 3.0129 }, 2400, 0.620, 0.9, 0.0040, 0, 0, 1, 0.60, 2620, 0.220); } },
                { "CLAVE",   75, 24000, 0.13, -11, 40, [] (int n, int r, R)   { return clave (n, r, 2500, 0.030); } },
            };
        }

        int seed (const std::string& name)
        {
            unsigned int h = 2166136261u;
            for (char c : name) { h ^= (unsigned char) c; h *= 16777619u; }
            return (int) (h & 0x7FFFFFFF);
        }

        std::vector<short> render (const Drum& drum)
        {
            const int n = (int) netRound (drum.seconds * drum.rate) & ~1;
            NetRandom rng (seed (drum.name));
            Wave wave = drum.voice (n, drum.rate, rng);

            const int rise = std::max (1, (int) netRound (FadeIn * drum.rate));
            const int fall = std::max (2, (int) netRound (Tail * n));

            for (int i = 0; i < rise && i < n; ++i) wave[(std::size_t) i] *= i / (double) rise;
            for (int i = 0; i < fall; ++i)
                wave[(std::size_t) (n - fall + i)] *= 0.5 * (1 + std::cos (Pi * i / (fall - 1.0)));

            return waveforms::quantise (wave);
        }
    }
}
