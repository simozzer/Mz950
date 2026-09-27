/*
 * Hold the keygroup writer to the reader, on a real disk.
 *
 *     DiskEditCheck <image.img|image.hfe>                 round-trip every setting
 *     DiskEditCheck <image> <edited.img>                  ...and save a heavily edited copy
 *
 * Like DiskDump, it takes a disk rather than carrying one: nobody's sample names or audio go
 * into the repository. What it checks, for every setting of every keygroup of every
 * programme on the disk, at the bottom, the top, the middle and past both ends:
 *
 *   - it reads back as the value written, clamped to the panel's range;
 *   - the reader the engine plays from - Disk::keygroups - sees the same value, so the
 *     writer agrees with a reader already held to the C# by crosscheck.ps1;
 *   - NOTHING ELSE IN THE IMAGE MOVED. Every byte of the 800K is compared, and the only
 *     ones allowed to differ are the one being written (and, for the first edit of a blank
 *     S900 filter envelope, the other three stages of it).
 *
 * The edited copy is for crosscheck.ps1: dumped by the C# reader and the C++ one and diffed,
 * it shows the C# Studio reads what the plugin wrote exactly as the plugin does.
 */

#include "Disk.h"
#include "SynthPatch.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

using s950::Disk;
using s950::KeygroupParam;
using s950::KeygroupParamInfo;

namespace
{
    int checks = 0, failures = 0;

    void fail (const std::string& where, const char* what, int got, int want)
    {
        ++failures;
        if (failures <= 40)
            std::printf ("  FAIL %s %s: got %d, want %d\n", where.c_str(), what, got, want);
    }

    /// The same setting as the playing reader parses it.
    int parsed (const Disk::Keygroup& k, KeygroupParam p)
    {
        switch (p)
        {
            case KeygroupParam::HighKey:          return k.highKey;
            case KeygroupParam::LowKey:           return k.lowKey;
            case KeygroupParam::VelocitySwitch:   return k.velocitySwitch;
            case KeygroupParam::VcaAttack:        return k.vcaAttack;
            case KeygroupParam::VcaDecay:         return k.vcaDecay;
            case KeygroupParam::VcaSustain:       return k.vcaSustain;
            case KeygroupParam::VcaRelease:       return k.vcaRelease;
            case KeygroupParam::VelToFilter:      return k.velToFilter;
            case KeygroupParam::KeyToFilter:      return k.keyToFilter;
            case KeygroupParam::VelToAttack:      return k.velToAttack;
            case KeygroupParam::VelToRelease:     return k.velToRelease;
            case KeygroupParam::VelToLoudness:    return k.velToLoudness;
            case KeygroupParam::WarpVelocity:     return k.warpVelocity;
            case KeygroupParam::WarpDepth:        return k.warpDepth;
            case KeygroupParam::WarpTime:         return k.warpTime;
            case KeygroupParam::LfoDelay:         return k.lfoDelay;
            case KeygroupParam::LfoRate:          return k.lfoRate;
            case KeygroupParam::LfoDepth:         return k.lfoDepth;
            case KeygroupParam::LfoAftertouch:    return k.lfoAftertouchDepth;
            case KeygroupParam::LfoModwheel:      return k.lfoModwheelDepth;
            case KeygroupParam::OutputPort:       return k.outputPort;
            case KeygroupParam::VcfAmount:        return k.vcfAmount;
            case KeygroupParam::VcfAttack:        return k.vcfAttack;
            case KeygroupParam::VcfDecay:         return k.vcfDecay;
            case KeygroupParam::VcfSustain:       return k.vcfSustain;
            case KeygroupParam::VcfRelease:       return k.vcfRelease;
            case KeygroupParam::Zone1Fine:        return k.zone1.fine;
            case KeygroupParam::Zone1Transpose:   return k.zone1.transpose;
            case KeygroupParam::Zone1Filter:      return k.zone1.filter;
            case KeygroupParam::Zone1Loudness:    return k.zone1.loudness;
            case KeygroupParam::Zone2Fine:        return k.zone2.fine;
            case KeygroupParam::Zone2Transpose:   return k.zone2.transpose;
            case KeygroupParam::Zone2Filter:      return k.zone2.filter;
            case KeygroupParam::Zone2Loudness:    return k.zone2.loudness;
            case KeygroupParam::ConstantPitch:    return k.constantPitch() ? 1 : 0;
            case KeygroupParam::LfoDesync:        return k.lfoDesync() ? 1 : 0;
            case KeygroupParam::OneShot:          return k.oneShot() ? 1 : 0;
            case KeygroupParam::VelocityReleaseOn:return k.velocityReleaseOn() ? 1 : 0;
            default:                              return -9999;
        }
    }

