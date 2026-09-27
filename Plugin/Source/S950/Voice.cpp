#include "Voice.h"

namespace s950
{
    void Voice::start (const KeygroupPatch& group, int n, int vel,
                       double rate, double wheel, long long sequence, double fade,
                       double glideSemis, double glideSeconds)
    {
        crossfade     = fade;
        handedOver    = false;
        wideSign      = 0;              // a plain voice until makeWide says otherwise
        partner       = -1;
        wideRatio     = 1.0;
        wideGainL     = 1.0;
        wideGainR     = 1.0;
        kg            = &group;
        sound         = group.sound;
        note          = n;
        velocity      = vel < 0 ? 0 : (vel > 127 ? 127 : vel);
        keygroupIndex = group.keygroupIndex;
        startedAt     = sequence;
        sampleRate    = rate;

        // --- pitch. Constant pitch means the keygroup ignores which key was struck.
        double semis = group.zoneTranspose;
        if (! group.constantPitch)
            semis += note - sound->rootPitch;

        const double ratio = std::pow (2.0, semis / 12.0);
        step      = sound->sourceRate / sampleRate * ratio;
        leaveRate = sound->sourceRate * ratio;
        pos       = 0.0;
        dir       = 1;               // forwards until an alternating loop turns it round

        // --- glide. The target pitch above is untouched; render() multiplies the offset on
        // top, as it does the wheel, so arriving is simply the offset reaching zero.
        const bool glides = glideSeconds > 0.0005 && glideSemis != 0.0;
        glideOffset    = glides ? glideSemis : 0.0;
        glidePerSecond = glides ? std::abs (glideSemis) / glideSeconds : 0.0;

        // --- amplitude envelope. Only the peak is fixed at the strike: it is what the
        // velocity and the zone trim decided, and no control moves it afterwards.
        /*
         * The velocity-to-loudness trim lands here, at the strike, and nowhere else.
         *
         * It decides how much softer a soft note is, which is a question about the strike -
         * so like the peak it sets, it is answered once and not revisited. Turning the knob
         * changes how the NEXT note answers the keyboard, not how loud a note already
         * sounding turns out to have been.
         */
        const double depth  = clamp01 (trimmed (group.velToLoudness, trims.velToLoudness) / 99.0);
        const double velDb  = -(127.0 - velocity) * cal::VelDbPerStep * depth;
        const double zoneDb = group.zoneLoudness * cal::LoudnessDbPerUnit;

        peak = std::min (cal::dbToGain (velDb + zoneDb), 4.0) * crossfade;

        // the times and the sustains come from the keygroup and the trims together
        applyTrims();

        stage = attack > 0.0005 ? Stage::attack : Stage::decay;
        t     = 0.0;
        gain  = attack > 0.0005 ? 0.0 : peak;

        /*
         * --- filter. The ceiling is the reconstruction limit, which moves with the rate the
         * audio leaves at - but we are running at the device rate, so it cannot exceed what
         * that can represent either.
         */
        ceiling    = std::min (cal::MaxRatio * leaveRate, sampleRate * 0.45);
        floorHz    = std::min (cal::FloorHz, ceiling);

        // cutoffShift is worked out in applyTrims, which has already run, so that the
        // velocity-to-filter trim can move it while the note sounds.

        vcfT          = 0.0;
        vcfReleasing  = false;
        vcfReleaseT   = 0.0;

        filter.reset();
        filter.setCutoff (cutoffNow(), sampleRate);

        // --- the LFO. Phase and fade start here; rate, depth and delay come from applyLfo,
        // which the trims can move while the note sounds.
        wheelCents = wheel;
        ownLfo     = group.lfoDesync;
        lfoPhase   = 0.0;
        fadeT      = 0.0;
        sinceOn    = 0.0;

        // Which output the keygroup goes to. Settled at the strike, like the peak: nothing
        // moves a sounding note from one socket to another on the machine either.
        cal::outputGains (group.outputPort, outL, outR);
        applyLfo();
    }

