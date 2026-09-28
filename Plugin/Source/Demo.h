#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

/*
 * DEMO MODE - a scripted run of the standalone, for recording a video of it.
 *
 *     Mz950.exe --demo script.txt
 *
 * Only the standalone looks for the flag; the plugin never does. Plugin/Demo/record-demo.ps1
 * starts it, films the window with ffmpeg, and puts the two together - see the script there
 * for the format of script.txt.
 *
 * The audio is not captured from the sound card. The processor's own output is written to a
 * WAV as it is rendered (Tap), and the script's MIDI goes into the processor at exact sample
 * positions, so the recording has no latency, no drift and nothing else on the machine in it.
 * The window flashes white at the instant the WAV starts, which is how the recorder lines the
 * film up with it; every step of the script is timed against the audio clock too, so what the
 * window shows and what the WAV holds stay in step for the whole run.
 */
namespace demo
{
    /// The audio-thread half: scheduled MIDI in, rendered audio out to a WAV.
    class Tap
    {
    public:
        Tap();
        ~Tap();

        /// Message thread. The WAV starts with the next block rendered.
        bool start (const juce::File& wav, double sampleRate, int channels);
        void stop();

        bool   isRunning() const { return running.load (std::memory_order_acquire); }
        double seconds()   const { return rate > 0.0 ? (double) clock.load (std::memory_order_acquire) / rate : 0.0; }

        /// Message thread: play a MIDI file's events from now on the audio clock.
        void play (const juce::MidiMessageSequence& sequence);

        /// Audio thread, before the block's messages are read: add the ones now due.
        void addDueMidi (juce::MidiBuffer& midi, int count);

        /// Audio thread, after rendering: the block into the WAV, and the clock on.
        void capture (const juce::AudioBuffer<float>& buffer, int count);

    private:
        std::atomic<bool>      running { false };
        std::atomic<long long> clock   { 0 };
        double                 rate    = 0.0;

        juce::TimeSliceThread writerThread { "Mz950 demo writer" };
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;

        juce::SpinLock midiLock;
        std::vector<std::pair<long long, juce::MidiMessage>> pending;    // by sample, in order
    };

    /// What the director can make the window do, supplied by the editor.
    struct Hooks
    {
        std::function<void (int)>                  showTab;          // 0 Program, 1 Perform, 2 Synth
        std::function<bool (const juce::File&)>    loadDisk;
        std::function<bool (const juce::String&)>  selectProgram;
        std::function<void (int)>                  chooseKeygroup;   // -1 for all of them
        std::function<bool (const juce::String&)>  showProgramPage;  // "filter", "tuning", ...
        std::function<bool (const juce::String&)>  choosePreset;     // a Synth-tab preset by name
        std::function<void (bool)>                 setDrums;         // the Synth tab's drums switch
        std::function<void()>                      flash;            // the white sync frame
    };

    /*
     * The message-thread half: reads the script and runs it against the Tap's clock.
     *
     * One command a line; # starts a comment; text with spaces goes in "quotes".
     *
     *     setup <command>              before the recording starts (load, program, tab, ...)
     *     lead <seconds>               film to run before the flash (default 3)
     *     at <seconds> <command>       on the audio clock, from the flash
     *     end <seconds>                stop, write the WAV and captions, and quit
     *     keep                         leave the controls as the script left them (by
     *                                  default they are put back as they were)
     *
     * The commands:
     *
     *     load <disk>                  a .hfe or .img; relative to the script's folder
     *     program <name>               a programme on the disk
     *     tab program|perform|synth
     *     keygroup <n>|all             on the Program tab
     *     page <name>                  a Program-tab page: envelopes, filter, lfo, ...
     *     preset <name>                a Synth-tab preset
     *     drums on|off                 the Synth tab's drum kit
     *     midi <file>                  play a MIDI file from this moment
     *     set <parameter> <value>      a Perform-tab control, in its own units
     *     ramp <parameter> <from> <to> <seconds>
     *     caption "<text>" [<seconds>] shown until the next caption, or for that long
     *     notesoff                     all notes off
     */
    class Director : private juce::Timer
    {
    public:
        Director (juce::AudioProcessor&, juce::AudioProcessorValueTreeState&, Tap&, Hooks, juce::File script);
        ~Director() override;

    private:
        struct Step { double at = 0.0; juce::StringArray words; };
        struct Ramp { juce::RangedAudioParameter* p; float from, to; double start, length; };
        struct Caption { double from, to; juce::String text; };

        void timerCallback() override;
        void run (const juce::StringArray& words, double now);
        juce::File resolve (const juce::String& path) const;
        void finish();
        void writeCaptions() const;

        juce::AudioProcessor& processor;
        juce::AudioProcessorValueTreeState& parameters;
        Tap&  tap;
        Hooks hooks;
        juce::File script, wav;

        // The controls as they were before the script ran, put back when it ends - the
        // standalone saves its settings on the way out, and a demo must not leave its gain
        // and glide behind in them.
        juce::ValueTree before;
        bool keep = false;          // "keep" in the script: leave the settings as it made them

        std::vector<juce::StringArray> setup;
        std::vector<Step>    steps;
        std::vector<Ramp>    ramps;
        std::vector<Caption> captions;
        double lead = 3.0, endAt = 30.0;
        size_t next = 0;

        enum class Stage { settling, leading, running, finishing, done } stage = Stage::settling;
        double stageStarted = 0.0;
    };
}