    bool isVcfStage (KeygroupParam p)
    {
        return p == KeygroupParam::VcfAttack || p == KeygroupParam::VcfDecay
            || p == KeygroupParam::VcfSustain || p == KeygroupParam::VcfRelease;
    }

    /// One write of one value, checked every way there is.
    void tryValue (const Disk& original, int programIndex, int kg, KeygroupParam p, int value)
    {
        Disk d = original;                          // a fresh copy each time: nothing leaks
        const auto& program = d.getEntries()[static_cast<std::size_t> (programIndex)];
        const auto& info    = s950::keygroupParamInfo (p);

        std::string where = program.name + " kg" + std::to_string (kg) + " " + info.name
                          + " <- " + std::to_string (value);

        const bool blankBefore = isVcfStage (p) && d.vcfBlank (program, kg);

        ++checks;
        if (! d.setKeygroupParam (program, kg, p, value))
        {
            fail (where, "write refused", 0, 1);
            return;
        }

        const int want = value < info.lo ? info.lo : (value > info.hi ? info.hi : value);

        ++checks;
        const int got = d.getKeygroupParam (program, kg, p);
        if (got != want) fail (where, "reads back", got, want);

        ++checks;
        const auto groups = d.keygroups (program);
        if (kg < static_cast<int> (groups.size()))
        {
            const int seen = parsed (groups[static_cast<std::size_t> (kg)], p);
            if (seen != want) fail (where, "the playing reader sees", seen, want);
        }

        // Which bytes were allowed to move.
        std::set<std::size_t> allowed;
        std::size_t at = 0;
        if (d.keygroupByteAt (program, kg, info.offset, at)) allowed.insert (at);

        if (blankBefore)
            for (int o = 34; o <= 37; ++o)
                if (d.keygroupByteAt (program, kg, o, at)) allowed.insert (at);

        const auto& before = original.getImage();
        const auto& after  = d.getImage();

        ++checks;
        if (before.size() != after.size())
        {
            fail (where, "image size changed", static_cast<int> (after.size()),
                  static_cast<int> (before.size()));
            return;
        }

        int strays = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
            if (before[i] != after[i] && allowed.count (i) == 0) ++strays;

        if (strays != 0) fail (where, "bytes changed outside the setting", strays, 0);
    }

    /// A value for the edited copy: distinct per keygroup and setting, always in range.
    int scrambled (const KeygroupParamInfo& info, int program, int kg, int p)
    {
        const int span = info.hi - info.lo + 1;
        return info.lo + (program * 31 + kg * 17 + p * 7 + 3) % span;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: DiskEditCheck <image> [edited.img]\n");
        return 2;
    }

    Disk original;
    std::string error;
    if (! original.loadFile (argv[1], error))
    {
        std::fprintf (stderr, "could not read %s: %s\n", argv[1], error.c_str());
        return 2;
    }

    const auto& entries = original.getEntries();
    int programs = 0, keygroups = 0;

    for (int e = 0; e < static_cast<int> (entries.size()); ++e)
    {
        if (entries[static_cast<std::size_t> (e)].type != 'P') continue;

        ++programs;
        const int n = Disk::keygroupCount (entries[static_cast<std::size_t> (e)]);

        for (int kg = 0; kg < n; ++kg)
        {
            ++keygroups;

            for (int p = 0; p < static_cast<int> (KeygroupParam::Count); ++p)
            {
                const auto  param = static_cast<KeygroupParam> (p);
                const auto& info  = s950::keygroupParamInfo (param);

                for (int v : { info.lo, info.hi, (info.lo + info.hi) / 2,
                               info.lo - 5, info.hi + 5, info.lo + 1 })
                    tryValue (original, e, kg, param, v);
            }
        }

        // Past the last keygroup nothing may be written, and nothing may move.
        {
            Disk d = original;
            ++checks;
            if (d.setKeygroupParam (d.getEntries()[static_cast<std::size_t> (e)], n,
                                    KeygroupParam::VcaDecay, 50))
                fail (entries[static_cast<std::size_t> (e)].name, "wrote past the last keygroup", 1, 0);
        }
    }

    std::printf ("  %d programmes, %d keygroups, %d settings each\n",
                 programs, keygroups, static_cast<int> (KeygroupParam::Count));

