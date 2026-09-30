#pragma once

#include "Voice.h"

#include <atomic>
#include <vector>

namespace s950
{
    /*
     * The instrument: eight voices, a patch, and a buffer to fill.
     *
     * Nothing in here knows what is driving it. In the C# an editor drives it from a piano
     * keyboard and a MIDI port; here a host drives it from its note events; the checks drive
     * it from a loop and render to a file. That separation is the one decision that made
     * this port a port rather than a rewrite.
     *
     * THREADS
     *
     * render() runs on the audio thread and must never allocate, never lock and never block.
     * Notes arrive from somewhere else, so they go into a small lock-free ring and are picked
     * up at the top of the next render(). A lock here would be a dropout.
     */
    class Engine
    {
    public:
        /// What the machine has. Voices past this steal the oldest.
        static constexpr int Polyphony = 8;

        explicit Engine (double sampleRate);

        double getSampleRate() const { return sampleRate; }

        /// The master trim, as a plain gain. Eight voices at once can clip.
        std::atomic<float> gain { 0.7f };

        /*
         * The player's trims, applied to every keygroup of whatever is loaded. See Trims.
         *
         * One atomic each rather than one lock around the set: they are written by the
         * message thread and read by the audio thread, and nothing here needs them to change
         * together. A control moved half a block before another is exactly what a pair of
         * hands does anyway.
         */
        struct AtomicTrims
        {
            std::atomic<float> cutoff { 0.0f }, amount { 0.0f };
            std::atomic<float> vcaAttack { 0.0f }, vcaDecay { 0.0f },
                               vcaSustain { 0.0f }, vcaRelease { 0.0f };
            std::atomic<float> vcfAttack { 0.0f }, vcfDecay { 0.0f },
                               vcfSustain { 0.0f }, vcfRelease { 0.0f };
            std::atomic<float> lfoRate { 0.0f }, lfoDepth { 0.0f }, lfoDelay { 0.0f };
            std::atomic<float> velToFilter { 0.0f }, velToLoudness { 0.0f };
            std::atomic<float> resonance { 0.0f };
            std::atomic<float> lfoShape { 0.0f }, lfoToFilter { 0.0f };
            std::atomic<float> lfoSync { 0.0f }, lfoSyncDiv { 8.0f };

            /// One reading of the lot, for a stretch of audio to be rendered against.
            Trims read() const
            {
                Trims t;
                t.cutoff     = cutoff.load (std::memory_order_relaxed);
                t.amount     = amount.load (std::memory_order_relaxed);
                t.vcaAttack  = vcaAttack.load (std::memory_order_relaxed);
                t.vcaDecay   = vcaDecay.load (std::memory_order_relaxed);
                t.vcaSustain = vcaSustain.load (std::memory_order_relaxed);
                t.vcaRelease = vcaRelease.load (std::memory_order_relaxed);
                t.vcfAttack  = vcfAttack.load (std::memory_order_relaxed);
                t.vcfDecay   = vcfDecay.load (std::memory_order_relaxed);
                t.vcfSustain = vcfSustain.load (std::memory_order_relaxed);
                t.vcfRelease = vcfRelease.load (std::memory_order_relaxed);
                t.lfoRate    = lfoRate.load (std::memory_order_relaxed);
                t.lfoDepth   = lfoDepth.load (std::memory_order_relaxed);
                t.lfoDelay   = lfoDelay.load (std::memory_order_relaxed);
                t.velToFilter   = velToFilter.load (std::memory_order_relaxed);
                t.velToLoudness = velToLoudness.load (std::memory_order_relaxed);
                t.resonance     = resonance.load (std::memory_order_relaxed);
                t.lfoShape      = lfoShape.load (std::memory_order_relaxed);
                t.lfoToFilter   = lfoToFilter.load (std::memory_order_relaxed);
                t.lfoSync       = lfoSync.load (std::memory_order_relaxed);
                t.lfoSyncDiv    = lfoSyncDiv.load (std::memory_order_relaxed);
                return t;
            }
        };

        AtomicTrims trims;

        /*
         * What to play.
         *
         * Called from the message thread. The patch is handed over rather than shared: see
         * the note in Engine.cpp on why this is a two-slot exchange and not simply an
         * atomic pointer. Call collectRetiredPatch() from the message thread now and then -
         * a timer, or the top of the next setPatch - or the old patch is never released.
         */
        void setPatch (PatchPtr patch) { trySetPatch (patch); }

        /*
         * Hand over a patch if the audio thread has taken the last one - false, touching
         * nothing, if it has not.
         *
         * setPatch used to overwrite `pending` unconditionally, which is a race the moment
         * the audio thread is taking the previous one at the same instant. A programme change
         * a minute is never going to hit it; an edit knob dragged at thirty changes a second
         * will. So a caller that changes the patch often keeps what it wants and tries again
         * - see VirtualS950Processor::timerCallback - and the newest wins.
         */
        bool trySetPatch (const PatchPtr& patch);

