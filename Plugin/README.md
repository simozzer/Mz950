# Mz950 — the plugin

The S950-compatible engine as a VST3, so a programme off a real Akai disk can be played from a
DAW. Mz950 is independent and not affiliated with Akai or inMusic.

It was called VirtualS950 before the rename, and it is still the same plugin to a host. The
plugin's identity codes (`Smoz` / `Ak95` in `CMakeLists.txt`) did not change, so Live sets saved
with VirtualS950 open with Mz950 in its place. **Never change those two codes.**

## Using it

The installer puts the VST3 in the folder hosts scan and the standalone player in the
program group. If your DAW has already scanned its plugin folders, tell it to rescan;
**Mz950** then appears under instruments.

**Load a disk.** Press **Load disk…** and pick an `.hfe` or `.img` (800K or 1600K HD) in
the Windows file dialog. It belongs to the plugin window, so it opens on the same monitor as
the plugin and stays on top of it, even when the host keeps plugin windows always on top.
It starts beside the disk already open, or the folder a disk was last chosen from, or the
sound library the installer left on the machine, whichever it finds first.

**Change programme** either in the plugin's own box or in the host's program selector — the
disk's programmes are offered to the host as the plugin's programs, so in Ableton Live the
Program chooser in the device title bar changes sound without opening the window.

**A saved song carries the disk.** The whole 800K image goes into the project, gzipped, not
a path to it: a few hundred kilobytes beside the audio a session already holds, and a set
that can never lose the sound it was made with. Move the image, rename it, or open the song
on another machine — it still plays. The programme comes back by name first and by position
second, so a disk edited and reordered since still returns what you meant.

**Three tabs: PROGRAM, PERFORM and SYNTH.** They separate the kinds of control, and the
colours say which kind a control is: amber is on the disk, cyan is an offset on top of it,
violet is something the S950 never had.

![The Program tab](../docs/img/plugin-program.png)

- **PROGRAM is what's on the disk.** Every setting of every keygroup, as absolute values in the
  S950's own units and ranges, so it reads like the machine's front panel. A strip across the
  keyboard shows the keygroups (overlapping ones stacked); click one to edit it, or choose
  *All keygroups*. Six small pages hold the settings: Envelopes, Filter, LFO, Velocity, Tuning,
  and Keys & output. An edit is written into the plugin's copy of the disk image, so it's saved
  with the project (Live asks to save after an edit) and held notes follow it as you turn the
  knob. *Save disk as…* writes the edited disk as a plain `.img` for the Studio, this plugin, or
  a Gotek or HxC floppy emulator. When a Perform offset is moving a setting, the control shows
  what is actually sounding underneath it in blue, e.g. **→ 52**.
  The strip lights up as you play: a keygroup that answers a note gets brighter, with a rim
  and a marker at the key, so you can see which keygroup to adjust.
- **PERFORM is what you do on top of it:** the offsets below, plus glide, polyphony and Wide.
  None of these is ever written to the disk.

  ![The Perform tab](../docs/img/plugin-perform.png)
- **SYNTH makes sounds with no disk at all** — and makes them *as a disk*. Three oscillators
  (sine, triangle, saw, square, pulse, organ, glass, buzz, hollow, three FM ratios, two ring
  modulators, a bend, phase distortion, three noise colours), each with level, octave, fine
  tune, start phase, a *shape* control (pulse width, FM index, ring amount, bend, skew, or a
  morph toward a sine) and a *sweep* (how much of that travels across the loop and back:
  pulse-width modulation, an FM sweep, a filter-like morph, baked into the sample the way a
  sampler has always done it). Each oscillator is drawn as a band-limited looped sample and
  becomes a keygroup layered across the keyboard; detune is two layers a few cents apart.
  An optional kit — kick, snare, clap, closed and open hat, ride, three toms — puts one-shots
  on General MIDI's notes (36–51), each with tune, decay, tone and level, and the oscillators
  move up to start at E2. Ten presets to start from. A change re-renders to a blank disk
  held in memory, which becomes the loaded disk: the Program tab edits it (and those edits
  survive a re-render), the set saves it, and *Save disk as…* writes it for a real S950.
  Every wave and drum is held to the C# `AkaiS950Synth` tool word for word by
  `synthcheck.ps1`; the disk it builds is read identically by the C# library.

  ![The Synth tab](../docs/img/plugin-synth.png)