    /*
     * A zone's pitch: transpose and fine as one signed 16-bit count of sixteenths of a
     * semitone. The first five are DSKA0058 SEQ BASS's zones, measured on the machine at
     * +12, +5, 0, -7 and -12; the last two are the library's +24 and its commonest detune.
     */
    {
        struct { int fine, transpose; double semitones; } cases[] =
        {
            { 192, 0, 12 }, { 80, 0, 5 }, { 0, 0, 0 }, { 144, -1, -7 }, { 64, -1, -12 },
            { 128, 1, 24 }, { 255, -1, -1.0 / 16 }
        };
        for (const auto& c : cases)
        {
            Disk::Zone z;
            z.fine = c.fine; z.transpose = c.transpose;
            ++checks;
            if (z.pitchOffset() != c.semitones)
                fail ("zone pitch", ("fine " + std::to_string (c.fine) + " transpose " + std::to_string (c.transpose)).c_str(),
                      static_cast<int> (z.pitchOffset() * 16), static_cast<int> (c.semitones * 16));
        }
    }

    /*
     * A rebuild after an edit hears the edit, and keeps the samples it had.
     *
     * The second half is what lets a held note follow an edit: Voice::adopt only takes new
     * settings onto a note playing the very sound object it started with.
     */
    for (const auto& e : entries)
    {
        if (e.type != 'P' || Disk::keygroupCount (e) == 0) continue;

        Disk d = original;
        const auto& program = d.getEntries()[static_cast<std::size_t> (&e - entries.data())];

        auto before = d.buildPatch (program);
        if (before == nullptr || before->keygroups.empty()) continue;

        const int kg   = before->keygroups.front().keygroupIndex;
        const int want = before->keygroups.front().vcaSustain == 40 ? 41 : 40;
        d.setKeygroupParam (program, kg, KeygroupParam::VcaSustain, want);

        auto after = d.buildPatch (program, before.get());

        ++checks;
        if (after == nullptr || after->keygroups.size() != before->keygroups.size())
        {
            fail (e.name, "rebuild changed the keygroups", 0, 1);
            continue;
        }

        ++checks;
        if (after->keygroups.front().vcaSustain != want)
            fail (e.name, "the rebuild hears the edit", after->keygroups.front().vcaSustain, want);

        for (std::size_t i = 0; i < after->keygroups.size(); ++i)
        {
            ++checks;
            if (after->keygroups[i].sound != before->keygroups[i].sound)
                fail (e.name, "a rebuild decoded a sample afresh", 0, 1);
        }
    }

    /*
     * A note starts at the sample's start marker (header 0x20), measured on the machine.
     * Every sound a programme plays is the sample's words from the marker on, with the loop
     * moved to match; a marker at or past the end of what would play is ignored.
     */
    {
        int marked = 0, cut = 0;
        std::set<std::string> seen;
        for (const auto& p : entries)
        {
            if (p.type != 'P') continue;
            const auto patch = original.buildPatch (p);
            if (patch == nullptr) continue;

            for (const auto& k : patch->keygroups)
            {
                if (k.sound == nullptr || ! seen.insert (k.sound->name).second) continue;
                const Disk::Entry* s = original.find (k.sound->name, 'S');
                if (s == nullptr) continue;

                const auto words = original.sampleWords12 (*s);
                const long long size  = static_cast<long long> (words.size());
                const long long end   = std::min<long long> (s->loopEnd, size);
                // a one-shot stops at its end marker; a loop turns round there
                const long long stop  = k.sound->loops ? size : (s->loopEnd > 0 && s->loopEnd < size ? s->loopEnd : size);
                const long long plays = k.sound->loops ? end : stop;
                const long long first = s->loopStart > 0 && s->loopStart < plays ? s->loopStart : 0;
                if (first > 0) ++marked;
                if (stop < size) ++cut;

                const std::string where = "sample " + s->name;
                ++checks;
                if (static_cast<long long> (k.sound->audio.size()) != stop - first)
                {
                    fail (where, "plays from the start marker to the end marker (length)",
                          static_cast<int> (k.sound->audio.size()), static_cast<int> (stop - first));
                    continue;
                }
                ++checks;
                if (! k.sound->audio.empty() && k.sound->audio.front() != words[static_cast<std::size_t> (first)] / 2048.0f)
                    fail (where, "plays from the start marker (first word)", 0, 1);

                if (k.sound->loops)
                {
                    ++checks;
                    if (k.sound->loopTo != static_cast<int> (end - first))
                        fail (where, "the loop end moves with the start", k.sound->loopTo, static_cast<int> (end - first));
                }
            }
        }
        std::printf ("  %d sounds played, %d of them from a start marker past word 0, %d one-shots cut at an end marker\n",
                     static_cast<int> (seen.size()), marked, cut);
    }

    // ------------------------------------------------------------- making a disk

