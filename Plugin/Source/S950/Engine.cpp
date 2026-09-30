#include "Engine.h"

#include <cstring>

namespace s950
{
    Engine::Engine (double rate)
        : sampleRate (rate <= 0 ? 48000.0 : rate)
    {
        matched.reserve (Polyphony);

        // std::atomic's default constructor leaves the value wherever the memory was.
        for (int i = 0; i < ActivitySlots; ++i)
        {
            keygroupHits[i].store (0u, std::memory_order_relaxed);
            keygroupLastNote[i].store (-1, std::memory_order_relaxed);
        }
    }

    /*
     * WHY THE PATCH CHANGES HANDS THIS WAY
     *
     * The C# simply assigned a reference and let the garbage collector work out when the old
     * programme could go. There is no collector here, and the two obvious replacements are
     * both wrong in the same place:
     *
     *   - An atomic<Patch*> leaks, or frees while a voice is halfway through a sample.
     *   - A shared_ptr read on the audio thread copies, which touches a reference count and,
     *     if that copy turns out to be the last, frees every sample buffer in the programme
     *     inside the audio callback. That is a dropout waiting for the worst moment.
     *
     * So the pointer is moved, never copied, and only ever in one direction at a time. The
     * message thread puts a patch in `pending`; the audio thread moves it into `audioPatch`
     * and moves what was there into `retired`; the message thread takes `retired` away and
     * destroys it. Moving a shared_ptr touches no reference count and allocates nothing, so
     * the audio thread's half costs two pointer swaps.
     *
     * The audio thread declines to take a new patch while a retired one is still waiting to
     * be collected. That means a second programme change before the host's message thread
     * has run is simply held until it has - a programme arriving a few milliseconds late
     * being the whole cost of never freeing on the wrong thread.
     */
    bool Engine::trySetPatch (const PatchPtr& patch)
    {
        collectRetiredPatch();          // make room, if the audio thread has handed one back

        // The audio thread has not taken the last one yet: `pending` is still its to take,
        // and writing it now could land mid-take. Leave it; the caller will try again.
        if (pendingReady.load (std::memory_order_acquire))
            return false;

        // The shared LFO runs at the rate the programme's keygroups ask for. They almost
        // always agree; where they do not, the first one wins, since one shared oscillator
        // cannot be at two rates at once.
        //
        // Kept as the panel's 0..99 rather than as a step, because the Rate trim goes on top
        // of it every stretch - see renderSpan. Worked out once here, it used to leave the
        // Rate knob doing nothing at all to any keygroup riding the shared LFO.
        double rate = -1.0;             // no keygroup rides it
        if (patch != nullptr)
        {
            for (const auto& k : patch->keygroups)
            {
                if (k.lfoDesync) continue;

                rate = k.lfoRate;
                break;
            }
        }

        sharedRate = rate;              // read only by the audio thread; a double write is atomic enough here
        pending    = patch;             // a copy: the caller keeps its own reference
        pendingReady.store (true, std::memory_order_release);
        return true;
    }

    void Engine::collectRetiredPatch()
    {
        if (! retiredReady.load (std::memory_order_acquire))
            return;

        retired.reset();                // the frees happen HERE, on the message thread
        retiredReady.store (false, std::memory_order_release);
    }

    void Engine::takePendingPatch()
    {
        if (! pendingReady.load (std::memory_order_acquire))
            return;

        // Only if the last one has been collected - otherwise we would have nowhere to put
        // what we are replacing, and dropping it here would free it on this thread.
        if (retiredReady.load (std::memory_order_acquire))
            return;

        retired    = std::move (audioPatch);
        audioPatch = std::move (pending);

        pendingReady.store (false, std::memory_order_release);
        retiredReady.store (true,  std::memory_order_release);

        // Notes already sounding take up the new settings where they stand, rather than
        // waiting to be struck again.
        repatch();

        /*
         * A programme with a different SHAPE - more keygroups, fewer - has no last notes
         * yet: glide memory is per keygroup, and keygroup 3 of one programme is nothing to
         * do with keygroup 3 of another. The same shape keeps its memory: that is the Synth
         * tab re-rendering under a held line, and the line must go on gliding through it.
         */
        const int had = retired != nullptr ? static_cast<int> (retired->keygroups.size()) : -1;
        const int now = audioPatch != nullptr ? static_cast<int> (audioPatch->keygroups.size()) : -1;

        if (had != now)
            for (auto& memory : glideFrom)
                memory = GlideMemory {};
    }

