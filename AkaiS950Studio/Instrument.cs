using System;
using System.Collections.Generic;
using AkaiS950Engine;
using AkaiS950List;

namespace AkaiS950Studio
{
    /// <summary>
    /// The instrument: a disk, an engine, a sound card and a MIDI port.
    ///
    /// This is the only place that knows about both halves. AkaiS950Engine knows nothing of
    /// disks, and AkaiDisk knows nothing of audio - which is what lets the same engine be
    /// driven by a plugin host later without either of them being disturbed.
    ///
    /// Everything here is safe to call from the UI thread. The engine's note queue is
    /// lock-free, so a click on the keyboard, a MIDI note from winmm's callback thread and
    /// the audio thread reading the queue never wait on each other.
    /// </summary>
    internal sealed class Instrument : IDisposable
    {
        readonly Engine _engine;
        readonly WasapiOut _out;
        MidiIn _midi;

        // Decoding a sample is slow enough to matter and the same one is played over and
        // over, so it is kept. Keyed on the entry itself, which is a new object each time
        // the directory is re-read - so a reload quietly invalidates the lot.
        readonly Dictionary<AkaiEntry, Sound> _sounds = new Dictionary<AkaiEntry, Sound>();

        /// <summary>
        /// How much of the loop join to crossfade, in milliseconds. Zero is what the S950
        /// itself does - it splices, and clicks when the points are bad.
        /// </summary>
        public double LoopCrossfadeMs = LoopSmoothing.DefaultCrossfadeMs;

        /// <summary>
        /// Pull the loop ends onto zero crossings as well. OFF, and see LoopSmoothing for
        /// why: it changes the loop length, and the loop length is the pitch of the looped
        /// part, so it trades a click for a tone that does not belong to the note.
        /// </summary>
        public bool SnapLoopsToZero = false;

        /// <summary>The port MIDI is listening on, or -1.</summary>
        public int MidiPort { get; private set; }

        /// <summary>
        /// Which MIDI channel to answer: 0 for all of them, or 1 to 16.
        ///
        /// All of them by default, which is not what the panel of an S950 does - but
        /// a filter that is on before anyone has chosen it is a keyboard that silently
        /// does nothing, and there is no way to tell that apart from a dead port.
        /// </summary>
        public int MidiChannel = 0;

        /// <summary>The name of the port MIDI is listening on, or null.</summary>
        public string MidiPortName { get; private set; }

        public bool Running { get; private set; }
        public string Error { get; private set; }
        public double LatencyMs { get { return _out.LatencyMs; } }
        public int SampleRate { get { return _out.SampleRate; } }
        public int ActiveVoices { get { return _engine.ActiveVoices; } }

        public Instrument()
        {
            MidiPort = -1;

            // The engine is built before the output, because opening the output calls the
            // fill callback once to prime the buffer.
            _engine = new Engine(48000);
            _out = new WasapiOut(Fill);
        }

        public bool Start()
        {
            if (Running) return true;

            if (!_out.Start(10))
            {
                Error = _out.Error;
                return false;
            }

            // The card decides the rate, not us. Rebuilding is cheaper than resampling
            // every voice, and this happens once.
            if (_out.SampleRate != (int)_engine.SampleRate)
            {
                _out.Stop();
                var again = new Engine(_out.SampleRate);
                again.SetPatch(_patch);
                _engineAtRate = again;
                if (!_out.Start(10)) { Error = _out.Error; return false; }
            }

            Running = true;
            return true;
        }

        // The engine actually in use: the one built at the card's rate, if there is one.
        Engine _engineAtRate;
        Engine Live { get { return _engineAtRate ?? _engine; } }

        Patch _patch;

        void Fill(float[] mono, int frames) { Live.Render(mono, 0, frames); }

        // ------------------------------------------------------------------- playing

        public void NoteOn(int note, int velocity) { Live.NoteOn(note, velocity); }

