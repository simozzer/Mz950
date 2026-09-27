using System;
using System.Collections.Generic;
using System.IO;
using AkaiS950List;

namespace AkaiS950Synth
{
    /// <summary>
    /// Build S950 disks full of synthesiser sounds, from nothing.
    ///
    ///     AkaiS950Synth [folder] [--img] [--list] [--wav]
    ///
    /// No samples are recorded and none are read: every waveform is worked out from its
    /// harmonics, band-limited to what the sample rate can hold, and written straight into
    /// a disk image along with programmes that set the machine's filter, envelopes and
    /// vibrato around it.
    ///
    /// The result loads on a real S950, in AkaiS950Studio, and in the plugin - the same
    /// disks in all three, because all three read the same format and share the same
    /// measured engine.
    /// </summary>
    internal static class Program
    {
        /// <summary>A disk holds 64 directory entries, and a sample and a programme each take one.</summary>
        const int DirectoryLimit = 64;

        static int Main(string[] args)
        {
            string folder = null;
            bool listOnly = false, raw = false, audition = false;

            foreach (var a in args)
            {
                if (a == "--list") listOnly = true;
                else if (a == "--img") raw = true;
                else if (a == "--wav") audition = true;
                else if (!a.StartsWith("--")) folder = a;
            }

            if (listOnly) { List(); return 0; }
            if (folder == null) folder = ".";

            try
            {
                Directory.CreateDirectory(folder);

                if (audition) { Audition(folder); return 0; }

                foreach (var bank in Patches.Banks())
                    BuildBank(bank, folder, raw ? "img" : "hfe");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("failed: " + ex.Message);
                return 1;
            }

            return 0;
        }

        // --------------------------------------------------------------------- listing

        static void List()
        {
            foreach (var bank in Patches.Banks())
            {
                var waves = WavesFor(bank);

                Console.WriteLine();
                Console.WriteLine("  {0}   {1} programmes + {2} samples = {3} of {4} files",
                                  bank.Name, bank.Programmes.Count, waves.Count,
                                  bank.Programmes.Count + waves.Count, DirectoryLimit);

                foreach (var p in bank.Programmes)
                {
                    var layers = new List<string>();
                    foreach (var l in p.Layers)
                        layers.Add(l.Sample
                                   + (l.Loudness != 0 ? " " + l.Loudness.ToString("+0;-0") : "")
                                   + Range(l)
                                   + (l.Hard != null
                                      ? " >" + l.Hard.At + "> " + l.Hard.Sample : ""));

                    //
                    // A synthesiser patch is a layer or three and fits on a line. A drum
                    // kit is eighteen keygroups, and run together they are a wall of text
                    // that nobody reads - which defeats the point of a listing whose whole
                    // job is to be read before a disk is written.
                    //
                    const int PerLine = 4;

                    for (int i = 0; i < layers.Count; i += PerLine)
                    {
                        var line = layers.GetRange(i, Math.Min(PerLine, layers.Count - i));

                        Console.WriteLine("    {0,-10} {1}{2}",
                                          i == 0 ? p.Name : "",
                                          string.Join(" + ", line.ToArray()),
                                          i + PerLine < layers.Count ? " +" : "");
                    }
                }
            }

            Console.WriteLine();
        }

        /// <summary>The part of the keyboard a layer answers to, when it is not all of it.</summary>
        static string Range(Layer l)
        {
            if (l.LowKey == null && l.HighKey == null) return "";
            return " [" + (l.LowKey ?? 0) + ".." + (l.HighKey ?? 127) + "]";
        }

