#pragma once

#include "Patch.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s950
{
    /*
     * Every setting of a keygroup that can be changed in place, one byte each.
     *
     * The byte positions and encodings are the C# Studio's (AkaiS950Studio/Editors.cs,
     * KeygroupEditor), so a disk edited here reads the same there and on the machine:
     *
     *   - most are 0..99, stored as they are;
     *   - velocity to release, warp depth, the VCF amount, and a zone's transpose and
     *     loudness are signed, -50..+50, in two's complement;
     *   - the output port is the panel's 0..10 stored one LOWER, so ALL is 0xFF;
     *   - a zone's fine tune is 0..255, in 256ths of a semitone upward;
     *   - the four flags are single bits of byte 18, and change one at a time, so the bits
     *     nobody has decoded yet survive every edit.
     */
    enum class KeygroupParam
    {
        HighKey, LowKey, VelocitySwitch,
        VcaAttack, VcaDecay, VcaSustain, VcaRelease,
        VelToFilter, KeyToFilter, VelToAttack, VelToRelease, VelToLoudness,
        WarpVelocity, WarpDepth, WarpTime,
        LfoDelay, LfoRate, LfoDepth, LfoAftertouch, LfoModwheel,
        OutputPort,
        VcfAmount, VcfAttack, VcfDecay, VcfSustain, VcfRelease,
        Zone1Fine, Zone1Transpose, Zone1Filter, Zone1Loudness,
        Zone2Fine, Zone2Transpose, Zone2Filter, Zone2Loudness,
        ConstantPitch, LfoDesync, OneShot, VelocityReleaseOn,
        Count
    };

    struct KeygroupParamInfo
    {
        enum Kind { Unsigned, Signed, Port, Bit };

        const char* name;
        int  offset;        // within the 70-byte keygroup record
        int  lo, hi;        // the panel's range, which writing clamps to
        Kind kind;
        int  mask;          // Bit only
    };

    /// Where a setting lives and how it is written. The one table everything reads.
    const KeygroupParamInfo& keygroupParamInfo (KeygroupParam p);

    /*
     * An 800K Akai S900/S950 floppy: 80 cylinders x 2 heads x 5 sectors of 1024 bytes.
     *
     * Block 0 holds a 64-entry directory at 0x000 and a 16-bit allocation table at 0x600;
     * file data starts at block 4. A file's blocks are a chain through that table rather
     * than a run, so a file is not contiguous and cannot be read as one.
     *
     * The reading half of AkaiS950List, ported - and one narrow kind of writing.
     *
     * A keygroup's SETTINGS can be changed where they stand: an envelope time, a filter, an
     * LFO rate, a key range. Each is one byte at a fixed place in the programme, and changing
     * its value moves nothing else. That is all this writes. Adding or removing a file, a
     * keygroup or a sample stays in the Studio, where a human is watching - because that is
     * where the rules that make writing dangerous live: that the directory must stay
     * contiguous, and that zone pointers are positions that have to be recomputed rather
     * than shifted. A value edit touches neither, and setKeygroupParam refuses to write
     * anywhere but inside one keygroup record of one programme.
     *
     * The encodings are the C# Studio's, field for field - see KeygroupParam.
     */
    class Disk
    {
    public:
        static constexpr int BlockSize  = 1024;
        static constexpr int DirOffset  = 0x000;
        static constexpr int DirEntries = 64;
        static constexpr int EntrySize  = 24;
        static constexpr int FatOffset  = 0x600;
        static constexpr int FatEnd     = 0x8000;   // end-of-chain marker
        static constexpr int HeaderSize = 60;       // per-file header before the payload

        static constexpr int ProgHeaderSize     = 38;
        static constexpr int KeygroupSize       = 70;
        static constexpr int KeygroupNameOffset = 24;   // zone 1's sample name
        static constexpr int KeygroupZoneStride = 22;   // zone 2's is 22 bytes further on

        /// One 24-byte entry in the directory.
        struct Entry
        {
            int         slot = 0;
            std::string name;
            char        type = 0;        // 'P' program, 'S' sample, 'D' drum set, 'O' overall
            int         length = 0;      // bytes, including the 60-byte file header
            int         startBlock = 0;
            int         chainBlocks = 0;
            bool        chainOk = false; // the allocation covers the declared length

            // Samples only, from the file's own 60-byte header.
            long long sampleCount = 0;
            int       sampleRate = 0;
            int       tuning = 0;        // 16ths of a semitone, C3 = 960
            char      loopMode = 'O';    // 'O' one-shot, 'L' loop, 'A' alternating
            char      loopDirection = 'N';
            long long loopStart = 0, loopEnd = 0, loopLength = 0;
            int       loudness = 0;      // signed

            int    nominalPitch() const { return tuning / 16; }
            int    finePitch()    const { return tuning % 16; }
            double seconds()      const { return sampleRate > 0 ? (double) sampleCount / sampleRate : 0.0; }
        };

        /// One zone of a keygroup: which sample, and how it is trimmed.
        struct Zone
        {
            std::string name;
            int         pointer = 0;
            int         fine = 0;
            int         transpose = 0;   // signed
            int         filter = 99;
            int         loudness = 0;    // signed

            double pitchOffset() const { return transpose + fine / 256.0; }

            /*
             * "2 SAMPLE" is what the panel leaves in a zone it is not using, and a pointer
             * of zero says the same thing. Reading those as a sample name is how the editor
             * came to show leftovers as though a second zone were loaded.
             */
            bool inUse() const { return ! name.empty() && name != "2 SAMPLE" && pointer != 0; }
        };

        /// One 70-byte keygroup record.
        struct Keygroup
        {
            int index = 0;
            int highKey = 127, lowKey = 0;
            int velocitySwitch = 128;

            int vcaAttack = 0, vcaDecay = 0, vcaSustain = 99, vcaRelease = 0;
            int vcfAttack = 0, vcfDecay = 0, vcfSustain = 99, vcfRelease = 0;
            int vcfAmount = 0;           // signed

            int velToFilter = 0, keyToFilter = 0, velToLoudness = 0;
            int velToAttack = 0, velToRelease = 0;

            /*
             * WARP, bytes 12 to 14: a pitch bend at note-on that decays back to pitch. Byte 13
             * is the DEPTH and is signed; byte 12 is how far velocity scales it, with 0 meaning
             * always full; byte 14 is the time constant. See cal::warpRatio.
             */
            int warpVelocity = 0, warpDepth = 0, warpTime = 0;

            /*
             * Byte 19: which output the keygroup goes to. The panel value, which the byte
             * stores one lower - so ALL is -1, the default in 1617 of 1908 keygroups.
             *
             *     0 ALL,  1..8 MONO 1 to 8,  9 LEFT,  10 RIGHT
             */
            int outputPort = 0;

            int lfoDelay = 0, lfoRate = 0, lfoDepth = 0;
            int lfoAftertouchDepth = 0, lfoModwheelDepth = 0;

            int flags = 0;

            Zone zone1, zone2;

            bool constantPitch()    const { return (flags & 0x01) != 0; }
            bool lfoDesync()        const { return (flags & 0x04) != 0; }
            bool oneShot()          const { return (flags & 0x08) != 0; }

            /*
             * Bit 4: the ON/OFF beside Release on the velocity page, found by diffing a disk
             * saved either side of flipping it. It enables velToRelease - with it clear, every
             * note is released as though its velocity were 1, which is not the same as no
             * effect. Clear in all 1908 keygroups of the library.
             */
            bool velocityReleaseOn() const { return (flags & 0x10) != 0; }
            bool hasSecondZone()    const { return zone2.inUse(); }
        };

        // -------------------------------------------------------------------- loading

        /// Read an image from disk. False, with `error` set, if it is not one.
        bool loadFile (const std::string& path, std::string& error);

        /// The same from bytes already in hand - which is how a plugin will restore one
        /// out of a host's saved project.
        bool loadBytes (std::string name, std::vector<unsigned char> bytes, std::string& error);

        const std::string&        getName()    const { return source; }
        const std::vector<Entry>& getEntries() const { return entries; }

        /// True if this came out of an .hfe rather than a plain sector image.
        bool wasHfe() const { return fromHfe; }

        /*
         * The decoded sectors, as a plain image.
         *
         * For putting a disk somewhere it can be got back from - a host's saved project,
         * say. Always the sectors, never the .hfe it may have arrived in: decoding is
         * deterministic and one-way, so keeping the result means a reload does no MFM work
         * and cannot come out differently.
         */
        const std::vector<unsigned char>& getImage() const { return image; }

        /*
         * How the recovery went, for an .hfe. Both zero for a plain image.
         *
         * Worth showing rather than hiding: an archived floppy is thirty years old, and a
         * disk that reads with three bad sectors is a different thing from one that reads
         * cleanly - especially if it is about to be played into a recording.
         */
        int getBadCrcSectors()  const { return badCrcSectors; }
        int getMissingSectors() const { return missingSectors; }

        /// The entry of that name and type, or nullptr.
        const Entry* find (const std::string& name, char type) const;

        // -------------------------------------------------------------------- reading

        /// A file's bytes, following its chain through the allocation table.
        std::vector<unsigned char> readFile (const Entry& e) const;

        /// A sample as stored: signed 12-bit values, -2048..2047.
        std::vector<short> sampleWords12 (const Entry& e) const;

        /// A program's keygroup records, in order.
        std::vector<Keygroup> keygroups (const Entry& program) const;

        static int keygroupCount (const Entry& program);

        // ---------------------------------------------------------- ready to be played

        /*
         * Turn a programme into something the engine can play.
         *
         * The same job Instrument.SetProgram does in the C# editor, and the same rule at
         * the centre of it: the two zones of a keygroup are velocity ALTERNATIVES, not
         * layers. They split the range at the switch, and a switch of 128 leaves zone 1 the
         * whole of it, which is how the panel turns the second zone off.
         */
        /*
         * `reuse` is the patch this one replaces, if any. A sample it already holds is taken
         * from it by name rather than decoded again - which makes rebuilding after a settings
         * edit cheap, and, more to the point, keeps the SAME sound object: Voice::adopt only
         * follows a change onto a held note when the sound it is playing is the one it had.
         * An edit that decoded afresh would leave every held note deaf to it.
         */
        PatchPtr buildPatch (const Entry& program, const Patch* reuse = nullptr) const;

        // ------------------------------------------------------------ changing settings

        /*
         * One keygroup setting, as the panel shows it.
         *
         * A filter envelope an S900 left blank - bytes 34 to 37 all spaces - reads as the
         * flat one the engine gives it, 0/0/99/0, rather than as 32s.
         */
        int getKeygroupParam (const Entry& program, int keygroup, KeygroupParam p) const;

        /*
         * Change one keygroup setting, in place. The value is clamped to the panel's range.
         *
         * Writes one byte, or for a blank filter envelope all four of it: the first edit to
         * any stage writes the flat 0/0/99/0 under the one being set, as the Studio does, so
         * the envelope becomes a real one rather than three spaces and a number.
         *
         * False if nothing could be written - no such keygroup, or the programme's chain
         * does not reach that far. True otherwise, whether or not the value changed.
         */
        bool setKeygroupParam (const Entry& program, int keygroup, KeygroupParam p, int value);

        /// True if an S900 left this keygroup's filter envelope blank.
        bool vcfBlank (const Entry& program, int keygroup) const;

        /// Bumped by every write, so a caller can tell a disk has changed since it looked.
        int getRevision() const { return revision; }

        /// Where byte `offset` of keygroup `keygroup` is in the image. False if nowhere.
        /// Public so a check can say exactly which bytes an edit was allowed to touch.
        bool keygroupByteAt (const Entry& program, int keygroup, int offset, std::size_t& at) const;

    private:
        /// Byte `offset` of a file, through its chain. False past the end of the file.
        bool fileByteAt (const Entry& e, int offset, std::size_t& at) const;

        int revision = 0;

        int  fat (int block) const;
        std::vector<int> chain (int start) const;
        void parseDirectory();
        void readSampleHeader (Entry& e) const;

        int  totalBlocks() const { return static_cast<int> (image.size()) / BlockSize; }

        unsigned int u32 (std::size_t at) const;
        unsigned int u16 (std::size_t at) const;

        static std::string cleanName (const unsigned char* b, std::size_t at);
        static Zone parseZone (const unsigned char* raw, int at);

        /// One sample, decoded and normalised, ready for a voice to read.
        SoundPtr soundFor (const Entry& sample) const;

        std::string                source;
        std::vector<unsigned char> image;
        std::vector<Entry>         entries;

        bool fromHfe = false;
        int  badCrcSectors = 0, missingSectors = 0;
    };
}