    // ---------------------------------------------------------------------- the ring

    void Engine::post (unsigned char kind, int a, int b, int at)
    {
        const int w    = writeIndex.load (std::memory_order_relaxed);
        const int next = (w + 1) % RingSize;

        if (next == readIndex.load (std::memory_order_acquire))
            return;                     // full: drop it rather than block

        ring[w].kind = kind;
        ring[w].a    = static_cast<unsigned char> (a < 0 ? 0 : (a > 255 ? 255 : a));
        ring[w].b    = static_cast<unsigned char> (b < 0 ? 0 : (b > 255 ? 255 : b));
        ring[w].at   = static_cast<unsigned short> (at < 0 ? 0 : (at > 65535 ? 65535 : at));

        writeIndex.store (next, std::memory_order_release);
    }

    bool Engine::peekEvent (int& at, int count) const
    {
        const int r = readIndex.load (std::memory_order_relaxed);

        if (r == writeIndex.load (std::memory_order_acquire))
            return false;

        /*
         * Clamped into this block rather than carried over to the next.
         *
         * A host should never hand over an offset past the end of the block it came
         * with, but if one does, holding the event back would mean keeping state about
         * a block that has already gone. Late by a few samples beats lost.
         */
        at = ring[r].at;
        if (at > count - 1) at = count - 1;
        if (at < 0)         at = 0;

        return true;
    }

    void Engine::applyNextEvent()
    {
        const int r = readIndex.load (std::memory_order_relaxed);

        if (r == writeIndex.load (std::memory_order_acquire))
            return;

        const Event e = ring[r];
        readIndex.store ((r + 1) % RingSize, std::memory_order_release);

        switch (e.kind)
        {
            // Legato only if another key is still down once this one is counted.
            // A drum key is not a held key: it never joins the stack mono returns to, and
            // never asks for legato.
            case EvNoteOn:
                if (isDrumNote (e.a, e.b)) { startNote (e.a, e.b, false); break; }
                pressKey (e.a, e.b);
                startNote (e.a, e.b, heldCount > 1);
                break;

            case EvNoteOff:
                if (! isDrumNote (e.a, 127)) liftKey (e.a);
                stopNote (e.a);
                break;

            case EvWide:    wideOn = e.a != 0;           break;

            case EvVoices:
                voiceLimit = e.a < 1 ? 1 : (e.a > Polyphony ? Polyphony : e.a);
                break;
            case EvWheel:   wheel = e.a;                break;
            case EvPressure: pressure = e.a;            break;
            case EvBend:    bend14 = (e.a << 7) | e.b;  break;
            case EvGlide:   glideOn = e.a != 0;         break;

            case EvAllOff:
                heldCount = 0;
                for (int i = 0; i < Polyphony; ++i) releaseVoice (i, true);   // a panic: no latency
                break;

            default: break;
        }
    }

    /*
     * Hand every sounding voice its keygroup out of the patch now loaded.
     *
     * Matched by keygroup index rather than by key range: a voice belongs to the keygroup
     * that started it, and an edit may have moved the ranges under it.
     */
    void Engine::repatch()
    {
        if (audioPatch == nullptr)
        {
            // Nothing to point at any more. The voices hold a shared_ptr to their audio, so
            // they can finish what they are playing - but their keygroup is gone.
            for (auto& v : voices)
                if (v.isActive()) v.kill();

            return;
        }

        for (auto& v : voices)
        {
            if (! v.isActive()) continue;

            const int want = v.getKeygroupIndex();
            if (want < 0) continue;

            for (const auto& kg : audioPatch->keygroups)
            {
                if (kg.keygroupIndex != want) continue;
                if (v.getVelocity() < kg.velocityFrom || v.getVelocity() > kg.velocityTo) continue;

                v.adopt (kg);
                break;
            }
        }
    }

