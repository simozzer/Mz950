#include "Disk.h"
#include "Hfe.h"

#include <algorithm>
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

    SoundPtr Disk::soundFor (const Entry& sample) const
    {
        const auto words = sampleWords12 (sample);
        if (words.empty()) return nullptr;

        auto s = std::make_shared<Sound>();
        s->name       = sample.name;
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
        const long long from = std::max<long long> (0, sample.loopEnd - sample.loopLength);

        s->loops    = sample.loopMode != 'O' && sample.loopLength > 0 && sample.loopEnd > 0 && to > from;
        s->alternates = sample.loopMode == 'A';
        s->loopFrom = static_cast<int> (from);
        s->loopTo   = static_cast<int> (to);

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

            if (reuse != nullptr)
                for (const auto& k : reuse->keygroups)
                    if (k.sound != nullptr && k.sound->name == name)
                    {
                        decoded.emplace_back (name, k.sound);
                        return k.sound;
                    }

            const Entry* e = find (name, 'S');
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
