using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Design;
using System.Linq;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;
using AkaiS950List;

namespace AkaiS950Studio
{
    /// <summary>
    /// Property-grid adapters. Every getter reads the live disk image and every
    /// setter writes straight back into it, so edits are immediate and there is no
    /// separate model to keep in step. Offsets are those of S950-Disk-Format.pdf.
    /// </summary>
    internal abstract class EditorBase
    {
        protected readonly AkaiDisk Disk;
        protected readonly AkaiEntry Entry;
        protected readonly int Base;

        /// <summary>
        /// Other records this edit should reach - the rest of a multiple selection, as
        /// byte offsets of their own base. Every write in the grid goes through Set, so
        /// naming them here is all it takes for a value typed once to land in all of them.
        /// </summary>
        protected int[] Also = new int[0];

        protected EditorBase(AkaiDisk d, AkaiEntry e, int baseOffset)
        {
            Disk = d; Entry = e; Base = baseOffset;
        }

        protected byte[] Bytes { get { return Disk.ReadFile(Entry); } }

        protected byte B(int o) { return Bytes[Base + o]; }
        protected int SB(int o) { return (sbyte)Bytes[Base + o]; }
        protected int U16(int o) { var b = Bytes; return b[Base + o] | (b[Base + o + 1] << 8); }
        protected int S16(int o) { return (short)U16(o); }

        protected void Set(int o, int v)
        {
            Disk.PokeFile(Entry, Base + o, (byte)v);
            for (int i = 0; i < Also.Length; i++) Disk.PokeFile(Entry, Also[i] + o, (byte)v);
        }

        /// <summary>
        /// One flag, left to each record's own byte. Writing the whole byte instead would
        /// carry the first keygroup's other flags onto the rest of the selection.
        /// </summary>
        protected void SetFlag(int o, int mask, bool on)
        {
            SetFlagAt(Base + o, mask, on);
            for (int i = 0; i < Also.Length; i++) SetFlagAt(Also[i] + o, mask, on);
        }

        void SetFlagAt(int at, int mask, bool on)
        {
            byte cur = Disk.ReadFile(Entry)[at];
            Disk.PokeFile(Entry, at, (byte)(on ? (cur | mask) : (cur & ~mask)));
        }
        protected void SetU16(int o, int v) { Set(o, v & 0xFF); Set(o + 1, (v >> 8) & 0xFF); }

        protected static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

        /// <summary>
        /// A filename lives in the directory as well as in the file, so renaming goes
        /// through the disk rather than poking the header. A name the disk will not
        /// accept leaves everything as it was.
        /// </summary>
        protected void Rename(string value)
        {
            try { Disk.RenameFile(Entry, value); }
            catch (InvalidOperationException) { /* duplicate - the grid keeps the old name */ }
        }

        protected string Str(int o, int len)
        {
            var b = Bytes; var c = new char[len];
            for (int i = 0; i < len; i++) c[i] = (char)b[Base + o + i];
            return new string(c).TrimEnd();
        }

        /// <summary>Sample names on this disk, for the zone drop-downs.</summary>
        internal string[] SampleNames
        {
            get
            {
                var names = Disk.Entries
                    .Where(x => x.Type == 'S')
                    .Select(x => x.Name)
                    .Where(n => !string.IsNullOrEmpty(n))
                    .Distinct(StringComparer.OrdinalIgnoreCase)
                    .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                    .ToList();
                names.Insert(0, UnusedZone);
                return names.ToArray();
            }
        }

        /// <summary>The placeholder the S950 writes into a second zone that is not in use.</summary>
        internal const string UnusedZone = "2 SAMPLE";

        /// <summary>
        /// Point a zone at a sample: write the name, and bring the internal reference
        /// two words along with it. Leaving the old reference behind would have the
        /// keygroup name one sample while referring to another.
        /// </summary>
        protected void SetZoneSample(int nameOffset, int refOffset, string v)
        {
            SetStr(nameOffset, 10, v);
            int r = Disk.SampleReference((v ?? "").Trim().ToUpperInvariant());
            if (r != 0) { Set(refOffset, r & 0xFF); Set(refOffset + 1, (r >> 8) & 0xFF); }
        }

