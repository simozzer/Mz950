#pragma once

#include "Cal.h"
#include "Filter.h"
#include "Patch.h"

namespace s950
{
    /*
     * The player's controls, sitting on top of whatever the programme says.
     *
     * Every one is an OFFSET in the panel's own units, and every one is zero by default, so
     * a programme plays exactly as the disk describes it until something is moved. That is
     * the whole point of an instrument built around a floppy: an absolute control would
     * flatten a programme's keygroups to one value the moment it was touched, where an
     * offset moves the shape and keeps it.
     *
     * The times and sustains are added to a stored 0..99 and clamped back into it; the
     * filter amount is added to a signed -50..+50.
     *
     */
    struct Trims
    {
        double cutoff = 0.0, amount = 0.0;
        double vcaAttack = 0.0, vcaDecay = 0.0, vcaSustain = 0.0, vcaRelease = 0.0;
        double vcfAttack = 0.0, vcfDecay = 0.0, vcfSustain = 0.0, vcfRelease = 0.0;
        double lfoRate = 0.0, lfoDepth = 0.0, lfoDelay = 0.0;
        double velToFilter = 0.0, velToLoudness = 0.0;
    };

    /*
     * One sounding note.
     *
     * Everything a voice does happens in render(), a block at a time, and nothing in here
     * allocates once the voice exists. That is the whole reason this half is separate from
     * the editor: an allocation in the middle of an audio callback is a click, and a click
     * in a plugin is someone else's ruined take.
     *
     * The signal path follows the machine: the sample is read out at whatever rate the pitch
     * asks for, and the filter runs AFTER that, at the rate the audio leaves at. So the
     * cutoff is a fixed number of hertz whichever key is held, rather than being dragged up
     * and down by the varispeed.
     *
     * A straight port of AkaiS950Engine/Voice.cs.
     */
    class Voice
    {
    public:
        bool isActive()  const { return stage != Stage::idle; }
        bool isHeld()    const { return stage != Stage::idle && stage != Stage::release; }
        int  getNote()     const { return note; }
        int  getVelocity() const { return velocity; }
        long long getStartedAt() const { return startedAt; }

        /// Which keygroup of the programme this voice came from, or -1.
        int getKeygroupIndex() const { return keygroupIndex; }

        /// The audio this voice is reading, so a caller can ask what is really sounding.
        const SoundPtr& getSound() const { return sound; }

        /// Start this voice. Nothing here allocates.
        ///
        /// `crossfade` is the positional crossfade's gain for this keygroup at this key,
        /// worked out once by the engine from every keygroup answering the note. See
        /// cal::crossfadeGain.
        ///
        /// `glideSemis` is how far from this note the pitch starts, in semitones, and
        /// `glideSeconds` how long it takes to arrive. Zero for either is no glide. See
        /// Engine::glide - it is not something the S950 had.
        void start (const KeygroupPatch& kg, int note, int velocity,
                    double sampleRate, double wheelCents, long long sequence,
                    double crossfade = 1.0,
                    double glideSemis = 0.0, double glideSeconds = 0.0);

        /*
         * The pitch this voice is sounding at, as a note number with a fraction.
         *
         * The key it was struck on plus however much glide it has left to cover, so a note
         * struck in the middle of a glide can start from where the last one had got to rather
         * than from where it was going. Ignores the wheel, the LFO and warp: those move every
         * note alike and a glide should not inherit them.
         */
        double getPitchNow() const { return note + glideOffset; }

        /// Still sliding towards its note. False once it arrives, or once release() stops it.
        bool isGliding() const { return glidePerSecond > 0.0 && glideOffset != 0.0; }

        /*
         * Take up new settings without restarting the note.
         *
         * For a value changed while the note is sounding - a filter dragged, an envelope
         * reshaped, a programme edited under a held loop. Everything that describes the note
         * is recomputed; everything that says where the note has got to is left exactly as
         * it is. So the sample goes on from where it was, the envelope stays in its stage,
         * the LFO keeps its phase and the filter keeps its state - the last of those
         * matters, because resetting a filter mid-note is a click.
         *
         * A keygroup now naming a different sample is not a change to this note, it is a
         * different note; the position we are at would not mean the same thing in other
         * audio. That waits for the next trigger.
         */
        void adopt (const KeygroupPatch& kg);

