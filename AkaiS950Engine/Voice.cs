using System;

namespace AkaiS950Engine
{
    /// <summary>
    /// One sounding note.
    ///
    /// Everything a voice does happens in Render, a block at a time, and nothing in here
    /// allocates once the voice exists - which is the whole reason the engine is separate
    /// from the editor. A garbage collection in the middle of an audio callback is a click,
    /// and a click in a plugin is someone else's ruined take.
    ///
    /// The signal path follows the machine: the sample is read out at whatever rate the
    /// pitch asks for, and the filter runs AFTER that, at the rate the audio leaves at. So
    /// the cutoff is a fixed number of hertz whichever key is held, rather than being
    /// dragged up and down by the varispeed.
    /// </summary>
    public sealed class Voice
    {
        /// <summary>How often the modulators are recomputed. 32 at 48 kHz is 0.67 ms.</summary>
        /// <summary>
        /// How often the modulators are recomputed. 32 at 48 kHz is 0.67 ms.
        ///
        /// It is also how often the filter is retuned, which is the tighter constraint: the
        /// fastest release drops the cutoff five and a half octaves in about a millisecond,
        /// and a sixth-order cascade moved that far in ONE step - keeping the state the old
        /// coefficients left behind - rings instead of closing. Stepping 17760 Hz to 311 Hz
        /// against a signal peaking at 1.0 gave 17.85 at a block of 64 and 1.24 at 32, with
        /// no further gain below that. So 32 is already past the cliff.
        /// </summary>
        const int ControlBlock = 32;

        public bool Active { get { return _stage != Stage.Idle; } }
        public int Note { get { return _note; } }
        public int Velocity { get { return _velocity; } }

        /// <summary>Which keygroup of the programme this voice came from, or -1.</summary>
        public int KeygroupIndex { get { return _kg == null ? -1 : _kg.KeygroupIndex; } }
        public long StartedAt { get { return _startedAt; } }
        /// <summary>The KEY is down. False from the note-off on, even while the envelope is
        /// still waiting out Cal.NoteOffLatencySeconds before it releases.</summary>
        public bool Held { get { return _stage != Stage.Idle && _stage != Stage.Release && !_keyUp; } }

        bool _keyUp;
        int  _pendingRelease;

        enum Stage { Idle, Attack, Decay, Sustain, Release }

        readonly Butterworth _filter = new Butterworth();

        KeygroupPatch _kg;
        Sound _sound;
        int _note;
        int _velocity;
        long _startedAt;

        double _sampleRate;          // the rate we are rendering at
        double _pos;                 // where we are in the sample, in frames
        double _step;                // frames per output sample, before the LFO

        // amplitude envelope, all in gain except the times
        Stage _stage = Stage.Idle;
        double _t;                   // seconds since the stage began
        double _attack, _decay, _release;
        double _peak, _sustain;

        /// <summary>
        /// The positional crossfade, worked out once when the note started. See
        /// Cal.CrossfadeGain.
        ///
        /// Kept rather than recomputed, and Adopt keeps it too: the fade depends on the key
        /// ranges of the OTHER keygroups answering this note, which a repatch may have moved
        /// under the voice. A voice belongs to the keygroup that started it, and it belongs
        /// to the balance it started in for the same reason.
        /// </summary>
        double _crossfade = 1.0;

        static double SustainDbFor(int stored) { return Cal.SustainDbFor(stored); }

        static double DecaySecondsFor(int stored, double sustainDb)
        {
            return Cal.VcaDecaySeconds(stored, sustainDb);
        }
        double _gain, _releaseFrom;

        // filter envelope
        double _vcfAttack, _vcfDecay, _vcfSustain, _vcfRelease, _vcfDepth;
        double _baseCutoff, _cutoffShift, _ceiling, _floor;

