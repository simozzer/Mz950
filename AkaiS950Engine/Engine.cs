using System;
using System.Collections.Generic;

namespace AkaiS950Engine
{
    /// <summary>
    /// The instrument: eight voices, a patch, and a buffer to fill.
    ///
    /// Nothing in here knows what is driving it. The editor drives it from a piano keyboard
    /// and a MIDI port; a plugin would drive it from a host's note events; the tests drive
    /// it from a loop and render to a file. That is deliberate - it is the one decision
    /// that keeps a plugin possible later without any of this being rewritten.
    ///
    /// THREADS
    ///
    /// Render runs on the audio thread and must never allocate, never lock and never block.
    /// Notes arrive from somewhere else, so they go into a small lock-free ring and are
    /// picked up at the top of the next Render. A lock here would be a dropout.
    /// </summary>
    public sealed class Engine
    {
        /// <summary>What the machine has. Voices past this steal the oldest.</summary>
        public const int Polyphony = 8;

        readonly Voice[] _voices = new Voice[Polyphony];
        readonly List<KeygroupPatch> _matched = new List<KeygroupPatch>(8);

        // The key ranges of whatever _matched is holding, so the crossfade can be worked out
        // without allocating on the audio thread. Sized to the polyphony because that is the
        // most voices one note can ever start.
        readonly int[] _matchedLow = new int[Polyphony];
        readonly int[] _matchedHigh = new int[Polyphony];

        // the event ring: written by whoever is playing, read by the audio thread
        struct Event { public byte Kind, A, B; }
        const int RingSize = 256;
        readonly Event[] _ring = new Event[RingSize];
        int _write, _read;

        const byte EvNoteOn = 1, EvNoteOff = 2, EvWheel = 3, EvAllOff = 4, EvRepatch = 5,
                   EvBend = 6, EvPressure = 7;

        Patch _patch;
        long _sequence;

        /*
         * What the last note-on actually started.
         *
         * Not for the engine's benefit - for the caller's, so a report of "every key
         * plays the same sample" can be answered with what the voices really picked up
         * rather than with what the caller meant to ask for. References only: naming
         * them would mean building a string on the audio thread, which is the one thing
         * the render path is not allowed to do.
         */
        readonly Sound[] _started = new Sound[Polyphony];
        int _startedCount, _startedNote = -1;
        double _sharedPhase, _sharedStep;
        int _wheel;
        int _pressure;               // channel aftertouch, 0..127, at rest at nothing
        int _bend14 = 8192;          // the pitch wheel, at rest in the middle

        public double SampleRate { get; private set; }

        /// <summary>The master trim, as a plain gain. Eight voices at once can clip.</summary>
        public float Gain = 0.7f;

        public Engine(double sampleRate)
        {
            SampleRate = sampleRate <= 0 ? 48000 : sampleRate;
            for (int i = 0; i < _voices.Length; i++) _voices[i] = new Voice();
        }

        /// <summary>
        /// What to play. Safe to call while running: the audio thread only ever reads it,
        /// and a reference assignment is atomic.
        /// </summary>
        public void SetPatch(Patch patch)
        {
            _patch = patch;
            _sharedStep = 0;
            if (patch != null)
            {
                // The programme's own LFO runs at the rate its keygroups ask for. They
                // almost always agree; where they do not, the first one wins, since one
                // shared oscillator cannot be at two rates at once.
                for (int i = 0; i < patch.Keygroups.Count; i++)
                {
                    KeygroupPatch k = patch.Keygroups[i];
                    if (k.LfoDesync) continue;
                    _sharedStep = 2.0 * Math.PI *
                        (Cal.LfoRateHzAtZero + k.LfoRate * Cal.LfoRateHzPerUnit) / SampleRate;
                    break;
                }
            }

            // Notes already sounding take up the new settings where they stand, rather
            // than waiting to be struck again. Posted rather than done here: the voices
            // belong to the audio thread. See Repatch.
            Post(EvRepatch, 0, 0);
        }

        /// <summary>The note the last note-on was for, or -1.</summary>
        public int LastNote { get { return _startedNote; } }

        /// <summary>How many voices that note-on started.</summary>
        public int LastStartedCount { get { return _startedCount; } }

        /// <summary>The sound one of them picked up.</summary>
        public Sound LastStarted(int i)
        {
            return i >= 0 && i < _startedCount ? _started[i] : null;
        }

        public int ActiveVoices
        {
            get
            {
                int n = 0;
                for (int i = 0; i < _voices.Length; i++) if (_voices[i].Active) n++;
                return n;
            }
        }

        // ------------------------------------------------------------------ playing

        public void NoteOn(int note, int velocity) { Post(EvNoteOn, note, velocity); }
        public void NoteOff(int note) { Post(EvNoteOff, note, 0); }
        public void Modwheel(int value) { Post(EvWheel, value, 0); }