    void Engine::pressKey (int note, int velocity)
    {
        liftKey (note);                     // struck again: to the top, not in twice

        if (heldCount < 128)
        {
            heldNote[heldCount]     = note;
            heldVelocity[heldCount] = velocity;
            ++heldCount;
        }
    }

    void Engine::liftKey (int note)
    {
        for (int i = 0; i < heldCount; ++i)
        {
            if (heldNote[i] != note) continue;

            for (int j = i + 1; j < heldCount; ++j)
            {
                heldNote[j - 1]     = heldNote[j];
                heldVelocity[j - 1] = heldVelocity[j];
            }

            --heldCount;
            return;
        }
    }

    bool Engine::isDrumNote (int note, int velocity)
    {
        if (audioPatch == nullptr) return false;

        audioPatch->matching (note, velocity, matched);
        if (matched.empty()) return false;

        for (const KeygroupPatch* kg : matched)
            if (! (kg->constantPitch && kg->oneShot)) return false;

        return true;
    }

    void Engine::startNote (int note, int velocity, bool legato)
    {
        if (audioPatch == nullptr)
            return;

        // A drum plays as a drum whatever the polyphony says: outside mono, outside the
        // limit, every keygroup answering it. See isDrumNote.
        const bool drum = isDrumNote (note, velocity);

        audioPatch->matching (note, velocity, matched);

        const bool mono = voiceLimit == 1 && ! drum;
        const double glideTime = glideSeconds.load (std::memory_order_relaxed);

        /*
         * Mono legato: the one voice is still held, playing this keygroup's sample, so move
         * it to the new key instead of striking it again. The glide comes from the same
         * per-keygroup memory as ever - which is this voice, as it sounds right now.
         */
        if (mono && legato && ! matched.empty())
        {
            const KeygroupPatch* kg = matched.front();
            Voice& v = voices[0];

            if (v.isHeld() && v.getKeygroupIndex() == kg->keygroupIndex && v.getSound() == kg->sound)
            {
                const double from       = glideOrigin (kg->keygroupIndex);
                const double glideSemis = (glideOn && from >= 0.0) ? from - note : 0.0;

                v.legatoTo (note, glideSemis, glideTime);
                noteHit (kg->keygroupIndex, note);

                // A wide pair moves as one.
                const int twin = v.getPartner();
                if (twin > 0 && twin < Polyphony && voices[twin].getPartner() == 0)
                    voices[twin].legatoTo (note, glideSemis, glideTime);

                if (kg->keygroupIndex >= 0 && kg->keygroupIndex < GlideSlots)
                {
                    auto& memory    = glideFrom[kg->keygroupIndex];
                    memory.pitch    = note;
                    memory.voice    = 0;
                    memory.sequence = v.getStartedAt();
                }

                return;
            }
        }

        /*
         * The positional crossfade needs every keygroup answering this note at once, so the
         * ranges are gathered before any voice starts. With the flag off, or with one
         * keygroup answering, crossfadeGain returns 1 and nothing below changes.
         *
         * Fixed arrays rather than a vector: this is the audio thread, and one note can
         * start no more voices than the polyphony allows.
         */
        int fadeLow[Polyphony], fadeHigh[Polyphony];
        const int fadeCount = std::min (static_cast<int> (matched.size()), Polyphony);

        for (int m = 0; m < fadeCount; ++m)
        {
            fadeLow[m]  = matched[static_cast<size_t> (m)]->lowKey;
            fadeHigh[m] = matched[static_cast<size_t> (m)]->highKey;
        }

        int index = -1;

        for (const KeygroupPatch* kg : matched)
        {
            ++index;

            // One voice, one keygroup - and at full level, since the crossfade is a balance
            // between keygroups and there is no second one here to balance against.
            if (mono && index > 0) break;

            const double fade = (! mono && audioPatch->positionalCrossfade && index < fadeCount)
                                  ? cal::crossfadeGain (note, fadeLow, fadeHigh,
                                                        fadeCount, index)
                                  : 1.0;

            /*
             * What the two performance controllers add, in cents.
             *
             * The modwheel is byte 22 and channel pressure is byte 21, and the aftertouch
             * run measured them to be THE SAME MECHANISM from different sources. At full,
             * aftertouch gave 71.95 cents against the wheel's 72.3 - one constant, not
             * two. Byte 21 scales it proportionally, reading 0.511 of full at 50 where a
             * straight proportion is 0.505 and byte 22 gave 0.509. And the two ADD: wheel
             * alone read 71.87 cents, wheel and pressure together 149.79, where taking the
             * larger would have left it at 71.87.
             *
             * Byte 21 used to be read and dropped, on the grounds that it is 0 in all 1908
             * keygroups of one person's disks. Other people have other disks.
             */
            double wheelCents = cal::LfoWheelCentsAtFull
                                  * (kg->lfoModwheelDepth / 99.0)
                                  * (wheel / 127.0)
                              + cal::LfoWheelCentsAtFull
                                  * (kg->lfoAftertouchDepth / 99.0)
                                  * (pressure / 127.0);

            if (kg->lfoDepth * cal::LfoDepthCentsPerUnit + wheelCents < 0.5)
                wheelCents = 0.0;

            /*
             * The trims go on before start(), not after: start() works out the envelope from
             * them and primes the filter with the cutoff at time zero, so a voice that began
             * from untrimmed settings would attack wrongly and click its way to the right
             * ones.
             */
            /*
             * Where this keygroup glides from - asked BEFORE take(), which may steal the very
             * voice holding the answer. Per keygroup, so a layered programme's two keygroups
             * each slide from their own last note, and a split never slides across itself.
             */
            const double from       = glideOrigin (kg->keygroupIndex);
            const double glideSemis = (glideOn && from >= 0.0) ? from - note : 0.0;

            /*
             * A glide away from a note whose key is already up takes that note over, so its
             * tail fades rather than ringing on beside the glide as a unison. See
             * Voice::handOver. A note still held is left sounding: that is a chord.
             */
            if (glideSemis != 0.0 && glideTime > 0.0005
                && kg->keygroupIndex >= 0 && kg->keygroupIndex < GlideSlots)
            {
                const auto& memory = glideFrom[kg->keygroupIndex];

                if (memory.voice >= 0 && voices[memory.voice].isActive()
                                      && ! voices[memory.voice].isHeld()
                                      && voices[memory.voice].getStartedAt() == memory.sequence)
                {
                    voices[memory.voice].handOver();

                    // and its twin, if it was wide: both halves were the one note
                    const int twin = voices[memory.voice].getPartner();
                    if (twin >= 0 && twin < Polyphony && voices[twin].getPartner() == memory.voice)
                        voices[twin].handOver();
                }
            }

            Voice& v = take (drum);
            v.setTrims (trims.read());

            const int first = static_cast<int> (&v - voices);

            if (kg->keygroupIndex >= 0 && kg->keygroupIndex < GlideSlots)
            {
                auto& memory    = glideFrom[kg->keygroupIndex];
                memory.pitch    = note;
                memory.voice    = first;
                memory.sequence = sequence;
            }

            v.start (*kg, note, velocity, sampleRate, wheelCents, sequence++, fade,
                     glideSemis, glideTime);
            noteHit (kg->keygroupIndex, note);

            /*
             * Wide: the second half. take() cannot hand back `v` - it is the newest voice and
             * held, and there are always at least two voices to choose from when wide is on -
             * so this is always another voice. Started identically, then the pair is told
             * which half is which. A drum is left single: it has no pitch to detune.
             */
            if (wideOn && ! kg->constantPitch)
            {
                Voice& w = take (drum);
                w.setTrims (trims.read());
                w.start (*kg, note, velocity, sampleRate, wheelCents, sequence++, fade,
                         glideSemis, glideTime);

                const int second = static_cast<int> (&w - voices);
                const bool offset = wideOffset.load (std::memory_order_relaxed);

                v.makeWide (-1, second, false);
                w.makeWide (+1, first,  offset);
            }
        }
    }