        /// <summary>
        /// The filter envelope's own clock, counting from the note on.
        ///
        /// NOT _t, which is what the amplitude envelope has reached in its CURRENT stage and
        /// is reset to zero every time that stage changes. Driving the filter from it made
        /// the filter envelope restart whenever the amplitude one moved from attack to decay
        /// and again from decay to sustain - so a programme with a slow amplitude decay swept
        /// its filter two or three times over. It went unnoticed because most of the library
        /// has an instant attack and decay, which resets a clock that has barely started.
        /// </summary>
        double _vcfT;

        /// <summary>
        /// The filter's release, which is a separate thing from the amplitude's.
        ///
        /// The envelope falls from wherever it had got to back to nothing, and "nothing"
        /// means the keygroup's own cutoff - the envelope's contribution decaying away
        /// rather than the filter slamming shut. Held separately from _stage because a
        /// one-shot keygroup ignores note-off entirely and neither envelope should release.
        /// </summary>
        bool _vcfReleasing;
        double _vcfReleaseFrom, _vcfReleaseT;

        // the LFO
        double _lfoCents, _lfoPhase, _lfoStep, _fadeSeconds, _fadeT;

        /// <summary>Seconds since the key went down. Warp's bend is measured from there, and
        /// _t cannot serve because it restarts at every envelope stage.</summary>
        double _sinceOn;

        /// <summary>Where the keygroup's output port puts this voice. See Cal.OutputGains.</summary>
        double _outL = 1.0, _outR = 1.0;

        /// <summary>+1 forwards, -1 backwards. Only an alternating loop ever makes it -1.</summary>
        int _dir = 1;

        /// <summary>The pitch wheel as a rate multiplier, set by the engine once per block.
        /// Named for the wheel because the LFO's own multiplier inside Render is already
        /// called bend, and one shadowing the other is a bug waiting to be written.</summary>
        double _wheelBend = 1.0;

        /// <summary>The wheel reaches a note already sounding, unlike velocity.</summary>
        public void SetBend(double ratio) { _wheelBend = ratio; }
        double _wheelCents;          // kept so Adopt can add it back to a new depth
        bool _ownLfo;

        /// <summary>Start this voice. Nothing here allocates.</summary>
        public void Start(KeygroupPatch kg, int note, int velocity, double sampleRate,
                          double wheelCents, long sequence)
        {
            Start(kg, note, velocity, sampleRate, wheelCents, sequence, 1.0);
        }