    /*
     * A disk from nothing: three samples, two programmes, zones pointing at the samples.
     * Every derived field is checked against the rules the hardware turned out to care
     * about - contiguous directory, the arena layout, the header restating it, zone
     * pointers as positions - and the image is saved for crosscheck.ps1 to read with the
     * C# library, which is the reader that has produced disks a real S950 played.
     */
    std::printf ("  making a disk from nothing\n");
    {
        Disk d = Disk::blank ("BUILT");
        std::string why;

        auto sawWords = [] (int n)
        {
            std::vector<short> w (static_cast<std::size_t> (n));
            for (int i = 0; i < n; ++i)
                w[static_cast<std::size_t> (i)] = static_cast<short> (-2048 + (4095 * (i % 153)) / 152);
            return w;
        };

        Disk::NewSample saw;  saw.name  = "saw";  saw.words12  = sawWords (1223);  saw.rate  = 40000;   // odd: last word dropped
        Disk::NewSample kick; kick.name = "kick"; kick.words12 = sawWords (6000);  kick.rate = 20000; kick.loopMode = 'O'; kick.rootNote = 36;
        Disk::NewSample big;  big.name  = "long"; big.words12  = sawWords (140000); big.rate = 20000;  // over a 128K page

        ++checks; if (! d.addSample (saw,  why)) fail ("built", why.c_str(), 0, 1);
        ++checks; if (! d.addSample (kick, why)) fail ("built", why.c_str(), 0, 1);
        ++checks; if (! d.addSample (big,  why)) fail ("built", why.c_str(), 0, 1);

        ++checks; if (! d.addProgram ("lead", 2, why)) fail ("built", why.c_str(), 0, 1);
        ++checks; if (! d.addProgram ("kit",  1, why)) fail ("built", why.c_str(), 0, 1);

        // a duplicate name within a type is refused, and the disk is untouched by it
        ++checks;
        if (d.addSample (saw, why)) fail ("built", "a second SAW was allowed", 1, 0);

        const auto& es = d.getEntries();
        ++checks;
        if (es.size() != 5) { fail ("built", "five entries", static_cast<int> (es.size()), 5); return 1; }

        // programmes first, then samples, in slots 0..4 with no gap
        const char order[] = { 'P', 'P', 'S', 'S', 'S' };
        for (int i = 0; i < 5; ++i)
        {
            ++checks;
            if (es[static_cast<std::size_t> (i)].slot != i || es[static_cast<std::size_t> (i)].type != order[i])
                fail ("built", "directory order and contiguity", es[static_cast<std::size_t> (i)].slot, i);
        }

        const Disk::Entry* lead = d.find ("LEAD", 'P');
        const Disk::Entry* kit  = d.find ("KIT",  'P');
        const Disk::Entry* sawE = d.find ("SAW",  'S');
        const Disk::Entry* kkE  = d.find ("KICK", 'S');
        const Disk::Entry* bigE = d.find ("LONG", 'S');

        ++checks;
        if (! lead || ! kit || ! sawE || ! kkE || ! bigE) { fail ("built", "every file found by name", 0, 1); return 1; }

        ++checks; if (sawE->sampleCount != 1222)   fail ("built", "SAW words (odd one dropped)", static_cast<int> (sawE->sampleCount), 1222);
        ++checks; if (sawE->sampleRate  != 40000)  fail ("built", "SAW rate", sawE->sampleRate, 40000);
        ++checks; if (sawE->loopMode    != 'L')    fail ("built", "SAW looped", sawE->loopMode, 'L');
        ++checks; if (kkE->loopMode     != 'O')    fail ("built", "KICK one-shot", kkE->loopMode, 'O');
        ++checks; if (sawE->loopLength  != 1222)   fail ("built", "SAW loop is the whole sample", static_cast<int> (sawE->loopLength), 1222);

        // the audio round-trips through the 12-bit packing exactly
        auto same = [&] (const Disk::Entry& e, const std::vector<short>& want, const char* what)
        {
            const auto got = d.sampleWords12 (e);
            const std::size_t n = want.size() & ~static_cast<std::size_t> (1);
            ++checks;
            if (got.size() != n) { fail ("built", what, static_cast<int> (got.size()), static_cast<int> (n)); return; }
            for (std::size_t i = 0; i < n; ++i)
                if (got[i] != want[i]) { fail ("built", what, got[i], want[i]); return; }
        };
        same (*sawE, saw.words12,  "SAW audio round-trips");
        same (*kkE,  kick.words12, "KICK audio round-trips");
        same (*bigE, big.words12,  "LONG audio round-trips");

        // sample RAM and loop descriptors follow on from one sample to the next
        auto header = [&] (const Disk::Entry& e, int at) { return static_cast<int> (d.getImage()[static_cast<std::size_t> (e.startBlock * Disk::BlockSize + at)]); };
        auto mem = [&] (const Disk::Entry& e) { return header (e, 0x36) | (header (e, 0x37) << 8) | (header (e, 0x38) << 16); };
        auto lp  = [&] (const Disk::Entry& e) { return header (e, 0x28) | (header (e, 0x29) << 8); };

        ++checks; if (mem (*sawE) != 0x18000)          fail ("built", "first sample at the RAM base", mem (*sawE), 0x18000);
        ++checks; if (mem (*kkE)  != 0x18000 + 2448)   fail ("built", "second follows: 1222 words in sixteens", mem (*kkE), 0x18000 + 2448);
        ++checks; if (mem (*bigE) != 0x18000 + 2448 + 12000) fail ("built", "third follows", mem (*bigE), 0x18000 + 2448 + 12000);
        ++checks; if (lp (*sawE)  != 0xB6F4)           fail ("built", "first loop descriptor", lp (*sawE), 0xB6F4);
        ++checks; if (lp (*kkE)   != 0xB6F4 + 30)      fail ("built", "a looped sample takes three", lp (*kkE), 0xB6F4 + 30);
        ++checks; if (lp (*bigE)  != 0xB6F4 + 30 + 20) fail ("built", "a one-shot takes two", lp (*bigE), 0xB6F4 + 50);

        // zones, then the arena
        ++checks; if (! d.setZoneSample (*lead, 0, 0, "SAW"))  fail ("built", "zone SAW", 0, 1);
        ++checks; if (! d.setZoneSample (*lead, 1, 0, "LONG")) fail ("built", "zone LONG", 0, 1);
        ++checks; if (! d.setZoneSample (*kit,  0, 0, "KICK")) fail ("built", "zone KICK", 0, 1);
        ++checks; if (d.setZoneSample (*kit, 1, 0, "KICK"))    fail ("built", "no third keygroup to name", 1, 0);

        d.setKeygroupParam (*kit, 0, KeygroupParam::ConstantPitch, 1);
        d.setKeygroupParam (*kit, 0, KeygroupParam::OneShot, 1);
        d.setKeygroupParam (*kit, 0, KeygroupParam::LowKey, 36);
        d.setKeygroupParam (*kit, 0, KeygroupParam::HighKey, 36);

        d.rebuildPointers();
        ++checks;
        if (d.rebuildPointers() != 0) fail ("built", "a second rebuild changes nothing", 1, 0);

        /*
         * LEAD has two keygroups, KIT one. Records: LEAD at 0 and 1, an empty record at 2,
         * KIT at 3; the table starts at record 4. The arena of a fresh disk is 0xC5F6.
         */
        const int arena = 0xC5F6, table = arena + 70 * 4;
        const auto leadRaw = d.readFile (*lead);
        const auto kitRaw  = d.readFile (*kit);
        auto u16 = [] (const std::vector<unsigned char>& b, int at) { return static_cast<int> (b[static_cast<std::size_t> (at)]) | (static_cast<int> (b[static_cast<std::size_t> (at) + 1]) << 8); };

        ++checks; if (u16 (leadRaw, 18) != arena)          fail ("built", "LEAD loads at the arena", u16 (leadRaw, 18), arena);
        ++checks; if (leadRaw[23] != 2)                    fail ("built", "LEAD header counts two", leadRaw[23], 2);
        ++checks; if (u16 (kitRaw, 18) != arena + 70 * 3)  fail ("built", "KIT loads after the empty record", u16 (kitRaw, 18), arena + 210);
        ++checks; if (kitRaw[23] != 1)                     fail ("built", "KIT header counts one", kitRaw[23], 1);

        ++checks; if (u16 (leadRaw, 38 + 68) != arena + 70)      fail ("built", "LEAD kg0 chains to kg1", u16 (leadRaw, 38 + 68), arena + 70);
        ++checks; if (u16 (leadRaw, 38 + 70 + 68) != 0)          fail ("built", "LEAD kg1 ends the chain", u16 (leadRaw, 38 + 70 + 68), 0);
        ++checks; if (u16 (kitRaw, 38 + 68) != 0)                fail ("built", "KIT kg0 ends the chain", u16 (kitRaw, 38 + 68), 0);

        ++checks; if (u16 (leadRaw, 38 + 40) != table)           fail ("built", "SAW is sample 0", u16 (leadRaw, 38 + 40), table);
        ++checks; if (u16 (leadRaw, 38 + 70 + 40) != table + 140) fail ("built", "LONG is sample 2", u16 (leadRaw, 38 + 70 + 40), table + 140);
        ++checks; if (u16 (kitRaw, 38 + 40) != table + 70)       fail ("built", "KICK is sample 1", u16 (kitRaw, 38 + 40), table + 70);
        ++checks; if (u16 (leadRaw, 38 + 62) != 0)               fail ("built", "an empty zone points nowhere", u16 (leadRaw, 38 + 62), 0);

        // and the engine can play it
        auto patch = d.buildPatch (*lead);
        ++checks;
        if (patch == nullptr || patch->keygroups.size() != 2 || patch->keygroups[0].sound == nullptr
            || patch->keygroups[1].sound == nullptr)
            fail ("built", "LEAD plays two keygroups with sounds", 0, 2);

        auto kitPatch = d.buildPatch (*kit);
        ++checks;
        if (kitPatch == nullptr || kitPatch->keygroups.size() != 1 || ! kitPatch->keygroups[0].constantPitch
            || ! kitPatch->keygroups[0].oneShot || kitPatch->keygroups[0].lowKey != 36)
            fail ("built", "KIT is a one-shot drum on 36", 0, 1);

        if (argc >= 4)
        {
            std::ofstream out (argv[3], std::ios::binary);
            const auto& image = d.getImage();
            out.write (reinterpret_cast<const char*> (image.data()), static_cast<std::streamsize> (image.size()));
            std::printf ("  built disk written to %s\n", argv[3]);
        }
    }