        /*
         * Release whatever the audio thread has finished with.
         *
         * Message thread only. Freeing a patch is freeing every sample buffer in it, and
         * that must not happen where a late free is a click.
         */
        void collectRetiredPatch();

        // ------------------------------------------------------------------- playing

        /*
         * `at` is where in the next block the event belongs, in samples.
         *
         * A host hands over a block of audio and, with it, the events that happen part
         * way through it. Applying them all at the start instead - which is what the C#
         * does, because nothing driving it has anything better to offer - rounds every
         * note to the block boundary. At a 512-sample buffer that is 11 ms of jitter,
         * and 11 ms is audible on a drum pattern, which is what this instrument is for.
         *
         * Zero is "at the start of the next block", which is right for a note struck by
         * hand, by a MIDI port, or by anything else with no finer timing to give.
         */
        void noteOn (int note, int velocity, int at = 0) { post (EvNoteOn,  note, velocity, at); }
        void noteOff (int note, int at = 0)              { post (EvNoteOff, note, 0, at); }
        void modwheel (int value, int at = 0)            { post (EvWheel,   value, 0, at); }

        /// Channel aftertouch, 0..127. One value for the whole keyboard: the S950 has no
        /// polyphonic pressure input, so there is nothing per-key to carry.
        void aftertouch (int value, int at = 0)          { post (EvPressure, value, 0, at); }

        /*
         * The pitch wheel, 0..16383 with 8192 at rest.
         *
         * Split across the event's two byte fields because fourteen bits do not fit in one,
         * and reassembled in applyNextEvent. Sample-accurate like every other message: a bend
         * lands where the host put it rather than on the block boundary, which matters more
         * for a wheel than for a note because a sweep is a stream of them.
         */
        void pitchBend (int value, int at = 0)
        {
            const int v = value < 0 ? 0 : (value > 16383 ? 16383 : value);
            post (EvBend, static_cast<unsigned char> ((v >> 7) & 0x7F),
                          static_cast<unsigned char> (v & 0x7F), at);
        }

        /*
         * How far the wheel bends, in semitones. The machine's MIDI page offers 1 to 12.
         *
         * Not read off the disk. It is a setting of the machine rather than of a programme,
         * and the OVERALL SETTINGS file that would hold it is only written when someone saves
         * it deliberately - so the plugin owns it, as the host's user does.
         */
        std::atomic<double> bendRange { 2.0 };
        void allNotesOff (int at = 0)                    { post (EvAllOff,  0, 0, at); }

        /*
         * PORTAMENTO, WHICH THE S950 NEVER HAD
         *
         * Everything else in this engine is the machine, measured. This is not: it is an
         * addition, and it is off until something turns it on, so a programme still plays
         * exactly as the disk describes it.
         *
         * Switched through the ring rather than an atomic, so it lands on the sample the host
         * put it at. A sequenced line toggling glide between two notes a few samples apart
         * has to get the order right, and a flag read once a block cannot promise that.
         *
         * Each new note glides from the pitch of the note struck before it IN THE SAME
         * KEYGROUP - or from where that note had got to, if it was still gliding. Crossing
         * into another keygroup does not glide: see GlideMemory. Within one keygroup a line
         * slides, and a chord slides as a whole from the note before it, because every note
         * of it starts from the same place.
         */
        void glide (bool on, int at = 0)                 { post (EvGlide, on ? 1 : 0, 0, at); }

        /// How long a glide takes, whatever the interval. Read when a note starts.
        std::atomic<double> glideSeconds { 0.12 };

        /*
         * How many of the eight voices to use, 1 to 8. Also not the machine's: it always
         * has all eight.
         *
         * One is MONO, and mono plays like a monosynth rather than a sampler with seven
         * voices missing. A key struck while another is still down in the same keygroup
         * moves the sounding note to it - legato, the sample carrying on - gliding there if
         * glide is on. Letting go of the top key goes back to the last one still held. A
         * detached note, or one in a different keygroup, strikes afresh. A layered programme
         * sounds only its first keygroup, there being one voice to sound it with.
         *
         * Through the ring like glide, so it lands on its sample. Voices past a lowered limit
         * are not cut off: they finish what they are playing and are then left alone.
         */
        void setVoiceLimit (int count, int at = 0)       { post (EvVoices, count, 0, at); }