        public void Start(KeygroupPatch kg, int note, int velocity, double sampleRate,
                          double wheelCents, long sequence, double crossfade)
        {
            _crossfade = crossfade;
            _kg = kg;
            _sound = kg.Sound;
            _note = note;
            _velocity = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
            _startedAt = sequence;
            _sampleRate = sampleRate;

            // --- pitch. Constant pitch means the keygroup ignores which key was struck.
            double semis = kg.ZoneTranspose;
            if (!kg.ConstantPitch) semis += note - _sound.RootPitch;

            // past the machine's fastest playback the note drops by octaves - Cal.MaxPlaybackHz
            double ratio = Cal.FoldedRatio(_sound.SourceRate, Math.Pow(2.0, semis / 12.0));
            _step = _sound.SourceRate / sampleRate * ratio;
            _pos = 0;
            _dir = 1;               // forwards until an alternating loop turns it round

            // --- amplitude envelope
            double vel = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
            double depth = Clamp01(kg.VelToLoudness / 99.0);
            double velDb = -(127.0 - vel) * Cal.VelDbPerStep * depth;
            double zoneDb = kg.ZoneLoudness * Cal.LoudnessDbPerUnit;
            // A stored sustain of ZERO is silence, and is not the bottom of the line the
            // other settings sit on - see Cal.VcaSilenceDb. It used to stop 39.6 dB down
            // and hang there, on 723 of the library's keygroups.
            double sustainDb = SustainDbFor(kg.VcaSustain);

            _attack = Cal.VcaAttackSeconds(
                          Cal.VelocityAttackByte(kg.VcaAttack, kg.VelToAttack, _velocity));
            _decay = DecaySecondsFor(kg.VcaDecay, sustainDb);
            _release = Cal.EnvSeconds(
                           Cal.VelocityReleaseByte(kg.VcaRelease, kg.VelToRelease,
                                                   _velocity, kg.VelocityReleaseOn));
            _peak = Math.Min(Cal.DbToGain(velDb + zoneDb), 4.0) * _crossfade;
            _sustain = _peak * Cal.DbToGain(sustainDb);

            _stage = _attack > 0.0005 ? Stage.Attack : Stage.Decay;
            _keyUp = false;
            _pendingRelease = 0;
            _t = 0;
            _gain = _attack > 0.0005 ? 0 : _peak;

            // --- filter. The top of its travel is a fixed frequency however fast or slow
            // the sample plays - measured, see Cal.TopHz - held under what the device rate
            // can represent.
            _ceiling = Math.Min(Cal.TopHz, sampleRate * 0.45);
            _floor = Math.Min(Cal.FloorHz, _ceiling);
            _baseCutoff = Cal.CutoffHz(kg.ZoneFilter, sampleRate);

            double track = Clamp(kg.KeyToFilter, 0, 99) / Cal.KeyFull;
            double keyShift = (note - Cal.KeyPivot) / 12.0 * track;
            double velShift = ((vel - Cal.VelPivot) / 127.0) *
                              (Clamp(kg.VelToFilter, 0, 99) / 99.0) * Cal.VelOctaves;
            _cutoffShift = keyShift + velShift;

            _vcfAttack = kg.VcfWritten ? Cal.EnvSeconds(kg.VcfAttack) * Cal.VcfTimeScale : 0;
            _vcfSustain = kg.VcfWritten ? Clamp01(kg.VcfSustain / 99.0) : 1;
            _vcfDecay = kg.VcfWritten ? Cal.VcfDecaySeconds(kg.VcfDecay, _vcfSustain) : 0;
            _vcfRelease = kg.VcfWritten ? Cal.EnvSeconds(kg.VcfRelease) * Cal.VcfTimeScale : 0;
            _vcfDepth = kg.VcfWritten ? (kg.VcfAmount / 50.0) * Cal.EnvOctaves : 0;

            _vcfT = 0;
            _vcfReleasing = false;
            _vcfReleaseT = 0;

            _filter.Reset();
            _filter.SetCutoff(CutoffNow(), sampleRate);

            // --- the LFO
            double own = kg.LfoDepth * Cal.LfoDepthCentsPerUnit;
            _wheelCents = wheelCents;
            _lfoCents = own + wheelCents;
            _ownLfo = kg.LfoDesync;
            _lfoPhase = 0;
            _lfoStep = 2.0 * Math.PI *
                       (Cal.LfoRateHzAtZero + kg.LfoRate * Cal.LfoRateHzPerUnit) / sampleRate;
            _fadeSeconds = Cal.LfoDelayFadeConstant / Math.Max(1, 100 - kg.LfoDelay);
            _fadeT = 0;
            _sinceOn = 0;

            // Which output the keygroup goes to. Settled at the strike, like the peak: nothing
            // moves a sounding note from one socket to another on the machine either.
            Cal.OutputGains(kg.OutputPort, out _outL, out _outR);
        }

