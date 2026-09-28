#pragma once

#include <cmath>
#include <algorithm>

namespace s950
{
    /*
     * The S950's low-pass: 6th-order Butterworth, 36 dB per octave - and, turned up, a
     * resonance the machine never had, up to and past self-oscillation.
     *
     * Three second-order sections in cascade, at the three Q values that make six poles
     * Butterworth rather than three identical resonant sections. Those are exact rather than
     * measured: only where the cutoff sits in hertz had to be found by recording (see Cal).
     *
     * Two of the sections, Q 0.52 and 0.71, are RBJ biquads, a straight port of
     * AkaiS950Engine/Filter.cs. The third, Q 1.93 - the one that already peaks - is a
     * state-variable filter instead (Zavalishin's topology-preserving form), because that is
     * the one resonance works on, and a biquad can only ring, never sustain. The SVF's
     * response is the same bilinear, prewarped second-order low-pass as the biquad it
     * replaced, so at zero resonance the whole filter is the S950's to within rounding.
     *
     * It runs FIRST in the cascade. For a linear filter the order is immaterial; with the
     * resonant stage's level control at work, whatever little it adds above the cutoff gets
     * the two sections after it, 24 dB an octave more, before it leaves.
     *
     * The coefficients are recomputed every few dozen samples, far finer than the ear
     * resolves, and the state is kept across a retune so a moving cutoff does not click.
     */
    class Butterworth
    {
    public:
        /// Forget the past - called when a voice starts, so no note begins mid-tail.
        void reset()
        {
            for (int s = 0; s < Biquads; ++s)
                x1[s] = x2[s] = y1[s] = y2[s] = 0.0;

            ic1 = ic2 = band = power = 0.0;
        }

        /*
         * Retune to a cutoff in hertz. State is kept, so there is no click.
         *
         * `resonance`, 0..1, is not the S950's - its filter has none - and 0 is exactly the
         * machine. Up to Threshold it raises the resonant stage's Q from its Butterworth 1.93
         * to QTop, exponentially so the knob's travel is even to the ear. Past it the damping
         * keeps falling, through zero to KMin, and the filter oscillates on its own: a sine at
         * the cutoff, so it follows the Filter knob, the envelope and velocity.
         *
         * What holds that oscillation at a level rather than letting it grow without end is
         * damping that rises with the stage's own band-pass power - see process(). The same
         * term tames loud peaks below the threshold, so a hot sample through a sharp
         * resonance is squeezed back rather than left to clip.
         */
        void setCutoff (double fc, double fs, double resonance = 0.0)
        {
            const double nyq = fs * 0.5;
            const double f   = fc < 10 ? 10 : (fc > nyq * 0.995 ? nyq * 0.995 : fc);

            const double pi = 3.14159265358979323846;
            const double w  = 2.0 * pi * f / fs;
            const double cw = std::cos (w);
            const double sw = std::sin (w);

            // --- the two plain sections
            for (int s = 0; s < Biquads; ++s)
            {
                const double al = sw / (2.0 * q (s));
                const double a0 = 1.0 + al;

                b0[s] = (1.0 - cw) / 2.0 / a0;
                b1[s] = (1.0 - cw) / a0;
                b2[s] = b0[s];
                a1[s] = -2.0 * cw / a0;
                a2[s] = (1.0 - al) / a0;
            }

            // --- the resonant one
            const double res = resonance < 0 ? 0 : (resonance > 1 ? 1 : resonance);
            const double q2  = q (Biquads);

            g = std::tan (pi * f / fs);

            const double qNow = q2 * std::pow (QTop / q2, std::min (res, Threshold) / Threshold);

            if (res <= Threshold)
                k0 = 1.0 / qNow;
            else
                k0 = 1.0 / QTop + (KMin - 1.0 / QTop) * (res - Threshold) / (1.0 - Threshold);

            // The level control grows with the knob, from nothing at 0 - so 0 stays linear,
            // and stays the S950 - to the amount that holds a full-resonance oscillation at
            // OscLevel. See process() for where that figure comes from.
            beta = Beta * res;

            /*
             * The input eased down as the resonance rises, to about -6 dB at the threshold.
             *
             * A peak of nearly +19 dB on a full-scale sample is past anything the engine's
             * output can carry, and analogue resonant filters lose passband the same way.
             * (q2 / Q)^0.25 is exactly 1 at zero resonance, so the S950 is untouched.
             */
            drive = std::pow (q2 / qNow, 0.25);

            // The level follower's speed: two cycles of the cutoff, but never under FollowMin
            // - two cycles at 16 kHz is six samples, fast enough to bend the sine again.
            const double seconds = std::max (FollowCycles / f, FollowMin);
            follow = 1.0 - std::exp (-1.0 / (seconds * fs));
        }

