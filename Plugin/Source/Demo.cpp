#include "Demo.h"

#include <algorithm>
#include <cmath>

namespace demo
{
    // ================================================================================ Tap

    Tap::Tap() {}

    Tap::~Tap()
    {
        stop();
    }

    bool Tap::start (const juce::File& file, double sampleRate, int channels)
    {
        stop();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr) return false;

        juce::WavAudioFormat wavFormat;
        // 32-bit float, so a loud passage is kept rather than clipped; the recorder sets the
        // level of the finished video from it.
        auto w = wavFormat.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (sampleRate)
                                                        .withNumChannels (juce::jmax (1, channels))
                                                        .withBitsPerSample (32)
                                                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (w == nullptr) return false;

        writerThread.startThread();
        writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 18);

        rate = sampleRate;
        clock.store (0, std::memory_order_release);
        {
            const juce::SpinLock::ScopedLockType lock (midiLock);
            pending.clear();
        }
        running.store (true, std::memory_order_release);
        return true;
    }

    void Tap::stop()
    {
        running.store (false, std::memory_order_release);

        // Long enough for the block in flight to finish before the writer goes.
        juce::Thread::sleep (50);
        writer.reset();                                // flushes and closes the file
        writerThread.stopThread (2000);
    }

    void Tap::play (const juce::MidiMessageSequence& sequence)
    {
        const long long from = clock.load (std::memory_order_acquire);

        std::vector<std::pair<long long, juce::MidiMessage>> events;
        for (const auto* e : sequence)
            if (! e->message.isMetaEvent())
                events.emplace_back (from + (long long) std::llround (e->message.getTimeStamp() * rate), e->message);

        const juce::SpinLock::ScopedLockType lock (midiLock);
        for (auto& e : events) pending.push_back (std::move (e));
        std::stable_sort (pending.begin(), pending.end(),
                          [] (const auto& a, const auto& b) { return a.first < b.first; });
    }

    void Tap::addDueMidi (juce::MidiBuffer& midi, int count)
    {
        if (! isRunning()) return;

        const long long now = clock.load (std::memory_order_relaxed);
        const juce::SpinLock::ScopedTryLockType lock (midiLock);
        if (! lock.isLocked()) return;                 // the next block takes them, 1 block late

        size_t used = 0;
        for (; used < pending.size() && pending[used].first < now + count; ++used)
            midi.addEvent (pending[used].second, (int) juce::jlimit (0LL, (long long) count - 1, pending[used].first - now));

        if (used > 0) pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) used);
    }

    void Tap::capture (const juce::AudioBuffer<float>& buffer, int count)
    {
        if (! isRunning()) return;

        if (writer != nullptr)
            writer->write (buffer.getArrayOfReadPointers(), count);

        clock.fetch_add (count, std::memory_order_acq_rel);
    }

    // =========================================================================== Director

    namespace
    {
        double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

        juce::StringArray wordsOf (juce::String line)
        {
            line = line.upToFirstOccurrenceOf ("#", false, false).trim();
            juce::StringArray w;
            w.addTokens (line, " \t", "\"");
            w.removeEmptyStrings();
            for (auto& s : w) s = s.unquoted();
            return w;
        }

        juce::String srtTime (double t)
        {
            const auto ms = (long long) std::llround (juce::jmax (0.0, t) * 1000.0);
            return juce::String::formatted ("%02lld:%02lld:%02lld,%03lld",
                                            ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
        }
    }

    Director::Director (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& params, Tap& t,
                        Hooks h, juce::File s)
        : processor (p), parameters (params), tap (t), hooks (std::move (h)), script (s)
    {
        wav = script.withFileExtension (".wav");

        juce::StringArray lines;
        script.readLines (lines);

        for (const auto& line : lines)
        {
            auto w = wordsOf (line);
            if (w.isEmpty()) continue;

            const auto head = w[0].toLowerCase();
            if (head == "setup")      { w.remove (0); setup.push_back (w); }
            else if (head == "lead")  lead  = w[1].getDoubleValue();
            else if (head == "end")   endAt = w[1].getDoubleValue();
            else if (head == "keep")  keep  = true;
            else if (head == "at")    { Step st; st.at = w[1].getDoubleValue(); w.removeRange (0, 2); st.words = w; steps.push_back (st); }
        }

        std::stable_sort (steps.begin(), steps.end(), [] (const Step& a, const Step& b) { return a.at < b.at; });

        before = parameters.copyState();
        stageStarted = nowSeconds();
        startTimerHz (100);
    }

    Director::~Director()
    {
        stopTimer();
        if (tap.isRunning()) tap.stop();
    }

    juce::File Director::resolve (const juce::String& path) const
    {
        return juce::File::isAbsolutePath (path) ? juce::File (path) : script.getParentDirectory().getChildFile (path);
    }

    void Director::timerCallback()
    {
        const double wall = nowSeconds();

        switch (stage)
        {
            case Stage::settling:
                // The window has to be up and the audio running before anything is filmed.
                if (wall - stageStarted < 1.5) return;
                for (const auto& w : setup) run (w, 0.0);
                stage = Stage::leading;
                stageStarted = wall;
                return;

            case Stage::leading:
                if (wall - stageStarted < lead) return;
                if (! tap.start (wav, processor.getSampleRate(), processor.getTotalNumOutputChannels()))
                {
                    stage = Stage::done;
                    return;
                }
                if (hooks.flash) hooks.flash();
                stage = Stage::running;
                return;

            case Stage::running:
            {
                const double now = tap.seconds();

                while (next < steps.size() && steps[next].at <= now)
                    run (steps[next++].words, now);

                for (auto it = ramps.begin(); it != ramps.end();)
                {
                    const double f = it->length > 0.0 ? juce::jlimit (0.0, 1.0, (now - it->start) / it->length) : 1.0;
                    const float  v = it->from + (float) f * (it->to - it->from);
                    it->p->setValueNotifyingHost (it->p->convertTo0to1 (v));
                    it = f >= 1.0 ? ramps.erase (it) : it + 1;
                }

                if (now >= endAt) finish();
                return;
            }

            case Stage::finishing:
                if (wall - stageStarted < 0.6) return;
                stage = Stage::done;
                stopTimer();
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
                return;

            case Stage::done:
                return;
        }
    }

    void Director::run (const juce::StringArray& w, double now)
    {
        if (w.isEmpty()) return;
        const auto cmd = w[0].toLowerCase();

        if (cmd == "load" && hooks.loadDisk)             hooks.loadDisk (resolve (w[1]));
        else if (cmd == "program" && hooks.selectProgram) hooks.selectProgram (w[1]);
        else if (cmd == "tab" && hooks.showTab)
        {
            const auto t = w[1].toLowerCase();
            hooks.showTab (t.startsWith ("perf") ? 1 : t.startsWith ("syn") ? 2 : 0);
        }
        else if (cmd == "keygroup" && hooks.chooseKeygroup)
            hooks.chooseKeygroup (w[1].equalsIgnoreCase ("all") ? -1 : w[1].getIntValue() - 1);
        else if (cmd == "page" && hooks.showProgramPage)  hooks.showProgramPage (w[1]);
        else if (cmd == "preset" && hooks.choosePreset)   hooks.choosePreset (w[1]);
        else if (cmd == "drums" && hooks.setDrums)        hooks.setDrums (w[1].equalsIgnoreCase ("on"));
        else if (cmd == "midi")
        {
            juce::FileInputStream in (resolve (w[1]));
            juce::MidiFile file;
            if (in.openedOk() && file.readFrom (in))
            {
                file.convertTimestampTicksToSeconds();
                juce::MidiMessageSequence all;
                for (int i = 0; i < file.getNumTracks(); ++i)
                    all.addSequence (*file.getTrack (i), 0.0);
                all.updateMatchedPairs();
                tap.play (all);
            }
        }
        else if (cmd == "set" || cmd == "ramp")
        {
            if (auto* p = parameters.getParameter (w[1]))
            {
                if (cmd == "set")
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) w[2].getDoubleValue()));
                else
                    ramps.push_back ({ p, (float) w[2].getDoubleValue(), (float) w[3].getDoubleValue(),
                                       now, w[4].getDoubleValue() });
            }
        }
        else if (cmd == "caption")
        {
            if (! captions.empty() && captions.back().to > now) captions.back().to = now;
            const double length = w.size() > 2 ? w[2].getDoubleValue() : 1.0e9;
            captions.push_back ({ now, now + length, w[1] });
        }
        else if (cmd == "notesoff")
        {
            juce::MidiMessageSequence off;
            off.addEvent (juce::MidiMessage::allNotesOff (1), 0.0);
            tap.play (off);
        }
    }

    void Director::finish()
    {
        for (auto& c : captions) c.to = juce::jmin (c.to, endAt);
        writeCaptions();

        tap.stop();
        ramps.clear();
        if (before.isValid() && ! keep) parameters.replaceState (before);
        stage = Stage::finishing;
        stageStarted = nowSeconds();
    }

    void Director::writeCaptions() const
    {
        juce::String srt;
        int n = 0;
        for (const auto& c : captions)
        {
            if (c.text.isEmpty() || c.to <= c.from) continue;
            srt << ++n << "\n" << srtTime (c.from) << " --> " << srtTime (c.to) << "\n" << c.text << "\n\n";
        }
        script.withFileExtension (".srt").replaceWithText (srt);
    }
}