        /// <summary>
        /// Channel aftertouch, 0..127. One value for the whole keyboard: the S950 has no
        /// polyphonic pressure input, so there is nothing per-key to carry.
        /// </summary>
        public void Aftertouch(int value) { Post(EvPressure, value, 0); }

        /// <summary>
        /// The pitch wheel, 0..16383 with 8192 at rest. Split across the event's two byte
        /// fields, because fourteen bits do not fit in one.
        /// </summary>
        public void PitchBend(int value)
        {
            int v = value < 0 ? 0 : (value > 16383 ? 16383 : value);
            Post(EvBend, (v >> 7) & 0x7F, v & 0x7F);
        }

        /// <summary>
        /// How far the wheel bends, in semitones. The machine's MIDI page offers 1 to 12.
        ///
        /// Not read off a disk: it belongs to the machine rather than to a programme, and the
        /// OVERALL SETTINGS file that would hold it is written only when somebody saves it
        /// deliberately. Two is the usual default everywhere.
        /// </summary>
        public double BendRange = 2.0;
        public void AllNotesOff() { Post(EvAllOff, 0, 0); }

        void Post(byte kind, int a, int b)
        {
            int w = _write;
            int next = (w + 1) % RingSize;
            if (next == _read) return;            // full: drop it rather than block

            _ring[w].Kind = kind;
            _ring[w].A = (byte)(a < 0 ? 0 : (a > 255 ? 255 : a));
            _ring[w].B = (byte)(b < 0 ? 0 : (b > 255 ? 255 : b));
            System.Threading.Thread.MemoryBarrier();
            _write = next;
        }

        void DrainEvents()
        {
            while (_read != _write)
            {
                Event e = _ring[_read];
                _read = (_read + 1) % RingSize;

                switch (e.Kind)
                {
                    case EvNoteOn: StartNote(e.A, e.B); break;
                    case EvNoteOff: StopNote(e.A); break;
                    case EvWheel: _wheel = e.A; break;
                    case EvPressure: _pressure = e.A; break;
                    case EvBend: _bend14 = (e.A << 7) | e.B; break;
                    case EvRepatch: Repatch(); break;
                    case EvAllOff:
                        for (int i = 0; i < _voices.Length; i++) _voices[i].Release();
                        break;
                }
            }
        }

        /// <summary>
        /// Hand every sounding voice its keygroup out of the patch now loaded.
        ///
        /// Runs here, on the audio thread, because the voices belong to it. SetPatch is
        /// called from whichever thread noticed the edit, and reaching into the voices
        /// from there would be a race with the block being rendered - so it posts,
        /// like a note does, and this picks it up in order with everything else.
        ///
        /// Matched by keygroup index rather than by key range: a voice belongs to the
        /// keygroup that started it, and an edit may have moved the ranges under it.
        /// </summary>
        void Repatch()
        {
            Patch p = _patch;
            if (p == null) return;

            for (int i = 0; i < _voices.Length; i++)
            {
                Voice v = _voices[i];
                if (!v.Active) continue;

                int want = v.KeygroupIndex;
                if (want < 0) continue;

                for (int k = 0; k < p.Keygroups.Count; k++)
                {
                    KeygroupPatch kg = p.Keygroups[k];
                    if (kg.KeygroupIndex != want) continue;
                    if (v.Velocity < kg.VelocityFrom || v.Velocity > kg.VelocityTo) continue;

                    v.Adopt(kg);
                    break;
                }
            }
        }

        void StartNote(int note, int velocity)
        {
            Patch p = _patch;
            _startedCount = 0;
            _startedNote = note;
            if (p == null) return;

            p.Matching(note, velocity, _matched);

            // The positional crossfade needs every keygroup answering this note at once,
            // so the ranges are gathered before any voice starts. With the flag off, or
            // with one keygroup answering, CrossfadeGain returns 1 and nothing changes.
            int matched = Math.Min(_matched.Count, _matchedLow.Length);
            for (int m = 0; m < matched; m++)
            {
                _matchedLow[m] = _matched[m].LowKey;
                _matchedHigh[m] = _matched[m].HighKey;
            }

            for (int m = 0; m < _matched.Count; m++)
            {
                KeygroupPatch kg = _matched[m];
                if (_startedCount < _started.Length) _started[_startedCount++] = kg.Sound;

                double fade = p.PositionalCrossfade && m < matched
                    ? Cal.CrossfadeGain(note, _matchedLow, _matchedHigh, matched, m)
                    : 1.0;

                /*
                 * What the two performance controllers add, in cents.
                 *
                 * The modwheel is byte 22 and channel pressure is byte 21, and the
                 * aftertouch run measured them to be THE SAME MECHANISM from different
                 * sources. At full, aftertouch gave 71.95 cents against the wheel's 72.3 -
                 * one constant, not two. Byte 21 scales it proportionally, reading 0.511
                 * of full at 50 where a straight proportion is 0.505 and byte 22 gave
                 * 0.509. And the two ADD: wheel alone read 71.87 cents, wheel and pressure
                 * together 149.79, where taking the larger would have left it at 71.87.
                 *
                 * Byte 21 used to be read and dropped, on the grounds that it is 0 in all
                 * 1908 keygroups of one person's disks. Other people have other disks.
                 */
                double wheelCents = Cal.LfoWheelCentsAtFull *
                                    (kg.LfoModwheelDepth / 99.0) * (_wheel / 127.0)
                                  + Cal.LfoWheelCentsAtFull *
                                    (kg.LfoAftertouchDepth / 99.0) * (_pressure / 127.0);

                if (kg.LfoDepth * Cal.LfoDepthCentsPerUnit + wheelCents < 0.5) wheelCents = 0;

                Voice v = Take();
                v.Start(kg, note, velocity, SampleRate, wheelCents, _sequence++, fade);
            }
        }

