using System;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;
using AkaiS950List;

namespace AkaiS950Studio
{
    /// <summary>
    /// Turns an audio file into something the S950 can hold: mono, 12-bit, at a rate
    /// the sampler uses, and small enough for the free space on the target disk. The
    /// disk's capacity is enforced here rather than left to fail at write time.
    /// </summary>
    internal sealed class ImportDialog : Form
    {
        readonly AudioClip _clip;
        readonly AkaiDisk _disk;
        readonly SamplePlayer _audio = new SamplePlayer();

        readonly TextBox _name = new TextBox();
        readonly ComboBox _rate = new ComboBox();
        readonly ComboBox _loop = new ComboBox();
        readonly NumericUpDown _pitch = new NumericUpDown();
        readonly NumericUpDown _trim = new NumericUpDown();
        readonly CheckBox _normalise = new CheckBox();
        readonly Label _source = new Label();
        readonly Label _summary = new Label();
        readonly Button _fit = new Button();
        readonly Button _preview = new Button();
        readonly Button _ok = new Button();

        readonly int _freeBlocks;
        short[] _converted;                 // cached for the rate and trim it was made at
        int _convertedRate, _convertedTrim;
        bool _convertedNormalised;

        /// <summary>The 12-bit sample the dialog produced, once it closes with OK.</summary>
        public short[] Words12 { get { return _converted; } }
        public int TargetRate { get { return SelectedRate; } }
        public string SampleName { get { return _name.Text.Trim(); } }
        public int NominalPitch { get { return (int)_pitch.Value; } }
        public char LoopMode { get { return "OLA"[Math.Max(0, _loop.SelectedIndex)]; } }

        public ImportDialog(AudioClip clip, AkaiDisk disk, string suggestedName)
        {
            _clip = clip;
            _disk = disk;
            _freeBlocks = disk.FreeBlocks;

            Text = "Add Sample to Disk";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(520, 372);
            Font = SystemFonts.MessageBoxFont;

            Build(suggestedName);
            Recalculate();
        }