        /// <summary>
        /// The waves a bank needs, worked out from the layers that name them.
        ///
        /// Derived rather than listed, so a disk can never be missing a sample one of its
        /// programmes refers to - which on this machine is not a warning but a keygroup
        /// that silently plays nothing - or be carrying one that nothing plays. It is also
        /// what makes the generated programmes free: twenty of them over five waves ask
        /// the disk for five samples.
        /// </summary>
        static List<Patches.Wave> WavesFor(Patches.Bank bank)
        {
            var wanted = new List<string>();

            foreach (var p in bank.Programmes)
                foreach (var l in p.Layers)
                {
                    if (!wanted.Contains(l.Sample)) wanted.Add(l.Sample);

                    // The hard strike's wave is a file on the disk like any other, and a
                    // zone naming a sample that is not there plays nothing at all.
                    if (l.Hard != null && !wanted.Contains(l.Hard.Sample))
                        wanted.Add(l.Hard.Sample);
                }

            var outp = new List<Patches.Wave>();
            foreach (var w in Patches.Waves())
                if (wanted.Contains(w.Name)) outp.Add(w);

            // A name that matches no wave is a typo, and finding out here beats finding out
            // from a keygroup that plays nothing.
            foreach (var name in wanted)
            {
                bool found = false;
                foreach (var w in outp) if (w.Name == name) { found = true; break; }
                if (!found) throw new InvalidOperationException("no wave called '" + name + "'");
            }

            return outp;
        }

        // -------------------------------------------------------------------- building

        // ------------------------------------------------------------------ auditioning

        /// <summary>
        /// Every one-shot as a WAV, to listen to.
        ///
        /// A disk image cannot be played, and a drum is judged by ear or not at all - no
        /// checksum says a kick is thin or a hat is airy. The looped waves are left out:
        /// they are eight cycles of a waveform and there is nothing to hear in one that
        /// the machine's envelope is not going to decide.
        ///
        /// What comes out is the words the disk carries, shifted up four bits to fill
        /// sixteen. It is not what the S950 sounds like - that adds the machine's own
        /// reconstruction filter, and the plugin is where to hear THAT - but it is
        /// exactly what is going in.
        /// </summary>
        static void Audition(string folder)
        {
            Console.WriteLine();

            foreach (var w in Patches.Waves())
            {
                if (w.Oneshot == null) continue;

                var words = w.Oneshot();
                string path = Path.Combine(folder, w.Name.Replace(' ', '-').ToLowerInvariant() + ".wav");
                WriteWav(path, words, w.Rate);

                Console.WriteLine("    {0,-10} {1,6} words  {2,6} Hz   {3}",
                                  w.Name, words.Length, w.Rate, path);
            }

            Console.WriteLine();
        }

        /// <summary>16-bit mono WAV. The 12-bit words are shifted up four, not rescaled.</summary>
        static void WriteWav(string path, short[] words, int rate)
        {
            using (var f = new BinaryWriter(File.Create(path)))
            {
                int bytes = words.Length * 2;

                f.Write(new[] { 'R', 'I', 'F', 'F' });
                f.Write(36 + bytes);
                f.Write(new[] { 'W', 'A', 'V', 'E' });
                f.Write(new[] { 'f', 'm', 't', ' ' });
                f.Write(16);
                f.Write((short)1);
                f.Write((short)1);
                f.Write(rate);
                f.Write(rate * 2);
                f.Write((short)2);
                f.Write((short)16);
                f.Write(new[] { 'd', 'a', 't', 'a' });
                f.Write(bytes);

                foreach (short v in words) f.Write((short)(v * 16));
            }
        }

        /// <summary>The words a wave is made of, whichever kind of wave it is.</summary>
        static short[] Words(Patches.Wave w)
        {
            if (w.Oneshot != null) return w.Oneshot();
            if (w.IsNoise) return Waveforms.Hiss(2000, 12345);

            int highest = Waveforms.HighestHarmonic(w.Rate, Patches.RootHz);
            return Waveforms.Render(w.Table(highest), w.Cycles, w.Rate, Patches.RootHz, w.Shake);
        }