    /*
     * The last note this keygroup played, as it sounds NOW if its voice is still ours and
     * still sounding - so a key struck mid-glide carries on from where the slide had got to
     * rather than jumping back to where it began. Otherwise the last key struck in it.
     */
    double Engine::glideOrigin (int keygroupIndex) const
    {
        if (keygroupIndex < 0 || keygroupIndex >= GlideSlots)
            return -1.0;

        const auto& memory = glideFrom[keygroupIndex];

        if (memory.voice >= 0 && voices[memory.voice].isActive()
                              && voices[memory.voice].getStartedAt() == memory.sequence)
            return voices[memory.voice].getPitchNow();

        return memory.pitch;
    }

    /*
     * Let go of every voice holding this note.
     *
     * Every one, not the first: pressing the same key twice before releasing it is ordinary,
     * and leaving the older voice held would strand it.
     */
    void Engine::stopNote (int note)
    {
        /*
         * Mono, letting go of the sounding key while others are still down: go back to the
         * newest of those, the way a monosynth does - legato if it is in the same keygroup,
         * gliding if glide is on, struck afresh at its own velocity if it is not.
         */
        int from = 0;

        if (voiceLimit == 1 && heldCount > 0 && voices[0].isHeld() && voices[0].getNote() == note)
        {
            startNote (heldNote[heldCount - 1], heldVelocity[heldCount - 1], true);
            from = 1;                       // voice 0 now plays the held key: leave it be
        }

        for (int i = from; i < Polyphony; ++i)
            if (voices[i].isActive() && voices[i].getNote() == note && voices[i].isHeld())
                releaseVoice (i);
    }