Program edits change only the values of existing settings, one byte each. They never add or
remove keygroups, samples or files, which stays in the Studio. `Tests/DiskEditCheck.cpp`, run
by `build.ps1` on every disk in `disks/`, round-trips every setting and checks that no other
byte of the image moves. `crosscheck.ps1` on an edited image confirms the C# Studio reads it
identically. **An edited programme has not yet been loaded on a real S950.**

**The player's controls.** Sixteen parameters, all automatable by the host and all reachable
from a MIDI controller: gain, and fifteen trims that move every keygroup of the loaded
programme together.

A trim is an *offset* from what the disk says, not a setting. It reads zero until you turn it
and double-clicks back to zero, and zero means "play what is on the floppy". That matters
because a programme carries its own cutoff and envelope per keygroup — often quite different
ones across the keyboard — and an absolute control would flatten all of that the moment it was
touched. An offset keeps the shape its author gave it and moves the whole of it, which is what
"brighter" means on an instrument like this.

| group | controls | CC | range |
|---|---|---|---|
| Sample | Filter | 74 | ±99 |
| VCF | Amnt | 70 | ±50 |
| VCA envelope | Attack, Decay, Sustain, Release | 73, 75, 79, 72 | ±99 |
| VCF envelope | Attack, Decay, Sustain, Release | 102, 103, 104, 105 | ±99 |
| LFO | Rate, Depth, Delay | 76, 77, 78 | 0..99 |
| Velocity | Freq, Loudness | 109, 112 | 0..99 |
| Glide *(not on the S950)* | On, Time | 65, 5 | off/on; 0 (no glide) – 3 s |
| Polyphony *(not on the S950)* | Poly | 106 | 1 (mono) – 8; CC in bands of 16 |
| Wide *(not on the S950)* | On, Detune, Spread, Offset start | 107, 108, 110, 111 | off/on; ±0–50 cents; 0–100%; off/on |

**Glide is the one addition to the machine.** Everything else here is the S950, measured;
the S950 has no portamento. It is off by default, so a programme still plays as the disk
describes it. CC 65 and CC 5 are General MIDI's own portamento switch and time, so a keyboard
with a glide control reaches them with no mapping. A glide takes the set time whatever the
interval, straight in semitones, and **stays inside a keygroup**: a note slides from the last
note played in its own keygroup, and crossing into another one — a different sample, and on a
split a different instrument — does not slide. Letting go of a key mid-glide stops the slide
where it stands, and the next note in that keygroup plays at its own pitch. CC 5 at 0 is no
glide and at 127 is three seconds. Constant-pitch keygroups never glide.

**Polyphony** is a limit on how many notes can sound at once, not a unison stack. At 3 a
fourth key steals the oldest note; at 1 (**mono**) no chord can be played, and it plays like a
monosynth. A key struck while another is still held in the same keygroup moves the note there
without restarting the sample or the envelope, gliding if glide is on. Letting go of the top
key goes back to the newest key still held. A detached note, or one in another keygroup,
starts fresh. In mono a layered programme sounds only its first keygroup. CC 106 is unassigned
in General MIDI; 0–15 is mono and 112–127 is all eight.

**Wide** plays every note as two voices, one detuned flat and panned left, one detuned sharp
by the same amount and panned right. The note stays centred on its true pitch and is as loud
as a single voice wherever Spread is set. It works in mono and poly. A pair is two of the
eight voices, so Wide caps playing at **four notes**, matching the S950's 8 voices; below
four, Polyphony sets the limit. The two halves are always stolen together, so a note never
loses its flat or sharp half. *Offset start* begins the sharp half 7 ms into the sample so the
pair doesn't flange at the attack. Constant-pitch keygroups are never doubled. Glide, legato
and the release hand-over all move both halves together.

72–79 are the General MIDI sound controllers, so a keyboard with knobs labelled *cutoff* and
*attack* reaches the right ones with no mapping — including 76, 77 and 78 for vibrato rate,
depth and delay, which is what this machine's LFO is. 102–105 and 109/112 are undefined
numbers taken for the filter envelope and for velocity, which GM has no assignments for.

The two envelopes are dragged as shapes rather than set as eight knobs: the corners are the
stages, and the graph shows the result for one representative keygroup — the programme's own
values with the trim added — so it is honest about what you will actually hear.

Three behaviours worth knowing before they surprise you:

- **The LFO and velocity knobs only add.** The filter and envelope trims go both ways because
  a programme always has an envelope and always has a cutoff. Nearly every programme leaves
  the LFO switched off, so a symmetric knob there would spend its whole lower half asking for
  less than nothing and clamping at zero.