        /// <summary>
        /// What the last note actually sounded, named.
        ///
        /// Read after the note has been rendered, so it is one buffer behind - which for
        /// a status line is close enough, and it is the truth rather than an intention.
        /// </summary>
        public string LastPlayed()
        {
            Engine e = Live;
            int n = e.LastStartedCount;
            if (n == 0) return "nothing";

            var names = new List<string>();
            for (int i = 0; i < n; i++)
            {
                Sound s = e.LastStarted(i);
                if (s != null && !names.Contains(s.Name)) names.Add(s.Name);
            }
            return string.Join(" + ", names.ToArray());
        }
        public void NoteOff(int note) { Live.NoteOff(note); }
        public void AllNotesOff() { Live.AllNotesOff(); }

        public float Gain
        {
            get { return Live.Gain; }
            set { _engine.Gain = value; if (_engineAtRate != null) _engineAtRate.Gain = value; }
        }

        // ---------------------------------------------------------------------- MIDI

        public static List<string> MidiPorts() { return MidiIn.Ports(); }

        /// <summary>
        /// Listen on a MIDI input.
        ///
        /// The output has to be running first. A MIDI note reaches the engine whether or
        /// not anything is draining it, and a note posted into an engine nobody is
        /// rendering looks exactly like a dead MIDI port from the outside.
        /// </summary>
        public bool OpenMidi(int port)
        {
            CloseMidi();

            if (!Running && !Start()) return false;

            _midi = new MidiIn(OnMidi);
            if (_midi.Start(port))
            {
                MidiPort = port;
                var names = MidiPorts();
                MidiPortName = port >= 0 && port < names.Count ? names[port] : ("input " + port);
                return true;
            }

            Error = _midi.Error;
            _midi = null;
            return false;
        }

        public void CloseMidi()
        {
            MidiPort = -1;
            MidiPortName = null;

            if (_midi == null) return;
            _midi.Dispose();
            _midi = null;
            Live.AllNotesOff();          // nothing is going to send the note-offs now
        }

        /// <summary>
        /// Called on winmm's own thread. Posts into the engine and does nothing else - no
        /// UI, no locks, nothing that could keep the MIDI driver waiting.
        /// </summary>
        void OnMidi(int status, int d1, int d2)
        {
            int kind = status & 0xF0;

            int channel = MidiChannel;
            if (channel != 0 && (status & 0x0F) != channel - 1) return;

            if (kind == 0x90 && d2 > 0) Live.NoteOn(d1, d2);
            else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) Live.NoteOff(d1);
            else if (kind == 0xB0 && d1 == 1) Live.Modwheel(d2);
            else if (kind == 0xB0 && (d1 == 120 || d1 == 123)) Live.AllNotesOff();
        }

        // ------------------------------------------------------------------- patches

        /// <summary>
        /// Load a programme. Everything the engine needs is copied out now, so playing a
        /// note never touches the disk image or the directory.
        /// </summary>
        // What SetProgram was last given, so the same request twice is free and so an
        // audition knows what to put back. A key click asks for the programme it is
        // already holding on every single press.
        AkaiDisk _programDisk;
        AkaiEntry _programEntry;
        Patch _programPatch;

        // The revision everything cached here was derived from.
        AkaiDisk _cacheDisk;
        int _cacheRevision = -1;

        /// <summary>
        /// Throw away anything derived from a disk that has since been edited.
        ///
        /// Called wherever a disk comes in. Without it an edit to a filter, an envelope
        /// or a sample is inaudible: the patch and the decoded audio were both built
        /// before the edit and neither has any reason to notice. Comparing the disk's
        /// revision is what makes that impossible to forget at an edit site.
        ///
        /// Editing one disk drops what was cached for another. They are cheap to
        /// rebuild, and the alternative is bookkeeping per disk for a case - editing
        /// two images while playing a third - that does not arise.
        /// </summary>
        void DropStaleCaches(AkaiDisk disk)
        {
            if (disk == null) return;
            if (ReferenceEquals(disk, _cacheDisk) && disk.Revision == _cacheRevision) return;

            _cacheDisk = disk;
            _cacheRevision = disk.Revision;

            _sounds.Clear();
            _programDisk = null;
            _programEntry = null;
            _programPatch = null;
        }

