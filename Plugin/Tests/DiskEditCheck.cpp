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

    std::printf ("  %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
