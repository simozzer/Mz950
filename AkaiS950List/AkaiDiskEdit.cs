using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace AkaiS950List
{
    /// <summary>
    /// The editing operations that resize or reorder a disk's files: deleting a sample,
    /// creating and removing programs, slicing a break into one-shots, and pointing
    /// keygroup zones at samples.
    ///
    /// They share one discipline. Every one of them finishes by reparsing the directory
    /// and calling <see cref="RebuildPointers"/>, because each changes a count the arena
    /// is derived from - the number of samples, programs or keygroups - and the stored
    /// pointers are recomputed from that layout rather than nudged by a delta.
    /// </summary>
    public sealed partial class AkaiDisk
    {
        /// <summary>The sampler's memory with the usual expansion fitted.</summary>
        public const int DefaultSamplerRam = 2304 * 1024;

        static int ClampKey(int k) { return k < 0 ? 0 : k > 127 ? 127 : k; }

        /// <summary>The placeholder name and zero pointer the S950 leaves in an unused zone.</summary>
        const string UnusedZone = "2 SAMPLE";

        // ------------------------------------------------------------ zones

        /// <summary>One keygroup zone that names a particular sample.</summary>
        public sealed class ZoneUse
        {
            public string Program;
            public int Keygroup;        // 1-based, as the list shows it
            public int Zone;            // 1 or 2

            public override string ToString()
            {
                return Program.Trim() + "  keygroup " + Keygroup + ", zone " + Zone;
            }
        }

        /// <summary>Every keygroup zone on this disk that names the given sample.</summary>
        public List<ZoneUse> SampleUsers(string name)
        {
            var users = new List<ZoneUse>();
            string want = (name ?? "").Trim().ToUpperInvariant();
            if (want.Length == 0) return users;

            foreach (var p in Entries.Where(e => e.Type == 'P').OrderBy(e => e.Slot))
            {
                var groups = Keygroups(p);
                for (int k = 0; k < groups.Count; k++)
                {
                    var zones = new[] { groups[k].Zone1, groups[k].Zone2 };
                    for (int z = 0; z < 2; z++)
                    {
                        if (zones[z] == null) continue;
                        if ((zones[z].Name ?? "").Trim().ToUpperInvariant() == want)
                            users.Add(new ZoneUse { Program = p.Name, Keygroup = k + 1, Zone = z + 1 });
                    }
                }
            }
            return users;
        }

        /// <summary>Points a keygroup zone at a sample: its name, and the pointer that goes with it.</summary>
        public void SetZoneSample(AkaiEntry program, int index, int zone, string name)
        {
            string clean = NormaliseName(name);
            int at = ProgHeaderSize + index * KeygroupSize + KeygroupNameOffset + zone * KeygroupZoneStride;

            for (int i = 0; i < 10; i++)
                PokeFile(program, at + i, (byte)(i < clean.Length ? clean[i] : ' '));

            int ptr = SampleReference(clean);
            PokeFile(program, at + 16, (byte)(ptr & 0xFF));
            PokeFile(program, at + 17, (byte)((ptr >> 8) & 0xFF));
        }

        /// <summary>Clears a zone, which is how the S950 marks one unused.</summary>
        public void ClearZone(AkaiEntry program, int index, int zone)
        {
            int at = ProgHeaderSize + index * KeygroupSize + KeygroupNameOffset + zone * KeygroupZoneStride;

            for (int i = 0; i < 10; i++)
                PokeFile(program, at + i, (byte)(i < UnusedZone.Length ? UnusedZone[i] : ' '));
            PokeFile(program, at + 16, 0);
            PokeFile(program, at + 17, 0);
        }

        /// <summary>Write one byte of one keygroup record.</summary>
        public void SetKeygroupByte(AkaiEntry program, int index, int offset, byte value)
        {
            PokeFile(program, ProgHeaderSize + index * KeygroupSize + offset, value);
        }

        /// <summary>The directory entry in a given slot, or null.</summary>
        public AkaiEntry EntryInSlot(int slot)
        {
            foreach (var e in Entries) if (e.Slot == slot) return e;
            return null;
        }

        // ------------------------------------------------------- the directory

        /// <summary>Directory slots not yet spoken for.</summary>
        public int FreeSlots()
        {
            int used = 0;
            for (int i = 0; i < DirEntries; i++)
                if (Image[DirOffset + i * EntrySize] != 0) used++;
            return DirEntries - used;
        }

        /// <summary>
        /// Squeeze the empty slots out of the directory, keeping the order of what is left.
        ///
        /// Every one of the 100 factory images holds its files in slots 0..n-1 with no holes,
        /// so the sampler is entitled to read the directory until the first empty slot and
        /// stop. Deleting a file by blanking its slot leaves exactly such a hole, and anything
        /// past it becomes invisible - or worse, the programs before it load and then reference
        /// samples that never arrived. Compacting keeps the shape the sampler expects. Relative
        /// order is preserved, so the position-based zone pointers stay correct.
        /// </summary>
        public int CompactDirectory()
        {
            int moved = 0, write = 0;
            for (int read = 0; read < DirEntries; read++)
            {
                int from = DirOffset + read * EntrySize;
                if (Image[from] == 0) continue;

                if (write != read)
                {
                    int to = DirOffset + write * EntrySize;
                    Array.Copy(Image, from, Image, to, EntrySize);
                    Array.Clear(Image, from, EntrySize);
                    moved++;
                }
                write++;
            }
            if (moved > 0) { Modified = true; ParseDirectory(); }
            return moved;
        }

        /// <summary>
        /// Open a gap at <paramref name="slot"/>, shifting every entry above it up one place.
        ///
        /// Programs come before the overall settings, the drum set and the samples - 98 of the
        /// 100 library disks are exactly that order - so a new program cannot simply be appended
        /// to the end of the directory. Relative order is preserved, so sample positions, and
        /// with them the zone pointers, are unaffected.
        /// </summary>
        public void MakeRoomAt(int slot)
        {
            if (FreeSlots() < 1)
                throw new InvalidOperationException("The directory is full: 64 files is the limit.");

            for (int i = DirEntries - 1; i > slot; i--)
                Array.Copy(Image, DirOffset + (i - 1) * EntrySize,
                           Image, DirOffset + i * EntrySize, EntrySize);

            Array.Clear(Image, DirOffset + slot * EntrySize, EntrySize);

            Modified = true;
            ParseDirectory();
        }

        // ------------------------------------------------------ deleting a sample

        /// <summary>What deleting a sample cost, for the report afterwards.</summary>
        public sealed class DeleteResult
        {
            public int ZonesCleared;
            public int PointersAdjusted;
            public int BlocksFreed;
        }

        /// <summary>
        /// Removes a sample and everything that pointed at it.
        ///
        /// Three things have to move together or the disk is left inconsistent:
        ///   - zones naming it are cleared to the unused placeholder;
        ///   - zones naming a *later* sample have their pointer pulled back one record,
        ///     because those pointers are the sample's position in directory order and
        ///     every later sample has just moved down one;
        ///   - later samples shift down in sample RAM by the space this one occupied.
        /// Then its blocks go back to the free pool and its directory slot is cleared.
        /// </summary>
        public DeleteResult DeleteSample(AkaiEntry e)
        {
            if (e == null || e.Type != 'S')
                throw new ArgumentException("not a sample", "e");

            var samples = SamplesInOrder();
            var byName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            int index = -1;

            for (int i = 0; i < samples.Count; i++)
            {
                byName[samples[i].Name.Trim()] = i;
                if (samples[i].Slot == e.Slot) index = i;
            }
            if (index < 0) throw new InvalidOperationException("that sample is not on this disk");

            string gone = e.Name.Trim();
            var result = new DeleteResult();

            foreach (var p in Entries.Where(x => x.Type == 'P').OrderBy(x => x.Slot).ToList())
            {
                var data = ReadFile(p);
                int count = KeygroupCount(p);

                for (int k = 0; k < count; k++)
                {
                    for (int z = 0; z < 2; z++)
                    {
                        int off = ProgHeaderSize + k * KeygroupSize
                                + KeygroupNameOffset + z * KeygroupZoneStride;
                        if (off + 18 > data.Length) continue;

                        string name = CleanName(data, off).Trim();

                        if (string.Equals(name, gone, StringComparison.OrdinalIgnoreCase))
                        {
                            ClearZone(p, k, z);
                            result.ZonesCleared++;
                            continue;
                        }

                        int at;
                        if (!byName.TryGetValue(name, out at) || at <= index) continue;

                        int ptr = data[off + 16] | (data[off + 17] << 8);
                        if (ptr == 0) continue;

                        int moved = ptr - KeygroupSize;
                        PokeFile(p, off + 16, (byte)(moved & 0xFF));
                        PokeFile(p, off + 17, (byte)((moved >> 8) & 0xFF));
                        result.PointersAdjusted++;
                    }
                }
            }

            ShiftSampleRam(e.Slot,
                           -RamSize(e.SampleCount),
                           -10 * LoopRecords(e.SampleCount, e.LoopMode));

            var chain = Chain(e.StartBlock);
            foreach (int b in chain) SetFat(b, 0);
            result.BlocksFreed = chain.Count;

            Array.Clear(Image, DirOffset + e.Slot * EntrySize, EntrySize);

            Modified = true;
            CompactDirectory();
            ParseDirectory();
            RebuildPointers();
            ParseDirectory();
            return result;
        }

        // ------------------------------------------------------ programs: add/delete
        //
        // A program file is a 38-byte header and one or more 70-byte keygroups. Rather than
        // invent one, these templates are the byte-by-byte mode of the library's 388 programs
        // and 1902 keygroups, so a program this tool creates is indistinguishable from one the
        // Akai wrote except in the fields that must differ.

        static readonly byte[] ProgramTemplate = {
            67, 79, 77, 32, 32, 32, 32, 32, 32, 32, 0, 32, 32, 32, 32, 32, 0, 0, 246, 197,
            0, 0, 255, 1, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };

        static readonly byte[] KeygroupTemplate = {
            127, 24, 128, 0, 80, 99, 30, 10, 50, 0, 0, 30, 0, 0, 99, 64, 64, 0, 4, 255,
            0, 0, 50, 0, 84, 79, 78, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
            0, 211, 0, 0, 99, 0, 50, 32, 83, 65, 77, 80, 76, 69, 32, 32, 32, 32, 32, 32,
            32, 32, 0, 0, 0, 0, 99, 0, 0, 0
        };

        /// <summary>The lowest MIDI program number no program on this disk is using.</summary>
        public int FreeProgramNumber()
        {
            var taken = new bool[128];
            foreach (var p in ProgramsInOrder())
            {
                var raw = ReadFile(p);
                if (raw.Length > 26) taken[raw[26] & 0x7F] = true;
            }
            for (int n = 0; n < 128; n++) if (!taken[n]) return n;
            return 0;
        }

        /// <summary>Optional settings for a new program; anything left null takes its default.</summary>
        public sealed class NewProgram
        {
            public int? ProgramNumber;
            public int? LowKey;
            public int? HighKey;
        }

        /// <summary>
        /// A new program holding one empty keygroup across the whole keyboard. It is placed
        /// after the last program, before everything else, and the arena is rebuilt around it.
        /// </summary>
        public AkaiEntry AddProgram(string name, NewProgram opts)
        {
            if (opts == null) opts = new NewProgram();

            var body = new byte[ProgHeaderSize + KeygroupSize];
            Array.Copy(ProgramTemplate, 0, body, 0, ProgramTemplate.Length);
            Array.Copy(KeygroupTemplate, 0, body, ProgHeaderSize, KeygroupTemplate.Length);

            // The file repeats its own name in bytes 0..9, and it is that copy the S950 puts on
            // the display - not the directory entry. A program named only in the directory shows
            // whatever the template happened to carry. The library agrees: 383 of its 388 programs
            // and all 1104 samples hold the same name in both places, and the five exceptions
            // differ only by leading spaces.
            string clean = NormaliseName(name);
            for (int i = 0; i < 10; i++) body[i] = (byte)(i < clean.Length ? clean[i] : ' ');

            body[23] = 1;                                   // one keygroup
            body[26] = (byte)(opts.ProgramNumber.HasValue
                              ? opts.ProgramNumber.Value & 0x7F
                              : FreeProgramNumber());

            int kg = ProgHeaderSize;
            body[kg + 0] = (byte)ClampKey(opts.HighKey.HasValue ? opts.HighKey.Value : 127);
            body[kg + 1] = (byte)ClampKey(opts.LowKey.HasValue ? opts.LowKey.Value : 0);
            PutU16(body, kg + KeygroupChainOffset, 0);      // the only keygroup

            // Both zones empty. '2 SAMPLE' with a zero pointer is how the library marks an
            // unused zone, and it is what ClearZone writes.
            for (int z = 0; z < 2; z++)
            {
                int at = kg + KeygroupNameOffset + z * KeygroupZoneStride;
                for (int i = 0; i < 10; i++)
                    body[at + i] = (byte)(i < UnusedZone.Length ? UnusedZone[i] : ' ');
                PutU16(body, at + 16, 0);
            }

            // after the last program, so the P / O / D / S grouping survives
            var progs = ProgramsInOrder();
            int slot = progs.Count > 0 ? progs[progs.Count - 1].Slot + 1 : 0;

            if (slot < Entries.Count) MakeRoomAt(slot);

            var e = AddFile(clean, 'P', body, slot);
            int landed = e.Slot;

            ParseDirectory();
            RebuildPointers();       // one more program moves the sample table
            ParseDirectory();

            return EntryInSlot(landed);
        }

        /// <summary>
        /// Remove a program. Nothing on a disk refers to a program - the drum set's voice
        /// indexes are just 0..7, and no field anywhere holds a program address - so this only
        /// has to free the blocks, close the directory and rebuild the arena around one fewer
        /// program.
        /// </summary>
        public int DeleteProgram(AkaiEntry e)
        {
            if (e == null || e.Type != 'P')
                throw new ArgumentException("not a program", "e");

            var chain = Chain(e.StartBlock);
            foreach (int b in chain) SetFat(b, 0);

            Array.Clear(Image, DirOffset + e.Slot * EntrySize, EntrySize);

            Modified = true;
            CompactDirectory();
            ParseDirectory();
            RebuildPointers();       // one fewer program moves the sample table
            ParseDirectory();

            return chain.Count;
        }

        /// <summary>Kept for the checkers: the whole arena is rebuilt from its layout.</summary>
        public int RepairKeygroupChains() { return RebuildPointers(); }

        // ------------------------------------------------------------------ slicing
        //
        // Cutting one complex sample into a run of one-shots. This is the first operation that
        // can exhaust three different limits at once - directory slots, disk blocks and the
        // sampler's own memory - so the whole plan is worked out and reported before anything
        // is written.

        /// <summary>How much sampler memory the samples on this disk occupy, in bytes.</summary>
        public int SampleMemoryUsed()
        {
            var s = SamplesInOrder();
            if (s.Count == 0) return 0;
            var last = s[s.Count - 1];
            return last.MemoryAddress + RamSize(last.SampleCount) - SampleRamBase;
        }

        /// <summary>
        /// Names for a run of slices. Ten characters is the hard limit, so the stem is cut short
        /// to leave room for the number, and a letter is added if that collides with a sample
        /// already here - names must be unique within a type.
        /// </summary>
        public List<string> SliceNames(string stemName, int count)
        {
            int digits = count.ToString().Length;

            var taken = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var e in Entries) if (e.Type == 'S') taken.Add(e.Name.Trim());

            for (int attempt = 0; attempt < 27; attempt++)
            {
                string mark = attempt == 0 ? "" : ((char)(96 + attempt)).ToString();
                int room = 10 - digits - 1 - mark.Length;

                string stem = NormaliseName(stemName);
                if (stem.Length > Math.Max(1, room)) stem = stem.Substring(0, Math.Max(1, room));
                stem = stem.TrimEnd();

                var names = new List<string>();
                bool clash = false;

                for (int i = 1; i <= count; i++)
                {
                    string n = stem + mark + " " + i.ToString().PadLeft(digits, '0');
                    if (taken.Contains(n)) { clash = true; break; }
                    names.Add(n);
                }
                if (!clash) return names;
            }
            throw new InvalidOperationException("Could not find unused names for " + count + " slices.");
        }

        /// <summary>One slice of a sample: a half-open span of words, and the name it will take.</summary>
        public sealed class Slice
        {
            public int Start, End, Count;
            public string Name;
        }

        /// <summary>What slicing would cost. Nothing is written until the plan is clean.</summary>
        public sealed class SlicePlan
        {
            public List<Slice> Slices = new List<Slice>();
            public int Slots, Blocks, Ram, Spare;
            public List<string> Problems = new List<string>();
            public bool Ok { get { return Problems.Count == 0; } }
        }

        /// <summary>Settings for a slice: where to map it, and how much sampler memory there is.</summary>
        public sealed class SliceOptions
        {
            public AkaiEntry Program;         // null to add samples without mapping them
            public int RootKey = 60;
            public string Name;               // stem for the slice names; defaults to the sample's
            public int MaxRam = DefaultSamplerRam;
        }

        /// <summary>
        /// What slicing would cost. Changes nothing; show this before committing to it.
        /// <paramref name="cuts"/> are slice starts in words.
        /// </summary>
        public SlicePlan PlanSlices(AkaiEntry e, IList<int> cuts, SliceOptions opts)
        {
            if (opts == null) opts = new SliceOptions();

            var words = SampleWords12(e);
            var plan = new SlicePlan();

            var starts = (cuts ?? new List<int>()).ToList();
            starts.Sort();

            long blocks = 0, ram = 0;
            for (int i = 0; i < starts.Count; i++)
            {
                int from = Math.Max(0, starts[i]) & ~1;
                int to = (i + 1 < starts.Count ? starts[i + 1] : words.Length) & ~1;
                int n = to - from;
                if (n < 2) continue;

                plan.Slices.Add(new Slice { Start = from, End = to, Count = n });
                blocks += BlocksFor(HeaderSize + 3 * n / 2);
                ram += RamSize(n);
            }

            plan.Slots = plan.Slices.Count;
            plan.Blocks = (int)blocks;
            plan.Ram = (int)ram;
            plan.Spare = opts.MaxRam - SampleMemoryUsed();

            if (plan.Slices.Count < 2)
                plan.Problems.Add("Fewer than two slices - nothing to cut.");

            if (plan.Slices.Count > FreeSlots())
                plan.Problems.Add(plan.Slices.Count + " slices need that many directory slots, but only " +
                                  FreeSlots() + " of " + DirEntries + " are free.");

            if (plan.Blocks > FreeBlocks)
                plan.Problems.Add("Needs " + plan.Blocks + " blocks, " + FreeBlocks + " free on the disk.");

            if (plan.Ram > plan.Spare)
                plan.Problems.Add("Needs " + (plan.Ram / 1024) + " KB of sampler memory, " +
                                  (plan.Spare / 1024) + " KB free of " + (opts.MaxRam / 1024) + " KB.");

            if (opts.Program != null)
            {
                int have = KeygroupCount(opts.Program);
                if (have + plan.Slices.Count > MaxKeygroups)
                    plan.Problems.Add(opts.Program.Name.Trim() + " has " + have + " keygroups; " +
                                      plan.Slices.Count + " more would pass the limit of " + MaxKeygroups + ".");
            }

            if (plan.Slices.Count >= 2)
            {
                try
                {
                    var names = SliceNames(string.IsNullOrEmpty(opts.Name) ? e.Name : opts.Name,
                                           plan.Slices.Count);
                    for (int i = 0; i < plan.Slices.Count; i++) plan.Slices[i].Name = names[i];
                }
                catch (Exception ex) { plan.Problems.Add(ex.Message); }
            }

            return plan;
        }

        /// <summary>What a slice actually did.</summary>
        public sealed class SliceResult
        {
            public List<string> Added = new List<string>();
            public int Keygroups;
            public int RootKey;
        }

        /// <summary>
        /// Cut the sample into one-shots, leaving the original alone. With a program, one
        /// keygroup per slice is appended from the root key upward, and each slice is tuned to
        /// the key it sits on so pressing that key plays it at the rate it was recorded.
        /// </summary>
        public SliceResult SliceSample(AkaiEntry e, IList<int> cuts, SliceOptions opts)
        {
            if (opts == null) opts = new SliceOptions();

            var plan = PlanSlices(e, cuts, opts);
            if (!plan.Ok) throw new InvalidOperationException(string.Join(" ", plan.Problems.ToArray()));

            var words = SampleWords12(e);
            int rate = e.SampleRate;
            int root = opts.RootKey;
            int progSlot = opts.Program != null ? opts.Program.Slot : -1;

            var result = new SliceResult { RootKey = root };

            for (int i = 0; i < plan.Slices.Count; i++)
            {
                var s = plan.Slices[i];
                var chunk = new short[s.Count];
                Array.Copy(words, s.Start, chunk, 0, s.Count);

                AddSample(s.Name, chunk, rate, ClampKey(root + i), 0, 'O');
                result.Added.Add(s.Name);
            }

            // Adding samples reparses, so the program entry has to be found again each time.
            if (progSlot >= 0)
            {
                for (int i = 0; i < result.Added.Count; i++)
                {
                    var program = EntryInSlot(progSlot);
                    if (program == null) break;

                    int at = AddKeygroup(program, KeygroupCount(program) - 1) - 1;
                    program = EntryInSlot(progSlot);
                    if (program == null) break;

                    SetKeygroupByte(program, at, 0, (byte)ClampKey(root + i));   // high key
                    SetKeygroupByte(program, at, 1, (byte)ClampKey(root + i));   // low key
                    SetZoneSample(program, at, 0, result.Added[i]);

                    // The new keygroup was seeded from an existing one, which may have carried a
                    // second zone. A slice plays one sample on one key.
                    ClearZone(program, at, 1);
                    result.Keygroups++;
                }
            }

            ParseDirectory();
            RebuildPointers();
            ParseDirectory();
            return result;
        }

        // ------------------------------------------------------------------ saving

        /// <summary>
        /// The image to write out, in either container.
        ///
        ///   img - the 800K or 1600K sector image as the sampler sees it. Always available: it is
        ///         exactly the bytes this object has been editing. FlashFloppy reads these.
        ///   hfe - the Gotek/HxC container. With the HFE this disk came from, that template
        ///         is patched sector by sector, so its bitstream, gaps and sync marks are
        ///         preserved exactly. Opened from a raw .img there is nothing to patch, so
        ///         the whole container is synthesised instead.
        /// </summary>
        public byte[] BuildImage(string format)
        {
            string want = string.IsNullOrEmpty(format) ? (IsHfe ? "hfe" : "img") : format.ToLowerInvariant();

            if (want == "img") return (byte[])Image.Clone();
            if (want != "hfe") throw new ArgumentException("unknown format '" + format + "'", "format");

            if (IsHfe && RawHfe != null) return PatchHfe();
            return HfeWrite.BuildHfe(Image, Image.Length / (SectorsPerTrack * 2 * BlockSize), 2, SectorsPerTrack);
        }

        /// <summary>Both containers can always be written, now that an HFE can be built from nothing.</summary>
        public bool CanSave(string format)
        {
            string want = (format ?? "").ToLowerInvariant();
            return want == "img" || want == "hfe";
        }

        /// <summary>Write the disk to a path in the given container.</summary>
        public void SaveAs(string path, string format)
        {
            File.WriteAllBytes(path, BuildImage(format));
        }

        /// <summary>Sectors per track: five 1024-byte sectors on a DD disk, ten on an HD one.</summary>
        public int SectorsPerTrack { get { return IsHighDensity ? 10 : 5; } }

        /// <summary>
        /// The HFE this disk was loaded from, with every sector's data field rewritten where
        /// it already sits. Factored out of SaveAs so BuildImage can reach it.
        /// </summary>
        byte[] PatchHfe()
        {
            var img = (byte[])RawHfe.Clone();
            int written = 0;

            for (int track = 0; track < 80; track++)
            {
                for (int side = 0; side < 2; side++)
                {
                    byte[] cells = Hfe.SideCells(img, track, side);
                    if (cells.Length == 0) continue;

                    var fields = HfeWrite.Scan(cells);
                    if (fields.Count == 0) continue;

                    byte[] bits = HfeWrite.BitsOf(cells);
                    bool touched = false;

                    foreach (var f in fields)
                    {
                        if (f.Size != BlockSize) continue;
                        int lba = (f.Cyl * 2 + f.Head) * SectorsPerTrack + (f.Sec - 1);
                        int off = lba * BlockSize;
                        if (lba < 0 || off + BlockSize > Image.Length) continue;

                        var data = new byte[BlockSize];
                        Array.Copy(Image, off, data, 0, BlockSize);
                        HfeWrite.PatchField(bits, f, data);
                        touched = true;
                        written++;
                    }

                    if (touched)
                        HfeImage.WriteSideCells(img, track, side, HfeWrite.Pack(bits));
                }
            }

            if (written == 0) throw new InvalidDataException("no sectors could be written");
            return img;
        }
    }
}