        protected void SetStr(int o, int len, string v)
        {
            if (v == null) v = "";
            if (v.Length > len) v = v.Substring(0, len);
            v = v.ToUpperInvariant().PadRight(len);
            for (int i = 0; i < len; i++) Set(o + i, (byte)v[i]);
        }
    }

    /// <summary>
    /// Drop-down of the samples on the disk being edited. Deliberately not exclusive:
    /// a program may legitimately name a sample that lives on another disk, and 164 of
    /// the 1908 keygroups in the reference corpus do exactly that.
    /// </summary>
    /// <summary>
    /// Draws a real checkbox in the property grid''s swatch. The grid has no native
    /// checkbox for bool - it offers a True/False drop-down - so the glyph is painted
    /// here. Double-clicking the row still toggles it, as it does for any bool.
    /// </summary>
    internal sealed class CheckBoxEditor : UITypeEditor
    {
        public override bool GetPaintValueSupported(ITypeDescriptorContext context) { return true; }

        public override UITypeEditorEditStyle GetEditStyle(ITypeDescriptorContext context)
        {
            return UITypeEditorEditStyle.None;
        }

        public override void PaintValue(PaintValueEventArgs e)
        {
            bool on = e.Value is bool && (bool)e.Value;
            var state = on ? CheckBoxState.CheckedNormal : CheckBoxState.UncheckedNormal;
            try
            {
                Size sz = CheckBoxRenderer.GetGlyphSize(e.Graphics, state);
                var at = new Point(e.Bounds.X + Math.Max(0, (e.Bounds.Width - sz.Width) / 2),
                                   e.Bounds.Y + Math.Max(0, (e.Bounds.Height - sz.Height) / 2));
                CheckBoxRenderer.DrawCheckBox(e.Graphics, at, state);
            }
            catch
            {
                // Visual styles disabled: fall back to a plain box and tick.
                var r = e.Bounds; r.Inflate(-2, -2);
                e.Graphics.DrawRectangle(Pens.Gray, r);
                if (on)
                {
                    e.Graphics.DrawLine(Pens.Black, r.Left + 2, r.Top + r.Height / 2, r.Left + r.Width / 2, r.Bottom - 2);
                    e.Graphics.DrawLine(Pens.Black, r.Left + r.Width / 2, r.Bottom - 2, r.Right - 2, r.Top + 1);
                }
            }
        }
    }

    /// <summary>Shows a boolean as On/Off rather than True/False.</summary>
    internal sealed class OnOffConverter : BooleanConverter
    {
        public override object ConvertTo(ITypeDescriptorContext context, System.Globalization.CultureInfo culture,
                                         object value, Type destinationType)
        {
            if (destinationType == typeof(string) && value is bool) return ((bool)value) ? "On" : "Off";
            return base.ConvertTo(context, culture, value, destinationType);
        }