        /*
         * Move a sounding note to another key without striking it again - mono legato.
         *
         * The sample carries on from where it is and the envelope stays in its stage; only
         * the pitch moves, gliding there if `glideSemis` and `glideSeconds` say so, exactly
         * as start() would have. Velocity is the first strike's: the note was never struck
         * again. Same keygroup and same sample only - Engine::startNote checks.
         */
        void legatoTo (int note, double glideSemis, double glideSeconds);

        /*
         * The player's own trims, on top of whatever the keygroup says.
         *
         * Both are in the panel's own units, because that is where they mean something: the
         * cutoff curve is a measured table against the stored 0..99, so adding to the stored
         * value bends the filter the way the machine's own knob would. Adding octaves instead
         * would be a different, straighter control that the S950 does not have.
         *
         * Set by the engine before every stretch it renders, so moving the control is heard
         * on notes that are already sounding rather than only on the next one.
         */
        void setTrims (const Trims& set) { trims = set; }

        /// The pitch wheel as a rate multiplier. Reaches a note already sounding, unlike velocity.
        void setBend (double ratio) { wheelBend = ratio; }

        /// Let go of the key. The note falls at its own release rate.
        void release();

        void kill() { stage = Stage::idle; }

        /*
         * A released note that a glide is taking over: fade it out fast.
         *
         * Without this, a glide from a note whose key is already up started a SECOND voice at
         * that note's pitch while the first was still in its release - two copies of one
         * sample at nearly one pitch, drifting apart, which is a unison. It was heard as the
         * sound thickening on a line of single notes, and only with polyphony above one,
         * because mono takes its one voice over anyway. The glide is the old note carrying
         * on; the old note's own tail should go.
         *
         * Ten milliseconds rather than cut dead, which would click. Only for a voice already
         * in its release: a key still held is being played, and is left alone.
         */
        void handOver();

        /*
         * WIDE - one half of a detuned pair. Not an S950 feature; see Engine::wide.
         *
         * `sign` is -1 for the flat half, which leans left, and +1 for the sharp half, which
         * leans right. Called straight after start(). With `offset` the sharp half starts a
         * few milliseconds into the sample, so the two are not in phase at the attack - two
         * copies starting together flange until they drift apart.
         */
        void makeWide (int sign, int partnerIndex, bool offset);

        /// The other half of this voice's pair, or -1. Only to be trusted if it points back.
        int getPartner() const { return partner; }

        /*
         * How far apart the pair sits and how wide, set by the engine before each stretch
         * so turning either control is heard on notes already sounding. Ignored by a voice
         * that is not half of a pair.
         */
        void setWide (double cents, double spread);

        /*
         * Add this voice into the buffer.
         *
         * `sharedPhase` is the programme's own LFO, which voices with the desync bit CLEAR
         * ride instead of their own. That was measured rather than assumed: with the bit
         * clear two voices held their phase to within 3 degrees over six seconds, and with
         * it set they ran at rates 3.5% apart and drifted a whole turn in the same six.
         */
        /// Mono, as it always was: full gain into one buffer.
        void render (float* buffer, int count, double sharedPhase)
        {
            render (buffer, nullptr, count, sharedPhase);
        }

        /*
         * Stereo, with the keygroup's output port deciding which side it lands on.
         *
         * A null `right` is the mono path and is bit-identical to what it always did - the pan
         * gains are ignored and the sample goes into `left` at full level. Only a caller with
         * two channels pays for the second write.
         */
        void render (float* left, float* right, int count, double sharedPhase);

    private:
        /// How often the modulators are recomputed. 32 at 48 kHz is 0.67 ms.
        /*
         * How often the modulators are recomputed. 32 at 48 kHz is 0.67 ms.
         *
         * This is also how often the filter is retuned, and that is the tighter constraint.
         * The fastest release the machine has drops the cutoff five and a half octaves in
         * about a millisecond, and a sixth-order cascade moved that far in ONE step - while
         * keeping the state the old coefficients left behind - rings instead of closing.
         * Stepping this cascade from 17760 Hz to 311 Hz against a signal peaking at 1.0:
         *
         *     block    64     32     16      8      4      1
         *     peak   17.85   1.24   2.49   2.29   1.83   1.57
         *
         * The cliff is between 64 and 32; below that the numbers are one realisation of a
         * sweep that overshoots a little whatever the step, and the scatter is the sawtooth
         * landing differently against each retune. So 32 is already on the right side of it
         * and going finer buys nothing for four times the modulator work.
         *
         * The web build bakes its filter with a 64-sample block and DID ring, at nine times
         * full scale; its release renderer uses 8 for that reason.
         */
        static constexpr int ControlBlock = 32;