        void Build(string suggestedName)
        {
            int y = 12;

            _source.SetBounds(12, y, 496, 34);
            _source.Text = "Source:  " + _clip.Describe() +
                           (_clip.Channels > 1 ? "\r\nStereo will be mixed down to mono." : "");
            Controls.Add(_source);
            y += 42;

            Label(y + 3, "Sample &name");
            _name.SetBounds(150, y, 150, 24);
            _name.MaxLength = 10;
            _name.CharacterCasing = CharacterCasing.Upper;
            _name.Text = AkaiDisk.NormaliseName(suggestedName);
            _name.TextChanged += (s, e) => Recalculate();
            Controls.Add(_name);
            Controls.Add(new Label
            {
                Bounds = new Rectangle(306, y + 3, 202, 20),
                Text = "10 characters, upper case",
                ForeColor = SystemColors.GrayText
            });
            y += 32;

            Label(y + 3, "Sample &rate");
            _rate.SetBounds(150, y, 150, 24);
            _rate.DropDownStyle = ComboBoxStyle.DropDownList;
            _rate.Items.Add("Keep " + _clip.SampleRate.ToString("N0") + " Hz");
            foreach (int r in AudioImport.KnownRates) _rate.Items.Add(r.ToString("N0") + " Hz");
            // The S950 samples at up to 48 kHz, so only something faster has to come down.
            _rate.SelectedIndex = _clip.SampleRate <= AudioImport.MaxRate
                ? 0
                : 1 + Array.IndexOf(AudioImport.KnownRates, AudioImport.MaxRate);
            _rate.SelectedIndexChanged += (s, e) => Recalculate();
            Controls.Add(_rate);
            y += 32;

            Label(y + 3, "Nominal &pitch");
            _pitch.SetBounds(150, y, 70, 24);
            _pitch.Minimum = 0;
            _pitch.Maximum = 127;
            _pitch.Value = 60;
            Controls.Add(_pitch);
            Controls.Add(new Label
            {
                Bounds = new Rectangle(226, y + 3, 282, 20),
                Text = "MIDI note the sample sounds at (C3 = 60)",
                ForeColor = SystemColors.GrayText
            });
            y += 32;

            Label(y + 3, "&Loop mode");
            _loop.SetBounds(150, y, 150, 24);
            _loop.DropDownStyle = ComboBoxStyle.DropDownList;
            _loop.Items.AddRange(new object[] { "One-shot", "Looping", "Alternating" });
            _loop.SelectedIndex = 0;
            Controls.Add(_loop);
            y += 32;

            Label(y + 3, "Length to &use");
            _trim.SetBounds(150, y, 90, 24);
            _trim.DecimalPlaces = 2;
            _trim.Increment = 0.1m;
            _trim.Minimum = 0.01m;
            _trim.Maximum = (decimal)Math.Max(0.01, _clip.Seconds);
            _trim.Value = _trim.Maximum;
            _trim.ValueChanged += (s, e) => Recalculate();
            Controls.Add(_trim);
            Controls.Add(new Label
            {
                Bounds = new Rectangle(246, y + 3, 60, 20),
                Text = "seconds",
                ForeColor = SystemColors.GrayText
            });

            _fit.SetBounds(320, y - 1, 120, 26);
            _fit.Text = "Trim to &Fit";
            _fit.Click += (s, e) => TrimToFit();
            Controls.Add(_fit);
            y += 32;

            _normalise.SetBounds(150, y, 300, 24);
            _normalise.Text = "Normalise to full scale";
            _normalise.Checked = true;
            _normalise.CheckedChanged += (s, e) => Recalculate();
            Controls.Add(_normalise);
            y += 34;

            _summary.SetBounds(12, y, 496, 54);
            _summary.BorderStyle = BorderStyle.FixedSingle;
            _summary.Padding = new Padding(6, 5, 6, 5);
            Controls.Add(_summary);
            y += 62;

            _preview.SetBounds(12, y, 110, 28);
            _preview.Text = "&Preview";
            _preview.Click += (s, e) => Preview();
            Controls.Add(_preview);

            _ok.SetBounds(300, y, 100, 28);
            _ok.Text = "Add";
            _ok.DialogResult = DialogResult.OK;
            _ok.Click += (s, e) => Convert();
            Controls.Add(_ok);

            var cancel = new Button { Bounds = new Rectangle(408, y, 100, 28), Text = "Cancel" };
            cancel.DialogResult = DialogResult.Cancel;
            Controls.Add(cancel);

            AcceptButton = _ok;
            CancelButton = cancel;
        }

        void Label(int y, string text)
        {
            Controls.Add(new Label { Bounds = new Rectangle(12, y, 134, 20), Text = text });
        }

        // ------------------------------------------------------------ arithmetic

        int SelectedRate
        {
            get
            {
                int i = _rate.SelectedIndex;
                return i <= 0 ? _clip.SampleRate : AudioImport.KnownRates[i - 1];
            }
        }

        /// <summary>Source samples kept, after the length limit.</summary>
        int TrimmedSourceLength
        {
            get
            {
                long n = (long)Math.Round((double)_trim.Value * _clip.SampleRate);
                return (int)Math.Max(0, Math.Min(_clip.Mono.Length, n));
            }
        }

        /// <summary>Words the import will produce - the same arithmetic the resampler uses.</summary>
        int WordsFor(int sourceLength)
        {
            int rate = SelectedRate;
            long n = rate == _clip.SampleRate
                ? sourceLength
                : (long)(sourceLength * ((double)rate / _clip.SampleRate));
            return (int)Math.Max(0, n) & ~1;
        }

        static int FileLength(int words) { return AkaiDisk.HeaderSize + words * 3 / 2; }

        /// <summary>The longest sample the free blocks can hold.</summary>
        int MaxWordsThatFit
        {
            get
            {
                long bytes = (long)_freeBlocks * AkaiDisk.BlockSize - AkaiDisk.HeaderSize;
                return (int)Math.Max(0, bytes * 2 / 3) & ~1;
            }
        }