    void Voice::adopt (const KeygroupPatch& group)
    {
        if (stage == Stage::idle)     return;
        if (group.sound != sound)     return;   // a different sample is a different note

        kg = &group;

        // --- pitch. Changing transpose moves the playback rate under the position we
        // already hold, which is what transposing a sounding note means.
        double semis = group.zoneTranspose;
        if (! group.constantPitch)
            semis += note - sound->rootPitch;

        const double ratio = std::pow (2.0, semis / 12.0);
        step      = sound->sourceRate / sampleRate * ratio;
        leaveRate = sound->sourceRate * ratio;

        // --- amplitude. The targets move; the gain walks to them from where it is.
        const double depth  = clamp01 (group.velToLoudness / 99.0);
        const double velDb  = -(127.0 - velocity) * cal::VelDbPerStep * depth;
        const double zoneDb = group.zoneLoudness * cal::LoudnessDbPerUnit;

        peak = std::min (cal::dbToGain (velDb + zoneDb), 4.0) * crossfade;
        applyTrims();

        // --- filter
        ceiling    = std::min (cal::MaxRatio * leaveRate, sampleRate * 0.45);
        floorHz    = std::min (cal::FloorHz, ceiling);

        // cutoffShift is worked out in applyTrims, which has already run, so that the
        // velocity-to-filter trim can move it while the note sounds.

        // deliberately no filter.reset() - see the note on adopt()

        // --- the LFO keeps its phase and its place in the fade-in
        ownLfo = group.lfoDesync;
        applyLfo();
    }

    void Voice::legatoTo (int n, double glideSemis, double glideSeconds)
    {
        if (stage == Stage::idle || kg == nullptr)
            return;

        note = n;

        // --- pitch, as start() works it out, under the position we already hold
        double semis = kg->zoneTranspose;
        if (! kg->constantPitch)
            semis += note - sound->rootPitch;

        const double ratio = std::pow (2.0, semis / 12.0);
        step      = sound->sourceRate / sampleRate * ratio;
        leaveRate = sound->sourceRate * ratio;
        ceiling   = std::min (cal::MaxRatio * leaveRate, sampleRate * 0.45);
        floorHz   = std::min (cal::FloorHz, ceiling);

        // --- glide, as start() does it. Key tracking follows by itself: applyTrims reads
        // `note` at the top of every control block.
        const bool glides = glideSeconds > 0.0005 && glideSemis != 0.0;
        glideOffset    = glides ? glideSemis : 0.0;
        glidePerSecond = glides ? std::abs (glideSemis) / glideSeconds : 0.0;
    }