        /// <summary>
        /// Take up new settings without restarting the note.
        ///
        /// For a value changed while the note is sounding - a filter dragged, an
        /// envelope reshaped, a programme edited under a held MIDI loop. Everything
        /// that describes the note is recomputed; everything that says where the note
        /// has got to is left exactly as it is. So the sample goes on from where it
        /// was, the envelope stays in its stage, the LFO keeps its phase and the filter
        /// keeps its state - the last of those matters, because resetting a filter
        /// mid-note is a click.
        ///
        /// A keygroup now naming a different sample is not a change to this note, it is
        /// a different note; the position we are at would not mean the same thing in
        /// other audio. That waits for the next trigger.
        /// </summary>
        public void Adopt(KeygroupPatch kg)
        {
            if (_stage == Stage.Idle) return;
            if (kg == null || !ReferenceEquals(kg.Sound, _sound)) return;

            _kg = kg;

            // --- pitch. Changing transpose moves the playback rate under the position
            // we already hold, which is what transposing a sounding note means.
            double semis = kg.ZoneTranspose;
            if (!kg.ConstantPitch) semis += _note - _sound.RootPitch;

            // past the machine's fastest playback the note drops by octaves - Cal.MaxPlaybackHz
            double ratio = Cal.FoldedRatio(_sound.SourceRate, Math.Pow(2.0, semis / 12.0));
            _step = _sound.SourceRate / _sampleRate * ratio;

            // --- amplitude. The targets move; the gain walks to them from where it is.
            double depth = Clamp01(kg.VelToLoudness / 99.0);
            double velDb = -(127.0 - _velocity) * Cal.VelDbPerStep * depth;
            double zoneDb = kg.ZoneLoudness * Cal.LoudnessDbPerUnit;
            double sustainDb = SustainDbFor(kg.VcaSustain);

            _attack = Cal.VcaAttackSeconds(
                          Cal.VelocityAttackByte(kg.VcaAttack, kg.VelToAttack, _velocity));
            _decay = DecaySecondsFor(kg.VcaDecay, sustainDb);
            _release = Cal.EnvSeconds(
                           Cal.VelocityReleaseByte(kg.VcaRelease, kg.VelToRelease,
                                                   _velocity, kg.VelocityReleaseOn));
            _peak = Math.Min(Cal.DbToGain(velDb + zoneDb), 4.0) * _crossfade;
            _sustain = _peak * Cal.DbToGain(sustainDb);

            // --- filter
            _ceiling = Math.Min(Cal.TopHz, _sampleRate * 0.45);
            _floor = Math.Min(Cal.FloorHz, _ceiling);
            _baseCutoff = Cal.CutoffHz(kg.ZoneFilter, _sampleRate);

            double track = Clamp(kg.KeyToFilter, 0, 99) / Cal.KeyFull;
            double keyShift = (_note - Cal.KeyPivot) / 12.0 * track;
            double velShift = ((_velocity - Cal.VelPivot) / 127.0) *
                              (Clamp(kg.VelToFilter, 0, 99) / 99.0) * Cal.VelOctaves;
            _cutoffShift = keyShift + velShift;

            _vcfAttack = kg.VcfWritten ? Cal.EnvSeconds(kg.VcfAttack) * Cal.VcfTimeScale : 0;
            _vcfSustain = kg.VcfWritten ? Clamp01(kg.VcfSustain / 99.0) : 1;
            _vcfDecay = kg.VcfWritten ? Cal.VcfDecaySeconds(kg.VcfDecay, _vcfSustain) : 0;
            _vcfRelease = kg.VcfWritten ? Cal.EnvSeconds(kg.VcfRelease) * Cal.VcfTimeScale : 0;
            _vcfDepth = kg.VcfWritten ? (kg.VcfAmount / 50.0) * Cal.EnvOctaves : 0;

            // deliberately no _filter.Reset() - see above

            // --- the LFO keeps its phase and its place in the fade-in
            _lfoCents = kg.LfoDepth * Cal.LfoDepthCentsPerUnit + _wheelCents;
            _ownLfo = kg.LfoDesync;
            _lfoStep = 2.0 * Math.PI *
                       (Cal.LfoRateHzAtZero + kg.LfoRate * Cal.LfoRateHzPerUnit) / _sampleRate;
            _fadeSeconds = Cal.LfoDelayFadeConstant / Math.Max(1, 100 - kg.LfoDelay);
        }