        /*
         * WIDE, WHICH THE S950 NEVER HAD EITHER
         *
         * Every note sounds as two voices, one detuned flat and leaning left, one detuned
         * sharp by the same amount and leaning right - centred on the right pitch, and as
         * loud as one voice. Mono or poly.
         *
         * A pair is two of the eight voices, so wide caps the notes at FOUR: the machine's
         * eight voices, two to a note. Below four the Polyphony control still sets it. The
         * two halves are stolen together, never one alone - a note left with only its flat
         * half would sound flat and off to one side, which is the worst artifact this could
         * have. Constant-pitch keygroups are not doubled: a drum has no pitch to detune.
         *
         * The switch is an event, like glide, and reaches the NEXT note struck. Detune and
         * spread are read every stretch and reach notes already sounding. The start offset
         * is read when a note starts.
         */
        void wide (bool on, int at = 0)                  { post (EvWide, on ? 1 : 0, 0, at); }

        std::atomic<double> wideCents  { 10.0 };        // each half, either way
        std::atomic<double> wideSpread { 0.7 };         // 0 centred, 1 hard left and right
        std::atomic<bool>   wideOffset { true };        // sharp half starts 7 ms in

        /// How many notes can sound at once right now, after wide has had its say.
        int getNoteLimit() const { return wideOn ? std::min (voiceLimit, Polyphony / 2) : voiceLimit; }

        // -------------------------------------------------------------------- render

        /// Fill `count` mono samples. Allocates nothing.
        /// Mono, as it always was. Bit-identical to before.
        void render (float* buffer, int count) { render (buffer, nullptr, count); }

        /*
         * Stereo, honouring each keygroup's output port.
         *
         * The machine's LEFT and RIGHT sockets are two mono outputs rather than a pan pot, so
         * a keygroup sent to one is absent from the other - 38 keygroups across four library
         * programmes, TUBULAR 2's bells among them. Everything else lands on both at full
         * level, which is what the mono path always did with everything.
         */
        void render (float* left, float* right, int count);

        /*
         * Where the host's transport is, for tempo sync. Audio thread, before render().
         *
         * `ppq` is the position at the start of the next block in quarter notes, and is only
         * read while `playing` - then the shared LFO is locked to it, so a synced S&H steps
         * on the grid and draws the same levels at the same bar every pass. Stopped, or with
         * no host at all (the standalone), the LFO runs free at the tempo. With no call at
         * all it runs at 120.
         */
        void setTransport (double bpm, double ppq, bool playing, double quartersPerBar)
        {
            hostBpm        = bpm > 0.0 ? bpm : 120.0;
            hostPpq        = ppq;
            hostPlaying    = playing;
            hostBarQuarters = quartersPerBar > 0.0 ? quartersPerBar : 4.0;
        }

        // ------------------------------------------------------------ for the caller

        int getActiveVoices() const;

        /*
         * What each keygroup has been asked to play, for a window that lights them up.
         *
         * A COUNT of notes answered, not a flag, and the last note: a drum hit is over
         * between two redraws of a window, and a flag would be off again before anything
         * saw it. A count that moved says "this was hit", however briefly. Written on the
         * audio thread as a note starts, read from anywhere.
         */
        static constexpr int ActivitySlots = 64;

        unsigned getKeygroupHits (int keygroup) const
        {
            return keygroup >= 0 && keygroup < ActivitySlots
                     ? keygroupHits[keygroup].load (std::memory_order_relaxed) : 0u;
        }

        int getKeygroupLastNote (int keygroup) const
        {
            return keygroup >= 0 && keygroup < ActivitySlots
                     ? keygroupLastNote[keygroup].load (std::memory_order_relaxed) : -1;
        }

        const Voice& getVoice (int i) const { return voices[i]; }

    private:
        static constexpr unsigned char EvNoteOn  = 1;
        static constexpr unsigned char EvNoteOff = 2;
        static constexpr unsigned char EvWheel   = 3;
        static constexpr unsigned char EvAllOff  = 4;
        static constexpr unsigned char EvBend    = 5;
        static constexpr unsigned char EvPressure = 6;
        static constexpr unsigned char EvGlide    = 7;
        static constexpr unsigned char EvVoices   = 8;
        static constexpr unsigned char EvWide     = 9;

        static constexpr int RingSize = 256;

        struct Event
        {
            unsigned char  kind, a, b;
            unsigned short at;          // samples into the block this belongs to
        };

        void post (unsigned char kind, int a, int b, int at);
        void takePendingPatch();

        /// The next event's place in this block, clamped into it. False if there is none.
        bool peekEvent (int& at, int count) const;

        /// Take the next event off the ring and do it.
        void applyNextEvent();

        /// Every voice into one stretch of the buffer.
        void renderSpan (float* left, float* right, int count);
        void repatch();
        /// `legato` allows a mono voice to move to this note rather than strike it.
        void startNote (int note, int velocity, bool legato = false);
        void stopNote (int note);