        enum class Stage { idle, attack, decay, sustain, release };

        double cutoffNow() const;

        /// The filter envelope while the key is still down: attack, decay, then sustain.
        double vcfEnvelopeHeld (double at) const;
        double envelopeAfter (double ahead) const;
        void   advanceStage();

        /*
         * Work the envelope times and levels out from the keygroup plus the trims.
         *
         * Called at the top of every control block, not once when the note starts, so that
         * moving a control is heard on notes already sounding. That is what a control is;
         * a value only read at note-on would be a setting.
         */
        void applyTrims();

        /*
         * The three LFO values the trims can move, derived from the keygroup and the trims.
         *
         * Separate from applyTrims only because the note's own LFO state - its phase, and how
         * far into the fade it is - is set once at the strike and must survive every later
         * call, while these three are recomputed from scratch each control block.
         */
        void applyLfo();

        /// A stored 0..99 panel value with its trim added, back inside 0..99.
        static double trimmed (int stored, double by)
        {
            return cal::clamp (stored + by, 0.0, 99.0);
        }

        /// A straight line in decibels from one gain to another.
        static double fall (double from, double to, double u)
        {
            if (from <= 1e-6) return 0.0;
            const double lo = std::max (to, 1e-6);
            return from * std::pow (lo / from, u);
        }

        static double clamp01 (double v) { return cal::clamp (v, 0.0, 1.0); }

        Butterworth filter;

        /*
         * The keygroup this voice is playing.
         *
         * A pointer into the patch, which the engine keeps alive for as long as any voice
         * refers to it - see Engine::setPatch. The sound is held by value as a shared_ptr
         * instead, because that is the one thing a voice reads every single sample.
         */
        const KeygroupPatch* kg = nullptr;
        SoundPtr             sound;

        int       note = 0, velocity = 0;
        int       keygroupIndex = -1;
        long long startedAt = 0;

        double sampleRate = 48000.0;
        double pos        = 0.0;     // where we are in the sample, in frames
        int    dir        = 1;       // +1 forwards, -1 back; only an alternating loop flips it

        /// The pitch wheel as a rate multiplier, set by the engine once per stretch. 1.0 at
        /// rest. Named for the wheel rather than "bend", because the LFO's own multiplier
        /// inside render() is already called that and one shadowing the other would be a bug
        /// waiting to be written.
        double wheelBend  = 1.0;
        double step       = 1.0;     // frames per output sample, before the LFO
        double leaveRate  = 40000.0; // the rate the audio leaves at, for the filter ceiling

        // amplitude envelope, all in gain except the times
        Stage  stage = Stage::idle;
        double t = 0.0;              // seconds since the stage began
        double attack = 0.0, decay = 0.0, releaseTime = 0.0;
        double peak = 1.0, sustain = 1.0;

        /*
         * The positional crossfade, worked out once when the note started.
         *
         * Kept rather than recomputed, and adopt() keeps it too: the fade depends on the key
         * ranges of the OTHER keygroups answering this note, which a repatch may have moved
         * under the voice. A voice belongs to the keygroup that started it, and it belongs
         * to the balance it started in for the same reason.
         */
        double crossfade = 1.0;
        double gain = 0.0, releaseFrom = 0.0;

        // filter envelope
        double vcfAttack = 0.0, vcfDecay = 0.0, vcfSustain = 1.0, vcfRelease = 0.0;
        double cutoffShift = 0.0, ceiling = 16000.0, floorHz = 311.0;