- **Velocity → Loudness reaches the next note you play**, not one already sounding. It decides
  how much softer a soft note is, which is a question about the strike, and the strike is over.
  Velocity → Freq does reach a sounding note, because a filter control you cannot play with is
  not a control.
- **The VCA attack steps rather than slides.** On the hardware it is a counter — 5.4/n seconds
  for whole n — so stored 70 and 75 give the same attack to four digits, as do 80 and 85, and
  everything from 90 up saturates at 2.70 s. The knob is faithful to that, which does feel odd
  under the mouse the first time.

To change the programme itself rather than trim it, edit the image in the Studio, save it, and
load the saved image here.

Eight voices, as the machine had. A ninth note takes a voice that is already releasing
before it takes one still held. The voice count in the window is there because it answers
the first question anyone asks of a plugin making no noise — is it getting the notes? —
without a debugger.

`docs/tutorial.html` in the installed folder has the same ground with the editor's half
as well.

## Where this has got to

`Source/S950` is the engine, ported from `AkaiS950Engine` and depending on nothing but the
C++ standard library — no JUCE, no SDK, no audio device. That is deliberate: it is the half
worth being sure of, and keeping it free of everything else means it can be compiled and
checked on its own, long before a plugin will load.

```
Source/S950/Cal.h           the measured constants, and the mappings from panel bytes
Source/S950/Filter.h        6th-order Butterworth, three biquads
Source/S950/Patch.h         Sound, KeygroupPatch, Patch — what a voice needs
Source/S950/Disk.h/cpp      reading an .hfe or .img into a Patch; editing keygroup settings
                            in place; building a disk from nothing (samples, programmes,
                            the pointer rules the hardware needs)
Source/S950/Synth.h/cpp     the synthesis: additive waves, wavetable sweeps, FM, ring,
                            bends, noise, the drum kit — AkaiS950Synth ported, with .NET's
                            System.Random ported exactly
Source/S950/SynthPatch.h/cpp the Synth tab's recipe, and its rendering to a disk
Source/S950/Voice.h/cpp     one sounding note
Source/S950/Engine.h/cpp    eight voices, the event ring, the patch hand-off, glide, mono,
                            the polyphony limit, Wide
Source/Look.h/cpp           the window's design system: panels, the knob, the colours
Source/ProgramPage.h/cpp    the Program tab
Source/SynthPage.h/cpp      the Synth tab
Tests/ConformanceCheck.cpp  the engine against the C# it came from (and its own rules)
Tests/Reference.h           GENERATED — what the C# computes, to be held to
Tests/DiskEditCheck.cpp     the disk writer, on real disks, and a disk built from nothing
Tests/SynthDump.cpp         the synthesis, rendered for synthcheck.ps1
Tests/DiskDump.cpp          the disk reader, dumped for crosscheck.ps1
```

**It compiles, and it agrees with the C# exactly.** For one commit it was a transcription
nobody had run, because the machine it was written on had no C++ compiler. The first build
once Visual Studio arrived was clean at `/W4`. Today `build.ps1` runs the conformance check
(2,714 checks), then the disk-writer check on every disk in `disks/` (round-tripping every
keygroup setting and checking no other byte moves, then building a disk from nothing and a
synth disk and asserting every pointer the hardware cares about), and only then builds the
plugin. Two more checks run by hand: `crosscheck.ps1` reads real and built disks with the C#
library and this one and diffs the dumps; `synthcheck.ps1` renders 45 waves and drums with the
C# synth and this port and compares them word for word — all 45 identical.

## What it needs

- **Visual Studio Community 2026** with "Desktop development with C++" — what this was built
  with, MSVC 14.51. Note that `cl.exe` is never on the PATH: the installer leaves it off on
  purpose, because the compiler needs INCLUDE, LIB and PATH set for one target architecture.
  `build.ps1` finds `vcvars64.bat` through vswhere and sets them; the Start menu's "Developer
  PowerShell for VS 2026" does the same thing by hand.
- **JUCE** — `git clone https://github.com/juce-framework/JUCE`. No installer, and it carries
  the VST3 SDK headers, so there is nothing to fetch from Steinberg. Drive it with JUCE's
  CMake support rather than a Projucer-generated solution: the Projucer emits project files
  for the Visual Studio versions it knows by name, and CMake does not care which one is
  installed.

