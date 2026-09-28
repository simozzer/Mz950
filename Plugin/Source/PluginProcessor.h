#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "S950/Engine.h"
#include "S950/Disk.h"
#include "S950/SynthPatch.h"
#include "Demo.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

/*
 * The plugin.
 *
 * Thin on purpose. Everything that decides how the instrument sounds is in Source/S950,
 * which knows nothing about JUCE and is checked against the C# engine it was ported from.
 * What is left here is the three things a host actually needs: give me a block, here are
 * the notes that happen inside it, and remember this when you save.
 */
class VirtualS950Processor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    VirtualS950Processor();
    ~VirtualS950Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    /*
     * The disk's programmes, as the host's own program list.
     *
     * This is what surfaces them outside this window. The wrapper turns the list into a
     * "Program" parameter, which Ableton shows in the device itself - unfold it, or hit
     * Configure to put it on the panel - so a programme can be picked, mapped to a macro,
     * drawn on an automation lane or sent as a MIDI program change, none of which needs the
     * plugin window open.
     *
     * The count is fixed and the names are not. getNumPrograms says why that is the only
     * arrangement a host can actually see.
     *
     * It is the same list the combo box shows, from the same place; neither is a copy.
     */
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ----------------------------------------------------------------- for the editor

    juce::AudioProcessorValueTreeState parameters;

    /// How many voices are sounding. Read by the editor on a timer; approximate by nature.
    int getActiveVoices() const;

    /// What is loaded, for the editor to name.
    juce::String getPatchName() const;

    /*
     * One keygroup's envelope, for the editor to draw.
     *
     * A programme's keygroups each have their own, and the controls move all of them
     * together - so a graph can only ever show one of them. This is the first keygroup that
     * has a sample, which is the one a picture of "the" envelope should be of: it is what
     * the lowest keys play, and in most of the library the keygroups agree anyway.
     *
     * Message thread only, and a snapshot: the editor redraws from it rather than holding a
     * pointer into a patch that may be replaced under it.
     */
    struct EnvelopeShape
    {
        int  attack = 0, decay = 0, sustain = 99, release = 0;

        /// False for an S900 programme, whose four filter bytes were never written.
        bool written = true;

        /// Both envelopes have all four stages now. Kept so the graph asks rather than
        /// assumes, since it was not always true.
        bool hasRelease = true;
    };

    EnvelopeShape getEnvelope (bool filter) const;

    // ------------------------------------------------------------------------- disks

    /*
     * Open an image and load its first programme.
     *
     * Message thread only. Decoding a disk allocates and takes long enough to matter, which
     * is exactly why the engine takes a finished patch rather than a disk: none of this can
     * happen anywhere near the audio thread.
     */
    bool loadDisk (const juce::File& file, juce::String& error);

    /// The programmes on the disk that is open, in directory order.
    juce::StringArray getProgramNames() const;

    /// Play one of them. Out of range does nothing.
    void selectProgram (int index);

    int getSelectedProgram() const { return selectedProgram; }

    /// The disk that is open, or an empty string.
    juce::String getDiskName() const;

    /*
     * Where that disk was read from, or an empty string.
     *
     * The editor opens its file browser next to it, the next disk being nearly always on
     * the same shelf as the last. Message thread only: this is written when a disk is
     * loaded and when the host restores a saved set, both of which happen there.
     */
    juce::String getDiskPath() const { return diskPath; }

    /// How the recovery went, for a disk read out of an .hfe. Zero for a plain image.
    int getBadSectors() const;
    int getMissingSectors() const;

    /*
     * Bumped whenever the loaded disk or programme changes, however it changed.
     *
     * The editor watches this on the timer it already runs. Without it, a set restored by
     * the host would leave an editor that had been built first showing an empty list, and
     * an editor built afterwards showing the right one - which is the same class of bug as
     * the list not surviving a window being closed.
     */
    int getDiskGeneration() const { return diskGeneration.load (std::memory_order_relaxed); }

    // --------------------------------------------------------- editing the programme

    /*
     * The loaded programme's keygroups, read and changed in place on the disk image.
     *
     * Absolute values in the panel's own units - the PROGRAM half of the window - as
     * against the trims, which are offsets and never touch the disk. An edit is written into
     * the image the plugin holds, so it is saved with the host's project like everything
     * else about the disk, and goes out with it when the disk is saved as a file.
     *
     * `keygroup` is 0-based; -1 means every keygroup of the programme at once. All of this is
     * message thread only. Nothing here is available with no disk loaded: the placeholder
     * saw is not on a disk and has nowhere to write to.
     */
    int  getKeygroupCount() const;
    int  getKeygroupValue (int keygroup, s950::KeygroupParam p) const;
    void setKeygroupValue (int keygroup, s950::KeygroupParam p, int value);

    /// An S900 programme's filter envelope, left as four spaces. See Disk::vcfBlank.
    bool isVcfBlank (int keygroup) const;

    /// The sample a keygroup's zone plays - 1 soft, 2 loud - or an empty string.
    juce::String getZoneSample (int keygroup, int zone) const;

    /// Bumped by every edit, so the window can redraw what it shows.
    int getEditRevision() const { return editRevision; }

    /*
     * What a keygroup is doing right now, for the strip that lights them up as they play.
     *
     * `hits` counts the notes it has answered since the engine was built - a window keeps
     * the last count it saw and flashes the keygroup when the count moves, which catches a
     * note too short to be seen sounding. `sounding` is whether a voice is playing it this
     * instant; `lastNote` is the key it last answered. Approximate by nature, like the
     * voice count.
     */
    struct KeygroupActivity { unsigned hits = 0; int lastNote = -1; bool sounding = false; };
    KeygroupActivity getKeygroupActivity (int keygroup) const;

    /// Whether a key is held down over MIDI right now - for the dots on the Program tab's
    /// keyboard. Written on the audio thread, read on the message thread, one bit a key.
    /// Demo mode's audio half - scheduled MIDI in, rendered audio out. Idle unless the
    /// standalone was started with --demo. See Demo.h.
    demo::Tap demoTap;

    bool isNoteHeld (int note) const
    {
        if (note < 0 || note > 127) return false;
        return ((heldNotes[note >> 6].load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0;
    }

    /// Which tab the window last showed - 0 Program, 1 Perform, 2 Synth - so reopening it,
    /// which a host does every time the window is closed, comes back where it was. Not saved.
    int editorTab = 0;

    // ---------------------------------------------------------------- the synth

    /*
     * The Synth tab's recipe, and its rendering to a disk. See S950/SynthPatch.h.
     *
     * Rendering a full kit takes up to half a second, so it runs on a thread of its own
     * and lands through the timer; the tab says "rendering" meanwhile. A knob turned
     * while one is running queues one more, with the newest recipe, so a knob dragged
     * fast costs two renders rather than fifty. The rendered disk then replaces whatever
     * disk was loaded - it is a disk like any other from there on.
     *
     * Message thread, all of it.
     */
    const s950::synth::Recipe& getRecipe() const { return recipe; }

    /// Keep this recipe, and render it shortly.
    void setRecipe (const s950::synth::Recipe& r);

    bool isRendering() const { return renderThread != nullptr; }
    juce::String getRenderError() const { return renderError; }

    /// True while the loaded disk is the synth's own rendering rather than a file.
    bool diskIsSynth() const { return synthDisk; }

    /*
     * The disk as it now stands, edits and all, written as a plain sector image - which is
     * what a Gotek or an HxC emulator, the Studio, and this plugin all open.
     */
    bool saveDiskAs (const juce::File& file, juce::String& error) const;

private:
    /// The directory entry of the programme that is playing, or nullptr.
    const s950::Disk::Entry* selectedEntry() const;

    /*
     * Give the engine `patch`, now or on a later timer tick.
     *
     * The engine refuses while it has not yet taken the last one - see
     * Engine::trySetPatch - so this remembers that one is owed and the timer pays it. The
     * newest patch is always the one handed over, so a burst of edits costs one exchange.
     */
    void sendPatch();
    bool patchOwed = false;

    int editRevision = 0;

    // the synth's rendering, see setRecipe
    s950::synth::Recipe    recipe;
    s950::synth::WaveCache waveCache;       // touched only by the render thread
    bool   synthDisk    = false;
    bool   renderWanted = false;

    // The keys held over MIDI, one bit each: set on note-on, cleared on note-off and by
    // all-notes-off. See isNoteHeld.
    std::atomic<std::uint64_t> heldNotes[2] { {0}, {0} };

    void holdNote (int note, bool down)
    {
        if (note < 0 || note > 127) return;
        const std::uint64_t bit = std::uint64_t (1) << (note & 63);
        if (down) heldNotes[note >> 6].fetch_or  (bit,  std::memory_order_relaxed);
        else      heldNotes[note >> 6].fetch_and (~bit, std::memory_order_relaxed);
    }
    double renderDueAt  = 0.0;

    std::unique_ptr<std::thread> renderThread;
    std::atomic<bool>            renderDone { false };
    std::unique_ptr<s950::Disk>  rendered;
    std::string                  renderFailure;
    juce::String                 renderError;

    void startRender();
    void finishRender();

    void timerCallback() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout describeParameters();

    /*
     * A sound to play until disks can be read.
     *
     * Eight cycles of a sawtooth, looped, so a key transposes it the way a keygroup
     * transposes a sample. It is a placeholder and says so in the editor - but it means the
     * whole chain from the host's MIDI to the speakers can be judged before AkaiS950List is
     * ported, which is worth more than an instrument that is silent until everything works.
     */
    static s950::PatchPtr makePlaceholderPatch();

    /*
     * The engine is rebuilt when the host changes the sample rate, because a voice works
     * out its pitch and its filter from the rate it was started at.
     */
    std::unique_ptr<s950::Engine> engine;
    s950::PatchPtr               patch;

    /*
     * The disk that is open, if one is.
     *
     * Kept whole rather than only the programme built from it, so the programme can be
     * changed without reading the file again - and so that saving the plugin's state can
     * one day put the entire 800K image in the host's project, which is what stops a saved
     * song from ever losing the sound it was made with.
     */
    /*
     * Take a disk that is already in memory - from a file just read, or from a host's
     * saved project. Fills in the programme list and plays the first one.
     */
    bool adoptDisk (std::unique_ptr<s950::Disk> opened, juce::String& error);

    std::unique_ptr<s950::Disk> disk;
    juce::String                diskPath;        // where it came from, for the label only
    juce::StringArray           programNames;
    int                         selectedProgram = -1;
    std::atomic<int>            diskGeneration { 0 };

    /*
     * True while the host is the one choosing.
     *
     * A programme the host asked for does not need announcing back to it, and announcing it
     * would mean editing the parameter while the host is still inside the call that set it.
     * A change made in this plugin's own window has no such caller, and does need announcing.
     */
    bool                        hostIsChoosing = false;

    /// The host chose an empty slot; the timer puts its chooser back. See setCurrentProgram.
    std::atomic<bool>           hostProgramOutOfStep { false };

    /// The engine renders one channel; the host usually wants two. Sized in prepareToPlay,
    /// because processBlock is not allowed to allocate.
    juce::AudioBuffer<float> mono;

    std::atomic<float>* gainParameter = nullptr;
    std::atomic<float>* bendRangeParameter = nullptr;

    /*
     * Portamento, which the machine never had - see Engine::glide.
     *
     * Not rows in trimControls: those are offsets that land in the engine's trims, and these
     * are an absolute switch and an absolute time with nothing on the disk to offset.
     */
    std::atomic<float>*         glideParameter     = nullptr;
    std::atomic<float>*         glideTimeParameter = nullptr;
    juce::RangedAudioParameter* glideControl       = nullptr;
    juce::RangedAudioParameter* glideTimeControl   = nullptr;

    /// What the engine was last told glide is: 0, 1, or -1 for not yet. Audio thread only.
    int glidePosted = -1;

    /// The voice count, 1..8 (1 is mono), and what the engine was last told it is.
    std::atomic<float>*         voicesParameter = nullptr;
    juce::RangedAudioParameter* voicesControl   = nullptr;
    int                         voicesPosted    = -1;

    /// Wide, which the machine never had either - see Engine::wide.
    std::atomic<float>*         wideParameter       = nullptr;
    std::atomic<float>*         wideDetuneParameter = nullptr;
    std::atomic<float>*         wideSpreadParameter = nullptr;
    std::atomic<float>*         wideOffsetParameter = nullptr;
    juce::RangedAudioParameter* wideControl         = nullptr;
    juce::RangedAudioParameter* wideDetuneControl   = nullptr;
    juce::RangedAudioParameter* wideSpreadControl   = nullptr;
    juce::RangedAudioParameter* wideOffsetControl   = nullptr;
    int                         widePosted          = -1;

    void applySwitch (juce::RangedAudioParameter* p, bool on);

    /*
     * One player's control: a parameter, the continuous controller that moves it, and the
     * engine field it ends up in.
     *
     * All three in one row on purpose. These have to agree - a parameter nothing reads, or
     * an engine field nothing writes, is a control that silently does nothing - and keeping
     * them apart is how that happens. Adding another means adding a row.
     *
     * The controller numbers are the General MIDI sound-controller assignments, so a
     * keyboard with knobs labelled "cutoff" and "attack" reaches the right ones with no
     * mapping: 72-79 are the standard set - including 76, 77 and 78 for vibrato rate, depth
     * and delay, which is what this machine's LFO is - and 102-105 are undefined ones taken
     * for the filter envelope, which GM has no numbers for.
     */
    struct TrimControl
    {
        int         cc;
        const char* id;

        /// Which field of the engine's trims this one lands in.
        std::atomic<float> s950::Engine::AtomicTrims::* target;

        /// Filled in by the constructor, from the parameter tree.
        std::atomic<float>*         value   = nullptr;
        juce::RangedAudioParameter* control = nullptr;
    };

    static constexpr int NumTrims = 15;
    TrimControl trimControls[NumTrims];

    /*
     * A continuous controller moving a parameter, from the audio thread.
     *
     * Bipolar: 0 is the bottom of the range, 127 the top, and 64 - where a controller's
     * centre detent sits - is close enough to zero to count as "as the disk has it".
     */
    void applyController (juce::RangedAudioParameter* p, int value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualS950Processor)
};