    // ------------------------------------------------------------- a synth disk

    /*
     * A recipe rendered to a disk: two oscillators and the drums. The layout it must have
     * - the oscillators layered from the synth's low key up, each drum one key wide on its
     * General MIDI note, constant pitch and one-shot - and the rule that a Program tab edit
     * survives a re-render, which is what makes the two tabs one instrument.
     */
    std::printf ("  a synth disk\n");
    {
        using namespace s950::synth;

        Recipe r = presets()[1].recipe;             // the fat saw: two detuned saws
        r.drumsOn = true;
        r.drums[static_cast<int> (DrumSlot::ride)].on = false;

        WaveCache cache;
        Disk d;
        std::string why;

        ++checks;
        if (! render (r, cache, nullptr, d, why)) { fail ("synth", why.c_str(), 0, 1); return 1; }

        int programCount = 0, samples = 0;
        for (const auto& e : d.getEntries()) { if (e.type == 'P') ++programCount; if (e.type == 'S') ++samples; }

        ++checks; if (programCount != 1)   fail ("synth", "one programme", programCount, 1);
        ++checks; if (samples != 2 + 8)    fail ("synth", "two waves and eight drums", samples, 10);

        const Disk::Entry* prog = d.find ("FAT SAW", 'P');
        ++checks;
        if (prog == nullptr) { fail ("synth", "the programme is named for the recipe", 0, 1); return 1; }

        const auto groups = d.keygroups (*prog);
        ++checks; if (groups.size() != 10) { fail ("synth", "a keygroup per layer", (int) groups.size(), 10); return 1; }

        // the oscillators: whole synth range, tuned, not constant pitch
        ++checks; if (groups[0].zone1.name != "OSC1" || groups[1].zone1.name != "OSC2") fail ("synth", "OSC1 and OSC2 first", 0, 1);
        ++checks; if (groups[0].lowKey != Recipe::SynthLowKeyWithDrums || groups[0].highKey != 127) fail ("synth", "OSC1 spans the synth range", groups[0].lowKey, Recipe::SynthLowKeyWithDrums);
        ++checks; if (groups[0].constantPitch() || groups[0].oneShot()) fail ("synth", "an oscillator tracks the key", 1, 0);

        // A detune of four cents is one sixteenth of a semitone, the machine's step: flat is
        // the high byte -1 and the low 255, sharp is 0 and 1.
        ++checks; if (groups[0].zone1.transpose != -1 || groups[0].zone1.fine != 255) fail ("synth", "OSC1 -4 cents", groups[0].zone1.fine, 255);
        ++checks; if (groups[1].zone1.transpose != 0  || groups[1].zone1.fine != 1)   fail ("synth", "OSC2 +4 cents", groups[1].zone1.fine, 1);
        ++checks; if (groups[0].zone1.pitchOffset() != -1.0 / 16) fail ("synth", "OSC1 plays a sixteenth flat", (int) (groups[0].zone1.pitchOffset() * 16), -1);
        ++checks; if (groups[1].zone1.pitchOffset() !=  1.0 / 16) fail ("synth", "OSC2 plays a sixteenth sharp", (int) (groups[1].zone1.pitchOffset() * 16), 1);

        // the drums: one key each on GM's notes, constant pitch, one-shot, and the ride left out
        bool sawRide = false;
        for (std::size_t k = 2; k < groups.size(); ++k)
        {
            const auto& g = groups[k];
            ++checks; if (g.lowKey != g.highKey)             fail ("synth", "a drum is one key wide", g.highKey, g.lowKey);
            ++checks; if (! g.constantPitch() || ! g.oneShot()) fail ("synth", "a drum is constant pitch, one-shot", 0, 1);
            if (g.zone1.name == "RIDE") sawRide = true;
        }
        ++checks; if (sawRide) fail ("synth", "a drum switched off is not on the disk", 1, 0);

        const Disk::Entry* kick = d.find ("KICK 1", 'S');
        ++checks; if (kick == nullptr || kick->loopMode != 'O') fail ("synth", "KICK 1 is a one-shot", 0, 1);
        int kickKg = -1;
        for (std::size_t k = 0; k < groups.size(); ++k) if (groups[k].zone1.name == "KICK 1") kickKg = (int) k;
        ++checks; if (kickKg < 0 || groups[(std::size_t) kickKg].lowKey != 36) fail ("synth", "the kick is on 36", kickKg < 0 ? -1 : groups[(std::size_t) kickKg].lowKey, 36);

        // it plays
        auto patch = d.buildPatch (*prog);
        ++checks; if (patch == nullptr || patch->keygroups.size() != 10) fail ("synth", "the engine builds all ten", patch ? (int) patch->keygroups.size() : 0, 10);

        // A Program tab edit survives a re-render: change OSC1's decay on the disk, render
        // again with a different level and a different OSC1 wave, and the decay is still
        // there while the level moved.
        d.setKeygroupParam (*prog, 0, KeygroupParam::VcaDecay, 77);
        r.osc[0].level = 50;
        r.osc[0].intensity = 20;                    // a different wave under the same name

        Disk again;
        ++checks;
        if (! render (r, cache, &d, again, why)) { fail ("synth", why.c_str(), 0, 1); return 1; }
        const Disk::Entry* prog2 = again.find ("FAT SAW", 'P');
        ++checks; if (prog2 == nullptr) { fail ("synth", "re-rendered programme", 0, 1); return 1; }
        ++checks; if (again.getKeygroupParam (*prog2, 0, KeygroupParam::VcaDecay) != 77) fail ("synth", "a Program tab edit survives a re-render", again.getKeygroupParam (*prog2, 0, KeygroupParam::VcaDecay), 77);
        ++checks; if (again.getKeygroupParam (*prog2, 0, KeygroupParam::Zone1Loudness) != -25) fail ("synth", "and the recipe's own knob moved", again.getKeygroupParam (*prog2, 0, KeygroupParam::Zone1Loudness), -25);

        // the cache did its job: three oscillator waves (OSC1 twice), eight drums
        ++checks; if (cache.waves.size() != 11) fail ("synth", "the wave cache holds each wave once", (int) cache.waves.size(), 11);

        // Rebuilding the patch from the new disk reuses the samples whose bytes did not
        // change - OSC2 and the drums - and decodes afresh the one that did, OSC1, though
        // its name is the same. That is what keeps a held note following an edit.
        {
            auto before = d.buildPatch (*prog);
            auto after  = again.buildPatch (*prog2, before.get());
            ++checks;
            if (before == nullptr || after == nullptr || after->keygroups.size() != before->keygroups.size())
                fail ("synth", "both patches build", 0, 1);
            else
            {
                ++checks; if (after->keygroups[0].sound == before->keygroups[0].sound) fail ("synth", "a re-rendered OSC1 is a new sound", 1, 0);
                ++checks; if (after->keygroups[1].sound != before->keygroups[1].sound) fail ("synth", "an unchanged OSC2 is the same sound", 0, 1);
                ++checks; if (after->keygroups[2].sound != before->keygroups[2].sound) fail ("synth", "an unchanged drum is the same sound", 0, 1);
            }
        }

        // text and back
        const auto back = fromText (toText (r));
        ++checks; if (toText (back) != toText (r)) fail ("synth", "the recipe survives being text", 0, 1);

        if (argc >= 4)
        {
            std::string path = argv[3];
            const auto dot = path.rfind ('.');
            path = (dot == std::string::npos ? path : path.substr (0, dot)) + "-synth.img";
            std::ofstream out (path, std::ios::binary);
            const auto& image = again.getImage();
            out.write (reinterpret_cast<const char*> (image.data()), static_cast<std::streamsize> (image.size()));
            std::printf ("  synth disk written to %s\n", path.c_str());
        }
    }