        static void BuildBank(Patches.Bank bank, string folder, string format)
        {
            var waves = WavesFor(bank);

            int files = waves.Count + bank.Programmes.Count;
            if (files > DirectoryLimit)
                throw new InvalidOperationException(
                    bank.Name + " wants " + files + " files and a disk holds " + DirectoryLimit);

            //
            // An S950 disk is empty when its directory and allocation table are zero, so an
            // image of nothing but zeroes is already a formatted blank.
            //
            var disk = AkaiDisk.LoadFromBytes(bank.Name, new byte[800 * 1024]);

            Console.WriteLine();
            Console.WriteLine("  {0}", bank.Name);

            foreach (var w in waves)
            {
                short[] words = Words(w);

                //
                // Looped, and the loop is the whole sample. AddSample defaults the length to
                // 2000 words, which is right only for the short ones, so it is written out
                // explicitly - a loop over part of a wavetable would sweep through half of
                // it and jump back.
                //
                // A one-shot takes none of that: it is played once, to the end, and the
                // loop fields are never read.
                //
                bool once = w.Oneshot != null;
                var e = disk.AddSample(w.Name, words, w.Rate,
                                       w.Root.HasValue ? w.Root.Value : Patches.RootNote,
                                       0, once ? 'O' : 'L');
                if (!once) SetLoopLength(disk, e, words.Length);

                Console.WriteLine("    wave  {0,-10} {1,6} words  {2,6} Hz{3}",
                                  w.Name, words.Length, w.Rate,
                                  once ? "   one-shot" : "");
            }

            foreach (var p in bank.Programmes)
            {
                var prog = disk.AddProgram(p.Name, null);

                // AddProgram starts a programme with one keygroup; a layer each after that.
                for (int i = 1; i < p.Layers.Length; i++) disk.AddKeygroup(prog, 0);

                //
                // And count them, because writing to a keygroup that is not there is not an
                // error - SetKeygroupByte simply has nowhere to put it. A three-layer patch
                // that came out with two layers looked entirely healthy from every other
                // angle for as long as nobody counted.
                //
                int have = AkaiDisk.KeygroupCount(prog);
                if (have != p.Layers.Length)
                    throw new InvalidOperationException(
                        p.Name + " wanted " + p.Layers.Length + " keygroups and has " + have);

                for (int i = 0; i < p.Layers.Length; i++)
                    WriteKeygroup(disk, prog, i, p, p.Layers[i]);

                Console.WriteLine("    prog  {0,-10} {1} layer(s)", p.Name, p.Layers.Length);
            }

            //
            // Zone pointers are positions in the keygroup arena, derived from the directory
            // rather than stored independently, so they are recomputed once everything is
            // in place. Getting this wrong is what crashes a real S950.
            //
            int repaired = disk.RepairKeygroupChains();

            string path = Path.Combine(folder, bank.Name + "." + format);
            disk.SaveAs(path, format);

            int sounding = Verify(path, format);

            Console.WriteLine("    -> {0}   {1} files, {2} blocks, {3} zones sound{4}",
                              path, files, disk.UsedBlocks, sounding,
                              repaired > 0 ? ", " + repaired + " pointer(s) fixed" : "");
        }

        /// <summary>
        /// Read the disk back and check that every zone will actually play something, and
        /// return how many there are.
        ///
        /// THIS EXISTS BECAUSE IT DID NOT, ONCE
        ///
        /// SetZoneSample counts zones from zero. Told to write zone "1", it put every
        /// sample name into the SECOND zone - which a velocity switch of 128 makes
        /// unreachable - and left the first holding the template's placeholder. Five disks
        /// were written, both readers parsed them identically, every dump looked plausible,
        /// and not one programme could make a sound: each built a patch with no keygroups
        /// in it, which a host quietly declines, so every programme played whatever had
        /// been playing before.
        ///
        /// Nothing upstream could have caught that. The bytes were valid, the structure was
        /// right, the readers agreed. The only question that would have found it is the one
        /// asked here: given this disk, does this programme have a zone naming a sample that
        /// is on it?
        /// </summary>
        static int Verify(string path, string format)
        {
            var disk = AkaiDisk.Load(path);

            var samples = new List<string>();
            foreach (var e in disk.Entries)
                if (e.Type == 'S') samples.Add(e.Name.Trim());

            int sounding = 0;

            foreach (var e in disk.Entries)
            {
                if (e.Type != 'P') continue;

                var groups = disk.Keygroups(e);
                if (groups.Count == 0)
                    throw new InvalidOperationException(e.Name.Trim() + " has no keygroups");

                foreach (var kg in groups)
                {
                    string where = e.Name.Trim() + " keygroup " + (kg.Index + 1);

                    var zone = kg.Zone1;

                    if (zone == null || !zone.InUse)
                        throw new InvalidOperationException(
                            where + " has nothing in zone 1 - it would be silent");

                    if (!samples.Contains(zone.Name.Trim()))
                        throw new InvalidOperationException(
                            where + " names '" + zone.Name.Trim() +
                            "', which is not on this disk");

                    sounding++;

                    //
                    // And the same of the second zone, when the switch says there is one.
                    //
                    // A switch below 128 hands every velocity at or above it to zone 2, so
                    // a zone 2 that is empty or names a sample the disk does not have is a
                    // hole in the keyboard: play it hard enough and nothing happens. That
                    // is the velocity-shaped version of the bug this whole check exists
                    // for, and it would be just as quiet.
                    //
                    if (kg.VelocitySwitchOff) continue;

                    if (!kg.HasSecondZone)
                        throw new InvalidOperationException(
                            where + " switches to zone 2 at velocity " + kg.VelocitySwitch +
                            " and has nothing there - anything harder would be silent");

                    if (!samples.Contains(kg.Zone2.Name.Trim()))
                        throw new InvalidOperationException(
                            where + " zone 2 names '" + kg.Zone2.Name.Trim() +
                            "', which is not on this disk");

                    sounding++;
                }
            }

            return sounding;
        }