        public void SetProgram(AkaiDisk disk, AkaiEntry program)
        {
            DropStaleCaches(disk);

            if (disk == null || program == null || program.Type != 'P')
            {
                _programDisk = null; _programEntry = null; _programPatch = null;
                _patch = null;
                Live.SetPatch(null);
                return;
            }

            // Already loaded, and nothing has invalidated it: leave the voices alone.
            if (ReferenceEquals(disk, _programDisk) && ReferenceEquals(program, _programEntry) &&
                _programPatch != null)
            {
                if (!ReferenceEquals(_patch, _programPatch))
                {
                    _patch = _programPatch;
                    Live.SetPatch(_programPatch);
                }
                return;
            }

            var patch = new Patch { Name = program.Name.Trim() };
            var groups = disk.Keygroups(program);

            // Program header byte 21: fade overlapping keygroups rather than sounding both
            // at full level. 48 library programmes set it and overlap, and they are the
            // multi-sampled instruments - the pianos above all.
            AkaiDisk.ProgramHeader head = disk.ReadProgramHeader(program);
            patch.PositionalCrossfade = head != null && head.PositionalCrossfade;

            for (int i = 0; i < groups.Count; i++)
            {
                AkaiDisk.Keygroup kg = groups[i];

                /*
                 * The two zones are velocity alternatives, so they split the range at the
                 * switch rather than both sounding.
                 *
                 * The byte is the LAST velocity of zone 1, not the first of zone 2. This read
                 * "zone1 0..split-1, zone2 split..127" and was out by one step. Measured on
                 * the hardware with a sine in zone 1 and noise in zone 2:
                 *
                 *     switch    1     zone 1 at velocity 1,  zone 2 from 2
                 *     switch   64     zone 1 through 64,     zone 2 from 65
                 *     switch  127     zone 1 through 127,    zone 2 never
                 *
                 * The last line is what makes it certain: at a switch of 127 the hard sample
                 * cannot be reached at all, which only follows if zone 2 begins at 128. It is
                 * also why the panel's range runs to 128 and why 128 turns the switch off -
                 * the special case that used to say so has gone, because with zone 2 starting
                 * at split + 1 a split of 127 or 128 leaves it an empty range and AddZone
                 * declines it on its own.
                 */
                int split = kg.VelocitySwitch;
                if (split < 1 || split > 128) split = 128;

                if (!kg.HasSecondZone)
                {
                    AddZone(disk, patch, kg, kg.Zone1, 0, 127);
                }
                else
                {
                    AddZone(disk, patch, kg, kg.Zone1, 0, Math.Min(127, split));
                    AddZone(disk, patch, kg, kg.Zone2, split + 1, 127);
                }
            }

            _programDisk = disk;
            _programEntry = program;
            _programPatch = patch;

            _patch = patch;
            Live.SetPatch(patch);
        }

        /// <summary>
        /// Put the loaded programme back after an audition has borrowed the engine.
        /// </summary>
        void RestoreProgram()
        {
            if (_programPatch == null || ReferenceEquals(_patch, _programPatch)) return;
            _patch = _programPatch;
            Live.SetPatch(_programPatch);
        }