        /*
         * The filter envelope's own clock, counting from the note on.
         *
         * NOT `t`, which is how far the AMPLITUDE envelope has got into its current stage and
         * is reset to zero every time that stage changes. Driving the filter from it made the
         * filter envelope restart whenever the amplitude one moved attack -> decay and again
         * decay -> sustain, so a programme with a slow amplitude decay swept its filter two or
         * three times over. It went unnoticed because most of the library has an instant
         * attack and decay, which resets a clock that has barely started.
         */
        double vcfT = 0.0;

        /*
         * The filter's release, separate from the amplitude's.
         *
         * The envelope falls from wherever it had reached back to nothing - and nothing means
         * the keygroup's own cutoff, the envelope's contribution decaying away rather than the
         * filter slamming shut. Kept apart from `stage` because a one-shot keygroup ignores
         * note-off completely and then neither envelope releases.
         */
        bool   vcfReleasing = false;
        double vcfReleaseFrom = 0.0, vcfReleaseT = 0.0;

        /*
         * The base cutoff and the envelope depth are NOT cached here, where everything else
         * about the note is. They come off the keygroup afresh in cutoffNow(), because the
         * trims move under a held note and a cached pair would freeze the control until the
         * next key was struck.
         *
         * The trims are taken as they arrive, NOT smoothed, and that is a decision rather
         * than an oversight.
         *
         * Measured, by stepping the trim mid-note and comparing the output's slew at the
         * change with the slew the waveform reaches anyway:
         *
         *     one CC step (1.56 units)   1.09x   - lost in the signal
         *     a fast drag (25 units)     2.84x   - a click
         *     slammed shut (-60 units)   0.05x   - closing is always safe
         *
         * So the only thing that clicks is a large jump applied in one lump: a preset
         * recall, a typed value, a double-click back to zero, or a step in an automation
         * lane. Continuous moves - a controller sweep, a dragged knob, smoothed automation -
         * arrive in steps around the size of the first row, and those are inaudible.
         *
         * A glide was tried and taken out again. It cost 15 ms of lag on every move, which
         * is felt on a filter sweep played to a beat, and the machine itself had no such
         * thing. Where a slow move is wanted, the host can ramp the parameter, which is the
         * right place for it: the plugin should not decide how fast a player's hand moves.
         */
        Trims trims;

        // the LFO
        double lfoCents = 0.0, lfoPhase = 0.0, lfoStep = 0.0;
        double fadeSeconds = 0.0, fadeT = 0.0;

        /// Seconds since the key went down. Warp's bend is measured from there, and `t` cannot
        /// serve because it restarts at every envelope stage.
        double sinceOn = 0.0;

        /// Where the keygroup's output port puts this voice. See cal::outputGains.
        double outL = 1.0, outR = 1.0;
        double wheelCents  = 0.0;    // kept so adopt() can add it back to a new depth
        bool   ownLfo = true;

        /*
         * Portamento: semitones still to travel, and how fast, in semitones a second.
         *
         * The offset walks to zero in a straight line in SEMITONES - an exponential in hertz -
         * so an octave glide spends as long on its bottom half as its top, which is what
         * every synth with a portamento knob does and what an ear hears as even.
         *
         * The speed is set at the strike from the distance and the time, so every glide takes
         * the same time however far it goes. Constant-rate is the other school; constant-time
         * is the one where a knob reading 200 ms means 200 ms.
         */
        double glideOffset = 0.0, glidePerSecond = 0.0;

        /// Handed over to a glide: the release is held to HandOverRelease. See handOver().
        bool handedOver = false;

        /*
         * Wide: which half of a pair this is (0 for not one), its twin, and what the detune
         * and the spread come to as a rate multiplier and two channel gains.
         *
         * The gains are an equal-power pan with both halves at -3 dB. Centred, each half is
         * 0.707 in both channels, and two copies that are not in phase add up in power to
         * one note's worth. Fully spread, each half is at 1.0 on its own side - so a wide
         * note is as loud as a plain one wherever the Spread control sits.
         */
        int    wideSign = 0, partner = -1;
        double wideRatio = 1.0, wideGainL = 1.0, wideGainR = 1.0;

        /// A release time that falls the whole way (-74 dB, where render() stops a voice) in
        /// 10 ms, given that a release time is the time to fall cal::VcaReleaseDb.
        static constexpr double HandOverRelease = 0.010 * cal::VcaReleaseDb / 74.0;
    };
}