    // ------------------------------------------------------------- the edited copy

    if (argc >= 3)
    {
        Disk d = original;

        int pi = 0;
        for (const auto& e : d.getEntries())
        {
            if (e.type != 'P') continue;

            const int n = Disk::keygroupCount (e);
            for (int kg = 0; kg < n; ++kg)
                for (int p = 0; p < static_cast<int> (KeygroupParam::Count); ++p)
                {
                    const auto param = static_cast<KeygroupParam> (p);
                    d.setKeygroupParam (e, kg, param,
                                        scrambled (s950::keygroupParamInfo (param), pi, kg, p));
                }
            ++pi;
        }

        std::ofstream out (argv[2], std::ios::binary);
        const auto& image = d.getImage();
        out.write (reinterpret_cast<const char*> (image.data()),
                   static_cast<std::streamsize> (image.size()));

        if (! out)
        {
            std::fprintf (stderr, "could not write %s\n", argv[2]);
            return 2;
        }

        // And read back from the file, not from memory: what was saved is what counts.
        Disk reread;
        ++checks;
        if (! reread.loadFile (argv[2], error))
            fail (argv[2], "the edited copy will not open", 0, 1);
        else
        {
            pi = 0;
            for (const auto& e : reread.getEntries())
            {
                if (e.type != 'P') continue;

                const int n = Disk::keygroupCount (e);
                for (int kg = 0; kg < n; ++kg)
                    for (int p = 0; p < static_cast<int> (KeygroupParam::Count); ++p)
                    {
                        const auto  param = static_cast<KeygroupParam> (p);
                        const int   want  = scrambled (s950::keygroupParamInfo (param), pi, kg, p);
                        ++checks;
                        const int   got   = reread.getKeygroupParam (e, kg, param);
                        if (got != want)
                            fail (e.name + " kg" + std::to_string (kg) + " (saved)",
                                  s950::keygroupParamInfo (param).name, got, want);
                    }
                ++pi;
            }
        }

        std::printf ("  edited copy written to %s and read back\n", argv[2]);
    }