        void AddZone(AkaiDisk disk, Patch patch, AkaiDisk.Keygroup kg, AkaiDisk.Zone zone,
                     int velFrom, int velTo)
        {
            if (velTo < velFrom) return;
            if (zone == null || string.IsNullOrEmpty(zone.Name)) return;

            AkaiEntry sample = FindSample(disk, zone.Name);
            if (sample == null) return;

            Sound sound = SoundFor(disk, sample);
            if (sound == null) return;

            patch.Keygroups.Add(new KeygroupPatch
            {
                LowKey = kg.LowKey,
                HighKey = kg.HighKey,
                KeygroupIndex = kg.Index,
                VelocityFrom = velFrom,
                VelocityTo = velTo,
                Sound = sound,

                VcaAttack = kg.VcaAttack, VcaDecay = kg.VcaDecay,
                VcaSustain = kg.VcaSustain, VcaRelease = kg.VcaRelease,

                VcfWritten = KeygroupPatch.LooksWritten(kg.VcfAttack, kg.VcfDecay,
                                                        kg.VcfSustain, kg.VcfRelease),
                VcfAttack = kg.VcfAttack, VcfDecay = kg.VcfDecay,
                VcfSustain = kg.VcfSustain, VcfRelease = kg.VcfRelease,
                VcfAmount = kg.VcfAmount,

                VelToFilter = kg.VelToFilter,
                KeyToFilter = kg.KeyToFilter,
                VelToLoudness = kg.VelToLoudness,
                VelToAttack = kg.VelToAttack,
                VelToRelease = kg.VelToRelease,
                VelocityReleaseOn = kg.VelocityReleaseOn,

                WarpVelocity = kg.WarpVelocity,
                WarpDepth = kg.WarpAttackOffset,
                WarpTime = kg.WarpTime,

                OutputPort = kg.OutputPort,

                LfoDelay = kg.LfoDelay, LfoRate = kg.LfoRate, LfoDepth = kg.LfoDepth,
                LfoModwheelDepth = kg.LfoModwheelDepth,
                LfoAftertouchDepth = kg.LfoAftertouchDepth, LfoDesync = kg.LfoDesync,

                ZoneFilter = zone.Filter,
                ZoneLoudness = zone.Loudness,
                ZoneTranspose = zone.PitchOffset,

                ConstantPitch = kg.ConstantPitch,
                OneShot = kg.OneShot
            });
        }

        /// <summary>One sample, decoded once and kept.</summary>
        Sound SoundFor(AkaiDisk disk, AkaiEntry e)
        {
            Sound got;
            if (_sounds.TryGetValue(e, out got)) return got;

            short[] words = disk.SampleWords12(e);
            if (words.Length == 0) return null;

            // The machine plays end-length .. end, so the loop start follows from the length.
            bool loops = e.LoopMode != 'O' && e.LoopLength > 0 && e.LoopEnd > 0;
            int to = (int)Math.Min(e.LoopEnd, words.Length);
            int from = (int)Math.Max(0, e.LoopEnd - e.LoopLength);
            loops = loops && to > from;

            // A note starts at the start marker (0x20), not at word 0 - measured on the
            // machine with DSKA0039 GRAND1, whose samples skip 1000 words of quiet lead-in.
            // See Disk::soundFor in the plugin. A marker at or past the end is ignored.
            // And a sample that does not loop stops at its END marker (0x1C), not at the end
            // of its audio - measured on DRUM-A's COWBELL (DSKA0004), whose marker is 100 ms
            // short. A loop already turns round there, so only one-shots are cut.
            int size = words.Length;
            if (!loops && e.LoopEnd > 0 && e.LoopEnd < words.Length) size = (int)e.LoopEnd;
            long playsTo = loops ? to : size;
            int first = e.LoopStart > 0 && e.LoopStart < playsTo ? (int)e.LoopStart : 0;
            from = Math.Max(from, first);

            var audio = new float[size - first];
            for (int i = 0; i < audio.Length; i++) audio[i] = words[first + i] / 2048f;

            var sound = new Sound
            {
                Name = e.Name.Trim(),
                Audio = audio,
                SourceRate = e.SampleRate < 1000 ? 40000 : e.SampleRate,
                RootPitch = e.NominalPitch + e.FinePitch / 16.0,
                Loops = loops,
                Alternates = e.LoopMode == 'A',
                LoopFrom = from - first,
                LoopTo = to - first
            };

            // Join the loop cleanly. The machine splices and clicks if the points are
            // bad; this does not, which is the one place the instrument knowingly sounds
            // better than the hardware. LoopCrossfadeMs = 0 turns it off.
            LoopSmoothing.Polish(sound, LoopCrossfadeMs, SnapLoopsToZero);

            _sounds[e] = sound;
            return sound;
        }

