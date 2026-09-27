using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace AkaiS950List
{
    /// <summary>One 24-byte entry in the Akai S900/S950 disk directory.</summary>
    public sealed class AkaiEntry
    {
        public int Slot;
        public string Name;
        public char Type;          // 'P' program, 'S' sample, 'D' drum set, 'O' overall
        public int Length;         // bytes, including the 60-byte file header
        public int StartBlock;
        public int ChainBlocks;    // blocks actually allocated in the FAT
        public bool ChainOk;       // allocation is big enough for the declared length

        // Samples only, read from the file's own 60-byte header.
        public long SampleCount;
        public int SampleRate;
        public int Tuning;         // 16ths of a semitone, C3 = 960
        public char LoopMode;      // 'O' one-shot, 'L' loop, 'A' alternating
        public long LoopStart, LoopEnd, LoopLength;

        /// <summary>
        /// Where the sample sits in the sampler's sample RAM, from bytes 0x36..0x38 of
        /// its header. The first sample on a disk lands at 0x18000 and each subsequent
        /// one follows at ceil(2 * words / 16) * 16 bytes - exact for all 1011
        /// consecutive pairs in the corpus. Two bytes per sample: the 12-bit packing on
        /// disk is expanded to 16-bit words in memory, aligned to 16 bytes.
        /// </summary>
        public int MemoryAddress;

        /// <summary>
        /// Loop direction from byte 0x2B: 'N' normal or 'R' reverse, in the same ASCII
        /// style as the loop mode at 0x1A. Only DSKA0083 'PHONE 3' is reversed.
        /// </summary>
        public char LoopDirection;

        /// <summary>Pointer to the sample's loop descriptors in RAM, byte 0x28.</summary>
        public int LoopDescriptorPtr;

        /// <summary>
        /// Sample loudness, signed, at 0x18 - which the reference document lists as zero.
        /// Non-zero in 200 of the 1110 samples here, spanning -31..+50. Confirmed on the
        /// panel: DSKA0002 'TONE 1' reads loudness 30 and stores 30.
        /// </summary>
        public int Loudness;

        /// <summary>Nominal pitch as a MIDI note number - the whole-semitone part of Tuning.</summary>
        public int NominalPitch { get { return Tuning / 16; } }

        /// <summary>Fine pitch, 0..15 - the sixteenths-of-a-semitone part of Tuning.</summary>
        public int FinePitch { get { return Tuning % 16; } }
        public double Seconds { get { return SampleRate > 0 ? (double)SampleCount / SampleRate : 0; } }
        public double Semitones { get { return Tuning / 16.0; } }

        public string TypeName
        {
            get
            {
                switch (Type)
                {
                    case 'P': return "program";
                    case 'S': return "sample";
                    case 'D': return "drum set";
                    case 'O': return "overall";
                    default: return "type " + Type;
                }
            }
        }
    }

    /// <summary>
    /// An 800K Akai S900/S950 floppy: 80 cylinders x 2 heads x 5 sectors of 1024 bytes.
    /// Block 0 holds a 64-entry directory at 0x000 and a 16-bit FAT at 0x600;
    /// file data starts at block 4.
    /// </summary>
    public sealed partial class AkaiDisk
    {
        public const int BlockSize = 1024;
        public const int DirOffset = 0x000;
        public const int DirEntries = 64;
        public const int EntrySize = 24;
        public const int FatOffset = 0x600;
        public const int FatEnd = 0x8000;   // end-of-chain marker
        public const int HeaderSize = 60;   // per-file header preceding the payload

        public string Source;
        public byte[] Image;

        /// <summary>The original file bytes, kept so edits can be written back in place.</summary>
        public byte[] RawHfe;
        public bool IsHfe;
        bool _modified;

        /// <summary>Whether the image differs from the file it was read from.</summary>
        public bool Modified
        {
            get { return _modified; }
            set { _modified = value; Revision++; }
        }

        /// <summary>
        /// Bumped whenever anything touches the image.
        ///
        /// Anything holding something derived from these bytes - decoded audio, a
        /// built patch - can compare this against the revision it derived from and
        /// find out that it is stale, without every edit having to remember to say so.
        /// Every write goes through code that sets Modified, so this comes for free;
        /// an edit site that forgot to announce itself is the failure this avoids.
        ///
        /// It counts writes, not versions: only equality means anything. Clearing
        /// Modified after a save bumps it too, which costs one rebuild and keeps the
        /// rule simple - undo restores the flag as well as the bytes, and that has to
        /// count as a change.
        /// </summary>
        public int Revision { get; private set; }
        public int BadCrcSectors;
        public int MissingSectors;
        public List<AkaiEntry> Entries = new List<AkaiEntry>();

        public int TotalBlocks { get { return Image.Length / BlockSize; } }

        /// <summary>Data blocks (4 and above) with no allocation-table entry.</summary>
        public int FreeBlocks
        {
            get
            {
                int n = 0;
                for (int b = 4; b < TotalBlocks; b++) if (Fat(b) == 0) n++;
                return n;
            }
        }

        public int UsedBlocks { get { return TotalBlocks - 4 - FreeBlocks; } }

        int Fat(int block)
        {
            int o = FatOffset + block * 2;
            if (o + 1 >= Image.Length) return FatEnd;
            return Image[o] | (Image[o + 1] << 8);
        }

        /// <summary>Walk a file's FAT chain, guarding against loops and bad links.</summary>
        public List<int> Chain(int start)
        {
            var blocks = new List<int>();
            var seen = new HashSet<int>();
            int b = start;
            while (b != FatEnd && b >= 0 && b < TotalBlocks && seen.Add(b))
            {
                blocks.Add(b);
                b = Fat(b);
            }
            return blocks;
        }

        public byte[] ReadFile(AkaiEntry e)
        {
            var blocks = Chain(e.StartBlock);
            var buf = new byte[blocks.Count * BlockSize];
            for (int i = 0; i < blocks.Count; i++)
                Array.Copy(Image, blocks[i] * BlockSize, buf, i * BlockSize, BlockSize);
            int n = Math.Min(e.Length, buf.Length);
            var outp = new byte[n];
            Array.Copy(buf, outp, n);
            return outp;
        }

        /// <summary>
        /// Decodes a sample file's payload to signed 16-bit PCM, ready to play.
        /// The disk packing is described in S950-Disk-Format section 6.2: the sample is
        /// split in half and the low nibbles of both halves are interleaved through the
        /// first N bytes, with the second half's high bytes following at the end.
        /// </summary>
        public short[] SamplePcm(AkaiEntry e)
        {
            var w = SampleWords12(e);
            for (int i = 0; i < w.Length; i++) w[i] = (short)(w[i] * 16);
            return w;
        }

        /// <summary>The sample as stored: signed 12-bit values, -2048..2047.</summary>
        public short[] SampleWords12(AkaiEntry e)
        {
            if (e == null || e.Type != 'S') return new short[0];

            var raw = ReadFile(e);
            int payload = raw.Length - HeaderSize;
            if (payload <= 0) return new short[0];

            // 1.5 bytes per word, and the packing works in pairs. A truncated file
            // gives back what it does hold rather than throwing.
            int n = (int)Math.Min(e.SampleCount, payload * 2 / 3) & ~1;
            if (n <= 0) return new short[0];

            int half = n / 2;
            var pcm = new short[n];
            for (int i = 0; i < half; i++)
            {
                int nibbles = raw[HeaderSize + 2 * i];
                pcm[i] = Signed12((raw[HeaderSize + 2 * i + 1] << 4) | (nibbles >> 4));
                pcm[half + i] = Signed12((raw[HeaderSize + n + i] << 4) | (nibbles & 0x0F));
            }
            return pcm;
        }

        static short Signed12(int w) { return (short)(w >= 2048 ? w - 4096 : w); }

        /// <summary>
        /// The inverse of <see cref="SampleWords12"/>: packs signed 12-bit words into
        /// the split-nibble layout of section 6.2. Values outside -2048..2047 clip.
        /// </summary>
        public static byte[] PackSampleData(short[] words12, int count)
        {
            int n = Math.Min(count, words12.Length) & ~1;
            if (n <= 0) return new byte[0];

            int half = n / 2;
            var buf = new byte[n + half];
            for (int i = 0; i < half; i++)
            {
                int a = Clip12(words12[i]);
                int b = Clip12(words12[half + i]);

                buf[2 * i] = (byte)(((a & 0x0F) << 4) | (b & 0x0F));
                buf[2 * i + 1] = (byte)((a >> 4) & 0xFF);
                buf[n + i] = (byte)((b >> 4) & 0xFF);
            }
            return buf;
        }

        static int Clip12(int v)
        {
            if (v > 2047) v = 2047;
            if (v < -2048) v = -2048;
            return v & 0xFFF;
        }

        static string CleanName(byte[] b, int off)
        {
            var sb = new StringBuilder(10);
            for (int i = 0; i < 10; i++)
            {
                byte c = b[off + i];
                sb.Append(c >= 0x20 && c < 0x7F ? (char)c : ' ');
            }
            return sb.ToString().TrimEnd();
        }

        public void ParseDirectory()
        {
            // Re-reading has to replace the list, not add to it: this runs again after
            // every edit and after adding a file.
            Entries.Clear();
            _sampleRefs = null;

            for (int i = 0; i < DirEntries; i++)
            {
                int o = DirOffset + i * EntrySize;
                if (o + EntrySize > Image.Length) break;
                if (Image[o] == 0x00) continue;                 // free slot

                char type = (char)Image[o + 16];
                if ("PSDO".IndexOf(type) < 0) continue;         // not a recognised file entry

                int len = Image[o + 17] | (Image[o + 18] << 8) | (Image[o + 19] << 16);
                int start = Image[o + 20] | (Image[o + 21] << 8);
                if (start < 0 || start >= TotalBlocks) continue;

                var e = new AkaiEntry
                {
                    Slot = i,
                    Name = CleanName(Image, o),
                    Type = type,
                    Length = len,
                    StartBlock = start
                };
                e.ChainBlocks = Chain(start).Count;
                int needed = (len + BlockSize - 1) / BlockSize;
                e.ChainOk = e.ChainBlocks >= needed;

                if (type == 'S') ReadSampleHeader(e);
                Entries.Add(e);
            }
        }

        uint U32(int o)
        {
            return (uint)(Image[o] | (Image[o + 1] << 8) | (Image[o + 2] << 16) | (Image[o + 3] << 24));
        }

        /// <summary>
        /// Parse a sample file's 60-byte header:
        ///   0x00 name (10), 0x0A zero (6), 0x10 sample words (4), 0x14 rate Hz (2),
        ///   0x16 tuning: nominal pitch * 16 + fine pitch (2), 0x18 loudness, signed (2),
        ///   0x1A loop mode (1), 0x1B zero (1),
        ///   0x1C end marker (4), 0x20 start marker (4), 0x24 loop length (4),
        ///   0x28 loop-descriptor pointer (2), 0x2B loop direction 'N'/'R',
        ///   0x36 sample memory address (3); the rest of 0x28..0x3B zero.
        /// Corroborated by the payload arithmetic: the directory length is always
        /// exactly 60 + 1.5 * SampleCount, i.e. 12 bits per sample.
        /// </summary>
        void ReadSampleHeader(AkaiEntry e)
        {
            int o = e.StartBlock * BlockSize;
            if (o + HeaderSize > Image.Length) return;
            e.SampleCount = U32(o + 0x10);
            e.SampleRate = Image[o + 0x14] | (Image[o + 0x15] << 8);
            e.Tuning = Image[o + 0x16] | (Image[o + 0x17] << 8);
            e.LoopMode = (char)Image[o + 0x1A];
            e.LoopEnd = U32(o + 0x1C);
            e.LoopStart = U32(o + 0x20);
            e.LoopLength = U32(o + 0x24);
            e.MemoryAddress = (int)(Image[o + 0x36] | (Image[o + 0x37] << 8) | (Image[o + 0x38] << 16));
            e.LoopDirection = (char)Image[o + 0x2B];
            e.LoopDescriptorPtr = Image[o + 0x28] | (Image[o + 0x29] << 8);
            e.Loudness = (short)(Image[o + 0x18] | (Image[o + 0x19] << 8));
        }

        public const int ProgHeaderSize = 38;
        public const int KeygroupSize = 70;
        public const int KeygroupNameOffset = 24;   // zone 1 sample name
        public const int KeygroupZoneStride = 22;   // zone 2 name is 22 bytes further on

        /// <summary>
        /// A 22-byte velocity zone: a sample and the per-sample parameters that
        /// go with it. Confirmed against an S950 front panel (FL BASS 1, DSKA0050).
        /// </summary>
        public sealed class Zone
        {
            public string Name;          // +0..9
            public int Pointer;          // +16..17, internal reference
            // +18 and +19 are ONE signed 16-bit number: fine the low byte (the panel shows
            // 128 for the byte 128, not -128), transpose the high byte. It counts SIXTEENTHS
            // of a semitone - the unit the sample header's pitch is kept in - so a downward
            // offset is transpose -1 with a large fine, which is why 206 of the 211 keygroups
            // at transpose -1 carry a non-zero fine while 1396 of the 1574 at transpose 0
            // carry none. It is not Transpose + Fine/256, which was assumed until measured.
            public int Fine;             // +18, unsigned - confirmed on the panel
            public int Transpose;        // +19, signed
            public int Filter;           // +20, 0..99

            /// <summary>
            /// Total pitch offset in semitones: (Transpose*256 + Fine) / 16.
            ///
            /// MEASURED 2026-09-27 on DSKA0058 SEQ BASS: (fine, transpose) of (192,0), (80,0),
            /// (0,0), (144,-1) and (64,-1) played +12, +5, 0, -7 and -12 semitones, all ten
            /// notes within 1.7 cents. See Zone::pitchOffset in the plugin.
            /// </summary>
            public double PitchOffset { get { return (Transpose * 256 + Fine) / 16.0; } }
            public int Loudness;         // +21, signed

            // +10..+15 belong to the keygroup, not the zone. Zone 1 carries the
            // filter envelope in +10..+13; its +14/+15 and the whole of zone 2's
            // +10..+15 are reserved. Those eight bytes hold 0 in every one of the
            // 238 keygroups an S950 wrote and ASCII space in all 1661 an S900 did,
            // with no exceptions either way - so they track the same six-byte block
            // the filter envelope sits in and were never given a meaning.

            /// <summary>An unused zone carries the placeholder name the S950 writes.</summary>
            public bool InUse
            {
                get { return !string.IsNullOrEmpty(Name) && Name != "2 SAMPLE" && Pointer != 0; }
            }

            public static Zone Parse(byte[] raw, int o)
            {
                return new Zone
                {
                    Name = CleanName(raw, o),
                    Pointer = raw[o + 16] | (raw[o + 17] << 8),
                    Fine = raw[o + 18],
                    Transpose = (sbyte)raw[o + 19],
                    Filter = raw[o + 20],
                    Loudness = (sbyte)raw[o + 21]
                };
            }
        }

        /// <summary>
        /// One 70-byte keygroup: a 24-byte header carrying the key range and the
        /// VCA envelope, two 22-byte velocity zones, and a link to the next keygroup.
        /// </summary>
        public sealed class Keygroup
        {
            public int Index;
            public int LowKey, HighKey;      // bytes 0, 1 - MIDI note numbers, C3 = 60

            // Byte 2. The velocity at which zone 2 takes over from zone 1. Confirmed
            // on the panel: DSKA0082 'CAR HORN' reads 115 and stores 115. 128 sits one
            // above the highest MIDI velocity. The panel displays 128 as a number rather
            // than an OFF legend, but a keygroup left there can never reach zone 2. The
            // split is exact: of the 266 keygroups that name a second sample, 45 carry
            // a real threshold; of the 1633 that name none, not one does.
            public int VelocitySwitch;
            public bool VelocitySwitchOff { get { return VelocitySwitch >= 128; } }
            public int NextKeygroup;         // bytes 68..69, 0 in the final keygroup
            public byte[] Raw;

            // Amplitude envelope, bytes 3..6. Confirmed against an S950 front panel.
            public int VcaAttack, VcaDecay, VcaSustain, VcaRelease;

            // VEL SENS block, bytes 7..11. Filter and loudness confirmed against
            // the panel; bytes 8..10 are the remaining entries (attack, a toggle
            // and release) and read 0 on every program checked so far.
            // VEL SENS page, bytes 7, 9, 10, 11.
            public int VelToFilter;     // byte 7
            public int VelToAttack;     // byte 9
            public int VelToRelease;    // byte 10, signed, spans exactly -50..+50
            public int VelToLoudness;   // byte 11

            /// <summary>
            /// Byte 19: which output the keygroup goes to. Panel value, and the byte stores
            /// it one lower - so ALL is stored as -1 (255), which is the default in 1617 of
            /// the 1908 library keygroups.
            ///
            ///     0        ALL
            ///     1 .. 8   MONO 1 to MONO 8, the individual outputs
            ///     9        LEFT
            ///    10        RIGHT
            ///
            /// Read off the panel, and every one of the 1908 keygroups lands inside that
            /// range under this mapping. Only drum and percussion programmes set it, which is
            /// what you would expect of individual outputs - and TUBULAR 2 reads L L L L R R
            /// R R, bells spread across the stereo field, while PIZ-CHORUS is two keygroups
            /// one each side.
            /// </summary>
            public int OutputPort;

            // Byte 10. Confirmed on the panel: DSKA0006 'DRUM-1' keygroup 4 reads
            // VEL SENS release -50 and stores 206. It is 0 in all but 20 of 1899
            // keygroups, which is why it took a deliberately chosen program to see.

            public int KeyToFilter;     // byte 8

            // WARP page, bytes 12..14.
            public int WarpVelocity, WarpAttackOffset, WarpTime;

            // LFO. Depth is byte 17, not 14 — byte 14 is WARP time.
            public int LfoDelay, LfoRate, LfoDepth;   // bytes 15, 16, 17
            // LFO depth modulation sources, in the order the panel lists them.
            // Aftertouch is 0 in every one of the 1908 keygroups - no library program
            // uses it - so its position is known from the panel, not from the corpus.
            // Modwheel is actively used: 427 keygroups move it off its default of 50.
            public int LfoAftertouchDepth;            // byte 21
            public int LfoModwheelDepth;              // byte 22
            // Byte 18 flags. Confirmed on the panel from the keygroup page that follows
            // LFO desync: bit 0 constant pitch, bit 2 LFO desync, bit 3 one-shot playback.
            public int Flags;                         // byte 18

            /// <summary>Byte 18 bit 0: the keygroup plays at a fixed pitch across its key range.</summary>
            public bool ConstantPitch { get { return (Flags & 0x01) != 0; } }

            /// <summary>Byte 18 bit 2.</summary>
            public bool LfoDesync { get { return (Flags & 0x04) != 0; } }

            /// <summary>Byte 18 bit 3: play the sample through, ignoring key release.</summary>
            public bool OneShot { get { return (Flags & 0x08) != 0; } }

            /// <summary>
            /// Byte 18 bit 4: the ON/OFF beside Release on the velocity page.
            ///
            /// Found by diffing a disk saved either side of flipping it, then confirmed by a
            /// second save that changed it alongside two other fields and moved no other bit.
            /// It enables <see cref="VelToRelease"/>: with it clear, every note is released as
            /// though its velocity were 1, which is why byte 10 looked like a fixed offset
            /// through two calibration runs. See Cal.VelocityReleaseByte.
            ///
            /// Clear in all 1908 keygroups of the library, so nothing in it uses this.
            /// </summary>
            public bool VelocityReleaseOn { get { return (Flags & 0x10) != 0; } }

            // Bit 1 is set in exactly one keygroup of 1908 and is unidentified.
            // Bits 5..7 are never set.

            // Byte 19. The panel value is this byte plus one, with 0xFF wrapping to 0:
            // 0 = all, 1..8 = the individual mono outputs, 9 = left, 10 = right. All
            // eleven panel settings occur in the corpus. 0xFF ("all") in 1608 of 1899.
            // Originally deduced from the disks alone (see the note below), then
            // confirmed against the front panel.
            //
            // Old note: 0xFF in 1608 of 1899 keygroups; otherwise 0..9 only.
            // Reads as an output routing assignment: drum kits give each keygroup
            // its own value, and DSKA0042 'TUBULAR 2' holds the same four samples
            // twice over identical key ranges, the copies differing only in fine
            // tune (+2) and this byte (8 vs 9) - a detuned stereo double, so 8/9
            // are a left/right pair. 0..7 then line up with individual outputs 1-8.
            public int Output;
            public bool HasOutput { get { return Output != 0xFF; } }
            public string OutputName
            {
                get
                {
                    if (Output == 0xFF) return "all";   // panel 0
                    if (Output == 8) return "L";            // panel 9
                    if (Output == 9) return "R";            // panel 10
                    return (Output + 1).ToString();         // panel 1..8
                }
            }

            // Filter envelope. Stored in zone 1's span but a keygroup-level setting:
            // zone 2's copy is blank in all 168 keygroups that use a second zone.
            public int VcfAmount;       // byte 23, signed -50..50
            public int VcfAttack, VcfDecay, VcfSustain, VcfRelease;   // bytes 34..37

            public string Vcf { get { return VcfAttack + "/" + VcfDecay + "/" + VcfSustain + "/" + VcfRelease; } }
            public string Lfo { get { return "d" + LfoDepth + " r" + LfoRate + " dly" + LfoDelay + (LfoDesync ? " desync" : ""); } }

            public Zone Zone1, Zone2;

            public string Vca { get { return VcaAttack + "/" + VcaDecay + "/" + VcaSustain + "/" + VcaRelease; } }

            // Zone 1 is the one every program uses; these keep older callers working.
            public string Sample1 { get { return Zone1 != null ? Zone1.Name : ""; } }
            public string Sample2 { get { return Zone2 != null ? Zone2.Name : ""; } }
            public bool HasSecondZone { get { return Zone2 != null && Zone2.InUse; } }

            public string KeyRange { get { return LowKey + " - " + HighKey; } }
        }

        /// <summary>Number of keygroups in a program: the file is 38 + n * 70 bytes.</summary>
        /// <summary>
        /// The 38-byte header at the start of a program file. It is a truncation of a
        /// 70-byte in-memory record: taking a disk's programs in directory order, the
        /// load address advances by 70 * (1 + keygroups) in 289 of 289 consecutive pairs.
        /// </summary>
        public sealed class ProgramHeader
        {
            public string Name;            // 0..9
            public int LoadAddress;        // 18, 19 - unsigned 16-bit, an address in sampler RAM
            public int KeygroupCount;      // 23 - matches the file length in all 390 programs
            public int ProgramNumber;      // 26 - zero-based; the panel shows this plus one

            /// <summary>MIDI program number as the panel shows it, counting from 1.</summary>
            public int MidiProgram { get { return ProgramNumber + 1; } }

            /// <summary>
            /// Bytes 16 and 17, signed 16-bit. Zero in 386 of 390 programs. Confirmed on the
            /// panel: DSKA0078 'SOFT FLUTE' reads -6 and stores 250, 255. Byte 17 is 0xFF on
            /// exactly the two programs whose value is negative and in range, which is the
            /// sign extension a 16-bit field requires. The one exception is DSKA0061
            /// 'THE ISLAND', which stores 254, 0 - reading +254, far outside the range of
            /// every other program, so most likely a stale byte rather than a setting.
            /// </summary>
            public int KeyToLoudness;

            /// <summary>
            /// Byte 21. Of the multi-keygroup programs that set it, 92% have overlapping
            /// keygroups, against 32% of those that do not - which is what a crossfade
            /// between overlapping zones requires. Confirmed on the panel.
            /// </summary>
            public bool PositionalCrossfade;
            public int FormatMarker;       // 22 - 255 on every S900 program, 0 on every S950 one

            /// <summary>True when the program was written by an S950 (filter envelope present).</summary>
            public bool IsS950 { get { return FormatMarker == 0; } }

            /// <summary>Bytes the program occupies in sampler RAM, header padded to 70.</summary>
            public int MemorySize { get { return 70 * (1 + KeygroupCount); } }
        }

        /// <summary>Parse the 38-byte header of a program file.</summary>
        public ProgramHeader ReadProgramHeader(AkaiEntry program)
        {
            if (program.Type != 'P') return null;
            byte[] raw = ReadFile(program);
            if (raw == null || raw.Length < ProgHeaderSize) return null;
            return new ProgramHeader
            {
                Name = CleanName(raw, 0),
                LoadAddress = raw[18] | (raw[19] << 8),
                FormatMarker = raw[22],
                KeygroupCount = raw[23],
                ProgramNumber = raw[26],
                KeyToLoudness = (short)(raw[16] | (raw[17] << 8)),
                PositionalCrossfade = raw[21] != 0
            };
        }
        public static int KeygroupCount(AkaiEntry program)
        {
            if (program.Type != 'P' || program.Length < ProgHeaderSize + KeygroupSize) return 0;
            int n = program.Length - ProgHeaderSize;
            return (n % KeygroupSize == 0) ? n / KeygroupSize : 0;
        }

        /// <summary>
        /// The sample name referenced by each keygroup, read from byte 24 of the
        /// 70-byte keygroup record. Returns one entry per keygroup, in order; a
        /// name may repeat, and may refer to a sample held on a different disk.
        /// </summary>
        public List<string> ReferencedSamples(AkaiEntry program)
        {
            var outp = new List<string>();
            foreach (var kg in Keygroups(program)) outp.Add(kg.Sample1);
            return outp;
        }

        /// <summary>Parse a program's keygroup records. See the format notes for which fields are confirmed.</summary>
        public List<Keygroup> Keygroups(AkaiEntry program)
        {
            var outp = new List<Keygroup>();
            int n = KeygroupCount(program);
            if (n == 0) return outp;

            var body = ReadFile(program);
            for (int k = 0; k < n; k++)
            {
                int o = ProgHeaderSize + k * KeygroupSize;
                if (o + KeygroupSize > body.Length) break;

                var raw = new byte[KeygroupSize];
                Array.Copy(body, o, raw, 0, KeygroupSize);

                outp.Add(new Keygroup
                {
                    Index = k,
                    HighKey = raw[0],
                    LowKey = raw[1],

                    VelocitySwitch = raw[2],

                    VcaAttack = raw[3], VcaDecay = raw[4], VcaSustain = raw[5], VcaRelease = raw[6],

                    VelToFilter = raw[7], VelToAttack = raw[9],
                    VelToRelease = (sbyte)raw[10], VelToLoudness = raw[11],
                    OutputPort = (sbyte)raw[19] + 1,
                    KeyToFilter = raw[8],

                    WarpVelocity = raw[12], WarpAttackOffset = (sbyte)raw[13], WarpTime = raw[14],

                    LfoDelay = raw[15], LfoRate = raw[16], LfoDepth = raw[17],
                    LfoAftertouchDepth = raw[21], LfoModwheelDepth = raw[22],
                    Flags = raw[18], Output = raw[19],

                    VcfAmount = (sbyte)raw[23],
                    VcfAttack = raw[34], VcfDecay = raw[35],
                    VcfSustain = raw[36], VcfRelease = raw[37],

                    Zone1 = Zone.Parse(raw, KeygroupNameOffset),
                    Zone2 = Zone.Parse(raw, KeygroupNameOffset + KeygroupZoneStride),
                    NextKeygroup = raw[68] | (raw[69] << 8),
                    Raw = raw
                });
            }
            return outp;
        }

        /// <summary>
        /// Write the disk back out in the container it came from. The format-aware
        /// overload in AkaiDiskEdit.cs does the work; this keeps the old call site.
        /// </summary>
        public void SaveAs(string path)
        {
            SaveAs(path, IsHfe ? "hfe" : "img");
        }

        Dictionary<string, int> _sampleRefs;

        /// <summary>
        /// The internal reference a given sample name carries on this disk, taken from
        /// any keygroup zone that already names it. A name always carries the same value
        /// within a disk - 1862 pairs over 1065 names with no conflicts - so reassigning
        /// a zone can reuse it. Returns 0 when the name is not referenced anywhere here.
        /// </summary>
        public int SampleReference(string name)
        {
            int i = SampleIndex(name);
            if (i < 0) return 0;

            return SampleTableAddress() + KeygroupSize * i;
        }

        /// <summary>Samples in directory order - the order of the descriptor table.</summary>
        public List<AkaiEntry> SamplesInOrder()
        {
            return Entries.Where(e => e.Type == 'S').OrderBy(e => e.Slot).ToList();
        }

        public int SampleIndex(string name)
        {
            if (name == null) return -1;
            string want = name.Trim();

            var samples = SamplesInOrder();
            for (int i = 0; i < samples.Count; i++)
                if (string.Equals(samples[i].Name.Trim(), want, StringComparison.OrdinalIgnoreCase))
                    return i;
            return -1;
        }

        /// <summary>
        /// Where the sample descriptor table starts, taken as the answer most of the
        /// zones agree on. A zone's pointer is the base plus 70 per sample position, so
        /// each zone implies a base; one left over from an earlier arrangement implies
        /// the wrong one, and would poison an answer taken from a single zone.
        /// </summary>
        public int SampleTableBase()
        {
            var votes = new Dictionary<int, int>();
            int best = -1, bestCount = 0;

            foreach (var p in Entries)
            {
                if (p.Type != 'P') continue;
                foreach (var kg in Keygroups(p))
                    foreach (var z in new[] { kg.Zone1, kg.Zone2 })
                    {
                        if (z == null || !z.InUse || z.Pointer == 0) continue;

                        int i = SampleIndex(z.Name);
                        if (i < 0) continue;

                        int b = z.Pointer - KeygroupSize * i;
                        int n;
                        votes[b] = n = (votes.TryGetValue(b, out n) ? n : 0) + 1;
                        if (n > bestCount) { bestCount = n; best = b; }
                    }
            }
            return best;
        }

        /// <summary>
        /// Rewrites every zone pointer from its sample's current position. Adding or
        /// removing samples moves those positions, and a pointer left behind refers to
        /// the wrong descriptor. Returns how many were wrong.
        /// </summary>
        public int RepairZonePointers()
        {
            int b = SampleTableAddress();

            int fixedUp = 0;
            foreach (var p in Entries)
            {
                if (p.Type != 'P') continue;

                var kgs = Keygroups(p);
                for (int k = 0; k < kgs.Count; k++)
                    for (int zi = 0; zi < 2; zi++)
                    {
                        var z = zi == 0 ? kgs[k].Zone1 : kgs[k].Zone2;
                        if (z == null || !z.InUse) continue;

                        int i = SampleIndex(z.Name);
                        if (i < 0) continue;

                        int want = b + KeygroupSize * i;
                        if (z.Pointer == want) continue;

                        int off = ProgHeaderSize + k * KeygroupSize +
                                  KeygroupNameOffset + zi * KeygroupZoneStride + 16;
                        PokeFile(p, off, (byte)(want & 0xFF));
                        PokeFile(p, off + 1, (byte)((want >> 8) & 0xFF));
                        fixedUp++;
                    }
            }
            if (fixedUp > 0) Modified = true;
            return fixedUp;
        }

        void RememberZone(Zone z)
        {
            if (z == null || z.Pointer == 0) return;
            string n = (z.Name ?? "").Trim();
            if (n.Length == 0 || _sampleRefs.ContainsKey(n)) return;
            _sampleRefs[n] = z.Pointer;
        }
        /// <summary>Replace a file's bytes in place. The length must not change.</summary>
        public void WriteFile(AkaiEntry e, byte[] data)
        {
            var blocks = Chain(e.StartBlock);
            if (data.Length > blocks.Count * BlockSize)
                throw new ArgumentException("data does not fit the existing allocation");
            for (int i = 0; i < blocks.Count; i++)
            {
                int take = Math.Min(BlockSize, data.Length - i * BlockSize);
                if (take <= 0) break;
                Array.Copy(data, i * BlockSize, Image, blocks[i] * BlockSize, take);
            }
            Modified = true;
        }

        // --------------------------------------------------------- adding files

        /// <summary>Where the first sample on a disk loads in the sampler's RAM.</summary>
        public const int SampleRamBase = 0x18000;

        /// <summary>Where the first sample's loop descriptors sit in RAM.</summary>
        public const int LoopDescriptorBase = 0xB6F4;

        public static int BlocksFor(int length) { return (length + BlockSize - 1) / BlockSize; }

        static string TypeNameOf(char type)
        {
            switch (type)
            {
                case 'P': return "program";
                case 'S': return "sample";
                case 'D': return "drum set";
                case 'O': return "overall";
                default: return "file";
            }
        }

        /// <summary>Bytes a sample occupies in sample RAM: two per word, on a 16-byte grid.</summary>
        public static int RamSize(long words) { return (int)((2 * words + 15) / 16 * 16); }

        /// <summary>
        /// 10-byte loop descriptors a sample consumes, from its loop mode and the number
        /// of whole 128K pages its data fills. See S950-Disk-Format section 6.1.
        /// </summary>
        public static int LoopRecords(long words, char loopMode)
        {
            int pages = (int)(2 * words / 131072);
            switch (loopMode)
            {
                case 'L': return 3 + pages;
                case 'A': return 3 * (1 + pages);
                default: return 2 + pages;      // 'O', one-shot
            }
        }

        /// <summary>
        /// A filename the S900/S950 will accept: up to 10 printable ASCII characters,
        /// upper-cased, with anything else turned into a space.
        /// </summary>
        public static string NormaliseName(string name)
        {
            var sb = new StringBuilder(10);
            foreach (char c in (name ?? "").ToUpperInvariant())
            {
                if (sb.Length == 10) break;
                sb.Append(c >= 0x20 && c < 0x7F ? c : ' ');
            }
            string s = sb.ToString().TrimEnd();
            return s.Length == 0 ? "UNTITLED" : s;
        }

        void SetFat(int block, int value)
        {
            int o = FatOffset + block * 2;
            Image[o] = (byte)(value & 0xFF);
            Image[o + 1] = (byte)((value >> 8) & 0xFF);
        }

        static void PutU16(byte[] b, int o, int v)
        {
            b[o] = (byte)(v & 0xFF);
            b[o + 1] = (byte)((v >> 8) & 0xFF);
        }

        static void PutU32(byte[] b, int o, long v)
        {
            b[o] = (byte)(v & 0xFF);
            b[o + 1] = (byte)((v >> 8) & 0xFF);
            b[o + 2] = (byte)((v >> 16) & 0xFF);
            b[o + 3] = (byte)((v >> 24) & 0xFF);
        }

        /// <summary>
        /// Writes a new file into free blocks and claims a directory slot for it.
        /// Nothing already on the disk is moved or altered. <paramref name="minSlot"/>
        /// forces the entry past existing files whose order matters - samples must stay
        /// in directory order for their RAM addresses to follow on.
        /// </summary>
        public AkaiEntry AddFile(string name, char type, byte[] contents, int minSlot)
        {
            string clean = NormaliseName(name);

            // Names are unique within a type, not across the disk: the library's own
            // images routinely name a program after the sample it plays. DSKA0044
            // alone does it thirteen times.
            foreach (var x in Entries)
                if (x.Type == type && string.Equals(x.Name, clean, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "'" + clean + "' is already used by another " + TypeNameOf(type) + ".");

            int need = BlocksFor(contents.Length);
            var free = new List<int>();
            for (int b = 4; b < TotalBlocks && free.Count < need; b++)
                if (Fat(b) == 0) free.Add(b);
            if (free.Count < need)
                throw new InvalidOperationException(
                    "Not enough room: needs " + need + " blocks of " + BlockSize +
                    " bytes, " + FreeBlocks + " free.");

            int slot = -1;
            for (int i = Math.Max(0, minSlot); i < DirEntries; i++)
                if (Image[DirOffset + i * EntrySize] == 0x00) { slot = i; break; }
            if (slot < 0)
                throw new InvalidOperationException("No free directory slot after the last file of this type.");

            for (int i = 0; i < need; i++)
            {
                int off = free[i] * BlockSize;
                Array.Clear(Image, off, BlockSize);
                int take = Math.Min(BlockSize, contents.Length - i * BlockSize);
                if (take > 0) Array.Copy(contents, i * BlockSize, Image, off, take);
                SetFat(free[i], i == need - 1 ? FatEnd : free[i + 1]);
            }

            int d = DirOffset + slot * EntrySize;
            Array.Clear(Image, d, EntrySize);
            for (int i = 0; i < 10; i++) Image[d + i] = (byte)(i < clean.Length ? clean[i] : ' ');
            Image[d + 16] = (byte)type;
            Image[d + 17] = (byte)(contents.Length & 0xFF);
            Image[d + 18] = (byte)((contents.Length >> 8) & 0xFF);
            Image[d + 19] = (byte)((contents.Length >> 16) & 0xFF);
            Image[d + 20] = (byte)(free[0] & 0xFF);
            Image[d + 21] = (byte)((free[0] >> 8) & 0xFF);

            Modified = true;
            ParseDirectory();

            foreach (var e in Entries) if (e.Slot == slot) return e;
            throw new InvalidOperationException("the new entry did not read back");
        }

        /// <summary>
        /// Adds a sample to the disk from signed 12-bit words. The RAM address and
        /// loop-descriptor pointer are derived from the last sample already present, so
        /// the new entry has to land after it in the directory; no existing header is
        /// touched. Markers default to the whole sample, unlooped.
        /// </summary>
        public AkaiEntry AddSample(string name, short[] words12, int sampleRate,
                                   int nominalPitch, int finePitch, char loopMode)
        {
            if (words12 == null) throw new ArgumentNullException("words12");
            int n = words12.Length & ~1;                 // the packing works in pairs
            if (n < 2) throw new InvalidOperationException("There is no audio to add.");

            int ram = SampleRamBase, ptr = LoopDescriptorBase, lastSlot = -1;
            foreach (var s in Entries)
            {
                if (s.Type != 'S' || s.Slot <= lastSlot) continue;
                ram = s.MemoryAddress + RamSize(s.SampleCount);
                ptr = s.LoopDescriptorPtr + 10 * LoopRecords(s.SampleCount, s.LoopMode);
                lastSlot = s.Slot;
            }

            var data = PackSampleData(words12, n);
            var file = new byte[HeaderSize + data.Length];

            string clean = NormaliseName(name);
            for (int i = 0; i < 10; i++) file[i] = (byte)(i < clean.Length ? clean[i] : ' ');

            PutU32(file, 0x10, n);
            PutU16(file, 0x14, sampleRate);
            PutU16(file, 0x16, nominalPitch * 16 + finePitch);
            PutU16(file, 0x18, 0);                       // loudness
            file[0x1A] = (byte)loopMode;
            PutU32(file, 0x1C, n);                       // end marker
            PutU32(file, 0x20, 0);                       // start marker
            PutU32(file, 0x24, Math.Min(n, 2000));       // loop length, the panel default
            PutU16(file, 0x28, ptr);
            file[0x2B] = (byte)'N';                      // loop direction
            file[0x36] = (byte)(ram & 0xFF);
            file[0x37] = (byte)((ram >> 8) & 0xFF);
            file[0x38] = (byte)((ram >> 16) & 0xFF);

            Array.Copy(data, 0, file, HeaderSize, data.Length);
            return AddFile(clean, 'S', file, lastSlot + 1);
        }

        // ------------------------------------------------------------ drum sets

        /// <summary>One of the eight voices in a drum set.</summary>
        public sealed class DrumVoice
        {
            public int Index;          // bytes 0..1, always 0..7 in order
            public int Note;           // byte 2, MIDI note this voice answers to
            public byte[] Raw;         // the whole 30-byte record

            public string NoteName
            {
                get
                {
                    string[] n = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    return n[((Note % 12) + 12) % 12] + (Note / 12 - 2);
                }
            }
        }

        /// <summary>
        /// The drum set file: a 22-byte header and eight 30-byte voice records.
        /// 22 + 8 x 30 = 262, the length every drum set in the corpus has.
        /// Only the structure and the note assignment are decoded; see the format notes.
        /// </summary>
        public sealed class DrumSet
        {
            public const int HeaderBytes = 22;
            public const int VoiceBytes = 30;
            public const int Voices = 8;

            public bool InUse;                     // header byte 0 is 0xFF when it is
            public byte[] Header;
            public List<DrumVoice> Records = new List<DrumVoice>();
        }

        /// <summary>Reads a drum set file, or null if the entry is not one.</summary>
        public DrumSet ReadDrumSet(AkaiEntry e)
        {
            if (e == null || e.Type != 'D') return null;

            var raw = ReadFile(e);
            int need = DrumSet.HeaderBytes + DrumSet.Voices * DrumSet.VoiceBytes;
            if (raw.Length < need) return null;

            var set = new DrumSet { InUse = raw[0] == 0xFF, Header = new byte[DrumSet.HeaderBytes] };
            Array.Copy(raw, set.Header, DrumSet.HeaderBytes);

            for (int i = 0; i < DrumSet.Voices; i++)
            {
                int o = DrumSet.HeaderBytes + i * DrumSet.VoiceBytes;
                var rec = new byte[DrumSet.VoiceBytes];
                Array.Copy(raw, o, rec, 0, DrumSet.VoiceBytes);

                set.Records.Add(new DrumVoice
                {
                    Index = raw[o] | (raw[o + 1] << 8),
                    Note = raw[o + 2],
                    Raw = rec
                });
            }
            return set;
        }

        // ------------------------------------------------- keygroups: add/delete

        /// <summary>Most keygroups a program can hold: 38 + 70 x 64 = 4518 bytes.</summary>
        public const int MaxKeygroups = 64;

        /// <summary>Bytes 68..69 of a keygroup: the RAM address of the next one, 0 at the end.</summary>
        public const int KeygroupChainOffset = 68;

        /// <summary>Where a zone keeps its pointer, relative to the keygroup record.</summary>
        public const int ZonePointerOffset = KeygroupNameOffset + 16;

        /// <summary>Where keygroup records start in the sampler's RAM on most disks.</summary>
        public const int ArenaDefault = 0xC5F6;

        int _arenaBase = -1;

        /// <summary>Programs in directory order - the order the arena follows.</summary>
        public List<AkaiEntry> ProgramsInOrder()
        {
            return Entries.Where(e => e.Type == 'P').OrderBy(e => e.Slot).ToList();
        }

        public int TotalKeygroups()
        {
            int n = 0;
            foreach (var p in ProgramsInOrder()) n += KeygroupCount(p);
            return n;
        }

        /// <summary>
        /// The first record index of each program, by directory slot, and the number of
        /// records before the sample table. Programs are packed in directory order with a
        /// single empty 70-byte record between consecutive ones - which is the "irregular
        /// spacing" an earlier version preserved by shifting rather than deriving.
        /// </summary>
        public Dictionary<int, int> ArenaLayout(out int records)
        {
            var at = new Dictionary<int, int>();
            int rec = 0;
            foreach (var p in ProgramsInOrder())
            {
                at[p.Slot] = rec;
                rec += KeygroupCount(p) + 1;
            }
            records = Math.Max(0, rec - 1);
            return at;
        }

        /// <summary>
        /// Where the arena starts. Nothing stores it, so it is read back from the zone
        /// pointers - the ones the sampler actually follows - and only from the keygroup
        /// chains when a disk has no samples to point at. Captured on load, because the
        /// counts it is derived from change the moment anything is edited.
        /// </summary>
        int DeriveArenaBase()
        {
            int table = SampleTableBase();
            if (table >= 0)
            {
                int fromZones = table - KeygroupSize * (TotalKeygroups() + ProgramsInOrder().Count - 1);
                if (fromZones > 0 && fromZones < SampleRamBase) return fromZones;
            }

            int records;
            var first = ArenaLayout(out records);
            var votes = new Dictionary<int, int>();
            int best = -1, bestCount = 0;

            foreach (var p in ProgramsInOrder())
            {
                if (KeygroupCount(p) < 2) continue;
                int v = ChainOf(ReadFile(p), 0) - KeygroupSize * (first[p.Slot] + 1);
                if (v <= 0 || v >= SampleRamBase) continue;

                int n;
                votes[v] = n = (votes.TryGetValue(v, out n) ? n : 0) + 1;
                if (n > bestCount) { bestCount = n; best = v; }
            }
            return best > 0 ? best : ArenaDefault;
        }

        public int ArenaBase()
        {
            if (_arenaBase < 0) _arenaBase = DeriveArenaBase();
            return _arenaBase;
        }

        /// <summary>Where the sample descriptor table begins, derived rather than guessed.</summary>
        public int SampleTableAddress()
        {
            int records;
            ArenaLayout(out records);
            return ArenaBase() + KeygroupSize * records;
        }

        /// <summary>
        /// Rewrites every keygroup chain pointer and every zone pointer from the layout.
        /// Call after anything that changes the number of programs, keygroups or samples.
        ///
        /// This replaces shifting the stored values by a delta, which conflated the chain
        /// pointers with the zone pointers - different address spaces - and compounded
        /// until a disk's chains sat 27,000 bytes below the arena and two programs shared
        /// a record. A derived layout cannot drift.
        /// </summary>
        public int RebuildPointers()
        {
            int records;
            var first = ArenaLayout(out records);
            int arena = ArenaBase(), table = arena + KeygroupSize * records, changed = 0;

            foreach (var p in ProgramsInOrder())
            {
                int count = KeygroupCount(p), start = first[p.Slot];
                var raw = ReadFile(p);

                // The program header restates two things the rest of the file implies:
                // where its keygroups load (18..19) and how many there are (23). The
                // sampler believes the header, so a stale count silently drops keygroups
                // and a stale load address drops one program's records onto another's.
                int load = arena + KeygroupSize * start;
                if (raw.Length > 19 && (raw[18] | (raw[19] << 8)) != load)
                {
                    PokeU16(p, 18, load);
                    changed++;
                }
                if (raw.Length > 23 && raw[23] != count)
                {
                    PokeFile(p, 23, (byte)count);
                    changed++;
                }

                for (int k = 0; k < count; k++)
                {
                    int kg = ProgHeaderSize + k * KeygroupSize;

                    int co = kg + KeygroupChainOffset;
                    int wantNext = k == count - 1 ? 0 : arena + KeygroupSize * (start + k + 1);
                    if (co + 1 < raw.Length && (raw[co] | (raw[co + 1] << 8)) != wantNext)
                    {
                        PokeU16(p, co, wantNext);
                        changed++;
                    }

                    for (int z = 0; z < 2; z++)
                    {
                        int no = kg + KeygroupNameOffset + z * KeygroupZoneStride;
                        int po = no + 16;
                        if (po + 1 >= raw.Length) continue;

                        string name = CleanName(raw, no).Trim();
                        int have = raw[po] | (raw[po + 1] << 8);
                        if (name.Length == 0 || name == "2 SAMPLE" || have == 0) continue;

                        int i = SampleIndex(name);
                        if (i < 0) continue;

                        int wantZone = table + KeygroupSize * i;
                        if (have != wantZone) { PokeU16(p, po, wantZone); changed++; }
                    }
                }
            }

            if (changed > 0) Modified = true;
            return changed;
        }

        /// <summary>
        /// The RAM address of a program's first keygroup record. It is not stored: each
        /// record points at the *next* one, so the first address is the first pointer
        /// less one record. A program with a single keygroup stores no pointer at all,
        /// so its base is inferred from the program before it, and failing that from the
        /// value 87 of the 96 multi-keygroup disks start at.
        /// </summary>
        public int KeygroupArenaBase(AkaiEntry program)
        {
            var raw = ReadFile(program);
            int count = KeygroupCount(program);

            if (count >= 2)
            {
                int first = ChainOf(raw, 0);
                if (first >= KeygroupSize) return first - KeygroupSize;
            }

            // Walk back to the nearest program that does record one.
            AkaiEntry prev = null;
            foreach (var p in Entries)
                if (p.Type == 'P' && p.Slot < program.Slot &&
                    (prev == null || p.Slot > prev.Slot)) prev = p;

            if (prev != null && KeygroupCount(prev) >= 2)
            {
                int b = ChainOf(ReadFile(prev), 0) - KeygroupSize;
                if (b > 0) return b + KeygroupSize * (KeygroupCount(prev) + 1);
            }
            return 0xC5F6;
        }

        static int ChainOf(byte[] file, int index)
        {
            int o = ProgHeaderSize + index * KeygroupSize + KeygroupChainOffset;
            return o + 1 < file.Length ? file[o] | (file[o + 1] << 8) : 0;
        }

        /// <summary>
        /// Rewrites a program's keygroup chain: each record points at the next, 70 bytes
        /// on, and the last holds zero. True of all 392 programs in the corpus.
        /// </summary>
        static void Relink(byte[] file, int count, int arenaBase)
        {
            for (int i = 0; i < count; i++)
            {
                int next = i == count - 1 ? 0 : arenaBase + KeygroupSize * (i + 1);
                int o = ProgHeaderSize + i * KeygroupSize + KeygroupChainOffset;
                PutU16(file, o, next);
            }
        }

        /// <summary>
        /// Adds a keygroup, seeded from an existing one so it is playable rather than
        /// blank, and returns the new count. The program file grows by 70 bytes.
        /// </summary>
        public int AddKeygroup(AkaiEntry program, int copyFrom)
        {
            if (program == null || program.Type != 'P') throw new ArgumentException("not a program", "program");

            ArenaBase();                       // captured before the count changes

            int count = KeygroupCount(program);
            if (count >= MaxKeygroups)
                throw new InvalidOperationException("A program can hold at most " + MaxKeygroups + " keygroups.");

            var data = ReadFile(program);
            int arenaBase = KeygroupArenaBase(program);

            var bigger = new byte[data.Length + KeygroupSize];
            Array.Copy(data, bigger, data.Length);

            int src = ProgHeaderSize + Math.Max(0, Math.Min(count - 1, copyFrom)) * KeygroupSize;
            Array.Copy(data, src, bigger, ProgHeaderSize + count * KeygroupSize, KeygroupSize);

            Relink(bigger, count + 1, arenaBase);
            ResizeFile(program, bigger);

            Modified = true;
            ParseDirectory();
            RebuildPointers();   // an extra keygroup moves the sample table, so every
            ParseDirectory();    // zone pointer past it changes too
            return count + 1;
        }

        /// <summary>Removes a keygroup and returns the new count. The file shrinks by 70 bytes.</summary>
        public int DeleteKeygroup(AkaiEntry program, int index)
        {
            if (program == null || program.Type != 'P') throw new ArgumentException("not a program", "program");

            ArenaBase();

            int count = KeygroupCount(program);
            if (count <= 1) throw new InvalidOperationException("A program must keep at least one keygroup.");
            if (index < 0 || index >= count) throw new ArgumentOutOfRangeException("index");

            var data = ReadFile(program);
            int arenaBase = KeygroupArenaBase(program);

            var smaller = new byte[data.Length - KeygroupSize];
            int cut = ProgHeaderSize + index * KeygroupSize;
            Array.Copy(data, 0, smaller, 0, cut);
            Array.Copy(data, cut + KeygroupSize, smaller, cut, data.Length - cut - KeygroupSize);

            Relink(smaller, count - 1, arenaBase);
            ResizeFile(program, smaller);

            Modified = true;
            ParseDirectory();
            RebuildPointers();
            ParseDirectory();
            return count - 1;
        }

        void PokeU16(AkaiEntry e, int offset, int value)
        {
            PokeFile(e, offset, (byte)(value & 0xFF));
            PokeFile(e, offset + 1, (byte)((value >> 8) & 0xFF));
        }

        /// <summary>
        /// Writes a file at a new length, taking more blocks or giving some back. The
        /// chain keeps the blocks it already had; only the difference is allocated.
        /// </summary>
        public void ResizeFile(AkaiEntry e, byte[] contents)
        {
            var chain = Chain(e.StartBlock);
            int need = BlocksFor(contents.Length);

            if (need > chain.Count)
            {
                int wanted = need - chain.Count;
                var extra = new List<int>();
                for (int b = 4; b < TotalBlocks && extra.Count < wanted; b++)
                    if (Fat(b) == 0) extra.Add(b);

                if (extra.Count < wanted)
                    throw new InvalidOperationException(
                        "Not enough room: needs " + wanted + " more block(s), " + FreeBlocks + " free.");
                chain.AddRange(extra);
            }

            for (int i = 0; i < need; i++)
            {
                int off = chain[i] * BlockSize;
                Array.Clear(Image, off, BlockSize);
                int take = Math.Min(BlockSize, contents.Length - i * BlockSize);
                if (take > 0) Array.Copy(contents, i * BlockSize, Image, off, take);
                SetFat(chain[i], i == need - 1 ? FatEnd : chain[i + 1]);
            }
            for (int i = need; i < chain.Count; i++) SetFat(chain[i], 0);

            int d = DirOffset + e.Slot * EntrySize;
            Image[d + 17] = (byte)(contents.Length & 0xFF);
            Image[d + 18] = (byte)((contents.Length >> 8) & 0xFF);
            Image[d + 19] = (byte)((contents.Length >> 16) & 0xFF);

            //
            // The caller's entry describes a file that is now a different length, and it
            // is the only copy it has: ParseDirectory replaces every AkaiEntry in the
            // list, so an entry held across an edit never hears about the change.
            //
            // THIS MATTERED. AddKeygroup works out how many keygroups a program has from
            // its entry's length, so a second AddKeygroup on a held entry counted the
            // keygroups the program had BEFORE the first one - and wrote the new keygroup
            // over the top of it instead of after it. Every three-layer programme came out
            // with two layers, silently, because a two-keygroup program is perfectly valid
            // and nothing downstream had any reason to object.
            //
            e.Length = contents.Length;
            e.ChainBlocks = chain.Count;
            e.ChainOk = e.ChainBlocks >= need;

            Modified = true;
        }

        /// <summary>
        /// Renames a file, writing the new name into both places it is held: the
        /// directory entry and the file's own header. Renaming a sample also rewrites
        /// every keygroup zone that referred to it by the old name, so no program is
        /// left pointing at something that is no longer there. Returns the number of
        /// keygroup references updated.
        /// </summary>
        public int RenameFile(AkaiEntry e, string newName)
        {
            if (e == null) throw new ArgumentNullException("e");

            string clean = NormaliseName(newName);
            if (string.Equals(clean, e.Name, StringComparison.Ordinal)) return 0;

            foreach (var x in Entries)
                if (x.Slot != e.Slot && x.Type == e.Type &&
                    string.Equals(x.Name, clean, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "'" + clean + "' is already used by another " + e.TypeName + ".");

            string old = e.Name;

            int d = DirOffset + e.Slot * EntrySize;
            for (int i = 0; i < 10; i++) Image[d + i] = (byte)(i < clean.Length ? clean[i] : ' ');

            // Programs and samples both carry their name in the first 10 bytes of the
            // file. Other types are left to the directory alone - their headers are not
            // decoded, so there is nothing to keep in step.
            if (e.Type == 'P' || e.Type == 'S')
                for (int i = 0; i < 10; i++)
                    PokeFile(e, i, (byte)(i < clean.Length ? clean[i] : ' '));

            int refs = e.Type == 'S' ? RetargetSampleReferences(old, clean) : 0;

            Modified = true;
            ParseDirectory();
            return refs;
        }

        /// <summary>
        /// Points every keygroup zone naming <paramref name="oldName"/> at a new name.
        /// Zones hold the sample's name directly, so a rename has to follow them.
        /// </summary>
        int RetargetSampleReferences(string oldName, string newName)
        {
            int changed = 0;
            foreach (var p in Entries)
            {
                if (p.Type != 'P') continue;

                var data = ReadFile(p);
                int count = KeygroupCount(p);

                for (int k = 0; k < count; k++)
                {
                    int kg = ProgHeaderSize + k * KeygroupSize;
                    for (int z = 0; z < 2; z++)
                    {
                        int off = kg + KeygroupNameOffset + z * KeygroupZoneStride;
                        if (off + 10 > data.Length) continue;
                        if (!string.Equals(CleanName(data, off), oldName, StringComparison.OrdinalIgnoreCase))
                            continue;

                        for (int i = 0; i < 10; i++)
                            PokeFile(p, off + i, (byte)(i < newName.Length ? newName[i] : ' '));
                        changed++;
                    }
                }
            }
            return changed;
        }

        /// <summary>
        /// Replaces a sample's audio with a shorter version - fewer words, usually at a
        /// lower rate - and hands the blocks it no longer needs back to the free pool.
        /// Markers are scaled with the word count, and the samples that follow have their
        /// RAM address and loop pointer shifted by the space this one gave up.
        /// Returns the number of blocks freed.
        /// </summary>
        public int ReplaceSampleData(AkaiEntry e, short[] words12, int sampleRate)
        {
            if (e == null || e.Type != 'S') throw new ArgumentException("not a sample", "e");

            int n = words12.Length & ~1;
            if (n < 2) throw new InvalidOperationException("There would be no audio left.");

            long oldWords = e.SampleCount;
            if (n > oldWords)
                throw new InvalidOperationException("A sample can only be made shorter in place.");

            return RewriteSample(e, words12, n, sampleRate,
                                 Scale(e.LoopEnd, oldWords, n),
                                 Scale(e.LoopStart, oldWords, n),
                                 Scale(e.LoopLength, oldWords, n));
        }

        /// <summary>
        /// Writes a shorter sample into the blocks it already occupies, hands back the
        /// ones it no longer needs, and shifts the samples that follow it in RAM.
        /// Markers are supplied by the caller: a resample scales them, a trim shifts them.
        /// </summary>
        int RewriteSample(AkaiEntry e, short[] words12, int n, int sampleRate,
                          long end, long start, long loopLength)
        {
            long oldWords = e.SampleCount;
            var data = PackSampleData(words12, n);
            int newLength = HeaderSize + data.Length;

            int had = Chain(e.StartBlock).Count;

            // Keep the old header and adjust only what the resize changes.
            var file = new byte[newLength];
            Array.Copy(Image, e.StartBlock * BlockSize, file, 0, HeaderSize);

            PutU32(file, 0x10, n);
            PutU16(file, 0x14, sampleRate);
            PutU32(file, 0x1C, end);
            PutU32(file, 0x20, start);
            PutU32(file, 0x24, loopLength);
            Array.Copy(data, 0, file, HeaderSize, data.Length);

            // ResizeFile takes more blocks or gives some back, so a sample may grow as
            // well as shrink - a slower time stretch needs the room.
            ResizeFile(e, file);

            // Shift, rather than re-derive: any sample whose stored values do not follow
            // the usual formula keeps its own arrangement.
            int ramDelta = RamSize(n) - RamSize(oldWords);
            int ptrDelta = 10 * (LoopRecords(n, e.LoopMode) - LoopRecords(oldWords, e.LoopMode));
            ShiftSampleRam(e.Slot, ramDelta, ptrDelta);

            Modified = true;
            ParseDirectory();
            return had - BlocksFor(newLength);      // negative when the sample grew
        }

        /// <summary>
        /// Replaces a sample's audio with a version of any length, growing the file if it
        /// needs to. Markers scale with the new length, as they do for a resample.
        /// </summary>
        public int ReplaceSampleAudio(AkaiEntry e, short[] words12, int sampleRate)
        {
            if (e == null || e.Type != 'S') throw new ArgumentException("not a sample", "e");

            int n = words12.Length & ~1;
            if (n < 2) throw new InvalidOperationException("There would be no audio left.");

            long oldWords = e.SampleCount;
            return RewriteSample(e, words12, n, sampleRate,
                                 Scale(e.LoopEnd, oldWords, n),
                                 Scale(e.LoopStart, oldWords, n),
                                 Scale(e.LoopLength, oldWords, n));
        }

        /// <summary>
        /// Changes only the rate a sample plays back at, which is what a sampler's
        /// varispeed does: the audio is untouched, so it plays faster or slower and the
        /// pitch moves with it. Nothing is reallocated.
        /// </summary>
        public void SetSampleRate(AkaiEntry e, int sampleRate)
        {
            if (e == null || e.Type != 'S') throw new ArgumentException("not a sample", "e");
            if (sampleRate < 1000 || sampleRate > 48000)
                throw new InvalidOperationException(sampleRate.ToString("N0") +
                    " Hz is outside anything the sampler uses.");

            PokeFile(e, 0x14, (byte)(sampleRate & 0xFF));
            PokeFile(e, 0x15, (byte)((sampleRate >> 8) & 0xFF));
            Modified = true;
            ParseDirectory();
        }

        /// <summary>What trimming a sample's leading silence would remove.</summary>
        public sealed class TrimPlan
        {
            public long Front;              // words of silence before the audio starts
            public long NewWords;
            public int BlocksFreed;
            public double Seconds;          // how much time that is
            public bool Anything { get { return Front > 0; } }
        }

        /// <summary>
        /// Finds the silence before a sample's first audible word. Only the front is
        /// considered: the tail is where a loop lives, and where a decay usually fades
        /// below any threshold you would pick.
        /// </summary>
        public TrimPlan PlanTrim(AkaiEntry e, int threshold)
        {
            var plan = new TrimPlan();
            if (e == null || e.Type != 'S') return plan;

            var w = SampleWords12(e);
            if (w.Length < 4) return plan;

            int first = 0;
            while (first < w.Length && Math.Abs((int)w[first]) <= threshold) first++;
            if (first >= w.Length) return plan;          // silent throughout: leave it be

            // The packing works in pairs, so keep an even count by leaving one more word.
            if (((w.Length - first) & 1) != 0) first--;
            if (first <= 0) return plan;

            plan.Front = first;
            plan.NewWords = w.Length - first;
            plan.BlocksFreed = BlocksFor(e.Length) -
                               BlocksFor(HeaderSize + (int)(plan.NewWords * 3 / 2));
            plan.Seconds = e.SampleRate > 0 ? (double)first / e.SampleRate : 0;
            return plan;
        }

        /// <summary>
        /// Removes the silence before a sample starts. Markers move with the audio: what
        /// was at word 1000 is at word 1000 - front afterwards. Returns the blocks freed.
        /// </summary>
        public int TrimSample(AkaiEntry e, int threshold)
        {
            var plan = PlanTrim(e, threshold);
            if (!plan.Anything) throw new InvalidOperationException("There is no leading silence to trim.");

            var w = SampleWords12(e);
            var kept = new short[plan.NewWords];
            Array.Copy(w, (int)plan.Front, kept, 0, kept.Length);

            long n = plan.NewWords;
            long start = Shift(e.LoopStart, plan.Front, n);
            long end = e.LoopEnd >= w.Length ? n : Shift(e.LoopEnd, plan.Front, n);
            long loop = Math.Min(e.LoopLength, n);

            return RewriteSample(e, kept, (int)n, e.SampleRate, end, start, loop);
        }

        static long Shift(long marker, long front, long newWords)
        {
            long v = marker - front;
            if (v < 0) v = 0;
            return v > newWords ? newWords : v;
        }

        static long Scale(long marker, long oldWords, long newWords)
        {
            if (oldWords <= 0) return 0;
            long v = marker * newWords / oldWords;
            return v < 0 ? 0 : Math.Min(v, newWords);
        }

        /// <summary>
        /// Moves every sample after <paramref name="afterSlot"/> along in sample RAM.
        /// Samples are laid out back to back in directory order, so one changing size
        /// displaces all the rest.
        /// </summary>
        public int ShiftSampleRam(int afterSlot, int ramDelta, int ptrDelta)
        {
            if (ramDelta == 0 && ptrDelta == 0) return 0;

            int moved = 0;
            foreach (var s in Entries)
            {
                if (s.Type != 'S' || s.Slot <= afterSlot) continue;

                int o = s.StartBlock * BlockSize;
                int ram = s.MemoryAddress + ramDelta;
                int ptr = s.LoopDescriptorPtr + ptrDelta;
                if (ram < 0 || ptr < 0) continue;               // refuse to write nonsense

                Image[o + 0x36] = (byte)(ram & 0xFF);
                Image[o + 0x37] = (byte)((ram >> 8) & 0xFF);
                Image[o + 0x38] = (byte)((ram >> 16) & 0xFF);
                PutU16(Image, o + 0x28, ptr & 0xFFFF);
                moved++;
            }
            Modified = true;
            return moved;
        }

        /// <summary>Write one byte into a file's payload at a given offset.</summary>
        public void PokeFile(AkaiEntry e, int offset, byte value)
        {
            var blocks = Chain(e.StartBlock);
            int bi = offset / BlockSize, bo = offset % BlockSize;
            if (bi < 0 || bi >= blocks.Count) throw new ArgumentOutOfRangeException("offset");
            Image[blocks[bi] * BlockSize + bo] = value;
            Modified = true;
        }
        /// <summary>Load a .hfe image or a raw 800K/1600K sector image.</summary>
        public static AkaiDisk Load(string file)
        {
            return LoadFromBytes(file, File.ReadAllBytes(file));
        }

        /// <summary>
        /// The same, from bytes already in hand. <paramref name="name"/> is only what the
        /// disk calls itself; nothing is read from it.
        /// </summary>
        public static AkaiDisk LoadFromBytes(string name, byte[] raw)
        {
            var d = new AkaiDisk { Source = name };

            bool isHfe = raw.Length > 8 && Encoding.ASCII.GetString(raw, 0, 8) == "HXCPICFE";
            if (isHfe)
            {
                int bad, miss;
                d.Image = Hfe.Extract(raw, out bad, out miss);
                d.BadCrcSectors = bad;
                d.MissingSectors = miss;
                d.RawHfe = raw;
                d.IsHfe = true;
            }
            else if (raw.Length == 819200 || raw.Length == 1638400)
            {
                d.Image = raw;
                d.RawHfe = raw;
                d.IsHfe = false;
            }
            else throw new InvalidDataException("not an HFE image or an 800K/1600K raw image");

            d.ParseDirectory();
            d.ArenaBase();      // pin it now, before any edit changes the counts it comes from
            return d;
        }
    }
}