JUCE 9 is **AGPLv3** unless you buy a licence — not GPL3, which is what JUCE 6 and 7 were.
`Mz950` is AGPLv3 to match, so there is no gap between what this repository says and
what the JUCE parts oblige. A closed-source plugin would need a commercial JUCE licence.

## Building the plugin

```
cd Plugin
cmake -S . -B build
cmake --build build --config Release --parallel
```

CMake ships inside Visual Studio, so there is nothing else to install — `build.ps1` finds it
the same way it finds the compiler. That produces three things:

- a **VST3**, installed to `%LOCALAPPDATA%\Programs\Common\VST3`. JUCE would rather put it in
  `C:\Program Files\Common Files\VST3`, but that cannot be written to — or created — without
  elevation, and building as administrator to test an audio plugin is the wrong trade. The
  per-user folder is the other location the VST3 specification names. In Ableton, add it once
  under Preferences → Plug-Ins → VST3 Plug-In Custom Folder.
- a **standalone** at `build/VirtualS950_artefacts/Release/Standalone/Mz950.exe`, which
  opens without a DAW. That is what makes "is the plugin broken, or is the host unhappy with
  it" answerable in one step rather than two.
- the **conformance check**, at `build/Release/ConformanceCheck.exe`.

## Checking the engine on its own

```
cd Plugin
.\build.ps1
```

It needs no JUCE and no audio device. It puts the same inputs through the port that
`ReferenceDump` put through the C#, and insists on the same answers to nine decimal places —
the filter's cutoff at 65 points, the envelope times at 35, the VCA attack at all 100 of its
settings, the LFO's rate and delay fade, every measured constant, plus the filter's DC gain
and stopband and the engine driven end to end through its event ring.

The attack gets all hundred rather than a ladder because it is a counter, and the interesting
thing about a counter is *where it steps*: a port that interpolated smoothly between the same
measured points would match every third value and be wrong in between. The check also holds
the property rather than only the numbers — whole steps, never going backwards, saturating at
5.4/2 — and does the same for the player's controls, which are tested by what they do to
rendered audio rather than by reading a variable back. A control that moved a number without
moving the sound would pass any test that asked the number.

Regenerate `Reference.h` after any change to `Cal`, `Filter` or the envelopes on the C# side:

```
.\test.ps1                                     # from the repo root, to be sure the C# is sound
csc /target:exe /main:ReferenceDump /out:%TEMP%\RefDump.exe AkaiS950Tests\ReferenceDump.cs AkaiS950Engine\*.cs
%TEMP%\RefDump.exe Plugin\Tests\Reference.h
```

## Why the port looks like the C#

Almost line for line, on purpose. Three implementations of this instrument now exist — the
web's `audio.js`, `AkaiS950Engine`, and this — and they are meant to agree to the digit,
because every number in them came off a recording of a real machine rather than out of a
manual. Keeping the shape identical is what makes a disagreement easy to find.

Two places where it could not stay identical, both in `Engine`:

- **The patch hand-off.** The C# assigned a reference and let the collector decide when the
  old programme could go. There is no collector here, and both obvious replacements free
  memory on the audio thread. The patch is moved between threads through a two-slot
  exchange instead; `Engine.cpp` says why at length. Call `collectRetiredPatch()` from the
  message thread, or the old patch is never released.
- **Sound ownership.** `Sound` is held by `shared_ptr`, which the C# did not need. A voice
  reads a sample buffer on the audio thread while the message thread may be replacing the
  programme, and shared ownership is what stops the buffer being freed underneath it.

## Still to do

The three things that were listed here — reading disks, saving the image into the project,
and an editor worth looking at — are all done. What is left is measurement, not code:

