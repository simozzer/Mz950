#pragma once

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace s950
{
    /*
     * One sample's audio, ready to be played.
     *
     * Normalised to +-1 once, when the patch is built, rather than per sample in the render
     * loop. The engine never sees a disk image: whoever builds the patch does the reading,
     * which is what lets the same engine sit behind a WinForms window, a plugin, or a test.
     *
     * Held by shared_ptr, which the C# did not need and this does. A voice reads this audio
     * on the audio thread; the message thread may replace the patch at any moment. Sharing
     * ownership means the buffer a voice is halfway through cannot be freed under it - the
     * last holder releases it, and if that turns out to be a voice, see the note on
     * Engine::setPatch about where that release happens.
     */
    struct Sound
    {
        std::string        name;

        /*
         * A hash of the bytes this was decoded from, so a rebuilt programme can tell a
         * sample that is the same from one that merely has the same name - the Synth tab
         * re-renders "OSC1" with different audio under the same name every time a wave knob
         * moves. Reusing an unchanged sample's Sound is what lets a held note follow an edit
         * in place (Voice::adopt keeps a note only on the sound it started with).
         */
        unsigned long long fingerprint = 0;
        std::vector<float> audio;            // -1 .. +1
        int                sourceRate = 40000;   // hertz, as stored on the disk
        double             rootPitch  = 60.0;    // the MIDI note at which it plays at sourceRate

        /*
         * The loop, in frames. The machine plays end-length .. end round and round, so the
         * start follows from the length rather than from the stored start - which is simply
         * zero in 250 of the library's 324 looped samples.
         */
        int  loopFrom = 0, loopTo = 0;

        /*
         * Loop mode 'A': forward, then backward, then forward again.
         *
         * MEASURED, run 13. A loop of N frames comes round every 2N, not 2N-2 - the frame at
         * each end is played twice as the direction turns rather than once. Four lengths from
         * 20 frames to 128 were played on the hardware and every one autocorrelated at exactly
         * 2N with a coefficient of 0.9996 or better, while the forward controls gave exactly N.
         * On a short loop that is the difference between a tenth of a semitone right and wrong,
         * which is why it was measured rather than chosen.
         *
         * 18 of the 1110 samples on the real disks use it, and they are the ones where it
         * shows: cymbals, sustained strings and piano, and ambient beds - WIND, RAIN, THUNDER,
         * WATER, INSECTS, TRAFFIC, JET.
         */
        bool alternates = false;
        bool loops    = false;

        /// How many samples of the loop join were crossfaded, or 0 for a plain splice.
        int loopSmoothed = 0;
    };

    using SoundPtr = std::shared_ptr<const Sound>;

    /*
     * One keygroup, flattened into what a voice needs.
     *
     * The raw 0..99 panel bytes are kept rather than seconds and hertz, because the mappings
     * from one to the other are the measured part and belong in one place - Cal.
     */
    struct KeygroupPatch
    {
        int lowKey = 0, highKey = 127;

        /*
         * Which velocities this entry answers to.
         *
         * A keygroup holds up to two VELOCITY zones - alternatives, not layers. The switch
         * in byte 2 is the boundary: softer than it plays zone 1, at it or harder plays
         * zone 2, and a switch of 128 is how the panel says "no second zone", since no
         * velocity can reach it.
         *
         * Sounding both was the C# engine's worst bug. 74 of the library's 168 two-zone
         * keygroups name the SAME sample in both, so playing them together put two copies of
         * one sample on top of each other - and 121 of the 168 set the switch to 128, so
         * their second zone should never have sounded at all. Together they rang like a bell
         * over every note. Keep the ranges; do not "simplify" this into layering.
         */
        int velocityFrom = 0, velocityTo = 127;

        /// Which keygroup this came from. Only the checks care, and they care a lot: two
        /// entries from the SAME keygroup must never answer one strike.
        int keygroupIndex = -1;

        SoundPtr sound;

        int  vcaAttack = 0, vcaDecay = 0, vcaSustain = 99, vcaRelease = 0;
        int  vcfAttack = 0, vcfDecay = 0, vcfSustain = 99, vcfRelease = 0;
        bool vcfWritten = true;          // an S900 program leaves the four bytes as spaces
        int  vcfAmount  = 0;             // signed, -50..+50

        int velToFilter = 0, keyToFilter = 0, velToLoudness = 0;

        /// Byte 9: how far a hard strike shortens the attack. See cal::velocityAttackByte.
        int velToAttack = 0;

        /// Byte 10, signed: which way velocity moves the release. See cal::velocityReleaseByte.
        int velToRelease = 0;

        /// Byte 18 bit 4. Clear means every note releases as though struck at velocity 1,
        /// which is not the same as no effect at all.
        bool velocityReleaseOn = false;

        /// WARP, bytes 12/13/14: a pitch bend at note-on. See cal::warpRatio. 13 is the depth
        /// and is signed; 12 is how far velocity scales it, 0 meaning always full; 14 is the
        /// time constant.
        int warpVelocity = 0, warpDepth = 0, warpTime = 0;

        /// Byte 19 + 1: 0 ALL, 1..8 the individual outputs, 9 LEFT, 10 RIGHT.
        /// See cal::outputGains.
        int outputPort = 0;

        int  lfoDelay = 0, lfoRate = 0, lfoDepth = 0, lfoModwheelDepth = 0;

        /// Byte 21: how far channel pressure scales the LFO's depth, exactly as byte 22
        /// does for the modwheel. Measured on the aftertouch run - see Engine::startNote.
        int  lfoAftertouchDepth = 0;

        bool lfoDesync = true;           // set in 1652 keygroups of 1908

        int    zoneFilter    = 99;       // zone 1's cutoff, 0..99
        int    zoneLoudness  = 0;        // signed trim, in the machine's decibel count
        double zoneTranspose = 0.0;      // semitones, including the fine part

        bool constantPitch = false;
        bool oneShot       = false;

        /*
         * The four VCF bytes read as ASCII spaces means the program was written by an S900,
         * which had no filter envelope at all - not an envelope that happens to be set to
         * 32. In the library this never bites, because none of the 1684 keygroups with blank
         * bytes sets an amount, but it would the moment one did.
         */
        static bool looksWritten (int a, int d, int s, int r)
        {
            return ! (a == 0x20 && d == 0x20 && s == 0x20 && r == 0x20);
        }
    };

    /// A program: the keygroups a note might land in.
    struct Patch
    {
        std::string                name;
        std::vector<KeygroupPatch> keygroups;

        /*
         * Program header byte 21: fade overlapping keygroups into one another rather than
         * sounding both at full level. See cal::XfadeDb.
         */
        bool positionalCrossfade = false;

        /*
         * Every entry this note and velocity should sound.
         *
         * Plural on purpose, but for one reason only: overlapping KEYGROUPS do layer, and
         * the machine sounds all of them. The two zones INSIDE a keygroup do not - they are
         * velocity alternatives, and only one of them answers any given strike.
         *
         * Fills a caller's vector rather than returning one, because the caller is the audio
         * thread and it keeps one around: allocating here would be a dropout waiting for a
         * busy moment.
         */
        void matching (int note, int velocity, std::vector<const KeygroupPatch*>& into) const
        {
            into.clear();

            for (const auto& k : keygroups)
            {
                if (k.sound == nullptr || k.sound->audio.empty())
                    continue;

                const int lo = std::min (k.lowKey, k.highKey);
                const int hi = std::max (k.lowKey, k.highKey);

                if (note < lo || note > hi)                       continue;
                if (velocity < k.velocityFrom || velocity > k.velocityTo) continue;

                into.push_back (&k);
            }
        }
    };

    using PatchPtr = std::shared_ptr<const Patch>;
}