    /*
     * A high-density disk: 1600 blocks, a five-block header, and a FAT of 1600 entries
     * that is the DD one carried on past its 800th (akaiutil's akai_flhhead_s). Filled past
     * block 800 so the second half of the table is exercised, then read back byte for byte.
     * With a third argument the image is saved there, for akaiutil to read.
     */
    std::printf ("  a high-density disk\n");
    {
        Disk d = Disk::blank ("HD", true);
        std::string why;

        ++checks; if (d.getImage().size() != 1638400) fail ("hd", "1600K", static_cast<int> (d.getImage().size() / 1024), 1600);
        ++checks; if (! d.isHighDensity() || d.headerBlocks() != 5) fail ("hd", "a five-block header", d.headerBlocks(), 5);

        std::vector<std::vector<short>> waves;
        for (int k = 0; k < 6; ++k)
        {
            std::vector<short> w (140000);
            for (int i = 0; i < 140000; ++i)
                w[static_cast<std::size_t> (i)] = static_cast<short> (((i * (k + 3)) % 4096) - 2048);
            waves.push_back (w);

            Disk::NewSample s; s.name = "HD" + std::to_string (k); s.words12 = w; s.rate = 48000;
            ++checks; if (! d.addSample (s, why)) fail ("hd", ("sample " + std::to_string (k) + ": " + why).c_str(), 0, 1);
        }
        ++checks; if (! d.addProgram ("HD PROG", 1, why)) fail ("hd", why.c_str(), 0, 1);

        // nothing starts inside the header, and the files reach past block 800
        int lowest = 1 << 30, highest = 0;
        for (const auto& e : d.getEntries())
        {
            lowest = std::min (lowest, e.startBlock);
            highest = std::max (highest, e.startBlock + e.chainBlocks - 1);
        }
        ++checks; if (lowest != 5) fail ("hd", "file data starts at block 5", lowest, 5);
        ++checks; if (highest <= 800) fail ("hd", "the files reach past block 800", highest, 801);

        // read back from the bytes alone, as a saved disk would be
        Disk back;
        ++checks;
        if (! back.loadBytes ("HD", d.getImage(), why)) fail ("hd", ("reload: " + why).c_str(), 0, 1);
        else
        {
            ++checks; if (back.getEntries().size() != 7) fail ("hd", "seven files come back", static_cast<int> (back.getEntries().size()), 7);
            for (int k = 0; k < 6; ++k)
            {
                const Disk::Entry* e = back.find ("HD" + std::to_string (k), 'S');
                ++checks;
                if (e == nullptr) { fail ("hd", "a sample comes back by name", 0, 1); continue; }
                ++checks; if (e->sampleRate != 48000) fail ("hd", "48 kHz survives", e->sampleRate, 48000);
                const auto got = back.sampleWords12 (*e);
                ++checks;
                if (got != waves[static_cast<std::size_t> (k)]) fail ("hd", ("sample " + std::to_string (k) + " audio round-trips").c_str(), static_cast<int> (got.size()), 140000);
            }
        }

        if (argc > 3)
        {
            std::ofstream out (argv[3], std::ios::binary);
            out.write (reinterpret_cast<const char*> (d.getImage().data()), static_cast<std::streamsize> (d.getImage().size()));
            std::printf ("  high-density image written to %s\n", argv[3]);
        }
    }

    std::printf ("  %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