0. **Positional crossfade is modelled now** — runs 15 and 17 measured it and all three engines
   apply it. Program header byte 21; 48 of the 390 library programmes have it on with
   overlapping keygroups, and they are the multi-sampled instruments, GRAND-PNO1 and 2 with
   nine keygroups apiece, GRANDX, CB CEL VL. Until this was done they all sounded both
   keygroups at full level: about 6 dB too loud across the overlap, with two different
   recordings of one note beating against each other.

   A key's position in the overlap is `x = (i + 1) / (N + 1)`, and the attenuation comes from
   a measured table rather than a formula — see `Cal.XfadeDb`. Seven overlap widths from 1 key
   to 21 agree to 0.1 dB wherever two of them land on the same `x`.

   **The first version of this table was measured through a limiter and was wrong.** Every
   take up to run 17 had 20–30% of its samples pinned within 0.1 dB of −0.49 dBFS. The
   crossfade is the rig's only two-tone measurement, which is what made it the worst
   casualty — a weak tone beside a limited strong one is dragged down by the strong one's
   gain reduction, so the error grew towards the overlap's edges: 0.2 dB at `x` = 0.1,
   1.5 dB at 0.5, 6.1 dB at 0.9. Re-recorded clean, the table shifted by up to 4.75 dB and
   two independent clean takes (runs 17 and 19) now agree to 0.05 dB.

   Every distinct level in the clean take is a whole number of **0.4 dB steps** — the same
   unit run 19 measured for the sustain. The machine counts decibels in 0.4 dB and the
   crossfade is a lookup of that count.

   Three things about it are less than settled:

   - **Width 9 disagrees with widths 13 and 21 at its last key**, −17.6 dB against −21.6 at
     nearly the same `x`, and it survived the clean re-record. With the gain quantised that
     is expected — two `(i, N)` pairs at nearby `x` can fall on different integer steps — so
     the real rule is arithmetic on `i` and `N`, not a function of `x`. Nobody has worked it
     out. Every measured width reproduces its own reading; an unplayed width could be 4 dB
     out near the far edge.
   - **Identical ranges are not faded at all, and the S950 saturates when they sound.**
     `XfadeSameRangeDb` is 0 — it was −3.7 from a limited take. The section distorts at any
     recording level: dropping the input 2.66 dB left every other section clean and changed
     this one's reading not at all, with its ceiling moving down by the same 2.66 dB, so the
     distortion is in the machine. That bounds the answer without needing it clean: a pair
     summing to +2.3 dB, and compression can only lower a sum, puts each layer above
     −0.71 dB. −3.7 and equal-power −3.0 are both ruled out by three decibels. A single
     0.4 dB step is still possible and would want a disk that trims both keygroups at source
     so the machine is not saturating while the ratio is read.
   - **Nobody has measured what byte 21 = 0 does to an overlap.** Both keygroups at full level
     is the assumption and what the engines do. One section on a future disk settles it.
1. **`EnvTime`'s fifty-byte gap is measured, and it was out by up to 35%.** Run 19 walked a
   release ladder and a decay ladder across stored 15 to 55; the two agree rung for rung to
   1.5%, so it is one curve. The old guess was 26% slow at stored 20, 31% at 25, 22% at 45.

   **It is not smooth, which is why no interpolation could have found it.** Ten stored units
   double the time, seven times over (×2.079 to ×2.185) — but the two five-unit steps inside
   each decade alternate ×1.60 then ×1.34, four times each, on both ladders independently. An
   even split would be ×1.463 twice. That is the same counter signature the VCA attack has.

   The old "20% slow around stored 45" was the symptom: the table's **anchor at stored 50 was
   16% slow**, so everything interpolated below it inherited the error. `VcaReleaseDb` moved
   40 → 42.5 with it, from the two run-9 anchors that survived the limiter because they are
   corner *frequencies* rather than levels.

   Still open: **0 to 15**. The fall at stored 15 is over in nine milliseconds, as short as an
   rms window can time. Reaching below it wants the amplitude tracked by Goertzel on a tone —
   a different rig, and a small span: the measured endpoint at stored 0 brackets it.
2. **Done — `EnvOctaves` is 8.3, and it was never 7.8.** Run 20 repeated run 1's static-corner
   measurement on a clean take, six amounts each way, and landed where run 1 had:

   | | run 1 | run 20 |
   |---|---|---|
   | opening | 8.37 | 8.39 |
   | closing | 8.28 | 8.20 |

   The 8.5 that sat here matched neither, and matched no reading in its own comment — a value
   the evidence had quietly outgrown. Run 5's 7.7 and 7.56 are the ones to discard: they came
   from a sweep that hit a stop, exactly what the note of the day warned about. Rendering the
   same disk through the model returns 8.51 against the 8.5 that went in, so the method is
   unbiased to 0.03 and the 2.4% correction is real.

   One thing left unmodelled: the two directions are not quite equal. Run 20 puts closing at
   0.978 of opening where the render gives 0.999, so that 2% asymmetry is the machine.

