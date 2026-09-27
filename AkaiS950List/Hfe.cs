using System;
using System.Collections.Generic;

namespace AkaiS950List
{
    /// <summary>A sector recovered from the MFM bitstream.</summary>
    public sealed class Sect
    {
        public int Cyl, Head, Sec, SizeCode;
        public byte[] Data;
        public bool IdCrcOk, DataCrcOk;
    }

    /// <summary>
    /// Decoder for HxC/Gotek .hfe images (HXCPICFE rev 0). The image stores raw MFM
    /// cell data for both sides interleaved in 256-byte chunks; this reads the cells,
    /// finds the A1A1A1 sync marks, decodes the ID and data fields and verifies CRCs.
    /// </summary>
    public static class Hfe
    {
        static ushort U16(byte[] b, int o) { return (ushort)(b[o] | (b[o + 1] << 8)); }

        /// <summary>Pull one side's cell bytes out of the interleaved track blocks.</summary>
        public static byte[] SideCells(byte[] img, int track, int side)
        {
            int lut = U16(img, 18) * 512;
            if (lut + track * 4 + 3 >= img.Length) return new byte[0];
            int tOff = U16(img, lut + track * 4) * 512;
            int tLen = U16(img, lut + track * 4 + 2);
            int half = tLen / 2;
            if (half <= 0) return new byte[0];

            var o = new byte[half];
            int w = 0, pos = tOff;
            while (w < half)
            {
                int chunk = Math.Min(256, half - w);
                int src = pos + (side == 0 ? 0 : 256);
                if (src < 0 || src + chunk > img.Length) break;
                Array.Copy(img, src, o, w, chunk);
                w += chunk;
                pos += 512;
            }
            return o;
        }

        /// <summary>One byte per MFM cell; HFE stores the first cell in bit 0.</summary>
        static byte[] Unpack(byte[] cells)
        {
            var bits = new byte[cells.Length * 8];
            for (int i = 0; i < cells.Length; i++)
            {
                byte c = cells[i];
                for (int b = 0; b < 8; b++) bits[i * 8 + b] = (byte)((c >> b) & 1);
            }
            return bits;
        }

        // In MFM the cells alternate clock, data; the data bits sit at odd offsets.
        static byte ReadByte(byte[] bits, int groupStart)
        {
            int n = bits.Length, v = 0;
            for (int i = 0; i < 8; i++) v = (v << 1) | bits[(groupStart + 2 * i + 1) % n];
            return (byte)v;
        }

        static ushort Crc(byte[] d, int len, ushort crc)
        {
            for (int i = 0; i < len; i++)
            {
                crc ^= (ushort)(d[i] << 8);
                for (int b = 0; b < 8; b++)
                    crc = (ushort)(((crc & 0x8000) != 0) ? ((crc << 1) ^ 0x1021) : (crc << 1));
            }
            return crc;
        }

        static readonly byte[] A1x3 = new byte[] { 0xA1, 0xA1, 0xA1 };

        public static List<Sect> DecodeTrack(byte[] cells)
        {
            var outp = new List<Sect>();
            if (cells.Length == 0) return outp;

            byte[] bits = Unpack(cells);
            int n = bits.Length;
            uint sr = 0;
            int lastSync = -100, run = 0;
            Sect pendingId = null;

            for (int p = 0; p < n; p++)
            {
                sr = (sr << 1) | bits[p];
                if ((sr & 0xFFFF) != 0x4489) continue;   // A1 with a missing clock

                run = (p - lastSync == 16) ? run + 1 : 1;
                lastSync = p;
                if (run < 3) continue;

                int g = p + 1;
                byte mark = ReadByte(bits, g); g += 16;

                if (mark == 0xFE)                        // ID address mark
                {
                    var id = new byte[4];
                    for (int i = 0; i < 4; i++) { id[i] = ReadByte(bits, g); g += 16; }
                    int cs = ReadByte(bits, g) << 8; g += 16;
                    cs |= ReadByte(bits, g); g += 16;

                    ushort c = Crc(A1x3, 3, 0xFFFF);
                    c = Crc(new[] { mark }, 1, c);
                    c = Crc(id, 4, c);

                    pendingId = new Sect
                    {
                        Cyl = id[0], Head = id[1], Sec = id[2], SizeCode = id[3],
                        IdCrcOk = (c == cs)
                    };
                }
                else if ((mark == 0xFB || mark == 0xF8) && pendingId != null)  // data mark
                {
                    int sz = 128 << (pendingId.SizeCode & 7);
                    var data = new byte[sz];
                    for (int i = 0; i < sz; i++) { data[i] = ReadByte(bits, g); g += 16; }
                    int cs = ReadByte(bits, g) << 8; g += 16;
                    cs |= ReadByte(bits, g); g += 16;

                    ushort c = Crc(A1x3, 3, 0xFFFF);
                    c = Crc(new[] { mark }, 1, c);
                    c = Crc(data, sz, c);

                    pendingId.Data = data;
                    pendingId.DataCrcOk = (c == cs);
                    outp.Add(pendingId);
                    pendingId = null;
                }
            }
            return outp;
        }

        /// <summary>
        /// Decode every track and lay the sectors out linearly:
        /// LBA = (cyl * sides + head) * spt + (sec - 1), 1024 bytes each.
        ///
        /// spt is 5 on a double-density disk (800K) and 10 on a high-density one (1600K) -
        /// the S950 formats both. The disk says which: every track is decoded first and the
        /// highest sector number found decides it, rather than the header's bit rate.
        /// </summary>
        public static byte[] Extract(byte[] raw, out int badCrc, out int missing)
        {
            const int Ssz = 1024;
            int tracks = raw[9], sides = raw[10];

            var decoded = new List<List<Sect>>();
            int highest = 0;
            for (int t = 0; t < tracks; t++)
                for (int s = 0; s < sides; s++)
                {
                    var list = DecodeTrack(SideCells(raw, t, s));
                    decoded.Add(list);
                    foreach (var sec in list)
                        if (sec.Data != null && sec.Sec >= 1 && sec.Sec <= 10 && sec.Sec > highest) highest = sec.Sec;
                }

            int Spt = highest > 5 ? 10 : 5;
            var img = new byte[tracks * sides * Spt * Ssz];
            var got = new bool[tracks * sides * Spt];
            badCrc = 0;

            for (int t = 0; t < tracks; t++)
                for (int s = 0; s < sides; s++)
                    foreach (var sec in decoded[t * sides + s])
                    {
                        if (sec.Data == null) continue;
                        if (sec.Sec < 1 || sec.Sec > Spt || sec.Cyl >= tracks || sec.Head >= sides) continue;

                        int lba = (sec.Cyl * sides + sec.Head) * Spt + (sec.Sec - 1);
                        if (lba < 0 || lba >= got.Length || got[lba]) continue;   // keep the first good copy

                        if (!sec.DataCrcOk || !sec.IdCrcOk) badCrc++;
                        Array.Copy(sec.Data, 0, img, lba * Ssz, Math.Min(Ssz, sec.Data.Length));
                        got[lba] = true;
                    }

            missing = 0;
            foreach (bool g in got) if (!g) missing++;
            return img;
        }
    }
}