        /*
         * A DRUM NOTE is one every keygroup answering it plays constant-pitch and one-shot -
         * which is what a kit's keygroups are, on a library disk and on the Synth tab's.
         *
         * Drums live outside the polyphony limit and outside mono. With Poly at 1 a kick
         * must not steal the lead, and a snare key held down is not a "held key" for legato
         * to return to. So a drum is played from the voices ABOVE the limit (all of them
         * when there is no limit), never enters the held-key stack, and never moves a voice
         * legato. take (true) hands out those voices.
         */
        bool isDrumNote (int note, int velocity);

        /// Voice i let go, and forgotten as a glide's origin if it was cut off mid-glide.
        void releaseVoice (int i, bool now = false);
        Voice& take (bool drum = false);

        double sampleRate = 48000.0;

        Voice voices[Polyphony];
        std::vector<const KeygroupPatch*> matched;   // reused, so starting a note allocates nothing

        Event            ring[RingSize] {};
        std::atomic<int> writeIndex { 0 };
        std::atomic<int> readIndex  { 0 };

        /*
         * The patch, exchanged between threads without either one waiting.
         *
         * audioPatch belongs to the audio thread and nothing else touches it. pending is
         * filled by the message thread and taken by the audio thread; retired goes the other
         * way. Both hand-offs move the shared_ptr rather than copying it, so no reference
         * count is touched on the audio thread and nothing is ever freed there.
         */
        PatchPtr          audioPatch;
        PatchPtr          pending;
        PatchPtr          retired;
        std::atomic<bool> pendingReady { false };
        std::atomic<bool> retiredReady { false };

        long long sequence = 0;
        double    sharedPhase = 0.0;
        double    sharedRate  = -1.0;   // the shared LFO's rate on the panel's 0..99, or -1 for none

        // the host's transport, for tempo sync - see setTransport. Audio thread only.
        double    hostBpm = 120.0, hostPpq = 0.0, hostBarQuarters = 4.0;
        bool      hostPlaying = false;
        unsigned  sharedCycle = 0;
        int       wheel = 0;
        int       pressure = 0;          // channel aftertouch, at rest at nothing
        int       bend14 = 8192;         // the pitch wheel, at rest in the middle

        /*
         * Portamento's memory, one slot per keygroup, indexed by keygroupIndex.
         *
         * Per keygroup because a glide only makes sense INSIDE one. A keygroup is one sample
         * over one range of keys; crossing into the next is a different sample, and on a
         * split it is a different instrument - a bass note sliding up into the lead above it
         * is not portamento, it is a mistake. So each keygroup remembers its own last note
         * and glides only from that.
         *
         * pitch is -1 until the keygroup has played, so its first note has nowhere to come
         * from. voice is the voice that note started, and sequence proves it has not since
         * been taken for something else. Fixed-size so a note never allocates; a keygroup
         * past the end of the table simply does not glide. Cleared when the patch changes,
         * because keygroup 3 of one programme has nothing to do with keygroup 3 of another.
         */
        struct GlideMemory
        {
            double    pitch    = -1.0;
            int       voice    = -1;
            long long sequence = -1;
        };

        static constexpr int GlideSlots = 64;

        bool        glideOn = false;
        GlideMemory glideFrom[GlideSlots];

        std::atomic<unsigned> keygroupHits[ActivitySlots];
        std::atomic<int>      keygroupLastNote[ActivitySlots];

        /// A keygroup answered `note`: count it, for the window. See getKeygroupHits.
        void noteHit (int keygroupIndex, int note)
        {
            if (keygroupIndex < 0 || keygroupIndex >= ActivitySlots) return;
            keygroupLastNote[keygroupIndex].store (note, std::memory_order_relaxed);
            keygroupHits[keygroupIndex].fetch_add (1u, std::memory_order_relaxed);
        }

        int  voiceLimit = Polyphony;     // in NOTES - see getNoteLimit
        bool wideOn     = false;

        /// Kill voice i, and its twin with it if it is half of a pair. See Engine::wide.
        void killPair (int i);

        /*
         * The keys that are down, oldest first, with how hard each was struck.
         *
         * Kept whatever the voice count, because switching to mono with keys held must know
         * what they are. Only mono reads it: letting go of the top key goes back to the one
         * below. A key appears once - struck again, it moves to the top.
         */
        int heldNote[128] {}, heldVelocity[128] {};
        int heldCount = 0;

        void pressKey (int note, int velocity);
        void liftKey (int note);

        /// Where a note in this keygroup glides from: -1 for nowhere.
        double glideOrigin (int keygroupIndex) const;
    };
}