    /*
     * The envelope times and levels, from the keygroup and the player's trims together.
     *
     * Everything here is cheap and everything here can move while a note sounds, so it is
     * redone at the top of every control block rather than kept from the strike. Changing
     * a time under a running envelope is well defined: advanceStage compares where the note
     * has got to against the time as it is NOW, so shortening a decay past the point already
     * reached simply moves the note on to its sustain.
     *
     * The peak is not here. That is what the velocity and the zone loudness decided when the
     * key went down, and no control reaches back to change how hard a note was struck.
     */
    void Voice::applyTrims()
    {
        /*
         * The trim first, then velocity on top of it. The trim is the player's offset from
         * what the disk says, so it is part of "the attack this voice is set to" - and byte 9
         * shortens whatever that turns out to be, exactly as it shortens the disk's own value
         * on the hardware. Turning the attack knob therefore moves the velocity response with
         * it rather than fighting it.
         */
        attack      = cal::vcaAttackSeconds (
                          cal::velocityAttackByte (trimmed (kg->vcaAttack, trims.vcaAttack),
                                                   kg->velToAttack, velocity));
        releaseTime = cal::envSeconds (
                          cal::velocityReleaseByte (trimmed (kg->vcaRelease, trims.vcaRelease),
                                                    kg->velToRelease, velocity,
                                                    kg->velocityReleaseOn));

        if (handedOver)
            releaseTime = std::min (releaseTime, HandOverRelease);

        // A stored sustain of ZERO is silence, and is not the bottom of the line the other
        // settings sit on - see cal::VcaSilenceDb. It used to stop 39.6 dB down and hang
        // there, on 723 of the library's keygroups. The decay is a RATE, so how long it
        // takes depends on how far it has to go - see cal::vcaDecaySeconds.
        const double sustainDb = cal::sustainDbFor (trimmed (kg->vcaSustain, trims.vcaSustain));
        sustain = peak * cal::dbToGain (sustainDb);

        decay = cal::vcaDecaySeconds (trimmed (kg->vcaDecay, trims.vcaDecay), sustainDb);

        /*
         * An S900 programme left its four VCF bytes blank: it has no filter envelope at all,
         * rather than one set to whatever a space happens to be as a number.
         *
         * Those keygroups still get the player's controls, they simply start from a flat
         * envelope - no attack, no decay, full sustain - which moves nothing until something
         * is dialled in. Refusing to shape them, which is what this did at first, left a
         * whole class of programmes with four controls that did nothing and no way to tell
         * why. Untrimmed the result is identical either way: an amount of zero is no
         * movement, whatever shape it is applied to.
         */
        const double baseAttack  = kg->vcfWritten ? kg->vcfAttack  : 0;
        const double baseDecay   = kg->vcfWritten ? kg->vcfDecay   : 0;
        const double baseSustain = kg->vcfWritten ? kg->vcfSustain : 99;
        const double baseRelease = kg->vcfWritten ? kg->vcfRelease : 0;

        vcfAttack  = cal::envSeconds (cal::clamp (baseAttack + trims.vcfAttack, 0.0, 99.0))
                   * cal::VcfTimeScale;
        vcfSustain = cal::clamp (baseSustain + trims.vcfSustain, 0.0, 99.0) / 99.0;
        vcfDecay   = cal::vcfDecaySeconds (cal::clamp (baseDecay + trims.vcfDecay, 0.0, 99.0),
                                          vcfSustain);
        vcfRelease = cal::envSeconds (cal::clamp (baseRelease + trims.vcfRelease, 0.0, 99.0))
                   * cal::VcfTimeScale;

        /*
         * How far velocity and the key have moved the cutoff, in octaves.
         *
         * Here rather than at the strike because the velocity-to-filter trim has to reach a
         * note already sounding - a control that only took effect on the next key is not a
         * control anyone can play with. The note and the velocity themselves cannot change,
         * so what this recomputes each block is only how much of them counts.
         *
         * Key tracking is measured to pivot at note 62, not 60, and velocity at 65 - so a
         * note played at exactly 65 is unmoved however much sensitivity is dialled in, and
         * softer strikes go down where harder ones go up.
         */
        const double track    = cal::clamp (kg->keyToFilter, 0, 99) / cal::KeyFull;
        const double keyShift = (note - cal::KeyPivot) / 12.0 * track;
        const double velShift = ((velocity - cal::VelPivot) / 127.0)
                                * (trimmed (kg->velToFilter, trims.velToFilter) / 99.0)
                                * cal::VelOctaves;
        cutoffShift = keyShift + velShift;

        applyLfo();
    }

    /*
     * Rate, depth and delay, from the keygroup and the player's trims together.
     *
     * Moving any of them under a sounding note is safe and is the point: the phase carries on
     * from where it was, so a rate change bends the wobble rather than restarting it, and the
     * fade keeps its place, so reaching for the delay does not re-trigger a fade-in that has
     * already finished.
     *
     * The depth the trim moves is the keygroup's own. Whatever the mod wheel is asking for is
     * added on top and is not trimmed - the wheel is the player's hand already, and putting a
     * second control on it would only fight the first.
     */
    void Voice::applyLfo()
    {
        const double rate  = cal::clamp (kg->lfoRate  + trims.lfoRate,  0.0, 99.0);
        const double depth = cal::clamp (kg->lfoDepth + trims.lfoDepth, 0.0, 99.0);
        const double delay = cal::clamp (kg->lfoDelay + trims.lfoDelay, 0.0, 99.0);

        lfoCents    = depth * cal::LfoDepthCentsPerUnit + wheelCents;
        lfoStep     = 2.0 * 3.14159265358979323846
                      * (cal::LfoRateHzAtZero + rate * cal::LfoRateHzPerUnit) / sampleRate;
        fadeSeconds = cal::LfoDelayFadeConstant / std::max (1.0, 100.0 - delay);
    }

    void Voice::release()
    {
        if (stage == Stage::idle || stage == Stage::release)
            return;

        /*
         * Letting go mid-glide stops the glide where it stands: the tail fades at the pitch
         * the note had reached rather than sliding on through the release. Ahead of the
         * one-shot test, because the KEY was let go even if the sample plays on - and a
         * glide belongs to the key. Engine::stopNote makes sure the next note does not slide
         * from here either.
         */
        glidePerSecond = 0.0;

        // A one-shot keygroup is a drum: it plays through whatever the key does.
        if (kg != nullptr && kg->oneShot && ! sound->loops)
            return;

        releaseFrom = gain;
        stage       = Stage::release;
        t           = 0.0;

        // The filter lets go at the same moment, from wherever its own envelope had reached -
        // which is why that is taken here rather than assumed to be the sustain level.
        vcfReleaseFrom = vcfEnvelopeHeld (vcfT);
        vcfReleasing   = true;
        vcfReleaseT    = 0.0;
    }

