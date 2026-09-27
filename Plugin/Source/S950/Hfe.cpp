#include "Hfe.h"

#include <algorithm>
#include <cstring>

namespace s950::hfe
{
    namespace
    {
        std::uint16_t u16 (const std::vector<unsigned char>& b, std::size_t at)
        {
            if (at + 1 >= b.size()) return 0;
            return static_cast<std::uint16_t> (b[at] | (b[at + 1] << 8));
        }

        /// One side's cell bytes, pulled out of the interleaved track blocks.
        std::vector<unsigned char> sideCells (const std::vector<unsigned char>& img,
                                              int track, int side)
        {
            const std::size_t lut = static_cast<std::size_t> (u16 (img, 18)) * 512;
            if (lut + static_cast<std::size_t> (track) * 4 + 3 >= img.size())
                return {};

            const std::size_t tOff = static_cast<std::size_t> (u16 (img, lut + static_cast<std::size_t> (track) * 4)) * 512;
            const int         tLen = u16 (img, lut + static_cast<std::size_t> (track) * 4 + 2);
            const int         half = tLen / 2;

            if (half <= 0) return {};

            std::vector<unsigned char> out (static_cast<std::size_t> (half));

            int         w   = 0;
            std::size_t pos = tOff;

            while (w < half)
            {
                const int         chunk = std::min (256, half - w);
                const std::size_t src   = pos + (side == 0 ? 0 : 256);

                if (src + static_cast<std::size_t> (chunk) > img.size()) break;

                std::memcpy (out.data() + w, img.data() + src, static_cast<std::size_t> (chunk));
                w   += chunk;
                pos += 512;
            }

            return out;
        }

        /// One byte per MFM cell; HFE stores the first cell in bit 0.
        std::vector<unsigned char> unpack (const std::vector<unsigned char>& cells)
        {
            std::vector<unsigned char> bits (cells.size() * 8);

            for (std::size_t i = 0; i < cells.size(); ++i)
            {
                const unsigned char c = cells[i];
                for (int b = 0; b < 8; ++b)
                    bits[i * 8 + static_cast<std::size_t> (b)] = static_cast<unsigned char> ((c >> b) & 1);
            }

            return bits;
        }

        /*
         * In MFM the cells alternate clock, data, so the data bits sit at odd offsets.
         *
         * The modulo is not caution: a sector's last field can run off the end of the track
         * and wrap round to its start, which is what the physical disk does too.
         */
        unsigned char readByte (const std::vector<unsigned char>& bits, std::size_t groupStart)
        {
            const std::size_t n = bits.size();
            if (n == 0) return 0;

            int v = 0;
            for (std::size_t i = 0; i < 8; ++i)
                v = (v << 1) | bits[(groupStart + 2 * i + 1) % n];

            return static_cast<unsigned char> (v);
        }

        /// CRC-16/CCITT, as the floppy format uses it.
        std::uint16_t crc16 (const unsigned char* d, std::size_t len, std::uint16_t crc)
        {
            for (std::size_t i = 0; i < len; ++i)
            {
                crc ^= static_cast<std::uint16_t> (d[i] << 8);

                for (int b = 0; b < 8; ++b)
                    crc = static_cast<std::uint16_t> ((crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021)
                                                                         : (crc << 1));
            }

            return crc;
        }

        struct Sector
        {
            int  cyl = 0, head = 0, sec = 0, sizeCode = 0;
            std::vector<unsigned char> data;
            bool idCrcOk = false, dataCrcOk = false;
        };

        const unsigned char A1x3[3] = { 0xA1, 0xA1, 0xA1 };