        /// <summary>
        /// Loop the whole sample.
        ///
        /// The machine plays end-length .. end round and round, so a loop length equal to
        /// the sample makes the loop the entire waveform - which for a wave built from a
        /// whole number of cycles joins without a click by construction.
        /// </summary>
        static void SetLoopLength(AkaiDisk disk, AkaiEntry e, int words)
        {
            for (int i = 0; i < 4; i++)
                disk.PokeFile(e, 0x24 + i, (byte)((words >> (8 * i)) & 0xFF));
        }

        // ------------------------------------------------------------------- keygroups

        static void WriteKeygroup(AkaiDisk disk, AkaiEntry prog, int index, Patch p, Layer layer)
        {
            //
            // Which keys this layer answers to. The whole keyboard unless the layer says
            // otherwise - most of these are synthesiser patches rather than multisamples,
            // and a stack wants every layer under every key.
            //
            // Where a layer does say otherwise it is a split, and it needs nothing else:
            // the keygroup has been a key range all along.
            //
            Set(disk, prog, index, 1, layer.LowKey ?? 0);          // low key
            Set(disk, prog, index, 0, layer.HighKey ?? 127);       // high key

            //
            // Where the second zone takes over. 128 is how the panel says there is no
            // second zone at all - no velocity can reach it - and that is what a layer
            // with no hard strike gets.
            //
            int switchAt = layer.Hard == null ? 128 : layer.Hard.At;
            if (switchAt < 1) switchAt = 1;
            if (switchAt > 128) switchAt = 128;
            Set(disk, prog, index, 2, switchAt);

            //
            // Each layer takes the patch's settings unless it says otherwise. That is what
            // lets one keygroup be struck and gone while the next is still arriving, and
            // what lets the top of a stack stay bright while its bottom stays round - the
            // envelope, the filter, the vibrato and the velocity response all belong to the
            // keygroup on this machine, not to the programme.
            //
            Set(disk, prog, index, 3, layer.A ?? p.A);
            Set(disk, prog, index, 4, layer.D ?? p.D);
            Set(disk, prog, index, 5, layer.S ?? p.S);
            Set(disk, prog, index, 6, layer.R ?? p.R);

            Set(disk, prog, index, 7, layer.VelToFilter ?? p.VelToFilter);
            Set(disk, prog, index, 8, layer.KeyToFilter ?? p.KeyToFilter);
            Set(disk, prog, index, 11, layer.VelToLoudness ?? p.VelToLoudness);

            Set(disk, prog, index, 15, layer.LfoDelay ?? p.LfoDelay);
            Set(disk, prog, index, 16, layer.LfoRate ?? p.LfoRate);
            Set(disk, prog, index, 17, layer.LfoDepth ?? p.LfoDepth);
            Set(disk, prog, index, 22, p.LfoModwheel);

            //
            // Bit 0 constant pitch, bit 2 LFO desync, bit 3 one-shot.
            //
            // Desync is set because these are synthesiser voices: each note wants its own
            // vibrato rather than all of them wobbling in lockstep, which is what the bit
            // was measured to do.
            //
            // Constant pitch is bit 0: the sample plays at its own rate whatever key asks
            // for it, which is what a drum kit wants and a tuned patch never does.
            int flags = 0x04;
            if (p.OneShot) flags |= 0x08;
            if (p.ConstantPitch) flags |= 0x01;
            Set(disk, prog, index, 18, flags);

            Set(disk, prog, index, 23, Signed(layer.VcfAmount ?? p.VcfAmount));
            Set(disk, prog, index, 34, layer.VcfA ?? p.VcfA);
            Set(disk, prog, index, 35, layer.VcfD ?? p.VcfD);
            Set(disk, prog, index, 36, layer.VcfS ?? p.VcfS);
            Set(disk, prog, index, 37, layer.VcfR ?? p.VcfR);

            //
            // Zone 1: the sample, then its trim. 24 is the name, 42..45 the rest.
            //
            // SetZoneSample counts zones from ZERO - it multiplies the argument by the zone
            // stride - so zone 1 is 0 here. Passing 1 puts the name 22 bytes further along
            // in zone 2, which leaves zone 1 holding the template's "2 SAMPLE" placeholder
            // and the trim bytes written below, and zone 2 holding a name that a velocity
            // switch of 128 means nothing can ever reach. The programme then has no
            // playable keygroup at all, the host declines to load an empty patch, and every
            // programme sounds like whatever was playing before it.
            //
            disk.SetZoneSample(prog, index, 0, layer.Sample);
            int fine, transpose;
            ZonePitch(layer.Transpose, layer.Fine, out fine, out transpose);
            Set(disk, prog, index, 42, fine);
            Set(disk, prog, index, 43, transpose);
            Set(disk, prog, index, 44, layer.Filter ?? p.Filter);
            Set(disk, prog, index, 45, Signed(layer.Loudness));

            //
            // Zone 2: the same four bytes, one zone stride further on. The zone record is
            // 22 bytes and the first starts at 24, so the name is at 46 and its trim at
            // 64..67 - which is everything up to the chain pointer at 68.
            //
            // Left alone when there is no hard strike. AddProgram's template writes the
            // "2 SAMPLE" placeholder there, which is how the machine itself marks a zone
            // unused, and the switch of 128 above means nothing would reach it anyway.
            //
            if (layer.Hard != null)
            {
                disk.SetZoneSample(prog, index, 1, layer.Hard.Sample);
                ZonePitch(layer.Hard.Transpose, layer.Hard.Fine, out fine, out transpose);
                Set(disk, prog, index, 64, fine);
                Set(disk, prog, index, 65, transpose);
                Set(disk, prog, index, 66, layer.Hard.Filter ?? layer.Filter ?? p.Filter);
                Set(disk, prog, index, 67, Signed(layer.Hard.Loudness));
            }
        }