        /// <summary>
        /// A note-off, as the machine takes one: the key is up now, and the release begins
        /// Cal.NoteOffLatencySeconds later.
        /// </summary>
        public void LetGo()
        {
            if (_stage == Stage.Idle || _stage == Stage.Release || _keyUp) return;

            _keyUp = true;
            _pendingRelease = Math.Max(1, (int)Math.Round(Cal.NoteOffLatencySeconds * _sampleRate));
        }

        /// <summary>Let go of the key, now. The note falls at its own release rate. What
        /// all-notes-off uses; a note-off from a player goes through LetGo.</summary>
        public void Release()
        {
            _keyUp = true;
            _pendingRelease = 0;

            if (_stage == Stage.Idle || _stage == Stage.Release) return;

            // A one-shot keygroup is a drum: it plays through whatever the key does.
            if (_kg.OneShot && !_sound.Loops) return;

            _releaseFrom = _gain;
            _stage = Stage.Release;
            _t = 0;

            // The filter lets go at the same moment, from wherever its own envelope had got
            // to - which is why that value is taken here rather than assumed to be sustain.
            _vcfReleaseFrom = VcfEnvelopeHeld(_vcfT);
            _vcfReleasing = true;
            _vcfReleaseT = 0;
        }

        public void Kill() { _stage = Stage.Idle; }

        /// <summary>
        /// Add this voice into the buffer.
        ///
        /// <paramref name="sharedPhase"/> is the programme's own LFO, which voices with the
        /// desync bit CLEAR ride instead of their own. That was measured rather than
        /// assumed: with the bit clear two voices held their phase to within 3 degrees over
        /// six seconds, and with it set they ran at rates 3.5% apart and drifted a whole
        /// turn in the same six.
        /// </summary>
        /// <summary>Mono, as it always was - every voice at full gain into one buffer.</summary>
        public void Render(float[] buffer, int offset, int count, double sharedPhase)
        {
            Render(buffer, null, offset, count, sharedPhase);
        }