    /*
     * Let one voice go - and if it was still gliding, forget it as a place to glide from.
     *
     * A glide abandoned half way has no pitch worth starting from: the next note in that
     * keygroup plays at its own pitch, as though nothing had gone before. A glide that had
     * already ARRIVED is a finished note, and the next one still slides from it as usual.
     *
     * Only when this voice is the one the keygroup remembers. Letting go of an older note
     * of a chord says nothing about where the keygroup's line has got to.
     */
    void Engine::releaseVoice (int i, bool now)
    {
        Voice& v = voices[i];

        if (v.isHeld() && v.isGliding())
        {
            const int k = v.getKeygroupIndex();

            if (k >= 0 && k < GlideSlots && glideFrom[k].voice == i
                       && glideFrom[k].sequence == v.getStartedAt())
                glideFrom[k] = GlideMemory {};
        }

        // A note-off is taken the way the machine takes one: the key is up now, the
        // envelope lets go cal::NoteOffLatencySeconds later.
        if (now) v.release(); else v.letGo();
    }

    /*
     * A voice to start a note on.
     *
     * Idle first, obviously. Then the oldest one that has already been let go and is only
     * fading - taking that costs a tail nobody is listening to. Only if every voice is still
     * held does it take one of those, and then the oldest.
     *
     * Taking the oldest regardless, which the C# did at first, would cut off a key the player
     * is holding while a released note was left ringing beside it - which reads exactly like
     * "the release did not happen".
     */
    Voice& Engine::take (bool drum)
    {
        /*
         * Only the first so many voices are handed out for NOTES: one per note allowed, or
         * two when wide. Mono always plays voice 0 (and 1, wide), which is what lets
         * startNote find the note it may move legato.
         *
         * DRUMS take the voices above that - the seven a mono lead leaves idle - so a kick
         * never steals the lead and the lead never steals the kick. With no limit there is
         * nothing above it, and a drum is a note like any other.
         */
        const int limit = std::min (Polyphony, getNoteLimit() * (wideOn ? 2 : 1));
        const int from  = (drum && limit < Polyphony) ? limit : 0;
        const int to    = (drum && limit < Polyphony) ? Polyphony : limit;

        for (int i = from; i < to; ++i)
            if (! voices[i].isActive()) return voices[i];

        int pick = -1;
        for (int i = from; i < to; ++i)
        {
            if (voices[i].isHeld()) continue;                 // still down: leave it
            if (pick < 0 || voices[i].getStartedAt() < voices[pick].getStartedAt()) pick = i;
        }

        if (pick < 0)
        {
            pick = from;
            for (int i = from + 1; i < to; ++i)
                if (voices[i].getStartedAt() < voices[pick].getStartedAt()) pick = i;
        }

        killPair (pick);
        return voices[pick];
    }