2b. **Done — `LoudnessDbPerUnit` is 0.401, not 0.29.** It rested on a single stored +20 that
   read 5.7 dB up, on a limited take; before that it was 0.21, from the emulation measuring
   itself. Run 20 walked eleven rungs from −50 to +50 with the whole section held 17 dB down
   by a shared velocity trim, so nothing saturated. Ten read, on a straight line to 0.21 dB,
   at **0.401 dB per unit** — 38% above the old value, on 1183 of the 1908 library keygroups.

   And it is 0.4 again: the sustain plateau and the positional crossfade both count in 0.4 dB
   steps, measured independently on different runs. **The machine has one internal decibel
   step and this is it.**

2c. **Done — `VcfTimeScale` is 0.71, and the filter's decay is a rate.** Run 20 could not
   settle this: it watched the filter's *release*, which only happens after the key comes up,
   and the amplitude is released at the same moment — so the note was dying at 15 dB a second
   underneath the measurement and the probe scatter grew with the setting, up to 3.4×.

   Run 21 watched the filter's **decay** instead, which happens while the key is still down,
   with the amplitude held dead flat. Six settings, each read against the same disk rendered
   through the model so the method's own lag cancels: **0.71, spread 0.06**. Run 20's 0.58 was
   mostly the droop.

   **And the decay turned out to be a rate, not a duration** — the same shape the amplitude's
   has. One decay setting, four depths for it to cover:

   ```
   octaves travelled   1.97   1.34   0.65
   seconds             0.82   0.66   0.51
   ```

   A duration is a flat line through those. They fit `time = 0.354 + 0.235 × octaves` to
   within 0.008 s, where the same disk rendered through the old duration model gives a slope
   of −0.038. The intercept is the method's lag; the slope is the filter. So both envelopes
   are rates and the machine has one generator — which is what `VcfTimeScale` being a plain
   ratio always implied.

   Two things run 21 also ruled out, both of which every filter run before it had held fixed:
   the **starting cutoff** does not matter (three bases two octaves apart, flat to 3%), and
   neither does the **direction** (a negative amount takes the same time as a positive one,
   to 6%). So the envelope really is octaves per second, not a count in cutoff-code units.
2a. **Done — a sustain of 0 is silence, and the decay is a rate.** This used to read "a decay
   bottoms out 39.6 dB down where the hardware falls at least 77", and it mattered more than
   anything else on the list: **1907 of the 1908 library keygroups set a decay and 723 decay
   to a sustain of 20 or less**, so every plucked and struck sound on every disk stopped dead
   39.6 dB up and sat there ringing.

   Run 19 settled both halves. The plateau is a straight line in decibels from stored 99 down
   to stored 5, twelve settings at 0.39–0.40 dB per unit — so `SustainDb` 39.6, which is
   exactly 0.4 per unit, is **confirmed rather than changed**. A stored 0 is not on that line
   at all: it falls straight past, hovers in the 12-bit quantisation around −60 dB and then
   goes to the floor, between 90 and 96 dB down. `VcaSilenceDb` is 96.0 — 240 steps of 0.4,
   the machine's own decibel step.

   And the decay turned out to be a **rate, not a duration**: holding it at stored 65 and
   moving the sustain through twelve depths gave 48.2 dB/s at every one. A duration would put
   every plateau at the same moment whatever its depth. The filter's decay is still modelled
   as a duration — that is where the rule came from, and it has never been measured.
3. **Both velocity sensitivities are modelled now**, and they do not share a shape. Byte 9
   shortens the attack with no pivot at all — velocity 1 leaves the byte where it is. Byte 10
   turns the release about velocity 64, so a soft strike lengthens it where a hard one shortens
   it, or the reverse if the depth is negative. Both are in the changelog with their numbers.

   The residual uncertainty is in the envelope table rather than in either rule: the release
   fit is within about 9% across eighteen clips, and its worst misses cluster around effective
   byte 45, all on the same side, which is the signature of a table that is slightly off there
   rather than a rule that is wrong.

   An earlier version of this list said neither was worth the trouble because no keygroup in
   the bundled library set them. That library is generated by this repository, so it only ever
   said what this repository writes; the 101 real disks set byte 9 in 164 keygroups.