        static void Set(AkaiDisk disk, AkaiEntry prog, int index, int offset, int value)
        {
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            disk.SetKeygroupByte(prog, index, offset, (byte)value);
        }

        /// <summary>A signed byte as the machine stores it.</summary>
        /// <summary>
        /// A zone's pitch as the machine reads it: the transpose (high) and fine (low) bytes
        /// are one signed 16-bit count of SIXTEENTHS of a semitone - measured, see
        /// AkaiDisk.Zone.PitchOffset. A patch's Fine is in 256ths; the machine cannot go finer
        /// than a sixteenth (6.25 cents), so a detune is rounded to it but never to nothing -
        /// a whisker of detune is the point of the layers that ask for one.
        /// </summary>
        static void ZonePitch(int semitones, int fine256, out int fineByte, out int transposeByte)
        {
            int f = (int)Math.Round(fine256 / 16.0, MidpointRounding.AwayFromZero);
            if (f == 0 && fine256 != 0) f = fine256 < 0 ? -1 : 1;

            int sixteenths = semitones * 16 + f;
            int high = sixteenths >= 0 ? sixteenths / 256 : -((255 - sixteenths) / 256);
            fineByte = sixteenths - high * 256;
            transposeByte = Signed(high);
        }

        static int Signed(int v)
        {
            if (v < -128) v = -128;
            if (v > 127) v = 127;
            return v < 0 ? 256 + v : v;
        }
    }
}