    void Engine::killPair (int i)
    {
        const int twin = voices[i].getPartner();

        // Only a twin that still points back: a voice restarted since is no longer its pair.
        if (twin >= 0 && twin < Polyphony && voices[twin].getPartner() == i)
            voices[twin].kill();

        voices[i].kill();
    }

    int Engine::getActiveVoices() const
    {
        int n = 0;
        for (const auto& v : voices)
            if (v.isActive()) ++n;

        return n;
    }

    void Engine::renderSpan (float* left, float* right, int count)
    {
        if (count <= 0) return;

        // Read once for the whole stretch, so every voice in it is shaped by the same
        // settings and a control moved mid-block cannot land differently on two voices.
        const Trims now = trims.read();

        /*
         * The pitch wheel, as a multiplier on the playback rate, worked out once per stretch.
         *
         * Read here rather than at note-on because a bend has to reach notes that are already
         * sounding - that is the whole point of a wheel, and it is what separates it from
         * velocity, which is settled when the key goes down.
         *
         * 8192 is the rest position and the two halves are not the same width: 8192 steps
         * below it and 8191 above. Dividing by 8192 either way would make a full upward bend
         * fall one step short of the range, which is inaudible but wrong; dividing by the
         * right half of the range gets both ends exactly.
         */
        const double bendNow =
            cal::bendRatio (bend14, bendRange.load (std::memory_order_relaxed));

        // Wide's detune and spread, like the trims: once per stretch, so a knob reaches
        // notes already sounding. Voices that are not half of a pair ignore them.
        const double cents  = wideCents.load (std::memory_order_relaxed);
        const double spread = wideSpread.load (std::memory_order_relaxed);

        // The shared LFO's speed this stretch, which the voices riding it follow sample by
        // sample - see Voice::render - so it can run as fast as their own.
        const double sharedStep = sharedRate < 0.0 ? 0.0
            : 2.0 * 3.14159265358979323846 * lfo::rateHz (sharedRate, now.lfoRate) / sampleRate;

        for (auto& v : voices)
            if (v.isActive())
            {
                v.setTrims (now);
                v.setBend (bendNow);
                v.setWide (cents, spread);
                v.render (left, right, count, sharedPhase, sharedCycle, sharedStep);
            }

        // The shared LFO moves with the audio, so it advances per stretch rather than
        // once per block - otherwise splitting a block would change how it sounds. The
        // whole cycles are counted too: they are what S&H draws a new level on.
        sharedPhase += sharedStep * count;
        while (sharedPhase > 2.0 * 3.14159265358979323846)
        {
            sharedPhase -= 2.0 * 3.14159265358979323846;
            ++sharedCycle;
        }
    }

    /*
     * Fill the block, stopping at each event to do it where it belongs.
     *
     * The C# renders a block and then applies whatever arrived, because a keyboard and a
     * MIDI port have no finer timing to give it. A host does: every note comes with an
     * offset into the block it was handed with, and rounding those to the block boundary
     * is up to 11 ms of jitter at a 512-sample buffer.
     *
     * So the block is rendered in stretches between events. With nothing in the ring
     * that is one stretch and the same work as before; with a note at sample 200 of 512
     * it is two, and the note starts on sample 200.
     */
    void Engine::render (float* buffer, float* right, int count)
    {
        takePendingPatch();

        if (count <= 0) return;

        std::memset (buffer, 0, static_cast<size_t> (count) * sizeof (float));
        if (right != nullptr)
            std::memset (right, 0, static_cast<size_t> (count) * sizeof (float));

        int at = 0;
        while (at < count)
        {
            int next;

            // everything due by now, in the order it arrived
            while (peekEvent (next, count) && next <= at)
                applyNextEvent();

            // up to the next one, or to the end of the block
            int until = peekEvent (next, count) ? next : count;
            if (until <= at)  until = at + 1;      // never stand still
            if (until > count) until = count;

            renderSpan (buffer + at, right != nullptr ? right + at : nullptr, until - at);
            at = until;
        }

        const float g = gain.load (std::memory_order_relaxed);

        for (int i = 0; i < count; ++i)
        {
            const float v = buffer[i] * g;
            buffer[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);

            if (right != nullptr)
            {
                const float r = right[i] * g;
                right[i] = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
            }
        }
    }
}