        std::vector<Sector> decodeTrack (const std::vector<unsigned char>& cells)
        {
            std::vector<Sector> out;
            if (cells.empty()) return out;

            const auto bits = unpack (cells);
            const std::size_t n = bits.size();

            std::uint32_t sr = 0;
            long long lastSync = -100;
            int  run = 0;

            bool   havePending = false;
            Sector pending;

            for (std::size_t p = 0; p < n; ++p)
            {
                sr = (sr << 1) | bits[p];

                // A1 written with a missing clock bit - the mark that says a field follows.
                if ((sr & 0xFFFF) != 0x4489) continue;

                run = (static_cast<long long> (p) - lastSync == 16) ? run + 1 : 1;
                lastSync = static_cast<long long> (p);
                if (run < 3) continue;

                std::size_t g = p + 1;
                const unsigned char mark = readByte (bits, g);
                g += 16;

                if (mark == 0xFE)                                   // ID address mark
                {
                    unsigned char id[4];
                    for (int i = 0; i < 4; ++i) { id[i] = readByte (bits, g); g += 16; }

                    int cs = readByte (bits, g) << 8; g += 16;
                    cs |= readByte (bits, g);         g += 16;

                    std::uint16_t c = crc16 (A1x3, 3, 0xFFFF);
                    c = crc16 (&mark, 1, c);
                    c = crc16 (id, 4, c);

                    pending = Sector();
                    pending.cyl      = id[0];
                    pending.head     = id[1];
                    pending.sec      = id[2];
                    pending.sizeCode = id[3];
                    pending.idCrcOk  = (c == cs);
                    havePending = true;
                }
                else if ((mark == 0xFB || mark == 0xF8) && havePending)   // data mark
                {
                    const std::size_t sz = static_cast<std::size_t> (128) << (pending.sizeCode & 7);

                    std::vector<unsigned char> data (sz);
                    for (std::size_t i = 0; i < sz; ++i) { data[i] = readByte (bits, g); g += 16; }

                    int cs = readByte (bits, g) << 8; g += 16;
                    cs |= readByte (bits, g);         g += 16;

                    std::uint16_t c = crc16 (A1x3, 3, 0xFFFF);
                    c = crc16 (&mark, 1, c);
                    c = crc16 (data.data(), sz, c);

                    pending.data      = std::move (data);
                    pending.dataCrcOk = (c == cs);

                    out.push_back (std::move (pending));
                    havePending = false;
                }
            }

            return out;
        }
    }

    bool looksLikeHfe (const std::vector<unsigned char>& raw)
    {
        return raw.size() > 8 && std::equal (raw.begin(), raw.begin() + 8, "HXCPICFE");
    }

    std::vector<unsigned char> extract (const std::vector<unsigned char>& raw,
                                        int& badCrc, int& missing)
    {
        constexpr int Ssz = 1024;

        badCrc  = 0;
        missing = 0;

        if (raw.size() < 12) return {};

        const int tracks = raw[9];
        const int sides  = raw[10];

        if (tracks <= 0 || sides <= 0) return {};

        /*
         * Five sectors a track on a double-density disk (800K) and ten on a high-density one
         * (1600K) - the S950 formats both. The disk says which: decode every track first and
         * see how high the sector numbers go, rather than trusting the header's bit rate.
         */
        std::vector<std::vector<Sector>> decoded;
        decoded.reserve (static_cast<std::size_t> (tracks * sides));
        int highest = 0;

        for (int t = 0; t < tracks; ++t)
            for (int s = 0; s < sides; ++s)
            {
                decoded.push_back (decodeTrack (sideCells (raw, t, s)));
                for (const auto& sec : decoded.back())
                    if (! sec.data.empty() && sec.sec >= 1 && sec.sec <= 10)
                        highest = std::max (highest, static_cast<int> (sec.sec));
            }

        const int Spt = highest > 5 ? 10 : 5;
        const std::size_t sectors = static_cast<std::size_t> (tracks) * sides * Spt;

        std::vector<unsigned char> img (sectors * Ssz, 0);
        std::vector<bool>          got (sectors, false);

        for (int t = 0; t < tracks; ++t)
        {
            for (int s = 0; s < sides; ++s)
            {
                for (const auto& sec : decoded[static_cast<std::size_t> (t * sides + s)])
                {
                    if (sec.data.empty()) continue;
                    if (sec.sec < 1 || sec.sec > Spt) continue;
                    if (sec.cyl >= tracks || sec.head >= sides) continue;

                    const long long lba = (static_cast<long long> (sec.cyl) * sides + sec.head) * Spt
                                        + (sec.sec - 1);

                    if (lba < 0 || lba >= static_cast<long long> (sectors)) continue;
                    if (got[static_cast<std::size_t> (lba)]) continue;   // keep the first good copy

                    if (! sec.dataCrcOk || ! sec.idCrcOk) ++badCrc;

                    std::memcpy (img.data() + static_cast<std::size_t> (lba) * Ssz,
                                 sec.data.data(),
                                 std::min (static_cast<std::size_t> (Ssz), sec.data.size()));

                    got[static_cast<std::size_t> (lba)] = true;
                }
            }
        }

        for (bool g : got)
            if (! g) ++missing;

        return img;
    }
}