        /// <summary>
        /// Stereo, with the keygroup's output port deciding which side it lands on.
        ///
        /// `right` null is the mono path and is bit-identical to what it always did: the pan
        /// gains are ignored and the sample goes into `left` at full level. Only a caller
        /// that actually has two channels pays for the second write.
        /// </summary>
        public void Render(float[] left, float[] right, int offset, int count, double sharedPhase)
        {
            if (_stage == Stage.Idle) return;

            // hoisted out of the sample loop: neither changes while a block renders
            bool stereo = right != null;
            float panL = stereo ? (float)_outL : 1f;
            float panR = stereo ? (float)_outR : 0f;
            float[] buffer = left;

            float[] audio = _sound.Audio;
            int last = audio.Length - 1;
            double dt = 1.0 / _sampleRate;

            int done = 0;
            while (done < count && _stage != Stage.Idle)
            {
                int n = Math.Min(ControlBlock, count - done);

                // A note-off waiting out its latency lets go at the block where the wait ends.
                if (_pendingRelease > 0)
                {
                    _pendingRelease -= n;
                    if (_pendingRelease <= 0) Release();
                }

                // --- the modulators, once per block
                _filter.SetCutoff(CutoffNow(), _sampleRate);

                double fade = _fadeSeconds > 0.0005
                            ? (_fadeT >= _fadeSeconds ? 1.0 : _fadeT / _fadeSeconds) : 1.0;
                double cents = _lfoCents * fade;
                double phase = _ownLfo ? _lfoPhase : sharedPhase;
                double bend = cents == 0 ? 1.0 : Math.Pow(2.0, cents * Math.Sin(phase) / 1200.0);

                /*
                 * WARP rides on top of the LFO, both as multipliers on the playback rate.
                 *
                 * It is worked out once a block like everything else here. The bend is fastest
                 * at the very start - a time constant of 34 ms at byte 14 = 0 - so a long
                 * control block will step down it in visible stairs rather than glide. That is
                 * the same trade the LFO already makes, and at the block sizes a host asks for
                 * it is well under a cent per step.
                 *
                 * Changing the playback rate drags the filter with it for free: the cutoff and
                 * the ceiling are both derived from the rate the audio leaves at.
                 */
                double warp = Cal.WarpRatio(_kg.WarpVelocity, _kg.WarpDepth, _kg.WarpTime,
                                            _velocity, _sinceOn);

                double step = _step * bend * warp * _wheelBend;

                double gainStart = _gain;
                double gainEnd = EnvelopeAfter(n * dt);
                double gainStep = (gainEnd - gainStart) / n;

                bool loops = _sound.Loops && _sound.LoopTo > _sound.LoopFrom;
                double end = loops ? _sound.LoopTo : audio.Length;

                for (int j = 0; j < n; j++)
                {
                    /*
                     * Round the loop, or off the end.
                     *
                     * The end to test against is the LOOP end, which for a sample whose
                     * loop runs to its last word is one frame past the last readable
                     * one. Testing against the array instead let the position sit in
                     * the gap between them, where it was neither wrapped nor finished,
                     * and the voice simply stopped after one pass. It cost three of the
                     * four failures the first run of EngineCheck reported.
                     */
                    /*
                     * An ALTERNATING loop turns round at each end instead of wrapping.
                     *
                     * Reflecting the position about the end - pos' = 2*end - pos - is what
                     * makes the cycle 2N frames with the end frame played twice, which is
                     * what the hardware does: four loop lengths from 20 to 128 frames all
                     * autocorrelated at exactly 2N, never 2N-2. Wrapping the other way, or
                     * reflecting about end-1, would give 2N-2 and be a tenth of a semitone
                     * flat on a short loop.
                     */
                    if (_dir > 0 && _pos >= end)
                    {
                        if (!loops) { _stage = Stage.Idle; return; }

                        if (_sound.Alternates)
                        {
                            _pos = 2 * end - _pos;
                            _dir = -1;
                            if (_pos < _sound.LoopFrom) _pos = _sound.LoopFrom;
                        }
                        else
                        {
                            double len = _sound.LoopTo - _sound.LoopFrom;
                            do { _pos -= len; } while (_pos >= end);
                            if (_pos < 0) _pos = _sound.LoopFrom;
                        }
                    }
                    else if (_dir < 0 && _pos < _sound.LoopFrom)
                    {
                        _pos = 2 * _sound.LoopFrom - _pos;
                        _dir = 1;
                        if (_pos >= end) _pos = end - 1;
                    }

                    // interpolate towards the next frame, which for the last one is
                    // wherever the loop restarts rather than off the end of the array
                    int i0 = (int)_pos;
                    if (i0 > last) i0 = last;
                    int i1 = i0 + 1;
                    if (i1 > last) i1 = loops ? _sound.LoopFrom : last;

                    double frac = _pos - i0;
                    double x = audio[i0] + (audio[i1] - audio[i0]) * frac;

                    float s = (float)(_filter.Process(x) * gainStart);
                    buffer[offset + done + j] += s * panL;
                    if (stereo) right[offset + done + j] += s * panR;
                    gainStart += gainStep;

                    _pos += step * _dir;
                }

                _gain = gainEnd;
                _t += n * dt;
                _fadeT += n * dt;
                _sinceOn += n * dt;

                // the filter's clock, which no amplitude stage change resets
                _vcfT += n * dt;
                if (_vcfReleasing) _vcfReleaseT += n * dt;
                _lfoPhase += _lfoStep * n;
                if (_lfoPhase > 2.0 * Math.PI) _lfoPhase -= 2.0 * Math.PI;

                AdvanceStage();
                done += n;
            }
        }