        /// <summary>
        /// Let go of every voice holding this note.
        ///
        /// Every one, not the first: pressing the same key twice before releasing it is
        /// ordinary, and leaving the older voice held would strand it.
        /// </summary>
        void StopNote(int note)
        {
            for (int i = 0; i < _voices.Length; i++)
                if (_voices[i].Active && _voices[i].Note == note && _voices[i].Held)
                    _voices[i].LetGo();      // the machine's note-off latency; see Cal

        }

        /// <summary>
        /// A voice to start a note on.
        ///
        /// Idle first, obviously. Then the oldest one that has already been let go and is
        /// only fading - taking that costs a tail nobody is listening to. Only if every
        /// voice is still held does it take one of those, and then the oldest.
        ///
        /// Taking the oldest regardless, which is what this did, would cut off a key the
        /// player is holding while a released note was left ringing beside it - which
        /// reads exactly like "the release did not happen".
        /// </summary>
        Voice Take()
        {
            for (int i = 0; i < _voices.Length; i++)
                if (!_voices[i].Active) return _voices[i];

            int pick = -1;
            for (int i = 0; i < _voices.Length; i++)
            {
                if (_voices[i].Held) continue;                  // still down: leave it
                if (pick < 0 || _voices[i].StartedAt < _voices[pick].StartedAt) pick = i;
            }

            if (pick < 0)
            {
                pick = 0;
                for (int i = 1; i < _voices.Length; i++)
                    if (_voices[i].StartedAt < _voices[pick].StartedAt) pick = i;
            }

            _voices[pick].Kill();
            return _voices[pick];
        }

        // ------------------------------------------------------------------- render

        /// <summary>
        /// Fill <paramref name="count"/> mono samples. Allocates nothing.
        /// </summary>
        public void Render(float[] buffer, int offset, int count)
        {
            DrainEvents();

            Array.Clear(buffer, offset, count);

            // The wheel reaches notes already sounding - that is what separates it from
            // velocity, which is settled when the key goes down.
            double bendMono = Cal.BendRatio(_bend14, BendRange);

            for (int i = 0; i < _voices.Length; i++)
                if (_voices[i].Active)
                {
                    _voices[i].SetBend(bendMono);
                    _voices[i].Render(buffer, offset, count, _sharedPhase);
                }

            _sharedPhase += _sharedStep * count;
            if (_sharedPhase > 2.0 * Math.PI) _sharedPhase %= 2.0 * Math.PI;

            float g = Gain;
            for (int i = 0; i < count; i++)
            {
                float v = buffer[offset + i] * g;
                buffer[offset + i] = v > 1f ? 1f : (v < -1f ? -1f : v);
            }
        }

        /// <summary>
        /// Fill <paramref name="count"/> stereo samples, honouring each keygroup's output port.
        ///
        /// The machine's LEFT and RIGHT sockets are two mono outputs rather than a pan pot, so
        /// a keygroup sent to one is absent from the other - 38 keygroups across four library
        /// programmes, including TUBULAR 2 whose bells read L L L L R R R R. Everything else
        /// lands on both sides at full level, which is what the mono path has always done.
        ///
        /// Allocates nothing. The mono overload above is untouched and still bit-identical.
        /// </summary>
        public void Render(float[] left, float[] right, int offset, int count)
        {
            DrainEvents();

            Array.Clear(left, offset, count);
            Array.Clear(right, offset, count);

            double bendStereo = Cal.BendRatio(_bend14, BendRange);

            for (int i = 0; i < _voices.Length; i++)
                if (_voices[i].Active)
                {
                    _voices[i].SetBend(bendStereo);
                    _voices[i].Render(left, right, offset, count, _sharedPhase);
                }

            _sharedPhase += _sharedStep * count;
            if (_sharedPhase > 2.0 * Math.PI) _sharedPhase %= 2.0 * Math.PI;

            float g = Gain;
            for (int i = 0; i < count; i++)
            {
                float l = left[offset + i] * g, r = right[offset + i] * g;
                left[offset + i]  = l > 1f ? 1f : (l < -1f ? -1f : l);
                right[offset + i] = r > 1f ? 1f : (r < -1f ? -1f : r);
            }
        }
    }
}