4a. **Done — the envelope curve is a counter, and the low bit of the byte is ignored.**
   Run 19 found the steps uneven in a way no smooth curve makes. Run 23 went one unit at a
   time from 45 to 56:

   ```
   stored    45     46     47     48     49     50     51     52     53     54     55     56
   dB/s   223.6  191.1  192.2  163.5  163.7  141.3  141.4  121.2  121.5  103.8  103.8   88.9
   ```

   (46,47), (48,49), (50,51), (52,53), (54,55) — each pair identical to better than **half a
   per cent**, where the step between pairs is fifteen. `EnvSeconds` now floors the low bit,
   and the table is indexed at the even member of each pair.

   Read by count rather than by byte the ratios are 1.1667, 1.1715, 1.1574, 1.1648, 1.1691,
   1.1676 — constant at **1.166**, which explains run 19's alternation exactly: five stored
   units is two counter steps or three, and 1.166² = 1.360 against its measured 1.34, 1.166³
   = 1.585 against its measured 1.60.

   Both runs agree where they overlap, to 2.5%. The top of the curve is not 1.166 a step —
   84 and 90 are three counts apart for 2% of growth — so those points stand as measured.

4. **Done — the VCA attack below 30 is measured, by setting the byte.** It had two points
   down there, 8 and 20, and *both* were reached sideways through the velocity rule, so they
   inherited whatever that rule got wrong. Run 23 set the byte directly at twelve settings:

   ```
   stored     8    10    12    15    18    20    22    25    28    30    35    40
   seconds .018  .026  .038  .058  .079  .097  .120  .154  .194  .227  .311  .392
   5.4/n    300   208   142    93    68    56    45    35    28    24    17    14
   ```

   The counter law holds all the way down — every one within 2% of 5.4 over a whole number,
   most within 0.6%. Of the two the table already had, **stored 20 was right** (56 against
   55) and **stored 8 was 11% out** (300 against 270). A value reached through another rule
   carries that rule's error, and the two disagreed about how much.

   Still open at the very bottom: **stored 0 is 7000 and still a guess.** Extrapolating the
   measured points downward puts it near 0.8 ms — under one sample at 44.1 kHz — so nothing
   here can tell it from instant, and "hard gate" remains how envelope generators are built
   rather than something read off this machine.

   Also not measured: whether the *attack* ignores its low bit the way `EnvTime` does. This
   ladder steps by two or more everywhere. It is a different counter with a different law,
   so the answer does not carry across.
5. **Done — the filter attack does not quantise like the VCA's, and the question split in
   two.** It used to read "it may be 5.4/n for whole n, but it was measured to about 5%,
   far too coarse to see 0.7% steps".

   The first half was answered by run 22: it traced the filter's attack with three of run
   21's decay settings **in the same take**, and they matched to 3% — 0.51/0.51, 0.82/0.82,
   1.43/1.39. So the filter's attack follows `EnvTime`, not the VCA attack's 5.4/n, which is
   a different curve entirely (across stored 60–70 the two differ by 30%).

   The second half — whether `EnvTime` itself steps — did not need the filter at all. A
   moving corner times to about 5%; a falling level times to about 1%. So it went to the VCA
   release, and the answer is item 4a above: it steps, in pairs, and the filter inherits that
   because run 22 established they are one curve.

5a. **Done — the filter's attack reads the same curve as its decay.** Run 21 measured the
   filter's decay and release and found both to be rates on a shared clock; the attack had
   only ever been assumed to match. Run 22 traced it, with three of run 21's decay settings
   repeated **in the same take** so the comparison did not have to cross two recordings:

   | stored | attack | decay |
   |---|---|---|
   | 55 | 0.51 s | 0.51 s |
   | 65 | 0.82 s | 0.82 s |
   | 75 | 1.43 s | 1.39 s |

   Identical to 3%. It also gives `VcfTimeScale` a second, independent reading — 0.74 from
   the attack against run 21's 0.71 ± 0.06 from the decay, which is well inside the spread.
   Left at 0.71.
6. **Done — Warp's depth constant is 6.44, measured.** It was 6.25, chosen because it is a
   sixteenth of a semitone exactly, from a fit that gave 6.21 and could not separate 6.0
   from 6.5.

   What that fit never did was **vary the depth**. Runs 10, 11 and 12 all pinned byte 13 at
   −50 and swept byte 12 and velocity instead, so the depth constant came out sideways from
   clips aimed at something else. Run 22 swept it — ten depths, both signs, byte 12 at zero:

   ```
   depth    -50   -40   -30   -20   -10    10    20    30    40    50
   cents   -325  -238  -214  -132   -72    63   122   200   242   330
   ```

   A line through the origin gives **6.436 ± 0.114**. 6.0 is 3.8 standard errors away and
   dead; 6.25 is 1.6 away and no longer the best estimate; 6.5 would also fit.

   Worth recording that the limiter is *not* the explanation here, since it has been the
   explanation for so much else: clipping a sine does not move its zero crossings, and the
   pitch is read by counting them. Runs 10–12 being the most heavily limited takes in the
   project barely touched this one. Nobody had varied the byte.

   Run 22 also confirmed `WarpTime` in passing — byte 14 = 50 read 68–72 ms across nine
   clips against the table's 69.5.