        void TrimToFit()
        {
            // Resampling preserves duration, so the limit in output words converts
            // straight to a length in seconds. Shave a hundredth off to stay inside it.
            double seconds = MaxWordsThatFit / (double)Math.Max(1, SelectedRate) - 0.01;
            decimal v = (decimal)Math.Min(_clip.Seconds, Math.Max(0.01, seconds));

            _trim.Value = Math.Max(_trim.Minimum, Math.Min(_trim.Maximum, v));
            Recalculate();
        }

        void Recalculate()
        {
            int words = WordsFor(TrimmedSourceLength);
            int length = FileLength(words);
            int blocks = AkaiDisk.BlocksFor(length);
            bool fits = blocks <= _freeBlocks && words >= 2;
            bool nameOk = _name.Text.Trim().Length > 0 && !NameTaken;

            var text = new System.Text.StringBuilder();
            text.Append(words.ToString("N0")).Append(" words at ")
                .Append(SelectedRate.ToString("N0")).Append(" Hz  =  ")
                .Append((words / (double)Math.Max(1, SelectedRate)).ToString("0.00", CultureInfo.InvariantCulture))
                .Append(" s,  ").Append(length.ToString("N0")).Append(" bytes");
            text.AppendLine();
            text.Append(blocks.ToString("N0")).Append(" block").Append(blocks == 1 ? "" : "s")
                .Append(" needed, ").Append(_freeBlocks.ToString("N0")).Append(" free on this disk");

            if (!fits)
            {
                text.AppendLine();
                text.Append(words < 2
                    ? "Nothing left to add - raise the length."
                    : "Too big for this disk. Shorten it, lower the rate, or use Trim to Fit.");
            }
            else if (!nameOk)
            {
                text.AppendLine();
                text.Append(NameTaken ? "That name is already used on this disk." : "Give the sample a name.");
            }

            _summary.Text = text.ToString();
            _summary.ForeColor = (fits && nameOk) ? SystemColors.ControlText : Color.FromArgb(168, 32, 32);
            _ok.Enabled = fits && nameOk;
            _preview.Enabled = words >= 2;
            _fit.Enabled = !fits;
        }

        bool NameTaken
        {
            get
            {
                // Only other samples count: a program may share the name, and on these
                // disks one usually does.
                string want = AkaiDisk.NormaliseName(_name.Text);
                foreach (var e in _disk.Entries)
                    if (e.Type == 'S' && string.Equals(e.Name, want, StringComparison.OrdinalIgnoreCase))
                        return true;
                return false;
            }
        }

        // ------------------------------------------------------------ conversion

        /// <summary>Resamples and quantises, reusing the last result when nothing changed.</summary>
        void Convert()
        {
            int rate = SelectedRate, trimmed = TrimmedSourceLength;
            if (_converted != null && _convertedRate == rate && _convertedTrim == trimmed
                && _convertedNormalised == _normalise.Checked) return;

            Cursor = Cursors.WaitCursor;
            try
            {
                var src = _clip.Mono;
                if (trimmed < src.Length)
                {
                    var cut = new float[trimmed];
                    Array.Copy(src, cut, trimmed);
                    src = cut;
                }

                var at = AudioImport.Resample(src, _clip.SampleRate, rate);
                var words = AudioImport.To12Bit(at, _normalise.Checked);

                int n = Math.Min(words.Length, WordsFor(trimmed));
                if (n < words.Length) Array.Resize(ref words, n);

                _converted = words;
                _convertedRate = rate;
                _convertedTrim = trimmed;
                _convertedNormalised = _normalise.Checked;
            }
            finally { Cursor = Cursors.Default; }
        }

        void Preview()
        {
            try
            {
                Convert();
                var pcm = new short[_converted.Length];
                for (int i = 0; i < pcm.Length; i++) pcm[i] = (short)(_converted[i] * 16);
                _audio.Play(pcm, SelectedRate);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Preview failed",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _audio.Dispose();
            base.OnFormClosed(e);
        }
    }
}