        /// <summary>Forget the decoded audio - after an edit, or a reload.</summary>
        /// <summary>
        /// Forget everything decoded or built. Normally unnecessary - a disk's revision
        /// does this by itself - and kept for a caller that has changed something the
        /// revision cannot see.
        /// </summary>
        public void Invalidate()
        {
            _sounds.Clear();
            _cacheDisk = null;
            _cacheRevision = -1;
            _programDisk = null; _programEntry = null; _programPatch = null;
        }

        static AkaiEntry FindSample(AkaiDisk d, string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            string want = name.Trim();

            for (int i = 0; i < d.Entries.Count; i++)
            {
                AkaiEntry x = d.Entries[i];
                if (x.Type == 'S' &&
                    string.Equals(x.Name.Trim(), want, StringComparison.OrdinalIgnoreCase))
                    return x;
            }
            return null;
        }

        /// <summary>
        /// Audition one sample on its own, with no keygroup around it.
        ///
        /// Built as a patch of one wide-open keygroup so it goes through the same engine as
        /// everything else - a second playback path is a second thing to keep in step.
        /// </summary>
        public void PlaySample(AkaiDisk disk, AkaiEntry sample, int note)
        {
            DropStaleCaches(disk);

            Sound sound = SoundFor(disk, sample);
            if (sound == null) return;

            var patch = new Patch { Name = sound.Name };
            patch.Keygroups.Add(new KeygroupPatch
            {
                LowKey = 0, HighKey = 127, VelocityFrom = 0, VelocityTo = 127, Sound = sound,
                VcaAttack = 0, VcaDecay = 0, VcaSustain = 99, VcaRelease = 0,
                VcfWritten = true, VcfSustain = 99, ZoneFilter = 99,
                LfoDesync = true
            });

            _patch = patch;
            Live.SetPatch(patch);
            Live.AllNotesOff();
            Live.NoteOn(note, 100);

            /*
             * Nothing will ever let go of this one.
             *
             * A keygroup gets its note-off from the key being released; auditioning a
             * sample has no key, so a looped sample would sound until the program closed.
             * A one-shot ends by itself and needs none of this, but a loop is exactly what
             * loops are for.
             *
             * The whole sample, or ten seconds, whichever is shorter - long enough to hear
             * what the loop does, short enough not to become furniture. Stop() ends it
             * sooner, and so does playing anything else.
             */
            double length = sound.Audio.Length / (double)Math.Max(1, sound.SourceRate);
            double seconds = sound.Loops
                ? Math.Min(10.0, Math.Max(2.0, length * 3))   // long enough to hear the join
                : length + 0.25;                              // it ends itself; this is a backstop
            StopAfter(note, seconds);
        }

        System.Threading.Timer _auditionTimer;

        /// <summary>Release a note after a while, for a preview nobody will release.</summary>
        void StopAfter(int note, double seconds)
        {
            if (_auditionTimer != null) { _auditionTimer.Dispose(); _auditionTimer = null; }

            _auditionTimer = new System.Threading.Timer(delegate
            {
                Live.NoteOff(note);
                RestoreProgram();
            }, null, (int)(seconds * 1000), System.Threading.Timeout.Infinite);
        }

        /// <summary>Everything off, now.</summary>
        public void Stop()
        {
            if (_auditionTimer != null) { _auditionTimer.Dispose(); _auditionTimer = null; }
            Live.AllNotesOff();
            RestoreProgram();
        }

        public void Dispose()
        {
            if (_auditionTimer != null) { _auditionTimer.Dispose(); _auditionTimer = null; }
            CloseMidi();
            _out.Dispose();
            Running = false;
        }
    }
}