7. **Done — aftertouch (byte 21) is measured and modelled, and it is the modwheel's twin.**

   This item used to excuse itself: "it is 0 in every one of the 1908 real keygroups, so
   nothing on any disk plays wrongly". That is the wrong test. The 1908 keygroups are **one
   person's shelf of disks**, and this is a tool other people point at their own libraries —
   so a byte nobody here happens to set is not a byte nobody sets. It had no evidence either
   way, and that was being written down as "no problem". Ranking work by how many library
   keygroups something touches is fair; deciding what counts as a *bug* that way is not.

   The aftertouch run (`lfoplan2.js`, four clips, 42 s) settled all three questions:

   - **The travel.** 71.95 cents at full pressure with byte 21 = 99, r² 0.999 across nine
     steps — against the modwheel's 72.3. **One constant, not two.**
   - **The law of the byte.** 36.42 cents at byte 21 = 50 against 71.32 at 99, a ratio of
     0.511 where a straight proportion is 0.505 and byte 22 measured 0.509. The same law.
   - **They add.** Wheel alone read 71.87 cents; wheel and pressure together 149.79. A
     machine taking the larger of the two would have stayed at 71.87.

   So no new constant — `LfoWheelCentsAtFull` serves both, and the two contributions sum.
   All three engines now carry byte 21 through to the voice, and the plugin handles channel
   pressure. The S950 has no polyphonic pressure input, so only the channel message counts.
8. **The eight individual outputs are played centred, which is a guess.** Byte 19 sends a
   keygroup to ALL, to one of MONO 1–8, or hard LEFT or RIGHT. LEFT and RIGHT are modelled;
   MONO 1–8 are centred because nobody has checked what the machine's main stereo pair does
   with a voice routed to an individual socket. If it drops out of the main mix — as it does
   on many samplers of that era — then 253 library keygroups should be silent on the stereo
   pair rather than centred.

   **Half-answered by run 22, and it needed no cables.** Eleven keygroups on one tone,
   identical in every byte but 19, read as levels on the outputs already connected:

   ```
   panel     0     1     2     3     4     5     6     7     8     9    10
           ALL  MONO1 MONO2 MONO3 MONO4 MONO5 MONO6 MONO7 MONO8  LEFT RIGHT
   dB     -0.2  -0.2  -0.2  -0.4  -0.1  -0.3   0.0  -0.1  -0.4  -0.2  -0.3
   ```

   **MONO 1–8 do not leave the main pair.** All eight read the same as ALL, to 0.4 dB, so
   the 253 library keygroups routed to individual outputs are present in the main mix and
   "centred" is not silently wrong. That is the part that was a guess and it is settled.

   **LEFT and RIGHT stay hard-panned, as a deliberate choice rather than an open question.**
   Run 22 could not read them: every take here is mono, and if the recording were one socket
   a RIGHT-panned voice would have vanished, while if it were a sum then ALL would have sat
   6 dB above both. Neither happened — all three read alike, which is what jack normalling
   does when nothing is plugged into RIGHT.

   It is not being chased. The plugin is played through a stereo pair, `LEFT` and `RIGHT`
   pan hard there, and that is what the panel names say they do. 38 keygroups across four
   library programmes use them. Settling it properly would want a plug in the RIGHT socket
   to break the normalling — worth a minute if anyone is curious, and nothing depends on it.

## Sample-accurate events

`noteOn`, `noteOff`, `modwheel` and `allNotesOff` each take an `at` — where in the next block
the event belongs, in samples. `render` fills the block in stretches between events rather
than applying them all at the top, so a note lands on the sample the host asked for.

This is the one place the port deliberately does *more* than the C#, and it is not a
refinement. Applying everything at the block boundary still sounds like a working
instrument — just one that quantises every note it is sent to the buffer size. At 512
samples that is 11 ms, which nobody hears as a fault; they hear a drum machine that does not
quite swing. The C# has no use for it, because a piano keyboard and a MIDI port have nothing
finer to offer, and `at` defaults to 0, which is exactly that behaviour.