    void Voice::handOver()
    {
        if (stage != Stage::release || handedOver)
            return;

        // The release is a rate measured from where it starts, so restart it from here -
        // otherwise the shorter time would be applied to all the time already spent, and
        // the gain would drop in one step, which is the click this is meant to avoid.
        handedOver  = true;
        releaseFrom = gain;
        t           = 0.0;
        releaseTime = std::min (releaseTime, HandOverRelease);
    }

    void Voice::makeWide (int sign, int partnerIndex, bool offset)
    {
        wideSign = sign < 0 ? -1 : 1;
        partner  = partnerIndex;

        /*
         * Seven milliseconds of the source into the sample, for the sharp half only. Long
         * enough that the two halves are well out of phase from the first cycle; short
         * enough that the attack is still the attack. Kept inside the audio, and inside the
         * loop for a sample that loops, so it can never start past where it can play.
         */
        if (offset && wideSign > 0 && sound != nullptr)
        {
            const double end = (sound->loops && sound->loopTo > sound->loopFrom)
                                 ? sound->loopTo : static_cast<double> (sound->audio.size());

            pos = std::min (sound->sourceRate * 0.007, std::max (0.0, end - 1.0));
        }
    }

    void Voice::setWide (double cents, double spread)
    {
        if (wideSign == 0)
            return;

        wideRatio = std::pow (2.0, wideSign * cents / 1200.0);

        // Equal-power: pi/4 is the centre, 0 hard left, pi/2 hard right.
        const double theta = 3.14159265358979323846 / 4.0
                             * (1.0 + wideSign * cal::clamp (spread, 0.0, 1.0));
        wideGainL = std::cos (theta);
        wideGainR = std::sin (theta);
    }

