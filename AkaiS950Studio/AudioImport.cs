using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace AkaiS950Studio
{
    /// <summary>Audio read from a file: mixed to mono, normalised to -1..1.</summary>
    internal sealed class AudioClip
    {
        public float[] Mono = new float[0];
        public int SampleRate = 44100;
        public int Channels = 1;
        public int Bits = 16;
        public string Format = "";

        public double Seconds { get { return SampleRate > 0 ? (double)Mono.Length / SampleRate : 0; } }

        public string Describe()
        {
            return Format + ", " + SampleRate.ToString("N0") + " Hz, " +
                   (Channels == 1 ? "mono" : Channels == 2 ? "stereo" : Channels + " channels") +
                   ", " + Bits + "-bit, " + Seconds.ToString("0.00") + " s";
        }
    }

    /// <summary>
    /// Reads WAV and AIFF/AIFC natively - PCM at 8, 16, 24 or 32 bits and IEEE float at
    /// 32 or 64, any channel count or rate - and falls back to ffmpeg, if one happens to
    /// be on PATH, for anything else. Also holds the rate conversion and the 12-bit
    /// quantiser used to turn audio into something the S950 can hold.
    /// </summary>
    internal static class AudioImport
    {
        public static AudioClip Load(string path)
        {
            var b = File.ReadAllBytes(path);
            if (b.Length < 12) throw new InvalidDataException("The file is too short to be audio.");

            string magic = Ascii(b, 0, 4);
            if (magic == "RIFF" && Ascii(b, 8, 4) == "WAVE") return LoadWav(b);
            if (magic == "FORM" && (Ascii(b, 8, 4) == "AIFF" || Ascii(b, 8, 4) == "AIFC")) return LoadAiff(b);

            var viaTool = LoadViaFfmpeg(path);
            if (viaTool != null) return viaTool;

            throw new InvalidDataException(
                "Not a WAV or AIFF file. Those two are read directly; other formats " +
                "(MP3, FLAC, Ogg) need ffmpeg on the PATH, which was not found.");
        }

        static string Ascii(byte[] b, int o, int n)
        {
            return o + n <= b.Length ? Encoding.ASCII.GetString(b, o, n) : "";
        }

        // ------------------------------------------------------------------ WAV

        static AudioClip LoadWav(byte[] b)
        {
            int format = 0, channels = 0, rate = 0, bits = 0;
            int dataAt = -1, dataLen = 0;

            int p = 12;
            while (p + 8 <= b.Length)
            {
                string id = Ascii(b, p, 4);
                long size = U32(b, p + 4);
                int body = p + 8;
                if (size < 0 || body + size > b.Length) size = b.Length - body;   // truncated file

                if (id == "fmt " && size >= 16)
                {
                    format = U16(b, body);
                    channels = U16(b, body + 2);
                    rate = (int)U32(b, body + 4);
                    bits = U16(b, body + 14);

                    // WAVE_FORMAT_EXTENSIBLE hides the real format in the sub-format GUID.
                    if (format == 0xFFFE && size >= 40) format = U16(b, body + 24);
                }
                else if (id == "data")
                {
                    dataAt = body;
                    dataLen = (int)size;
                }

                p = body + (int)size + ((size & 1) != 0 ? 1 : 0);     // chunks pad to even
            }

            if (dataAt < 0) throw new InvalidDataException("The WAV file has no data chunk.");
            if (channels <= 0 || rate <= 0) throw new InvalidDataException("The WAV file has no usable format chunk.");
            if (format != 1 && format != 3)
                throw new InvalidDataException("Only uncompressed PCM and IEEE-float WAV files can be read " +
                                               "(this one is format " + format + ").");

            var mono = ReadFrames(b, dataAt, dataLen, channels, bits, format == 3, false);
            return new AudioClip
            {
                Mono = mono,
                SampleRate = rate,
                Channels = channels,
                Bits = bits,
                Format = "WAV " + (format == 3 ? "float" : "PCM")
            };
        }

        // ----------------------------------------------------------------- AIFF

        static AudioClip LoadAiff(byte[] b)
        {
            int channels = 0, bits = 0;
            double rate = 0;
            string compression = "NONE";
            int dataAt = -1, dataLen = 0;

            int p = 12;
            while (p + 8 <= b.Length)
            {
                string id = Ascii(b, p, 4);
                long size = U32BE(b, p + 4);
                int body = p + 8;
                if (size < 0 || body + size > b.Length) size = b.Length - body;

                if (id == "COMM" && size >= 18)
                {
                    channels = U16BE(b, body);
                    bits = U16BE(b, body + 6);
                    rate = Extended80(b, body + 8);
                    if (size >= 22) compression = Ascii(b, body + 18, 4);
                }
                else if (id == "SSND" && size >= 8)
                {
                    int offset = (int)U32BE(b, body);
                    dataAt = body + 8 + offset;
                    dataLen = (int)size - 8 - offset;
                }

                p = body + (int)size + ((size & 1) != 0 ? 1 : 0);
            }

            if (dataAt < 0) throw new InvalidDataException("The AIFF file has no sound data chunk.");
            if (channels <= 0 || rate <= 0) throw new InvalidDataException("The AIFF file has no usable COMM chunk.");

            bool isFloat = compression == "fl32" || compression == "FL32" ||
                           compression == "fl64" || compression == "FL64";
            bool littleEndian = compression == "sowt";          // AIFC's byte-swapped PCM
            if (compression != "NONE" && !isFloat && !littleEndian)
                throw new InvalidDataException("Compressed AIFF ('" + compression + "') cannot be read.");

            var mono = ReadFrames(b, dataAt, dataLen, channels, bits, isFloat, !littleEndian);
            return new AudioClip
            {
                Mono = mono,
                SampleRate = (int)Math.Round(rate),
                Channels = channels,
                Bits = bits,
                Format = "AIFF " + (isFloat ? "float" : "PCM")
            };
        }

        /// <summary>80-bit IEEE extended, as AIFF stores its sample rate.</summary>
        static double Extended80(byte[] b, int o)
        {
            int expon = ((b[o] & 0x7F) << 8) | b[o + 1];
            double hi = U32BE(b, o + 2), lo = U32BE(b, o + 6);

            double f;
            if (expon == 0 && hi == 0 && lo == 0) f = 0;
            else
            {
                expon -= 16383;
                f = hi * Math.Pow(2, expon - 31) + lo * Math.Pow(2, expon - 63);
            }
            return (b[o] & 0x80) != 0 ? -f : f;
        }

        // ------------------------------------------------------- sample unpacking

        /// <summary>Reads interleaved frames of any supported width and mixes to mono.</summary>
        static float[] ReadFrames(byte[] b, int at, int len, int channels, int bits,
                                  bool isFloat, bool bigEndian)
        {
            int bytes = Math.Max(1, bits / 8);
            int frame = bytes * channels;
            if (frame <= 0) return new float[0];

            int frames = Math.Max(0, Math.Min(len, b.Length - at) / frame);
            var mono = new float[frames];

            for (int i = 0; i < frames; i++)
            {
                double sum = 0;
                int o = at + i * frame;
                for (int c = 0; c < channels; c++)
                    sum += OneSample(b, o + c * bytes, bits, isFloat, bigEndian);
                mono[i] = (float)(sum / channels);
            }
            return mono;
        }

        static double OneSample(byte[] b, int o, int bits, bool isFloat, bool bigEndian)
        {
            if (isFloat)
            {
                if (bits == 64) return BitConverter.ToDouble(Ordered(b, o, 8, bigEndian), 0);
                return BitConverter.ToSingle(Ordered(b, o, 4, bigEndian), 0);
            }

            switch (bits)
            {
                case 8:
                    // WAV stores 8-bit unsigned; AIFF stores it signed.
                    return bigEndian ? (sbyte)b[o] / 128.0 : (b[o] - 128) / 128.0;

                case 16:
                    {
                        int v = bigEndian ? (b[o] << 8) | b[o + 1] : (b[o + 1] << 8) | b[o];
                        return (short)v / 32768.0;
                    }

                case 24:
                    {
                        int v = bigEndian
                            ? (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]
                            : (b[o + 2] << 16) | (b[o + 1] << 8) | b[o];
                        if (v >= 0x800000) v -= 0x1000000;
                        return v / 8388608.0;
                    }

                case 32:
                    {
                        int v = bigEndian
                            ? (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]
                            : (b[o + 3] << 24) | (b[o + 2] << 16) | (b[o + 1] << 8) | b[o];
                        return v / 2147483648.0;
                    }

                default:
                    throw new InvalidDataException(bits + "-bit audio is not supported.");
            }
        }

        static byte[] Ordered(byte[] b, int o, int n, bool bigEndian)
        {
            var t = new byte[n];
            Array.Copy(b, o, t, 0, n);
            if (bigEndian) Array.Reverse(t);
            return t;
        }

        static int U16(byte[] b, int o) { return b[o] | (b[o + 1] << 8); }
        static long U32(byte[] b, int o)
        {
            return (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
        }
        static int U16BE(byte[] b, int o) { return (b[o] << 8) | b[o + 1]; }
        static long U32BE(byte[] b, int o)
        {
            return (uint)((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]);
        }

        // --------------------------------------------------------------- ffmpeg

        /// <summary>
        /// Decodes through ffmpeg when one is installed, which covers MP3, FLAC, Ogg and
        /// the rest. Returns null when there is no ffmpeg to ask.
        /// </summary>
        static AudioClip LoadViaFfmpeg(string path)
        {
            string exe = OnPath("ffmpeg.exe");
            if (exe == null) return null;

            string temp = Path.Combine(Path.GetTempPath(), "akai-import-" + Guid.NewGuid().ToString("N") + ".wav");
            try
            {
                var psi = new ProcessStartInfo(exe,
                    "-v error -y -i \"" + path + "\" -vn -acodec pcm_s24le \"" + temp + "\"")
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardError = true
                };

                using (var p = Process.Start(psi))
                {
                    string err = p.StandardError.ReadToEnd();
                    p.WaitForExit();
                    if (p.ExitCode != 0 || !File.Exists(temp))
                        throw new InvalidDataException("ffmpeg could not decode this file. " + OneLine(err));
                }

                var clip = LoadWav(File.ReadAllBytes(temp));
                clip.Format = "decoded by ffmpeg";
                return clip;
            }
            finally
            {
                try { if (File.Exists(temp)) File.Delete(temp); }
                catch (IOException) { /* it is only a temp file */ }
            }
        }

        /// <summary>
        /// ffmpeg reports a failure over several lines, repeating the full path and its own
        /// internal context. None of that belongs in a one-line message, so the noise is
        /// dropped and what is left is collapsed onto a single line and cut short.
        /// </summary>
        static string OneLine(string text)
        {
            if (string.IsNullOrEmpty(text)) return "";

            var lines = text.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                            .Select(l => StripContext(l.Trim()))
                            .Where(l => l.Length > 0)
                            .ToList();
            if (lines.Count == 0) return "";

            // The line that only echoes the path back says nothing the caller does not
            // already know. Everything else is worth keeping, so prefer the last of it.
            var useful = lines.Where(l => !IsPathEcho(l)).ToList();
            string s = (useful.Count > 0 ? useful : lines)[(useful.Count > 0 ? useful : lines).Count - 1];

            s = string.Join(" ", s.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries));
            return s.Length > 160 ? s.Substring(0, 157) + "..." : s;
        }

        /// <summary>Drops ffmpeg's leading "[component @ address]" tag from a line.</summary>
        static string StripContext(string line)
        {
            if (!line.StartsWith("[")) return line;
            int close = line.IndexOf(']');
            return close < 0 ? line : line.Substring(close + 1).Trim();
        }

        /// <summary>
        /// True for the line that just repeats the filename - "Error opening input file
        /// C:\...\thing.wav." - which carries no information the caller lacks. Note that
        /// "Error opening input files:" is a different, useful line, so the test is on the
        /// path rather than on the prefix alone.
        /// </summary>
        static bool IsPathEcho(string line)
        {
            return line.StartsWith("Error opening input file ", StringComparison.Ordinal) &&
                   (line.IndexOf(Path.DirectorySeparatorChar) >= 0 || line.IndexOf('/') >= 0);
        }

        /// <summary>
        /// Whether an ffmpeg can be found, which decides whether anything beyond WAV and
        /// AIFF can be read. Looked up each time rather than cached, since the answer
        /// changes the moment one is installed - though not for a process already running,
        /// which inherited its PATH at launch.
        /// </summary>
        public static bool FfmpegAvailable { get { return OnPath("ffmpeg.exe") != null; } }

        /// <summary>
        /// The extensions worth showing in a file list. The first four are read directly;
        /// the rest need ffmpeg, and are offered anyway so the reason one will not open is
        /// a message rather than a file that simply never appears.
        /// </summary>
        public static readonly string[] AudioExtensions =
        {
            ".wav", ".aif", ".aiff", ".aifc",
            ".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".aac",
            ".wma", ".alac", ".ape", ".wv", ".mp4", ".caf", ".au", ".snd"
        };

        /// <summary>True if the name ends in something worth trying to open.</summary>
        public static bool LooksLikeAudio(string path)
        {
            string ext = (Path.GetExtension(path) ?? "").ToLowerInvariant();
            foreach (var e in AudioExtensions) if (e == ext) return true;
            return false;
        }

        /// <summary>True for the two formats read without help.</summary>
        public static bool ReadNatively(string path)
        {
            string ext = (Path.GetExtension(path) ?? "").ToLowerInvariant();
            return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".aifc";
        }

        static string OnPath(string exe)
        {
            string paths = Environment.GetEnvironmentVariable("PATH") ?? "";
            foreach (var dir in paths.Split(';'))
            {
                if (dir.Length == 0) continue;
                try
                {
                    string full = Path.Combine(dir.Trim('"'), exe);
                    if (File.Exists(full)) return full;
                }
                catch (ArgumentException) { /* a malformed PATH entry */ }
            }
            return null;
        }

        // ----------------------------------------------------------- conversion

        /// <summary>
        /// Rate conversion by windowed-sinc interpolation. The kernel is widened when
        /// converting downwards so the low-pass comes with it and the result does not
        /// alias.
        /// </summary>
        public static float[] Resample(float[] src, int fromRate, int toRate)
        {
            if (src.Length == 0 || fromRate == toRate || fromRate <= 0 || toRate <= 0) return src;

            double ratio = (double)toRate / fromRate;
            double cutoff = Math.Min(1.0, ratio);
            const int Lobes = 12;
            int half = (int)Math.Ceiling(Lobes / cutoff);

            int outLen = Math.Max(1, (int)(src.Length * ratio));
            var dst = new float[outLen];

            for (int i = 0; i < outLen; i++)
            {
                double centre = i / ratio;
                int c = (int)Math.Floor(centre);
                double sum = 0, weight = 0;

                for (int k = c - half; k <= c + half; k++)
                {
                    if (k < 0 || k >= src.Length) continue;
                    double t = k - centre;
                    double w = Sinc(t * cutoff) * Blackman(t / half);
                    sum += src[k] * w;
                    weight += w;
                }
                dst[i] = (float)(weight != 0 ? sum / weight : 0);
            }
            return dst;
        }

        static double Sinc(double x)
        {
            if (Math.Abs(x) < 1e-9) return 1.0;
            double px = Math.PI * x;
            return Math.Sin(px) / px;
        }

        static double Blackman(double t)
        {
            if (t <= -1 || t >= 1) return 0;
            return 0.42 + 0.5 * Math.Cos(Math.PI * t) + 0.08 * Math.Cos(2 * Math.PI * t);
        }

        /// <summary>Peak level of a clip, 0..1 or beyond if the source clipped.</summary>
        public static float Peak(float[] mono)
        {
            float peak = 0;
            for (int i = 0; i < mono.Length; i++)
            {
                float a = Math.Abs(mono[i]);
                if (a > peak) peak = a;
            }
            return peak;
        }

        /// <summary>
        /// Quantises to signed 12 bits. Normalising is usually worth it: 12 bits leaves
        /// little room to waste on headroom.
        /// </summary>
        public static short[] To12Bit(float[] mono, bool normalise)
        {
            float peak = Peak(mono);
            double gain = (normalise && peak > 1e-7f) ? 1.0 / peak : 1.0;

            var outp = new short[mono.Length];
            for (int i = 0; i < mono.Length; i++)
            {
                int v = (int)Math.Round(mono[i] * gain * 2047.0);
                if (v > 2047) v = 2047;
                if (v < -2048) v = -2048;
                outp[i] = (short)v;
            }
            return outp;
        }

        /// <summary>
        /// Stretches or compresses in time without moving the pitch, by overlap-adding
        /// windowed frames at a different spacing than they were taken. Each frame is
        /// nudged within a search window to the offset that best continues the waveform
        /// already written, which is what stops the joins phasing (WSOLA).
        /// <paramref name="ratio"/> is output length over input length.
        /// </summary>
        public static short[] TimeStretch(short[] words12, double ratio)
        {
            if (words12 == null || words12.Length == 0) return new short[0];
            if (Math.Abs(ratio - 1) < 1e-9) return (short[])words12.Clone();

            var src = new float[words12.Length];
            for (int i = 0; i < src.Length; i++) src[i] = words12[i] / 2048f;

            const int Frame = 1024;
            const int Seek = 256;               // how far either side to hunt for a join
            int hopOut = Frame / 2;             // 50% overlap: Hann windows sum to unity
            double hopIn = hopOut / ratio;

            int outLen = Math.Max(2, (int)(src.Length * ratio));
            var acc = new float[outLen + Frame];
            var win = Hann(Frame);

            double inPos = 0;
            int follow = 0;                     // where the last frame would have carried on

            for (int outPos = 0; outPos + Frame <= acc.Length; outPos += hopOut)
            {
                int at = (int)Math.Round(inPos);
                if (outPos > 0) at = BestJoin(src, follow, at, Seek, hopOut);

                if (at < 0) at = 0;
                if (at + Frame > src.Length) break;

                for (int i = 0; i < Frame; i++) acc[outPos + i] += src[at + i] * win[i];

                follow = at + hopOut;
                inPos += hopIn;
            }

            var outp = new short[outLen & ~1];
            for (int i = 0; i < outp.Length; i++)
            {
                int v = (int)Math.Round(acc[i] * 2048f);
                if (v > 2047) v = 2047;
                if (v < -2048) v = -2048;
                outp[i] = (short)v;
            }
            return outp;
        }

        /// <summary>
        /// The offset near <paramref name="centre"/> whose waveform best matches what the
        /// previous frame was about to do. Correlation is sampled rather than summed over
        /// every point - the peak is broad, and this runs inside a dialog.
        /// </summary>
        static int BestJoin(float[] src, int follow, int centre, int seek, int len)
        {
            if (follow + len > src.Length) return centre;

            int best = centre;
            double bestScore = double.NegativeInfinity;

            for (int d = -seek; d <= seek; d += 2)
            {
                int c = centre + d;
                if (c < 0 || c + len > src.Length) continue;

                double sum = 0;
                for (int i = 0; i < len; i += 4) sum += src[follow + i] * src[c + i];
                if (sum > bestScore) { bestScore = sum; best = c; }
            }
            return best;
        }

        static float[] Hann(int n)
        {
            var w = new float[n];
            for (int i = 0; i < n; i++)
                w[i] = (float)(0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (n - 1)));
            return w;
        }

        /// <summary>
        /// Halves a stored sample's rate: half the words for half the disk space, with
        /// the sinc kernel's low-pass keeping the discarded top octave from folding back
        /// as aliasing. Pitch and duration are unchanged.
        /// </summary>
        public static short[] HalveRate(short[] words12)
        {
            var f = new float[words12.Length];
            for (int i = 0; i < f.Length; i++) f[i] = words12[i] / 2048f;

            var half = Resample(f, 2, 1);

            var outp = new short[half.Length & ~1];
            for (int i = 0; i < outp.Length; i++)
            {
                int v = (int)Math.Round(half[i] * 2048f);
                if (v > 2047) v = 2047;
                if (v < -2048) v = -2048;
                outp[i] = (short)v;
            }
            return outp;
        }

        /// <summary>
        /// Where a complex sample should be cut into one-shots.
        ///
        /// A peak envelope on a short hop, tracked by a decaying peak-follower. A slice starts
        /// where the envelope jumps above the follower by more than the rise ratio, is loud
        /// enough in absolute terms, and is far enough from the previous slice. No FFT: on a
        /// mono 12-bit break, an energy rise is what the ear is calling a hit anyway.
        ///
        /// <paramref name="sensitivity"/> is 0..100 and moves two things at once - how big a
        /// jump has to be, and how quiet a hit may be - because separating them gives two dials
        /// that only make sense together. 50 lands on the eighths and sixteenths of a breakbeat.
        ///
        /// Returns the start of each slice, always beginning at 0 and always even, since a
        /// sample's word count must be.
        /// </summary>
        public static List<int> DetectSlices(short[] words, int rate, int sensitivity, int minSliceMs)
        {
            var outp = new List<int>();
            if (words == null || words.Length < 2) { outp.Add(0); return outp; }

            int sens = sensitivity < 0 ? 0 : sensitivity > 100 ? 100 : sensitivity;
            if (minSliceMs < 5) minSliceMs = 5;
            if (minSliceMs > 1000) minSliceMs = 1000;
            if (rate < 1000) rate = 40000;

            int hop = Math.Max(8, (int)Math.Round(rate * 0.004));          // ~4 ms
            int minGap = Math.Max(hop, (int)Math.Round(rate * minSliceMs / 1000.0));
            double rise = 3.0 - 2.3 * (sens / 100.0);                      // 3.0x at 0, 0.7x at 100
            double floorFrac = 0.16 - 0.14 * (sens / 100.0);               // of the overall peak

            int n = words.Length / hop;
            if (n < 2) { outp.Add(0); return outp; }

            var env = new double[n];
            double peak = 0;
            for (int i = 0; i < n; i++)
            {
                int m = 0;
                for (int j = i * hop; j < (i + 1) * hop; j++)
                {
                    int a = words[j] < 0 ? -words[j] : words[j];
                    if (a > m) m = a;
                }
                env[i] = m;
                if (m > peak) peak = m;
            }

            double decay = Math.Exp(-hop / (rate * 0.090));                // ~90 ms follower
            double floorAbs = peak * floorFrac;
            double follow = 0;
            int last = -minGap;

            for (int i = 0; i < n; i++)
            {
                int at = i * hop;
                if (env[i] > floorAbs && env[i] > follow * rise && at - last >= minGap)
                {
                    // walk back to where the hit actually starts rising, so the transient is kept
                    int st = at;
                    while (st > 0 && st > at - hop * 3 &&
                           (words[st - 1] < 0 ? -words[st - 1] : words[st - 1]) < env[i] * 0.25) st--;
                    outp.Add(st & ~1);
                    last = at;
                }
                follow = Math.Max(env[i], follow * decay);
            }

            if (outp.Count == 0 || outp[0] > minGap) outp.Insert(0, 0); else outp[0] = 0;
            return outp;
        }
        /// <summary>
        /// Sample rates to offer on import. These are the values the 101-image corpus
        /// actually contains, dropping the one-off oddities that look like varispeed
        /// rather than a chosen setting. The library runs from 11,773 Hz to 44,329 Hz;
        /// 12,500 Hz is the lowest of the regular 1,250 Hz grid values present.
        ///
        /// Plus 48,000 Hz, which the library never uses but the S950 samples at: it is the
        /// machine's top rate (the S900's was 40 kHz). Leaving it off made every 48 kHz file
        /// come down to 44.1 on import for no reason.
        /// </summary>
        public static readonly int[] KnownRates =
        {
            12500, 17500, 20000, 22050, 22500, 23750, 25000, 27500, 30000,
            32500, 33750, 35000, 36250, 37500, 38750, 40000, 44100, 48000
        };

        /// <summary>The fastest rate the S950 samples at, and so the most a sample needs.</summary>
        public const int MaxRate = 48000;
    }
}