        /// <summary>
        /// The filter envelope while the key is still down: attack, decay, then sustain.
        /// </summary>
        double VcfEnvelopeHeld(double t)
        {
            if (t < _vcfAttack) return _vcfAttack > 0 ? t / _vcfAttack : 1;

            if (t < _vcfAttack + _vcfDecay)
                return _vcfDecay > 0 ? 1 - (1 - _vcfSustain) * ((t - _vcfAttack) / _vcfDecay)
                                     : _vcfSustain;
            return _vcfSustain;
        }

        /// <summary>
        /// Where the cutoff is now.
        ///
        /// On its own clock, and with a release. The release runs the envelope's contribution
        /// back to nothing in a straight line, the same shape the decay has - so the filter
        /// returns to the keygroup's own cutoff as the note dies rather than holding the
        /// brightness it happened to have when the key came up.
        /// </summary>
        double CutoffNow()
        {
            double env;

            // A fixed RATE, not a fixed time - see the note on Cal.VcfTimeScale. A release
            // from half depth takes half as long, because the envelope falls at the speed the
            // release byte sets rather than always arriving after it.
            if (_vcfReleasing)
                env = _vcfRelease > 0.0005
                    ? Math.Max(0, _vcfReleaseFrom - _vcfReleaseT / _vcfRelease)
                    : 0;
            else
                env = VcfEnvelopeHeld(_vcfT);

            double hz = _baseCutoff * Math.Pow(2.0, _cutoffShift + env * _vcfDepth);
            return hz > _ceiling ? _ceiling : (hz < _floor ? _floor : hz);
        }

        /// <summary>
        /// Where the amplitude envelope will be in <paramref name="ahead"/> seconds.
        ///
        /// The decay and the release are straight lines in DECIBELS, not in amplitude. That
        /// is measured: a stored decay of 80 fell 2.9, 2.7, 3.0, 3.0, 2.7, 2.9 dB per fifth
        /// of a second, dead constant for two and a half seconds. A line in amplitude hangs
        /// near the peak and then drops off a cliff, which is audibly another instrument.
        /// </summary>
        double EnvelopeAfter(double ahead)
        {
            double t = _t + ahead;

            switch (_stage)
            {
                case Stage.Attack:
                    return _attack > 0 ? _peak * Math.Min(1.0, t / _attack) : _peak;

                case Stage.Decay:
                    if (_decay <= 0.0005) return _sustain;
                    return Fall(_peak, _sustain, Math.Min(1.0, t / _decay));

                case Stage.Release:
                    if (_release <= 0.0005) return 0;

                    // A RATE: Cal.VcaReleaseDb in one release time, from wherever the key came
                    // up, and it keeps falling. This used to run from _releaseFrom to 1e-4 over
                    // exactly one release time, which is 80 dB from a note at full level - twice
                    // the machine's speed - and made a quiet note fade in the same wall-clock
                    // time as a loud one, which is a duration, not a rate.
                    return _releaseFrom * Math.Pow(10.0, -Cal.VcaReleaseDb / 20.0 * (t / _release));

                default:
                    return _sustain;
            }
        }

        /// <summary>A straight line in decibels from one gain to another.</summary>
        static double Fall(double from, double to, double u)
        {
            if (from <= 1e-6) return 0;
            double lo = Math.Max(to, 1e-6);
            return from * Math.Pow(lo / from, u);
        }

        void AdvanceStage()
        {
            switch (_stage)
            {
                case Stage.Attack:
                    if (_t >= _attack) { _stage = Stage.Decay; _t = 0; }
                    break;
                case Stage.Decay:
                    if (_t >= _decay) { _stage = Stage.Sustain; _t = 0; _gain = _sustain; }
                    break;
                case Stage.Release:
                    // The gain alone decides when the note is over. It used to end at
                    // _t >= _release as well, which with a RATE cuts the tail off after one
                    // release time however loud the note still is - audible as a click on
                    // anything with a long release.
                    if (_gain <= 2e-4) _stage = Stage.Idle;
                    break;
            }
        }

        static double Clamp(double v, double lo, double hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        static double Clamp01(double v) { return Clamp(v, 0, 1); }
    }
}