    void Voice::render (float* left, float* right, int count, double sharedPhase)
    {
        if (stage == Stage::idle)
            return;

        // hoisted out of the sample loop: neither changes while a block renders
        const bool  stereo = right != nullptr;
        // A wide half folds into the mono path at -3 dB, the level its pan law gives it at
        // the centre, so the two halves together are one note's worth there too.
        const float panL   = stereo ? static_cast<float> (outL * wideGainL)
                                    : (wideSign != 0 ? 0.70710678f : 1.0f);
        const float panR   = stereo ? static_cast<float> (outR * wideGainR) : 0.0f;
        float* const buffer = left;

        const float* audio = sound->audio.data();
        const int    last  = static_cast<int> (sound->audio.size()) - 1;
        const double dt    = 1.0 / sampleRate;

        int done = 0;
        while (done < count && stage != Stage::idle)
        {
            const int n = std::min (ControlBlock, count - done);

            // --- the modulators, once per block. The envelopes are rebuilt from the trims
            // first, so a control moved under a held note is heard on that note.
            applyTrims();
            filter.setCutoff (cutoffNow(), sampleRate);

            const double fade = fadeSeconds > 0.0005
                              ? (fadeT >= fadeSeconds ? 1.0 : fadeT / fadeSeconds) : 1.0;
            const double cents = lfoCents * fade;
            const double phase = ownLfo ? lfoPhase : sharedPhase;
            const double bend  = cents == 0.0 ? 1.0
                               : std::pow (2.0, cents * std::sin (phase) / 1200.0);
            /*
             * WARP rides on top of the LFO, both as multipliers on the playback rate.
             *
             * Worked out once a block like everything else here. The bend is fastest at the
             * very start - 34 ms at byte 14 = 0 - so a long control block steps down it rather
             * than gliding, which is the trade the LFO already makes and is well under a cent
             * per step at the block sizes a host asks for.
             *
             * Changing the playback rate drags the filter with it for free: the cutoff and the
             * ceiling both derive from the rate the audio leaves at.
             */
            const double warp = cal::warpRatio (kg->warpVelocity, kg->warpDepth, kg->warpTime,
                                                velocity, sinceOn);

            /*
             * The glide, taken at the MIDDLE of this control block and then moved on by the
             * whole of it. Reading it at the start would leave every block half a block
             * behind, and a fast glide over a wide interval would land a little late.
             *
             * A constant-pitch keygroup ignores the key, so it ignores a glide between keys
             * too - otherwise a drum would swoop every time the line under it moved.
             */
            double glide = 1.0;
            if (glideOffset != 0.0)
            {
                const double by = glidePerSecond * n * dt;

                if (! kg->constantPitch)
                {
                    const double mid = glideOffset > 0.0 ? std::max (0.0, glideOffset - by * 0.5)
                                                         : std::min (0.0, glideOffset + by * 0.5);
                    glide = std::pow (2.0, mid / 12.0);
                }

                glideOffset = glideOffset > 0.0 ? std::max (0.0, glideOffset - by)
                                                : std::min (0.0, glideOffset + by);
            }

            const double stepNow = step * bend * warp * wheelBend * glide * wideRatio;

            double       gainNow  = gain;
            const double gainEnd  = envelopeAfter (n * dt);
            const double gainStep = (gainEnd - gainNow) / n;

            const bool   loops = sound->loops && sound->loopTo > sound->loopFrom;
            const double end   = loops ? sound->loopTo : static_cast<double> (sound->audio.size());

            for (int j = 0; j < n; ++j)
            {
                /*
                 * Round the loop, or off the end.
                 *
                 * The end to test against is the LOOP end, which for a sample whose loop
                 * runs to its last word is one frame past the last readable one. Testing
                 * against the array instead let the position sit in the gap between them,
                 * where it was neither wrapped nor finished, and the voice simply stopped
                 * after one pass. In the C# that cost three of the four failures the first
                 * run of EngineCheck reported; do not "tidy" it back.
                 */
                /*
                 * An ALTERNATING loop turns round at each end instead of wrapping.
                 *
                 * Reflecting about the end - pos' = 2*end - pos - is what makes the cycle 2N
                 * frames with the end frame played twice, which is what the hardware does:
                 * four loop lengths from 20 to 128 all autocorrelated at exactly 2N, never
                 * 2N-2. Reflecting about end-1 instead would give 2N-2 and be a tenth of a
                 * semitone flat on a short loop.
                 */
                if (dir < 0 && pos < sound->loopFrom)
                {
                    pos = 2.0 * sound->loopFrom - pos;
                    dir = 1;
                    if (pos >= end) pos = end - 1;
                }
                else if (dir > 0 && pos >= end && loops && sound->alternates)
                {
                    pos = 2.0 * end - pos;
                    dir = -1;
                    if (pos < sound->loopFrom) pos = sound->loopFrom;
                }
                else if (pos >= end)
                {
                    if (! loops) { stage = Stage::idle; return; }

                    const double len = sound->loopTo - sound->loopFrom;
                    do { pos -= len; } while (pos >= end);
                    if (pos < 0) pos = sound->loopFrom;
                }

                // interpolate towards the next frame, which for the last one is wherever
                // the loop restarts rather than off the end of the array
                int i0 = static_cast<int> (pos);
                if (i0 > last) i0 = last;

                int i1 = i0 + 1;
                if (i1 > last) i1 = loops ? sound->loopFrom : last;

                const double frac = pos - i0;
                const double x    = audio[i0] + (audio[i1] - audio[i0]) * frac;

                const float s = static_cast<float> (filter.process (x) * gainNow);
                buffer[done + j] += s * panL;
                if (stereo) right[done + j] += s * panR;
                gainNow += gainStep;

                pos += stepNow * dir;
            }

            gain  = gainEnd;
            t     += n * dt;
            fadeT += n * dt;
            sinceOn += n * dt;

            // the filter's clock, which no amplitude stage change resets
            vcfT += n * dt;
            if (vcfReleasing) vcfReleaseT += n * dt;

            lfoPhase += lfoStep * n;
            if (lfoPhase > 2.0 * 3.14159265358979323846)
                lfoPhase -= 2.0 * 3.14159265358979323846;

            advanceStage();
            done += n;
        }
    }