        /// One sample through all three sections.
        double process (double x)
        {
            /*
             * The resonant stage: a TPT state-variable low-pass whose damping k rises with
             * the smoothed power of its own band-pass output, k = k0 + beta * power.
             *
             * An automatic gain control. With k0 below zero, small signals grow and large
             * ones are damped, so an oscillation settles where the two balance - where the
             * power is -k0 / beta, a band-pass amplitude of sqrt (-2 k0 / beta). At the
             * cutoff this stage's low-pass output is as large as its band-pass.
             *
             * Smoothed rather than instantaneous on purpose. Damping driven by band^2 sample
             * by sample (Rayleigh's oscillator) holds the level just as well, but it swings
             * twice a cycle and so bends the sine: at a 16 kHz cutoff its third harmonic
             * folded back to a few hundred hertz at -16 dB. A follower two cycles long moves
             * the damping far too slowly to bend a cycle, and the oscillation is a sine.
             */
            power += follow * (band * band - power);

            const double k  = std::min (k0 + beta * power, KMax);
            const double d1 = 1.0 / (1.0 + g * (g + k));
            const double d2 = g * d1;
            const double d3 = g * d2;

            const double v3 = x * drive - ic2;
            const double v1 = d1 * ic1 + d2 * v3;
            const double v2 = ic2 + d2 * ic1 + d3 * v3;

            ic1  = 2.0 * v1 - ic1;
            ic2  = 2.0 * v2 - ic2;
            band = v1;

            // A voice left sounding in silence decays into denormals, which are slow.
            if (std::fabs (ic1) < 1e-20) ic1 = 0.0;
            if (std::fabs (ic2) < 1e-20) ic2 = 0.0;

            x = v2;

            // --- then the two plain sections
            for (int s = 0; s < Biquads; ++s)
            {
                const double y = b0[s] * x + b1[s] * x1[s] + b2[s] * x2[s]
                                           - a1[s] * y1[s] - a2[s] * y2[s];
                x2[s] = x1[s]; x1[s] = x;
                y2[s] = y1[s]; y1[s] = y;
                x = y;
            }
            return x;
        }

        /// Where on the 0..1 knob the filter starts to sing by itself - k0 crossing zero.
        static double oscillationStarts()
        {
            return Threshold + (1.0 - Threshold) * (1.0 / QTop) / (1.0 / QTop - KMin);
        }

    private:
        static constexpr int Biquads = 2;

        /// How far up the knob is resonance, and the Q it has reached there. Past it is
        /// self-oscillation.
        static constexpr double Threshold = 0.9;
        static constexpr double QTop      = 40.0;

        /// The damping at full resonance: how hard the oscillation is driven.
        static constexpr double KMin = -0.1;

        /// A ceiling on the saturating damping, so a huge transient cannot ask for more
        /// than the stage can be solved for in one step.
        static constexpr double KMax = 4.0;

        /*
         * How loud a full-resonance oscillation is at the filter's output, before the VCA:
         * -14 dB, so eight voices of it at the default gain stay short of clipping.
         *
         * The two plain sections pass q(0) * q(1) = 0.366 at the cutoff, so the resonant
         * stage has to oscillate at OscLevel / 0.366, and Beta follows from the balance in
         * process() with k0 at KMin.
         */
        static constexpr double OscLevel  = 0.2;
        static constexpr double PlainGain = 0.51763809020504 * 0.70710678118655;
        static constexpr double StageAmp  = OscLevel / PlainGain;
        static constexpr double Beta      = 2.0 * -KMin / (StageAmp * StageAmp);

        /// How many cycles of the cutoff the level follower averages over.
        static constexpr double FollowCycles = 2.0;
        static constexpr double FollowMin    = 0.01;

        /*
         * The Q of each section, which is what makes the cascade Butterworth: 0.52, 0.71 and
         * 1.93 for k = 0, 1, 2.
         *
         * Worked out on the spot rather than held in a static table: three cosines, only
         * when a cutoff changes, is nothing beside the filtering itself - and a function
         * needs no thought about when a static gets initialised.
         */
        static double q (int k)
        {
            return 1.0 / (2.0 * std::cos (3.14159265358979323846 * (2 * k + 1) / 12.0));
        }

        double b0[Biquads] {}, b1[Biquads] {}, b2[Biquads] {};
        double a1[Biquads] {}, a2[Biquads] {};
        double x1[Biquads] {}, x2[Biquads] {};
        double y1[Biquads] {}, y2[Biquads] {};

        // the resonant stage: its coefficient, damping, level control, input trim, and state
        double g = 0.0, k0 = 1.0, beta = 0.0, drive = 1.0, follow = 0.0;
        double ic1 = 0.0, ic2 = 0.0, band = 0.0, power = 0.0;
    };
}
