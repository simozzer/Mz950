#include "Disk.h"
#include "Hfe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>

namespace s950
{
    // ------------------------------------------------------------------------ loading

    bool Disk::loadFile (const std::string& path, std::string& error)
    {
        std::ifstream in (path, std::ios::binary);
        if (! in)
        {
            error = "could not open " + path;
            return false;
        }

        std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (in)),
                                          std::istreambuf_iterator<char>());
        return loadBytes (path, std::move (bytes), error);
    }

    bool Disk::loadBytes (std::string name, std::vector<unsigned char> bytes, std::string& error)
    {
        fromHfe        = false;
        badCrcSectors  = 0;
        missingSectors = 0;

        /*
         * An .hfe is not a picture of the sectors - it is a recording of the flux a floppy
         * controller would see reading them. Getting an image out of one means doing what
         * the controller does, which is what Hfe.cpp is for.
         */
        if (hfe::looksLikeHfe (bytes))
        {
            auto recovered = hfe::extract (bytes, badCrcSectors, missingSectors);

            if (recovered.empty())
            {
                error = "that HFE image could not be decoded";
                return false;
            }

            fromHfe = true;
            bytes   = std::move (recovered);
        }

        if (bytes.size() != 819200 && bytes.size() != 1638400)
        {
            error = "not an 800K or 1600K image (" + std::to_string (bytes.size()) + " bytes)";
            return false;
        }

        source = std::move (name);
        image  = std::move (bytes);
        parseDirectory();
        return true;
    }

    unsigned int Disk::u16 (std::size_t at) const
    {
        if (at + 1 >= image.size()) return 0;
        return static_cast<unsigned int> (image[at]) | (static_cast<unsigned int> (image[at + 1]) << 8);
    }

    unsigned int Disk::u32 (std::size_t at) const
    {
        if (at + 3 >= image.size()) return 0;
        return  static_cast<unsigned int> (image[at])
             | (static_cast<unsigned int> (image[at + 1]) << 8)
             | (static_cast<unsigned int> (image[at + 2]) << 16)
             | (static_cast<unsigned int> (image[at + 3]) << 24);
    }

    std::string Disk::cleanName (const unsigned char* b, std::size_t at)
    {
        std::string s;
        s.reserve (10);

        for (int i = 0; i < 10; ++i)
        {
            const unsigned char c = b[at + i];
            s.push_back (c >= 0x20 && c < 0x7F ? static_cast<char> (c) : ' ');
        }

        while (! s.empty() && s.back() == ' ')
            s.pop_back();

        return s;
    }

    int Disk::fat (int block) const
    {
        const std::size_t at = static_cast<std::size_t> (FatOffset) + static_cast<std::size_t> (block) * 2;
        if (at + 1 >= image.size()) return FatEnd;
        return static_cast<int> (u16 (at));
    }

    /*
     * A file's blocks, in order.
     *
     * `seen` is not caution for its own sake: a damaged allocation table can point a block
     * at itself or back into the chain, and without it this would not return.
     */
    std::vector<int> Disk::chain (int start) const
    {
        std::vector<int> blocks;
        std::set<int>    seen;

        int b = start;
        while (b != FatEnd && b >= 0 && b < totalBlocks() && seen.insert (b).second)
        {
            blocks.push_back (b);
            b = fat (b);
        }

        return blocks;
    }

    void Disk::parseDirectory()
    {
        entries.clear();

        for (int i = 0; i < DirEntries; ++i)
        {
            const std::size_t o = static_cast<std::size_t> (DirOffset) + static_cast<std::size_t> (i) * EntrySize;
            if (o + EntrySize > image.size()) break;
            if (image[o] == 0x00) continue;                       // free slot

            const char type = static_cast<char> (image[o + 16]);
            if (type != 'P' && type != 'S' && type != 'D' && type != 'O')
                continue;                                         // not a file entry

            const int len   = static_cast<int> (image[o + 17])
                            | (static_cast<int> (image[o + 18]) << 8)
                            | (static_cast<int> (image[o + 19]) << 16);
            const int start = static_cast<int> (u16 (o + 20));

            if (start < 0 || start >= totalBlocks()) continue;

            Entry e;
            e.slot       = i;
            e.name       = cleanName (image.data(), o);
            e.type       = type;
            e.length     = len;
            e.startBlock = start;

            e.chainBlocks = static_cast<int> (chain (start).size());
            const int needed = (len + BlockSize - 1) / BlockSize;
            e.chainOk = e.chainBlocks >= needed;

            if (type == 'S') readSampleHeader (e);

            entries.push_back (e);
        }
    }

    void Disk::readSampleHeader (Entry& e) const
    {
        const std::size_t o = static_cast<std::size_t> (e.startBlock) * BlockSize;
        if (o + HeaderSize > image.size()) return;

        e.sampleCount   = u32 (o + 0x10);
        e.sampleRate    = static_cast<int> (u16 (o + 0x14));
        e.tuning        = static_cast<int> (u16 (o + 0x16));
        e.loudness      = static_cast<short> (u16 (o + 0x18));
        e.loopMode      = static_cast<char> (image[o + 0x1A]);
        e.loopEnd       = u32 (o + 0x1C);
        e.loopStart     = u32 (o + 0x20);
        e.loopLength    = u32 (o + 0x24);
        e.loopDirection = static_cast<char> (image[o + 0x2B]);
    }

    const Disk::Entry* Disk::find (const std::string& name, char type) const
    {
        for (const auto& e : entries)
            if (e.type == type && e.name == name)
                return &e;

        return nullptr;
    }

    // ------------------------------------------------------------ changing settings

    const KeygroupParamInfo& keygroupParamInfo (KeygroupParam p)
    {
        using K = KeygroupParamInfo;

        // In the order of the enum. The ranges and kinds are the Studio's KeygroupEditor.
        static const KeygroupParamInfo table[] =
        {
            { "High key",            0,   0, 127, K::Unsigned, 0 },
            { "Low key",             1,   0, 127, K::Unsigned, 0 },
            { "Velocity switch",     2,   1, 128, K::Unsigned, 0 },
            { "VCA attack",          3,   0,  99, K::Unsigned, 0 },
            { "VCA decay",           4,   0,  99, K::Unsigned, 0 },
            { "VCA sustain",         5,   0,  99, K::Unsigned, 0 },
            { "VCA release",         6,   0,  99, K::Unsigned, 0 },
            { "Velocity to filter",  7,   0,  99, K::Unsigned, 0 },
            { "Key to filter",       8,   0,  99, K::Unsigned, 0 },
            { "Velocity to attack",  9,   0,  99, K::Unsigned, 0 },
            { "Velocity to release", 10, -50, 50, K::Signed,   0 },
            { "Velocity to loudness",11,  0,  99, K::Unsigned, 0 },
            { "Warp velocity",       12,  0,  99, K::Unsigned, 0 },
            { "Warp depth",          13, -50, 50, K::Signed,   0 },
            { "Warp time",           14,  0,  99, K::Unsigned, 0 },
            { "LFO delay",           15,  0,  99, K::Unsigned, 0 },
            { "LFO rate",            16,  0,  99, K::Unsigned, 0 },
            { "LFO depth",           17,  0,  99, K::Unsigned, 0 },
            { "LFO from aftertouch", 21,  0,  50, K::Unsigned, 0 },
            { "LFO from modwheel",   22,  0,  50, K::Unsigned, 0 },
            { "Output",              19,  0,  10, K::Port,     0 },
            { "VCF amount",          23, -50, 50, K::Signed,   0 },
            { "VCF attack",          34,  0,  99, K::Unsigned, 0 },
            { "VCF decay",           35,  0,  99, K::Unsigned, 0 },
            { "VCF sustain",         36,  0,  99, K::Unsigned, 0 },
            { "VCF release",         37,  0,  99, K::Unsigned, 0 },
            { "Soft fine",           42,  0, 255, K::Unsigned, 0 },
            { "Soft transpose",      43, -50, 50, K::Signed,   0 },
            { "Soft filter",         44,  0,  99, K::Unsigned, 0 },
            { "Soft loudness",       45, -50, 50, K::Signed,   0 },
            { "Loud fine",           64,  0, 255, K::Unsigned, 0 },
            { "Loud transpose",      65, -50, 50, K::Signed,   0 },
            { "Loud filter",         66,  0,  99, K::Unsigned, 0 },
            { "Loud loudness",       67, -50, 50, K::Signed,   0 },
            { "Constant pitch",      18,  0,   1, K::Bit,      0x01 },
            { "LFO desync",          18,  0,   1, K::Bit,      0x04 },
            { "One shot",            18,  0,   1, K::Bit,      0x08 },
            { "Velocity release on", 18,  0,   1, K::Bit,      0x10 },
        };

        static_assert (sizeof (table) / sizeof (table[0])
                           == static_cast<std::size_t> (KeygroupParam::Count),
                       "one row per KeygroupParam, in order");

        return table[static_cast<int> (p)];
    }

    bool Disk::fileByteAt (const Entry& e, int offset, std::size_t& at) const
    {
        if (offset < 0 || offset >= e.length)
            return false;

        const auto blocks = chain (e.startBlock);
        const auto bi = static_cast<std::size_t> (offset / BlockSize);

        if (bi >= blocks.size())
            return false;

        at = static_cast<std::size_t> (blocks[bi]) * BlockSize
           + static_cast<std::size_t> (offset % BlockSize);

        return at < image.size();
    }

    /*
     * Mapped one byte at a time, never as a run: a programme is chained through the
     * allocation table, and a 70-byte keygroup can straddle the end of one block and the
     * start of another that is nowhere near it.
     */
    bool Disk::keygroupByteAt (const Entry& program, int keygroup, int offset, std::size_t& at) const
    {
        if (program.type != 'P' || offset < 0 || offset >= KeygroupSize)
            return false;

        if (keygroup < 0 || keygroup >= keygroupCount (program))
            return false;

        return fileByteAt (program, ProgHeaderSize + keygroup * KeygroupSize + offset, at);
    }

    bool Disk::vcfBlank (const Entry& program, int keygroup) const
    {
        for (int o = 34; o <= 37; ++o)
        {
            std::size_t at = 0;
            if (! keygroupByteAt (program, keygroup, o, at) || image[at] != 0x20)
                return false;
        }

        return true;
    }

    int Disk::getKeygroupParam (const Entry& program, int keygroup, KeygroupParam p) const
    {
        const auto& info = keygroupParamInfo (p);

        if (p == KeygroupParam::VcfAttack || p == KeygroupParam::VcfDecay
            || p == KeygroupParam::VcfSustain || p == KeygroupParam::VcfRelease)
        {
            if (vcfBlank (program, keygroup))
                return p == KeygroupParam::VcfSustain ? 99 : 0;
        }

        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, info.offset, at))
            return 0;

        const unsigned char b = image[at];

        switch (info.kind)
        {
            case KeygroupParamInfo::Signed: return static_cast<signed char> (b);
            case KeygroupParamInfo::Port:   return b == 0xFF ? 0 : b + 1;
            case KeygroupParamInfo::Bit:    return (b & info.mask) != 0 ? 1 : 0;
            default:                        return b;
        }
    }

    bool Disk::setKeygroupParam (const Entry& program, int keygroup, KeygroupParam p, int value)
    {
        const auto& info = keygroupParamInfo (p);

        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, info.offset, at))
            return false;

        const int v = value < info.lo ? info.lo : (value > info.hi ? info.hi : value);

        // A blank S900 filter envelope becomes the flat one first, as the Studio does it.
        if ((p == KeygroupParam::VcfAttack || p == KeygroupParam::VcfDecay
             || p == KeygroupParam::VcfSustain || p == KeygroupParam::VcfRelease)
            && vcfBlank (program, keygroup))
        {
            const unsigned char flat[] = { 0, 0, 99, 0 };

            for (int o = 0; o < 4; ++o)
            {
                std::size_t stage = 0;
                if (keygroupByteAt (program, keygroup, 34 + o, stage))
                    image[stage] = flat[o];
            }
        }

        unsigned char b = image[at];

        switch (info.kind)
        {
            case KeygroupParamInfo::Signed:
                b = static_cast<unsigned char> (static_cast<signed char> (v));
                break;

            case KeygroupParamInfo::Port:
                b = static_cast<unsigned char> (v == 0 ? 0xFF : v - 1);
                break;

            case KeygroupParamInfo::Bit:
                b = static_cast<unsigned char> (v != 0 ? (b | info.mask) : (b & ~info.mask));
                break;

            default:
                b = static_cast<unsigned char> (v);
                break;
        }

        image[at] = b;
        ++revision;
        return true;
    }

    // ------------------------------------------------------------- making a disk

    namespace
    {
        /// A programme's 38-byte header as a library disk has it: the name, then the load
        /// address 0xC5F6 at 18..19, keygroup count at 23, MIDI programme at 26.
        const unsigned char ProgramTemplate[38] = {
            67, 79, 77, 32, 32, 32, 32, 32, 32, 32, 0, 32, 32, 32, 32, 32, 0, 0, 246, 197,
            0, 0, 255, 1, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };

        /// One keygroup as the machine writes a fresh one: the whole keyboard, no second
        /// zone, every setting at the panel's default. The four VCF bytes are spaces, which
        /// is "no filter envelope"; a caller that wants one writes all four.
        const unsigned char KeygroupTemplate[70] = {
            127, 24, 128, 0, 80, 99, 30, 10, 50, 0, 0, 30, 0, 0, 99, 64, 64, 0, 4, 255,
            0, 0, 50, 0, 84, 79, 78, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
            0, 211, 0, 0, 99, 0, 50, 32, 83, 65, 77, 80, 76, 69, 32, 32, 32, 32, 32, 32,
            32, 32, 0, 0, 0, 0, 99, 0, 0, 0
        };

        const char* const UnusedZone = "2 SAMPLE";

        std::string trimmed (std::string s)
        {
            while (! s.empty() && s.back() == ' ') s.pop_back();
            std::size_t i = 0;
            while (i < s.size() && s[i] == ' ') ++i;
            return s.substr (i);
        }

        bool sameName (const std::string& a, const std::string& b)
        {
            const auto x = trimmed (a), y = trimmed (b);
            if (x.size() != y.size()) return false;
            for (std::size_t i = 0; i < x.size(); ++i)
                if (std::toupper (static_cast<unsigned char> (x[i])) != std::toupper (static_cast<unsigned char> (y[i])))
                    return false;
            return true;
        }

        int clip12 (int v)
        {
            if (v > 2047) v = 2047;
            if (v < -2048) v = -2048;
            return v & 0xFFF;
        }

        /// The sampler's RAM a sample takes: two bytes a word, in sixteens.
        int ramSize (long long words) { return static_cast<int> ((2 * words + 15) / 16 * 16); }

        /// 10-byte loop descriptors a sample consumes, by loop mode and whole 128K pages.
        int loopRecords (long long words, char loopMode)
        {
            const int pages = static_cast<int> (2 * words / 131072);
            switch (loopMode)
            {
                case 'L': return 3 + pages;
                case 'A': return 3 * (1 + pages);
                default:  return 2 + pages;      // 'O', one-shot
            }
        }

        void put16 (std::vector<unsigned char>& b, std::size_t at, unsigned v)
        {
            b[at] = static_cast<unsigned char> (v & 0xFF);
            b[at + 1] = static_cast<unsigned char> ((v >> 8) & 0xFF);
        }

        void put32 (std::vector<unsigned char>& b, std::size_t at, unsigned long v)
        {
            for (int i = 0; i < 4; ++i)
                b[at + static_cast<std::size_t> (i)] = static_cast<unsigned char> ((v >> (8 * i)) & 0xFF);
        }

        void putName (unsigned char* at, const std::string& clean)
        {
            for (int i = 0; i < 10; ++i)
                at[i] = static_cast<unsigned char> (i < static_cast<int> (clean.size()) ? clean[static_cast<std::size_t> (i)] : ' ');
        }
    }

    Disk Disk::blank (std::string name)
    {
        // An S950 disk is empty when its directory and allocation table are zero, so an
        // image of nothing but zeroes is already a formatted blank.
        Disk d;
        std::string error;
        d.loadBytes (std::move (name), std::vector<unsigned char> (819200, 0), error);
        return d;
    }

    /// A filename the S900/S950 will accept: up to 10 printable characters, upper-cased.
    std::string Disk::normaliseName (const std::string& name)
    {
        std::string s;
        for (char c : name)
        {
            if (s.size() == 10) break;
            const auto u = static_cast<unsigned char> (std::toupper (static_cast<unsigned char> (c)));
            s.push_back (u >= 0x20 && u < 0x7F ? static_cast<char> (u) : ' ');
        }
        while (! s.empty() && s.back() == ' ') s.pop_back();
        return s.empty() ? "UNTITLED" : s;
    }

    std::string Disk::normaliseNameFor (const std::string& name) { return normaliseName (name); }

    void Disk::setFat (int block, int value)
    {
        putU16 (static_cast<std::size_t> (FatOffset) + static_cast<std::size_t> (block) * 2,
                static_cast<unsigned> (value));
    }

    void Disk::putU16 (std::size_t at, unsigned value)
    {
        if (at + 1 >= image.size()) return;
        image[at]     = static_cast<unsigned char> (value & 0xFF);
        image[at + 1] = static_cast<unsigned char> ((value >> 8) & 0xFF);
    }

    void Disk::pokeFile (const Entry& e, int offset, unsigned char value)
    {
        std::size_t at = 0;
        if (fileByteAt (e, offset, at)) { image[at] = value; ++revision; }
    }

    const Disk::Entry* Disk::entryInSlot (int slot) const
    {
        for (const auto& e : entries)
            if (e.slot == slot) return &e;
        return nullptr;
    }

    int Disk::sampleIndex (const std::string& name) const
    {
        int i = 0;
        for (const auto& e : entries)
        {
            if (e.type != 'S') continue;
            if (sameName (e.name, name)) return i;
            ++i;
        }
        return -1;
    }

    /*
     * A new file into free blocks and a directory slot at or after `minSlot`. Nothing
     * already on the disk moves. Samples must land after the last sample, so their RAM
     * addresses follow on; that is what minSlot is for.
     */
    bool Disk::addFile (const std::string& name, char type, const std::vector<unsigned char>& contents,
                        int minSlot, int& slotOut, std::string& error)
    {
        const std::string clean = normaliseName (name);

        // Names are unique within a type, not across the disk: library images routinely
        // name a programme after the sample it plays.
        for (const auto& x : entries)
            if (x.type == type && sameName (x.name, clean))
            {
                error = "'" + clean + "' is already used by another file of that type";
                return false;
            }

        const int need = std::max (1, static_cast<int> ((contents.size() + BlockSize - 1) / BlockSize));
        std::vector<int> free;
        for (int b = 4; b < totalBlocks() && static_cast<int> (free.size()) < need; ++b)
            if (fat (b) == 0) free.push_back (b);

        if (static_cast<int> (free.size()) < need)
        {
            error = "not enough room on the disk: " + std::to_string (need) + " blocks needed";
            return false;
        }

        int slot = -1;
        for (int i = std::max (0, minSlot); i < DirEntries; ++i)
            if (image[static_cast<std::size_t> (DirOffset + i * EntrySize)] == 0x00) { slot = i; break; }

        if (slot < 0)
        {
            error = "the directory is full: 64 files is the limit";
            return false;
        }

        for (int i = 0; i < need; ++i)
        {
            const std::size_t off  = static_cast<std::size_t> (free[static_cast<std::size_t> (i)]) * BlockSize;
            const std::size_t from = static_cast<std::size_t> (i) * BlockSize;
            const std::size_t take = std::min (static_cast<std::size_t> (BlockSize),
                                               contents.size() > from ? contents.size() - from : 0);

            std::fill (image.begin() + static_cast<long> (off), image.begin() + static_cast<long> (off + BlockSize),
                       static_cast<unsigned char> (0));
            if (take > 0)
                std::copy (contents.begin() + static_cast<long> (from), contents.begin() + static_cast<long> (from + take),
                           image.begin() + static_cast<long> (off));

            setFat (free[static_cast<std::size_t> (i)], i == need - 1 ? FatEnd : free[static_cast<std::size_t> (i) + 1]);
        }

        const std::size_t d = static_cast<std::size_t> (DirOffset + slot * EntrySize);
        std::fill (image.begin() + static_cast<long> (d), image.begin() + static_cast<long> (d + EntrySize),
                   static_cast<unsigned char> (0));
        putName (image.data() + d, clean);
        image[d + 16] = static_cast<unsigned char> (type);
        image[d + 17] = static_cast<unsigned char> (contents.size() & 0xFF);
        image[d + 18] = static_cast<unsigned char> ((contents.size() >> 8) & 0xFF);
        image[d + 19] = static_cast<unsigned char> ((contents.size() >> 16) & 0xFF);
        putU16 (d + 20, static_cast<unsigned> (free[0]));

        ++revision;
        parseDirectory();
        slotOut = slot;
        return true;
    }

    /// Open a directory slot by moving every later entry up one. Needs a free slot.
    void Disk::makeRoomAt (int slot)
    {
        for (int i = DirEntries - 1; i > slot; --i)
            std::copy_n (image.begin() + static_cast<long> (DirOffset + (i - 1) * EntrySize), EntrySize,
                         image.begin() + static_cast<long> (DirOffset + i * EntrySize));

        std::fill_n (image.begin() + static_cast<long> (DirOffset + slot * EntrySize), EntrySize,
                     static_cast<unsigned char> (0));
        ++revision;
        parseDirectory();
    }

    bool Disk::addSample (const NewSample& s, std::string& error)
    {
        const int n = static_cast<int> (s.words12.size()) & ~1;     // the packing works in pairs
        if (n < 2) { error = "there is no audio to add"; return false; }

        /*
         * The RAM address and the loop-descriptor pointer follow on from the last sample
         * already there - which is why a new sample has to land after it in the directory.
         * The header is the only place either is written down.
         */
        int ram = SampleRamBase, ptr = LoopDescriptorBase, lastSlot = -1;
        for (const auto& e : entries)
        {
            if (e.type != 'S' || e.slot <= lastSlot) continue;

            const std::size_t h = static_cast<std::size_t> (e.startBlock) * BlockSize;
            const int mem = static_cast<int> (image[h + 0x36]) | (static_cast<int> (image[h + 0x37]) << 8)
                          | (static_cast<int> (image[h + 0x38]) << 16);

            ram      = mem + ramSize (e.sampleCount);
            ptr      = static_cast<int> (u16 (h + 0x28)) + 10 * loopRecords (e.sampleCount, e.loopMode);
            lastSlot = e.slot;
        }

        // The 12-bit split-nibble packing of section 6.2, exactly as the C# writes it.
        const int half = n / 2;
        std::vector<unsigned char> file (static_cast<std::size_t> (HeaderSize + n + half), 0);
        unsigned char* data = file.data() + HeaderSize;

        for (int i = 0; i < half; ++i)
        {
            const int a = clip12 (s.words12[static_cast<std::size_t> (i)]);
            const int b = clip12 (s.words12[static_cast<std::size_t> (half + i)]);

            data[2 * i]     = static_cast<unsigned char> (((a & 0x0F) << 4) | (b & 0x0F));
            data[2 * i + 1] = static_cast<unsigned char> ((a >> 4) & 0xFF);
            data[n + i]     = static_cast<unsigned char> ((b >> 4) & 0xFF);
        }

        const std::string clean = normaliseName (s.name);
        putName (file.data(), clean);

        const long loopLength = s.loopMode == 'O' ? std::min (n, 2000)
                              : (s.loopLength < 0 ? n : std::min (s.loopLength, n));

        put32 (file, 0x10, static_cast<unsigned long> (n));
        put16 (file, 0x14, static_cast<unsigned> (s.rate));
        put16 (file, 0x16, static_cast<unsigned> (s.rootNote * 16 + s.fine));
        put16 (file, 0x18, 0);                                   // loudness
        file[0x1A] = static_cast<unsigned char> (s.loopMode);
        put32 (file, 0x1C, static_cast<unsigned long> (n));      // end marker
        put32 (file, 0x20, 0);                                   // start marker
        put32 (file, 0x24, static_cast<unsigned long> (loopLength));
        put16 (file, 0x28, static_cast<unsigned> (ptr));
        file[0x2B] = 'N';                                        // loop direction
        file[0x36] = static_cast<unsigned char> (ram & 0xFF);
        file[0x37] = static_cast<unsigned char> ((ram >> 8) & 0xFF);
        file[0x38] = static_cast<unsigned char> ((ram >> 16) & 0xFF);

        int slot = 0;
        return addFile (clean, 'S', file, lastSlot + 1, slot, error);
    }

    bool Disk::addProgram (const std::string& name, int keygroups, std::string& error)
    {
        if (keygroups < 1 || keygroups > MaxKeygroups)
        {
            error = "a programme holds 1 to 64 keygroups";
            return false;
        }

        std::vector<unsigned char> body (static_cast<std::size_t> (ProgHeaderSize + keygroups * KeygroupSize), 0);
        std::copy (std::begin (ProgramTemplate), std::end (ProgramTemplate), body.begin());

        // The file repeats its own name in bytes 0..9, and that copy is what the S950 puts
        // on its display, not the directory entry.
        const std::string clean = normaliseName (name);
        putName (body.data(), clean);
        body[23] = static_cast<unsigned char> (keygroups);

        // the lowest MIDI programme number nothing on the disk is using
        {
            bool taken[128] = {};
            for (const auto& e : entries)
                if (e.type == 'P')
                {
                    const auto raw = readFile (e);
                    if (raw.size() > 26) taken[raw[26] & 0x7F] = true;
                }
            int number = 0;
            while (number < 127 && taken[number]) ++number;
            body[26] = static_cast<unsigned char> (number);
        }

        for (int k = 0; k < keygroups; ++k)
        {
            unsigned char* kg = body.data() + ProgHeaderSize + k * KeygroupSize;
            std::copy (std::begin (KeygroupTemplate), std::end (KeygroupTemplate), kg);

            kg[0] = 127;                                          // high key
            kg[1] = 0;                                            // low key
            kg[KeygroupChainOffset] = kg[KeygroupChainOffset + 1] = 0;   // rebuildPointers fills these

            // Both zones empty: "2 SAMPLE" with a zero pointer is how the library marks
            // an unused zone, and what the Studio's ClearZone writes.
            for (int z = 0; z < 2; ++z)
            {
                unsigned char* zone = kg + KeygroupNameOffset + z * KeygroupZoneStride;
                putName (zone, UnusedZone);
                zone[16] = zone[17] = 0;
            }
        }

        // After the last programme, so the P / S grouping survives - which means opening a
        // slot if samples already sit there.
        int slot = 0;
        for (const auto& e : entries)
            if (e.type == 'P') slot = e.slot + 1;

        if (slot < static_cast<int> (entries.size()))
        {
            if (static_cast<int> (entries.size()) >= DirEntries)
            {
                error = "the directory is full: 64 files is the limit";
                return false;
            }
            makeRoomAt (slot);
        }

        int landed = 0;
        if (! addFile (clean, 'P', body, slot, landed, error))
            return false;

        rebuildPointers();       // one more programme moves the sample table
        return true;
    }

    bool Disk::setZoneSample (const Entry& program, int keygroup, int zone, const std::string& sample)
    {
        if (program.type != 'P' || zone < 0 || zone > 1) return false;
        if (keygroup < 0 || keygroup >= keygroupCount (program)) return false;

        const std::string clean = normaliseName (sample);
        const int at = ProgHeaderSize + keygroup * KeygroupSize + KeygroupNameOffset + zone * KeygroupZoneStride;

        for (int i = 0; i < 10; ++i)
            pokeFile (program, at + i,
                      static_cast<unsigned char> (i < static_cast<int> (clean.size()) ? clean[static_cast<std::size_t> (i)] : ' '));

        // The pointer is the sample's POSITION in the descriptor table, not an address.
        const int index = sampleIndex (clean);
        const int ptr   = index >= 0 ? sampleTableAddress() + KeygroupSize * index : 0;

        pokeFile (program, at + 16, static_cast<unsigned char> (ptr & 0xFF));
        pokeFile (program, at + 17, static_cast<unsigned char> ((ptr >> 8) & 0xFF));
        return true;
    }

    // --------------------------------------------------------- the keygroup arena

    int Disk::chainOf (const std::vector<unsigned char>& program, int keygroup) const
    {
        const std::size_t at = static_cast<std::size_t> (ProgHeaderSize + keygroup * KeygroupSize + KeygroupChainOffset);
        if (at + 1 >= program.size()) return 0;
        return static_cast<int> (program[at]) | (static_cast<int> (program[at + 1]) << 8);
    }

    /*
     * Programmes are packed in directory order with one empty 70-byte record between
     * consecutive ones. That predicts every chain pointer and every table base on all 98
     * library disks that have them, exactly.
     */
    int Disk::arenaRecords (std::vector<std::pair<int, int>>& firstBySlot) const
    {
        firstBySlot.clear();
        int rec = 0;
        for (const auto& e : entries)
        {
            if (e.type != 'P') continue;
            firstBySlot.emplace_back (e.slot, rec);
            rec += keygroupCount (e) + 1;
        }
        return std::max (0, rec - 1);
    }

    /*
     * Where the arena starts. Nothing stores it, so it is read back from the zone
     * pointers - the ones the sampler follows - and only from the keygroup chains when a
     * disk has no samples to point at; a blank disk gets the value 97 of the library's 100
     * disks start at. Captured on first use, because the counts it derives from change the
     * moment anything is added.
     */
    int Disk::arenaBase() const
    {
        if (arenaBaseCache >= 0) return arenaBaseCache;

        int programs = 0, totalKeygroups = 0;
        for (const auto& e : entries)
            if (e.type == 'P') { ++programs; totalKeygroups += keygroupCount (e); }

        // the table base most zones agree on
        {
            std::vector<std::pair<int, int>> votes;
            int best = -1, bestCount = 0;

            for (const auto& e : entries)
            {
                if (e.type != 'P') continue;
                for (const auto& kg : keygroups (e))
                    for (const Zone* z : { &kg.zone1, &kg.zone2 })
                    {
                        if (! z->inUse() || z->pointer == 0) continue;
                        const int i = sampleIndex (z->name);
                        if (i < 0) continue;

                        const int b = z->pointer - KeygroupSize * i;
                        int n = 0;
                        for (auto& v : votes) if (v.first == b) n = ++v.second;
                        if (n == 0) { votes.emplace_back (b, 1); n = 1; }
                        if (n > bestCount) { bestCount = n; best = b; }
                    }
            }

            if (best >= 0)
            {
                const int fromZones = best - KeygroupSize * (totalKeygroups + programs - 1);
                if (fromZones > 0 && fromZones < SampleRamBase)
                    return arenaBaseCache = fromZones;
            }
        }

        // otherwise the chains: each programme with two or more keygroups implies a base
        {
            std::vector<std::pair<int, int>> first;
            arenaRecords (first);

            std::vector<std::pair<int, int>> votes;
            int best = -1, bestCount = 0;

            for (const auto& e : entries)
            {
                if (e.type != 'P' || keygroupCount (e) < 2) continue;

                int start = 0;
                for (const auto& f : first) if (f.first == e.slot) start = f.second;

                const int v = chainOf (readFile (e), 0) - KeygroupSize * (start + 1);
                if (v <= 0 || v >= SampleRamBase) continue;

                int n = 0;
                for (auto& x : votes) if (x.first == v) n = ++x.second;
                if (n == 0) { votes.emplace_back (v, 1); n = 1; }
                if (n > bestCount) { bestCount = n; best = v; }
            }

            return arenaBaseCache = (best > 0 ? best : ArenaDefault);
        }
    }

    int Disk::sampleTableAddress() const
    {
        std::vector<std::pair<int, int>> first;
        return arenaBase() + KeygroupSize * arenaRecords (first);
    }

    int Disk::rebuildPointers()
    {
        std::vector<std::pair<int, int>> first;
        const int records = arenaRecords (first);
        const int arena   = arenaBase();
        const int table   = arena + KeygroupSize * records;
        int changed = 0;

        for (const auto& p : entries)
        {
            if (p.type != 'P') continue;

            const int count = keygroupCount (p);
            int start = 0;
            for (const auto& f : first) if (f.first == p.slot) start = f.second;

            const auto raw = readFile (p);

            // The header restates the layout - where the keygroups load and how many there
            // are - and the sampler believes the header. See the memory of DSKA0000.
            const int load = arena + KeygroupSize * start;
            if (raw.size() > 19 && (static_cast<int> (raw[18]) | (static_cast<int> (raw[19]) << 8)) != load)
            {
                pokeFile (p, 18, static_cast<unsigned char> (load & 0xFF));
                pokeFile (p, 19, static_cast<unsigned char> ((load >> 8) & 0xFF));
                ++changed;
            }
            if (raw.size() > 23 && raw[23] != count)
            {
                pokeFile (p, 23, static_cast<unsigned char> (count));
                ++changed;
            }

            for (int k = 0; k < count; ++k)
            {
                const int kg = ProgHeaderSize + k * KeygroupSize;

                const int co = kg + KeygroupChainOffset;
                const int wantNext = k == count - 1 ? 0 : arena + KeygroupSize * (start + k + 1);
                if (co + 1 < static_cast<int> (raw.size()) && chainOf (raw, k) != wantNext)
                {
                    pokeFile (p, co,     static_cast<unsigned char> (wantNext & 0xFF));
                    pokeFile (p, co + 1, static_cast<unsigned char> ((wantNext >> 8) & 0xFF));
                    ++changed;
                }

                for (int z = 0; z < 2; ++z)
                {
                    const int no = kg + KeygroupNameOffset + z * KeygroupZoneStride;
                    const int po = no + 16;
                    if (po + 1 >= static_cast<int> (raw.size())) continue;

                    const std::string name = trimmed (cleanName (raw.data(), static_cast<std::size_t> (no)));
                    const int have = static_cast<int> (raw[static_cast<std::size_t> (po)])
                                   | (static_cast<int> (raw[static_cast<std::size_t> (po) + 1]) << 8);
                    if (name.empty() || name == UnusedZone || have == 0) continue;

                    const int i = sampleIndex (name);
                    if (i < 0) continue;

                    const int wantZone = table + KeygroupSize * i;
                    if (have != wantZone)
                    {
                        pokeFile (p, po,     static_cast<unsigned char> (wantZone & 0xFF));
                        pokeFile (p, po + 1, static_cast<unsigned char> ((wantZone >> 8) & 0xFF));
                        ++changed;
                    }
                }
            }
        }

        return changed;
    }

    // ------------------------------------------------------------------------ reading

    std::vector<unsigned char> Disk::readFile (const Entry& e) const
    {
        const auto blocks = chain (e.startBlock);

        std::vector<unsigned char> buf;
        buf.reserve (blocks.size() * BlockSize);

        for (int b : blocks)
        {
            const std::size_t at = static_cast<std::size_t> (b) * BlockSize;
            if (at + BlockSize > image.size()) break;
            buf.insert (buf.end(), image.begin() + at, image.begin() + at + BlockSize);
        }

        if (static_cast<int> (buf.size()) > e.length && e.length >= 0)
            buf.resize (static_cast<std::size_t> (e.length));

        return buf;
    }

    /*
     * A sample as stored: signed 12-bit, in the split-nibble layout of section 6.2.
     *
     * Three bytes hold two words, and the two halves of the sample are interleaved rather
     * than laid out one after another: word i and word half+i share a byte of nibbles. The
     * count is rounded down to a pair, and a truncated file gives back what it does hold
     * rather than reading past the end.
     */
    std::vector<short> Disk::sampleWords12 (const Entry& e) const
    {
        if (e.type != 'S') return {};

        const auto raw = readFile (e);
        const int payload = static_cast<int> (raw.size()) - HeaderSize;
        if (payload <= 0) return {};

        long long n = std::min<long long> (e.sampleCount, payload * 2 / 3);
        n &= ~1LL;
        if (n <= 0) return {};

        const int half = static_cast<int> (n / 2);
        std::vector<short> pcm (static_cast<std::size_t> (n));

        auto signed12 = [] (int w) { return static_cast<short> (w >= 2048 ? w - 4096 : w); };

        for (int i = 0; i < half; ++i)
        {
            const int nibbles = raw[static_cast<std::size_t> (HeaderSize + 2 * i)];

            pcm[static_cast<std::size_t> (i)] =
                signed12 ((raw[static_cast<std::size_t> (HeaderSize + 2 * i + 1)] << 4) | (nibbles >> 4));

            pcm[static_cast<std::size_t> (half + i)] =
                signed12 ((raw[static_cast<std::size_t> (HeaderSize + n + i)] << 4) | (nibbles & 0x0F));
        }

        return pcm;
    }

    int Disk::keygroupCount (const Entry& program)
    {
        if (program.type != 'P' || program.length < ProgHeaderSize + KeygroupSize)
            return 0;

        const int n = program.length - ProgHeaderSize;
        return (n % KeygroupSize == 0) ? n / KeygroupSize : 0;
    }

    Disk::Zone Disk::parseZone (const unsigned char* raw, int at)
    {
        Zone z;
        z.name      = cleanName (raw, static_cast<std::size_t> (at));
        z.pointer   = raw[at + 16] | (raw[at + 17] << 8);
        z.fine      = raw[at + 18];
        z.transpose = static_cast<signed char> (raw[at + 19]);
        z.filter    = raw[at + 20];
        z.loudness  = static_cast<signed char> (raw[at + 21]);
        return z;
    }

    std::vector<Disk::Keygroup> Disk::keygroups (const Entry& program) const
    {
        std::vector<Keygroup> out;

        const int n = keygroupCount (program);
        if (n == 0) return out;

        const auto body = readFile (program);

        for (int k = 0; k < n; ++k)
        {
            const std::size_t o = static_cast<std::size_t> (ProgHeaderSize)
                                + static_cast<std::size_t> (k) * KeygroupSize;

            if (o + KeygroupSize > body.size()) break;

            const unsigned char* raw = body.data() + o;

            Keygroup kg;
            kg.index   = k;
            kg.highKey = raw[0];
            kg.lowKey  = raw[1];

            kg.velocitySwitch = raw[2];

            kg.vcaAttack  = raw[3];
            kg.vcaDecay   = raw[4];
            kg.vcaSustain = raw[5];
            kg.vcaRelease = raw[6];

            kg.velToFilter  = raw[7];
            kg.keyToFilter  = raw[8];
            kg.velToAttack  = raw[9];
            kg.velToRelease = static_cast<signed char> (raw[10]);
            kg.velToLoudness = raw[11];

            kg.warpVelocity = raw[12];
            kg.warpDepth    = static_cast<signed char> (raw[13]);
            kg.warpTime     = raw[14];
            kg.outputPort   = static_cast<signed char> (raw[19]) + 1;

            kg.lfoDelay = raw[15];
            kg.lfoRate  = raw[16];
            kg.lfoDepth = raw[17];
            kg.flags    = raw[18];

            kg.lfoAftertouchDepth = raw[21];
            kg.lfoModwheelDepth   = raw[22];

            kg.vcfAmount  = static_cast<signed char> (raw[23]);
            kg.vcfAttack  = raw[34];
            kg.vcfDecay   = raw[35];
            kg.vcfSustain = raw[36];
            kg.vcfRelease = raw[37];

            kg.zone1 = parseZone (raw, KeygroupNameOffset);
            kg.zone2 = parseZone (raw, KeygroupNameOffset + KeygroupZoneStride);

            out.push_back (kg);
        }

        return out;
    }

    // ----------------------------------------------------------- ready to be played

    /// FNV-1a over a sample file's bytes: header and audio both, so a marker edit counts.
    unsigned long long Disk::fingerprintOf (const Entry& sample) const
    {
        unsigned long long h = 1469598103934665603ull;
        for (unsigned char b : readFile (sample)) { h ^= b; h *= 1099511628211ull; }
        return h;
    }

    SoundPtr Disk::soundFor (const Entry& sample) const
    {
        const auto words = sampleWords12 (sample);
        if (words.empty()) return nullptr;

        auto s = std::make_shared<Sound>();
        s->name        = sample.name;
        s->fingerprint = fingerprintOf (sample);
        s->sourceRate = sample.sampleRate < 1000 ? 40000 : sample.sampleRate;
        s->rootPitch  = sample.nominalPitch() + sample.finePitch() / 16.0;

        s->audio.resize (words.size());
        for (std::size_t i = 0; i < words.size(); ++i)
            s->audio[i] = words[i] / 2048.0f;

        /*
         * The machine plays end-length .. end round and round, so the start follows from
         * the length rather than from the stored start - which is simply zero in 250 of the
         * library's 324 looped samples.
         */
        const long long to   = std::min<long long> (sample.loopEnd, static_cast<long long> (words.size()));
        long long       from = std::max<long long> (0, sample.loopEnd - sample.loopLength);

        s->loops    = sample.loopMode != 'O' && sample.loopLength > 0 && sample.loopEnd > 0 && to > from;
        s->alternates = sample.loopMode == 'A';

        /*
         * A note starts at the start marker (0x20), not at word 0.
         *
         * MEASURED, 2026-09-27: DSKA0039 GRAND1, note 60 struck twelve times on the machine.
         * Every GRAND sample has its marker at 1000 words, past 500-750 words of lead-in at
         * -30 to -36 dB. The machine's attack went straight from silence to within 6 dB of
         * the peak; played from word 0, the same note first spent 24 ms at about -30 dB.
         * Played from the marker, the attack matched the machine's shape within 2 dB.
         *
         * Cropping here keeps the voice unaware of it. A marker at or past the end of what
         * would play is ignored, as the Studio's audition ignores it.
         */
        const long long playsTo = s->loops ? to : static_cast<long long> (words.size());
        const long long first   = sample.loopStart > 0 && sample.loopStart < playsTo ? sample.loopStart : 0;
        if (first > 0)
        {
            s->audio.erase (s->audio.begin(), s->audio.begin() + first);
            from = std::max (from, first);
        }

        s->loopFrom = static_cast<int> (from - first);
        s->loopTo   = static_cast<int> (to - first);

        return s;
    }

    PatchPtr Disk::buildPatch (const Entry& program, const Patch* reuse) const
    {
        if (program.type != 'P') return nullptr;

        auto patch = std::make_shared<Patch>();
        patch->name = program.name;

        // Program header byte 21: fade overlapping keygroups rather than sounding both at
        // full level. 48 library programmes set it and overlap, and they are the
        // multi-sampled instruments - the pianos above all.
        {
            const auto head = readFile (program);
            patch->positionalCrossfade = head.size() > 21 && head[21] != 0;
        }

        // One decode per sample, however many keygroups name it.
        std::vector<std::pair<std::string, SoundPtr>> decoded;

        auto soundNamed = [&] (const std::string& name) -> SoundPtr
        {
            if (name.empty()) return nullptr;

            for (const auto& d : decoded)
                if (d.first == name)
                    return d.second;

            const Entry* e = find (name, 'S');

            // Only a sample whose BYTES are the ones the old sound was decoded from: the
            // same name is not enough, since the Synth tab re-renders a wave under its name.
            if (reuse != nullptr && e != nullptr)
            {
                const auto print = fingerprintOf (*e);
                for (const auto& k : reuse->keygroups)
                    if (k.sound != nullptr && k.sound->name == name && k.sound->fingerprint == print)
                    {
                        decoded.emplace_back (name, k.sound);
                        return k.sound;
                    }
            }

            SoundPtr s = e != nullptr ? soundFor (*e) : nullptr;

            decoded.emplace_back (name, s);
            return s;
        };

        for (const auto& kg : keygroups (program))
        {
            /*
             * The two zones split the velocity range at the switch rather than both
             * sounding. A switch of 128 leaves zone 1 the whole range, which is how the
             * panel says there is no second zone.
             *
             * Getting this wrong is not subtle: 74 of the library's 168 two-zone keygroups
             * name the SAME sample in both, so layering them puts two copies of one sample
             * on top of each other and every note rings like a bell.
             */
            int split = kg.velocitySwitch;
            if (split < 1 || split > 128) split = 128;

            auto addZone = [&] (const Zone& zone, int velFrom, int velTo)
            {
                if (velTo < velFrom || zone.name.empty()) return;

                SoundPtr sound = soundNamed (zone.name);
                if (sound == nullptr) return;

                KeygroupPatch p;
                p.lowKey        = kg.lowKey;
                p.highKey       = kg.highKey;
                p.keygroupIndex = kg.index;
                p.velocityFrom  = velFrom;
                p.velocityTo    = velTo;
                p.sound         = sound;

                p.vcaAttack  = kg.vcaAttack;
                p.vcaDecay   = kg.vcaDecay;
                p.vcaSustain = kg.vcaSustain;
                p.vcaRelease = kg.vcaRelease;

                p.vcfWritten = KeygroupPatch::looksWritten (kg.vcfAttack, kg.vcfDecay,
                                                            kg.vcfSustain, kg.vcfRelease);
                p.vcfAttack  = kg.vcfAttack;
                p.vcfDecay   = kg.vcfDecay;
                p.vcfSustain = kg.vcfSustain;
                p.vcfRelease = kg.vcfRelease;
                p.vcfAmount  = kg.vcfAmount;

                p.velToFilter   = kg.velToFilter;
                p.keyToFilter   = kg.keyToFilter;
                p.velToLoudness = kg.velToLoudness;
                p.velToAttack   = kg.velToAttack;
                p.velToRelease  = kg.velToRelease;
                p.velocityReleaseOn = kg.velocityReleaseOn();

                p.warpVelocity  = kg.warpVelocity;
                p.warpDepth     = kg.warpDepth;
                p.warpTime      = kg.warpTime;
                p.outputPort    = kg.outputPort;

                p.lfoDelay         = kg.lfoDelay;
                p.lfoRate          = kg.lfoRate;
                p.lfoDepth         = kg.lfoDepth;
                p.lfoModwheelDepth = kg.lfoModwheelDepth;
                p.lfoAftertouchDepth = kg.lfoAftertouchDepth;
                p.lfoDesync        = kg.lfoDesync();

                p.zoneFilter    = zone.filter;
                p.zoneLoudness  = zone.loudness;
                p.zoneTranspose = zone.pitchOffset();

                p.constantPitch = kg.constantPitch();
                p.oneShot       = kg.oneShot();

                patch->keygroups.push_back (p);
            };

            if (! kg.hasSecondZone())
            {
                addZone (kg.zone1, 0, 127);
            }
            else
            {
                /*
                 * The byte is the LAST velocity of zone 1, not the first of zone 2.
                 *
                 * This read `zone1 0..split-1, zone2 split..127` and was out by one step.
                 * Measured on the hardware with a sine in zone 1 and noise in zone 2:
                 *
                 *     switch    1     zone 1 at velocity 1,  zone 2 from 2
                 *     switch   64     zone 1 through 64,     zone 2 from 65
                 *     switch  127     zone 1 through 127,    zone 2 never
                 *
                 * That last line is what makes it certain. At a switch of 127 the hard sample
                 * cannot be reached at all, which only follows if zone 2 begins at 128 - and
                 * it is why the panel's range runs to 128 and why 128 means the switch is off.
                 * The special case that used to say so has gone: with zone 2 starting at
                 * split + 1, a split of 127 or 128 leaves it an empty range and addZone
                 * declines it on its own.
                 */
                addZone (kg.zone1, 0, std::min (127, split));
                addZone (kg.zone2, split + 1, 127);
            }
        }

        return patch;
    }
}