    /*
     * Where the cutoff is, `at` seconds into the note.
     *
     * The base and the depth are worked out here rather than kept from the note's start,
     * because the player's two trims sit on top of them and those move while the note is
     * held. Both are applied in the panel's own units - the trim is added to the stored
     * 0..99 cutoff and to the signed amount, and the measured curve is read afterwards -
     * so the control bends the filter the way the machine's own does, steepening as it
     * climbs, rather than sliding it a flat number of octaves.
     *
     * With both trims at zero this is exactly what it was before they existed, which is
     * what keeps the conformance numbers honest.
     */
    double Voice::vcfEnvelopeHeld (double at) const
    {
        if (at < vcfAttack)
            return vcfAttack > 0 ? at / vcfAttack : 1.0;

        if (at < vcfAttack + vcfDecay)
            return vcfDecay > 0 ? 1.0 - (1.0 - vcfSustain) * ((at - vcfAttack) / vcfDecay)
                                : vcfSustain;
        return vcfSustain;
    }

    double Voice::cutoffNow() const
    {
        /*
         * The release runs the envelope's contribution back to nothing in a straight line -
         * the same shape the decay has - so the filter returns to the keygroup's own cutoff
         * as the note dies, rather than holding whatever brightness it happened to have when
         * the key came up.
         */
        const double env = vcfReleasing
            // A fixed RATE, not a fixed time - see cal::ReleaseIsARate. A release from half
            // depth takes half as long, because the release byte sets the speed the envelope
            // falls at rather than when it arrives.
            ? (vcfRelease > 0.0005
                   ? std::max (0.0, vcfReleaseFrom - vcfReleaseT / vcfRelease)
                   : 0.0)
            : vcfEnvelopeHeld (vcfT);

        const double base = cal::cutoffHz (kg->zoneFilter + trims.cutoff, leaveRate);

        /*
         * A programme with no filter envelope of its own contributes no amount, so an
         * untrimmed keygroup moves the cutoff by nothing at all - exactly as before these
         * controls existed. Dial an amount in and it has the flat envelope above to apply it
         * to, which is a constant offset until a decay or a sustain is dialled in as well.
         */
        const double baseAmount = kg->vcfWritten ? kg->vcfAmount : 0;
        const double depth =
            (cal::clamp (baseAmount + trims.amount, -50.0, 50.0) / 50.0) * cal::EnvOctaves;

        const double hz = base * std::pow (2.0, cutoffShift + env * depth);
        return hz > ceiling ? ceiling : (hz < floorHz ? floorHz : hz);
    }

    /*
     * Where the amplitude envelope will be in `ahead` seconds.
     *
     * The decay and the release are straight lines in DECIBELS, not in amplitude. That is
     * measured: a stored decay of 80 fell 2.9, 2.7, 3.0, 3.0, 2.7, 2.9 dB per fifth of a
     * second, dead constant for two and a half seconds. A line in amplitude hangs near the
     * peak and then drops off a cliff, which is audibly another instrument.
     */
    double Voice::envelopeAfter (double ahead) const
    {
        const double at = t + ahead;

        switch (stage)
        {
            case Stage::attack:
                return attack > 0 ? peak * std::min (1.0, at / attack) : peak;

            case Stage::decay:
                if (decay <= 0.0005) return sustain;
                return fall (peak, sustain, std::min (1.0, at / decay));

            case Stage::release:
                if (releaseTime <= 0.0005) return 0.0;

                /*
                 * A RATE: cal::VcaReleaseDb in one release time, from wherever the key came up,
                 * and it keeps falling. This used to run from releaseFrom to 1e-4 over exactly
                 * one release time, which is 80 dB from a note at full level - twice the
                 * machine's speed - and made a quiet note fade in the same wall-clock time as a
                 * loud one, which is a duration rather than a rate.
                 */
                return releaseFrom
                         * std::pow (10.0, -cal::VcaReleaseDb / 20.0 * (at / releaseTime));

            default:
                return sustain;
        }
    }

    void Voice::advanceStage()
    {
        switch (stage)
        {
            case Stage::attack:
                if (t >= attack) { stage = Stage::decay; t = 0.0; }
                break;

            case Stage::decay:
                if (t >= decay) { stage = Stage::sustain; t = 0.0; gain = sustain; }
                break;

            case Stage::release:
                // The gain alone decides when the note is over. It used to end at
                // t >= releaseTime as well, which with a RATE cuts the tail off after one
                // release time however loud the note still is - a click on any long release.
                if (gain <= 2e-4) stage = Stage::idle;
                break;

            default:
                break;
        }
    }
}