        public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture,
                                           object value)
        {
            var s = value as string;
            if (s != null)
            {
                s = s.Trim();
                if (s.Equals("On", StringComparison.OrdinalIgnoreCase)) return true;
                if (s.Equals("Off", StringComparison.OrdinalIgnoreCase)) return false;
            }
            return base.ConvertFrom(context, culture, value);
        }
    }
    /// <summary>Re-labels a property''s category without touching anything else.</summary>
    internal sealed class RecategorisedProperty : PropertyDescriptor
    {
        readonly PropertyDescriptor _inner;
        readonly string _category;

        public RecategorisedProperty(PropertyDescriptor inner, string category)
            : base(inner) { _inner = inner; _category = category; }

        public override string Category { get { return _category; } }
        public override Type ComponentType { get { return _inner.ComponentType; } }
        public override Type PropertyType { get { return _inner.PropertyType; } }
        public override bool IsReadOnly { get { return _inner.IsReadOnly; } }
        public override TypeConverter Converter { get { return _inner.Converter; } }
        public override string Description { get { return _inner.Description; } }
        public override bool CanResetValue(object c) { return _inner.CanResetValue(c); }
        public override object GetValue(object c) { return _inner.GetValue(c); }
        public override void ResetValue(object c) { _inner.ResetValue(c); }
        public override void SetValue(object c, object v) { _inner.SetValue(c, v); }
        public override bool ShouldSerializeValue(object c) { return _inner.ShouldSerializeValue(c); }
    }
    internal sealed class SampleNameConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext context) { return true; }

        public override bool GetStandardValuesExclusive(ITypeDescriptorContext context) { return false; }

        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context)
        {
            var e = context != null ? context.Instance as EditorBase : null;
            return new StandardValuesCollection(e != null ? e.SampleNames : new string[0]);
        }
    }
    internal sealed class ProgramEditor : EditorBase
    {
        public ProgramEditor(AkaiDisk d, AkaiEntry e) : base(d, e, 0) { }

        [Category("Program"), Description("10 characters. Renames the directory entry too.")]
        public string Name { get { return Str(0, 10); } set { Rename(value); } }

        [Category("Program"), Description("MIDI program change number, as the panel shows it (from 1).")]
        public int MidiProgram
        {
            get { return B(26) + 1; }
            set { Set(26, Clamp(value, 1, 128) - 1); }
        }

        [Category("Program"), Description("Loudness change across the keyboard. Signed.")]
        public int KeyToLoudness
        {
            get { return S16(16); }
            set { SetU16(16, Clamp(value, -50, 50)); }
        }

        [Category("Program"), Description("Crossfade between overlapping keygroups.")]
        [Editor(typeof(CheckBoxEditor), typeof(UITypeEditor)), TypeConverter(typeof(OnOffConverter))]
        public bool PositionalCrossfade
        {
            get { return B(21) != 0; }
            set { Set(21, value ? 255 : 0); }
        }

        [Category("Read-only"), ReadOnly(true), Description("Derived from the file length.")]
        public int Keygroups { get { return B(23); } }

        [Category("Read-only"), ReadOnly(true), Description("Where the program loads in sampler RAM.")]
        public string LoadAddress { get { return "0x" + U16(18).ToString("X4"); } }

        [Category("Read-only"), ReadOnly(true)]
        public string WrittenBy { get { return B(22) == 0 ? "S950" : "S900"; } }

        public override string ToString() { return Name; }
    }

    internal sealed class KeygroupEditor : EditorBase, ICustomTypeDescriptor
    {
        readonly int _index;

        public KeygroupEditor(AkaiDisk d, AkaiEntry e, int index)
            : this(d, e, index, null) { }

        /// <summary>
        /// The keygroup the grid shows, and any others the same edit should reach. That is
        /// how setting one filter across a whole drum kit works: select the rows, type the
        /// value once, and every one of them takes it - under a single undo, since the
        /// baseline is the whole image either way.
        /// </summary>
        public KeygroupEditor(AkaiDisk d, AkaiEntry e, int index, IList<int> alsoIndices)
            : base(d, e, AkaiDisk.ProgHeaderSize + index * AkaiDisk.KeygroupSize)
        {
            _index = index;
            if (alsoIndices == null || alsoIndices.Count == 0) return;

            var bases = new int[alsoIndices.Count];
            for (int i = 0; i < alsoIndices.Count; i++)
                bases[i] = AkaiDisk.ProgHeaderSize + alsoIndices[i] * AkaiDisk.KeygroupSize;
            Also = bases;
        }

        /// <summary>
        /// How many keygroups a write from this editor reaches. For the tests and the
        /// status line, not for the grid - it is a fact about the selection rather than
        /// a property of the keygroup.
        /// </summary>
        [Browsable(false)]
        public int Reaches { get { return 1 + Also.Length; } }

        [Category("Key range"), Description("Highest MIDI note, C3 = 60.")]
        public int HighKey { get { return B(0); } set { Set(0, Clamp(value, 0, 127)); } }

        [Category("Key range"), Description("Lowest MIDI note, C3 = 60.")]
        public int LowKey { get { return B(1); } set { Set(1, Clamp(value, 0, 127)); } }

        [Category("Key range"), RefreshProperties(RefreshProperties.All)]
        [Description("Velocity at which zone 2 takes over from zone 1. 128 is above the highest MIDI velocity, so it leaves zone 2 silent.")]
        public int VelocitySwitch { get { return B(2); } set { Set(2, Clamp(value, 1, 128)); } }

        [Category("VCA envelope")] public int VcaAttack { get { return B(3); } set { Set(3, Clamp(value, 0, 99)); } }
        [Category("VCA envelope")] public int VcaDecay { get { return B(4); } set { Set(4, Clamp(value, 0, 99)); } }
        [Category("VCA envelope")] public int VcaSustain { get { return B(5); } set { Set(5, Clamp(value, 0, 99)); } }
        [Category("VCA envelope")] public int VcaRelease { get { return B(6); } set { Set(6, Clamp(value, 0, 99)); } }

        [Category("VCF envelope")] public int VcfAttack { get { return B(34); } set { Set(34, Clamp(value, 0, 99)); } }
        [Category("VCF envelope")] public int VcfDecay { get { return B(35); } set { Set(35, Clamp(value, 0, 99)); } }
        [Category("VCF envelope")] public int VcfSustain { get { return B(36); } set { Set(36, Clamp(value, 0, 99)); } }
        [Category("VCF envelope")] public int VcfRelease { get { return B(37); } set { Set(37, Clamp(value, 0, 99)); } }

        [Category("VCF envelope"), Description("Envelope depth into the filter. Signed.")]
        public int VcfAmount { get { return SB(23); } set { Set(23, Clamp(value, -50, 50)); } }

        [Category("Velocity sensitivity")] public int ToLoudness { get { return B(11); } set { Set(11, Clamp(value, 0, 99)); } }
        [Category("Velocity sensitivity")] public int ToFilter { get { return B(7); } set { Set(7, Clamp(value, 0, 99)); } }
        [Category("Velocity sensitivity")] public int ToAttack { get { return B(9); } set { Set(9, Clamp(value, 0, 99)); } }

        [Category("Velocity sensitivity"), Description("Signed, -50..50.")]
        public int ToRelease { get { return SB(10); } set { Set(10, Clamp(value, -50, 50)); } }

        [Category("Velocity sensitivity"), Description("Key position into the filter. 0 reads as Off.")]
        public int KeyToFilter { get { return B(8); } set { Set(8, Clamp(value, 0, 99)); } }

        [Category("LFO")] public int LfoDelay { get { return B(15); } set { Set(15, Clamp(value, 0, 99)); } }
        [Category("LFO")] public int LfoRate { get { return B(16); } set { Set(16, Clamp(value, 0, 99)); } }
        [Category("LFO")] public int LfoDepth { get { return B(17); } set { Set(17, Clamp(value, 0, 99)); } }

        [Category("LFO"), Description("LFO depth from aftertouch.")]
        public int FromAftertouch { get { return B(21); } set { Set(21, Clamp(value, 0, 50)); } }

        [Category("LFO"), Description("LFO depth from the modulation wheel.")]
        public int FromModwheel { get { return B(22); } set { Set(22, Clamp(value, 0, 50)); } }

        [Category("WARP")] public int WarpVelocity { get { return B(12); } set { Set(12, Clamp(value, 0, 99)); } }
        [Category("WARP")] public int WarpTime { get { return B(14); } set { Set(14, Clamp(value, 0, 99)); } }

        [Category("WARP"), Description("Signed.")]
        public int WarpAttackOffset { get { return SB(13); } set { Set(13, Clamp(value, -50, 50)); } }

        [Category("Flags"), Description("Play the keygroup at a fixed pitch across its range.")]
        [Editor(typeof(CheckBoxEditor), typeof(UITypeEditor)), TypeConverter(typeof(OnOffConverter))]
        public bool ConstantPitch
        {
            get { return (B(18) & 1) != 0; }
            set { SetFlag(18, 1, value); }
        }

        [Category("Flags"), Description("Free-run the LFO instead of retriggering it per note.")]
        [Editor(typeof(CheckBoxEditor), typeof(UITypeEditor)), TypeConverter(typeof(OnOffConverter))]
        public bool LfoDesync
        {
            get { return (B(18) & 4) != 0; }
            set { SetFlag(18, 4, value); }
        }

        [Category("Flags"), Description("Play the sample through, ignoring key release.")]
        [Editor(typeof(CheckBoxEditor), typeof(UITypeEditor)), TypeConverter(typeof(OnOffConverter))]
        public bool OneShot
        {
            get { return (B(18) & 8) != 0; }
            set { SetFlag(18, 8, value); }
        }

        [Category("Output"), Description("0 all, 1-8 individual mono outputs, 9 left, 10 right.")]
        public int Output
        {
            get { byte v = B(19); return v == 255 ? 0 : v + 1; }
            set { int p = Clamp(value, 0, 10); Set(19, p == 0 ? 255 : p - 1); }
        }

        [Category("Zone 1"), TypeConverter(typeof(SampleNameConverter))]
        [Description("Sample this zone plays. The list is the samples on this disk; you may also type a name that lives on another disk.")]
        public string Sample1 { get { return Str(24, 10); } set { SetZoneSample(24, 40, value); } }

        [Category("Zone 1"), Description("High byte of the zone's pitch, signed: worth 16 semitones a step. With Fine it is one count of sixteenths of a semitone - -1 with Fine 64 is -12.")]
        public int Transpose1 { get { return SB(43); } set { Set(43, Clamp(value, -50, 50)); } }

        [Category("Zone 1"), Description("Low byte of the zone's pitch, 0..255, in sixteenths of a semitone: 16 is a semitone up, 192 an octave.")]
        public int Fine1 { get { return B(42); } set { Set(42, Clamp(value, 0, 255)); } }

        [Category("Zone 1")] public int Filter1 { get { return B(44); } set { Set(44, Clamp(value, 0, 99)); } }

        [Category("Zone 1"), Description("Signed.")]
        public int Loudness1 { get { return SB(45); } set { Set(45, Clamp(value, -50, 50)); } }

        [Category("Zone 2"), TypeConverter(typeof(SampleNameConverter))]
        [Description("Second sample, selected above the velocity switch. Set it to 2 SAMPLE to leave the zone unused.")]
        public string Sample2 { get { return Str(46, 10); } set { SetZoneSample(46, 62, value); } }

        [Category("Zone 2"), Description("High byte of the zone's pitch, signed: worth 16 semitones a step. With Fine it is one count of sixteenths of a semitone - -1 with Fine 64 is -12.")]
        public int Transpose2 { get { return SB(65); } set { Set(65, Clamp(value, -50, 50)); } }

        [Category("Zone 2"), Description("Low byte of the zone's pitch, 0..255, in sixteenths of a semitone: 16 is a semitone up, 192 an octave.")]
        public int Fine2 { get { return B(64); } set { Set(64, Clamp(value, 0, 255)); } }

        [Category("Zone 2")] public int Filter2 { get { return B(66); } set { Set(66, Clamp(value, 0, 99)); } }

        [Category("Zone 2"), Description("Signed.")]
        public int Loudness2 { get { return SB(67); } set { Set(67, Clamp(value, -50, 50)); } }

        // ------------------------------------------------ dynamic zone labels

        /// <summary>
        /// The two zones are velocity layers, so label them by what they actually do
        /// here rather than by a fixed "soft"/"hard". With the switch off there is no
        /// soft and hard - there is just the sample - and saying otherwise would be
        /// wrong for the 1633 keygroups in the reference corpus that have no zone 2.
        /// </summary>
        string ZoneCategory(int zone)
        {
            int t = B(2);
            if (t >= 128)
                return zone == 1
                    ? "Zone 1  -  sample"
                    : "Zone 2  -  unused (velocity switch off)";

            return zone == 1
                ? "Zone 1  -  soft (velocity 1-" + (t - 1) + ")"
                : "Zone 2  -  hard (velocity " + t + "-127)";
        }

        PropertyDescriptorCollection Described()
        {
            var baseProps = TypeDescriptor.GetProperties(typeof(KeygroupEditor));
            var outp = new List<PropertyDescriptor>();
            foreach (PropertyDescriptor p in baseProps)
            {
                // The two envelopes and the filter amount are edited graphically
                // beside the grid, so they are not repeated as numbers in it.
                if (p.Category == "VCA envelope" || p.Category == "VCF envelope") continue;

                if (p.Category == "Zone 1") outp.Add(new RecategorisedProperty(p, ZoneCategory(1)));
                else if (p.Category == "Zone 2") outp.Add(new RecategorisedProperty(p, ZoneCategory(2)));
                else outp.Add(p);
            }
            return new PropertyDescriptorCollection(outp.ToArray());
        }

        PropertyDescriptorCollection ICustomTypeDescriptor.GetProperties() { return Described(); }
        PropertyDescriptorCollection ICustomTypeDescriptor.GetProperties(Attribute[] a) { return Described(); }

        AttributeCollection ICustomTypeDescriptor.GetAttributes() { return TypeDescriptor.GetAttributes(typeof(KeygroupEditor)); }
        string ICustomTypeDescriptor.GetClassName() { return typeof(KeygroupEditor).Name; }
        string ICustomTypeDescriptor.GetComponentName() { return ToString(); }
        TypeConverter ICustomTypeDescriptor.GetConverter() { return TypeDescriptor.GetConverter(typeof(KeygroupEditor)); }
        EventDescriptor ICustomTypeDescriptor.GetDefaultEvent() { return null; }
        PropertyDescriptor ICustomTypeDescriptor.GetDefaultProperty() { return null; }
        object ICustomTypeDescriptor.GetEditor(Type t) { return null; }
        EventDescriptorCollection ICustomTypeDescriptor.GetEvents() { return EventDescriptorCollection.Empty; }
        EventDescriptorCollection ICustomTypeDescriptor.GetEvents(Attribute[] a) { return EventDescriptorCollection.Empty; }
        object ICustomTypeDescriptor.GetPropertyOwner(PropertyDescriptor pd) { return this; }
        public override string ToString() { return "Keygroup " + (_index + 1); }
    }

    internal sealed class SampleEditor : EditorBase
    {
        public SampleEditor(AkaiDisk d, AkaiEntry e) : base(d, e, 0) { }

        [Category("Sample"),
         Description("10 characters. Renames the directory entry and any keygroup using it.")]
        public string Name { get { return Str(0, 10); } set { Rename(value); } }

        [Category("Pitch"), Description("MIDI note the sample sounds at. C3 = 60.")]
        public int NominalPitch
        {
            get { return U16(0x16) / 16; }
            set { SetU16(0x16, Clamp(value, 0, 127) * 16 + (U16(0x16) % 16)); }
        }

        [Category("Pitch"), Description("Sixteenths of a semitone, 0..15.")]
        public int FinePitch
        {
            get { return U16(0x16) % 16; }
            set { SetU16(0x16, (U16(0x16) / 16) * 16 + Clamp(value, 0, 15)); }
        }

        [Category("Sample"), Description("Signed loudness trim.")]
        public int Loudness
        {
            get { return S16(0x18); }
            set { SetU16(0x18, Clamp(value, -50, 50)); }
        }

        [Category("Loop"), Description("O one-shot, L looping, A alternating.")]
        public char LoopMode
        {
            get { return (char)B(0x1A); }
            set
            {
                // Not a poke. The mode decides how many descriptors the sample takes,
                // so it moves the descriptor pointer of every sample after this one -
                // which is why this goes through the disk, as renaming does.
                char c = char.ToUpperInvariant(value);
                if (c != 'O' && c != 'L' && c != 'A') return;
                try { Disk.SetLoopMode(Entry, c); }
                catch (ArgumentException) { /* the grid keeps the old mode */ }
            }
        }

        [Category("Loop"),
         Description("N normal, R reverse. Changing this REWRITES THE AUDIO backwards - " +
                     "it is a destructive edit on the machine, not a playback flag.")]
        public char LoopDirection
        {
            get { return (char)B(0x2B); }
            set
            {
                // Not a poke, and this one used to be.
                //
                // Measured: a sample written forwards with 0x2B set to 'R' plays forwards,
                // one-shot or looping. So the machine rewrites the audio backwards and keeps
                // the byte as a record of it, and setting the byte alone marked a sample as
                // reversed while it went on playing forwards - a disk contradicting itself,
                // from a property setter that looked harmless.
                char c = char.ToUpperInvariant(value);
                if (c != 'N' && c != 'R') return;

                try { Disk.SetSampleDirection(Entry, c); }
                catch (InvalidOperationException) { /* no audio; the grid keeps the old value */ }
                catch (ArgumentException) { /* same */ }
            }
        }

        [Category("Read-only"), ReadOnly(true)]
        public int SampleWords
        {
            get { var b = Bytes; return b[0x10] | (b[0x11] << 8) | (b[0x12] << 16) | (b[0x13] << 24); }
        }

        [Category("Read-only"), ReadOnly(true)]
        public int SampleRate { get { return U16(0x14); } }

        [Category("Read-only"), ReadOnly(true), Description("Where the sample loads in sample RAM.")]
        public string MemoryAddress
        {
            get { var b = Bytes; return "0x" + (b[0x36] | (b[0x37] << 8) | (b[0x38] << 16)).ToString("X6"); }
        }

        public override string ToString() { return Name; }
    }
}
