# Changelog

What changed between releases, for the notes that go with a tag.

Every number here came off a recording of a real S950 rather than out of a manual, so where
a change is a measurement it says what was measured and what it replaced. Where it is an
assumption it says that too — those are the ones another afternoon with a recorder settles.

## Unreleased

### macOS

A universal (Apple Silicon and Intel) VST3, Audio Unit and standalone, macOS 11 or later,
shipped as a zip. It's built by CI and not notarized: `install.sh` puts everything in the
user's own Library and clears quarantine, and READ ME FIRST.txt explains why. Mz950 Studio
stays Windows-only.

### The LFO's Rate is exponential, and reaches the audio range

Rate now works in **octaves** from the programme's own rate: -99 is six octaves down
(×1/64) and +99 eight up (×256), held to 0.02–500 Hz. Every notch is the same musical step,
so a slow rate is as easy to set as a fast one, and a stepped LFO is easy to line up with a
beat. At the top, Square or S&H on Pitch or Filter turns into a buzz and then a scream. The
knob reads as a multiplier, ×0.25 to ×4.00 and beyond.

Getting there took two engine changes:
- **The control block shortens for a fast LFO.** It gives at least 16 updates a cycle, down
  to 4 samples, so the LFO is followed rather than sampled into a few steps. It only
  shortens when the LFO is fast and moving something. Eight voices at 500 Hz on pitch and
  filter measured 1.4% of one core, against 0.65% as the S950.
- **The shared LFO is followed through each host buffer.** Before, it held one value per
  buffer. The engine still matches the C# reference.

A session saved with a Rate trim will now play at a different rate, since the scale
changed from units to octaves.

### Synth tab: the oscillators are mixed into one voice

Reported: in synth mode, notes used more voices than they should, and with glide on a
single voice you could hear oscillators being dropped. Each oscillator was its own keygroup,
so one note of a three-oscillator patch took three of the eight voices (six with Wide), and
mono sounds only a programme's first keygroup.

The oscillators are now mixed into **one looped sample**, one keygroup, **one voice a
note**, so mono and glide move the whole sound and eight notes are eight notes.

- **Detune:** a loop plays at one pitch, so a detune can't join in a few cycles. The loop is
  made just long enough for every oscillator to complete whole cycles within a cent of its
  setting, 1.1 to 1.3 s for the detuned presets and at most 2.5 s. The beating is drawn into
  it. The old keygroups could only tune in the S950's 6.25-cent steps, so this is closer.
- **Octaves** still fit in a few cycles.
- **Sweeps** keep their speed as whole round trips.
- **Level** is balanced in the mix with the same decibels the zone loudness used to give it.

A saved project whose synth disk has the old keygroup-per-oscillator layout is rendered again
when it loads, and the first oscillator's Program tab settings (envelopes, filter, LFO) come
across to the mix. The single oscillator waves are unchanged: still word for word the C#
tool's (`synthcheck.ps1`, 45 of 45).

A multi-oscillator patch may be a few decibels quieter than before, because it's now one
voice at full scale instead of several summed.

### LFO tempo sync

A **Sync** switch under the LFO's Shape box, on MIDI CC 115. While it's on, the Rate knob
becomes a **division** knob, on CC 116: 4 bars, 2 bars, 1 bar, 1/2, 1/4., 1/4, 1/4T, 1/8.,
1/8, 1/8T, 1/16., 1/16, 1/16T, 1/32 and 1/64. Bars follow the host's time signature.

- **Transport playing:** the LFO's phase is set from the song position at every block, so
  Square and S&H step exactly on the grid. S&H draws its levels from the cycle number since
  the song's start, so the same bar gets the same steps on every pass.
- **Transport stopped, or in the standalone:** it runs free at the tempo, 120 BPM with no
  host.
- **Every note rides the one LFO** while Sync is on, including keygroups that have their own
  (desync set). That's what makes them step together.

Checked at 120 BPM: 1/4 turns over twice a second and 1/8 four times. Started a quarter of
a beat before the next beat, it turns 0.125 s in, on the beat.

## v0.6.0 — 2026-09-28

Filter resonance up to self-oscillation, LFO shapes, and a Perform-tab LFO that reaches the
filter and goes further than the S950's. None of it is written to the disk, and at zero all of
it is the machine.

### Filter resonance

A **Res** knob beside Filter on the Perform tab, MIDI CC 71. The S950's filter has no
resonance, so this is an addition like glide and Wide, in their violet. It starts at 0%, where
the filter is the machine's, matching the old one to within 1e-12.

- **Up to 90%:** the peak at the cutoff rises to about +15 dB and the slope stays 36 dB an
  octave. The input eases down as it rises, by about 6 dB at the top of the range, the way an
  analogue filter loses passband, so a hot sample doesn't clip.
- **Past about 92%:** the filter **self-oscillates**, a sine at the cutoff. It follows the
  Filter knob, the filter envelope and velocity, and the VCA gates it. It settles at -14 dB
  per voice. The pitch is within 0.2% of the cutoff, and harmonics and aliasing stay below
  -54 dB at 44.1, 48 and 96 kHz, even with the filter wide open.

How: the filter's resonant stage (Q 1.93) is now a state-variable filter instead of a biquad,
since a biquad can only ring, never sustain. It has an automatic level control: its damping
rises with its own smoothed band-pass power and, past the threshold, goes negative. A first
version damped on the instantaneous signal. It held the level just as well but bent the sine,
and at a 16 kHz cutoff it folded a tone back at -16 dB. The smoothed follower removed that.
The other two stages are unchanged. Resonance moves notes already sounding.

### LFO shapes

A **Shape** box in the Perform tab's LFO panel, MIDI CC 113 in four bands of 32: **Sine**,
**Saw**, **Square** and **S&H**. Sine is the S950's LFO and stays the default. The other three
are additions, and like Res it is a performance setting that is never written to the disk.
Rate, depth, delay and desync work as before for every shape.

- **Every shape starts where the sine does,** at the centre and heading up, so switching
  shape under a held note doesn't throw it to the other side of its pitch.
- **Saw** rises to the top, jumps to the bottom and rises back.
- **Square** sits high for the first half of each cycle.
- **S&H** holds one random pitch per cycle and steps to a new one at Rate. The level comes
  from a hash of the cycle number, not a running generator. So voices riding the programme's
  shared LFO (desync off) step together, while a desynced keygroup's notes each have their own.

### The Perform tab's LFO reaches past the S950's, and can move the filter

Reported: the LFO knobs were hard to hear. On the running plugin they did work: Depth +50 was
±79 cents on PWM STRGS. But four things got in the way, and all four are fixed:

- **Rate did nothing on shared-LFO keygroups (fixed bug).** Keygroups riding the programme's
  shared LFO (desync off) ignored the Rate knob entirely: the shared oscillator's speed was set
  once, when the programme loaded. It now takes the trim every stretch.
- **Pitch depth only reached the S950's ±150 cents.** It keeps the machine's 1.5 cents a step up to
  +50, then grows to **a whole octave at +99**, so square and S&H become pitch jumps you can't
  miss.
- **Rate could only add.** It now goes both ways. Above zero it's the S950's measured curve.
  Below it, it carries on past the machine's slowest (1.8 Hz) down to **0.1 Hz**.
- **Delay could only add**, so it couldn't remove a programme's slow fade-in, and short notes
  never reached any depth. It goes both ways too. Below zero it shortens the fade, to **none at
  all** at -99.

**Filter depth**, new: a knob in the LFO panel, on MIDI CC 114, that sends the LFO to the cutoff,
up to three octaves either way. It uses the LFO's rate, shape and delay, and its depth is
separate from Pitch. A square or S&H there gives the classic stepped filter. Not on the
S950, so it's violet.

With every trim at zero the LFO is exactly the S950's, as before. A session saved with LFO
Depth past +50 will now sound deeper.

## v0.5.3 — 2026-09-28

The Program tab's keygroups over a piano keyboard, a keygroup list, and a demo mode that
records the standalone's own video.

### The Program tab's keygroups over a piano keyboard

The strip of keygroup bars now sits over a keyboard, C0 to G8, drawn as one: white keys the
same width, black keys over their joins, and each keygroup's bar spanning exactly the keys it
covers. The selected keygroup's keys are shaded in the Program amber - every keygroup's, with
**All keygroups** - the other keygroups' keys faintly, and keys nothing plays are left grey.
**A key held over MIDI shows a blue dot**, ringed so it reads on an amber key or a black one.
Clicking a key chooses the keygroup that plays it, and hovering names the key.

The bars are outlines with a see-through fill, so the keys show through them - amber for the
keygroup being edited, grey for the rest. And a keygroup is chosen from a list as well: the
**All keygroups** button became a box listing "All keygroups" and then every keygroup by
number, keys and sample, with **<** and **>** either side to step through them - so a drum
kit's single-key keygroups, a few pixels wide on the strip, are as easy to reach as any.

The window is 640 x 672: the keyboard needed the height, or the Program tab's "no filter
envelope" note sat on the second row of knobs.

### Demo mode: the standalone records its own video

`Mz950.exe --demo script.txt` runs a timed script - load a disk, choose a programme, switch
tabs and keygroups, move the Perform controls, pick Synth presets, play MIDI files, show
captions - and `Plugin/Demo/record-demo.ps1` films it with ffmpeg and assembles the result:
a captioned MP4, a clean one, the captions as an .srt, and the audio as a WAV.

The audio is not taken from the sound card. The processor writes its own output to a 32-bit
float WAV as it renders, and the script's MIDI goes in at exact sample positions, so there is
no latency, no drift, and nothing else on the machine in the recording. The window flashes
white at the instant the WAV starts; the recorder finds that frame and lines the film up on
it, and every step of the script runs on the audio clock, so picture and sound stay together
for the whole run - measured within one frame. `Plugin/Demo/demo.txt` is an 89-second tour of
the three tabs; only the standalone ever looks for the flag. When the script ends the controls
go back to how they were, so a recording leaves nothing behind in the standalone's settings
(`keep` in a script leaves them as it set them).

## v0.5.2 — 2026-09-28

The Windows file dialog, a smaller window with a balanced layout, and old Synth-tab disks put
back in tune.

### Load disk opens the Windows file dialog, on the plugin's monitor and on top of it

The plugin's **Load disk** (and the Program tab's **Save disk**) used to open JUCE's file
browser inside the plugin window - small and unfamiliar, but the only way round two faults of
JUCE's native dialog: it opened on the primary monitor whatever screen the plugin was on, and
a host that keeps plugin windows always on top could cover it. Both now open the Windows file
dialog, owned by the plugin window, so Windows keeps it above that window - topmost or not -
and opens it on the same monitor. Checked on two monitors with the standalone set always on
top: the dialog came up on the second monitor, owned by the plugin window, above it. The
"could not open/save" messages are owned the same way. If the Windows dialog cannot be shown,
the old in-window browser is still there as a fallback.

### A smaller window, smaller knobs, and pages that line up

With the file browser no longer opening inside it, the window drops from 720 x 720 to 640 x 640
and the knobs from about 70 px to 50, the gain knob included.

- **Perform:** every row now splits at the same centre gutter, so the panels line up down the
  page - FILTER and VELOCITY on the left of the first row, the LFO on the right. The VCF
  envelope's Amount knob sits halfway down beside it rather than at its top.
- **Program:** each page's controls are laid out as one block centred in the panel, rather
  than in its top-left corner; columns still line up from row to row.
- **Synth:** the knob rows share the page's height, so the drums panel reaches the bottom, and
  the drum's switch and knobs spread across the panel like the pads above them.
- The tab bar's rule runs the width of the disk row.

### Synth disks saved before v0.5.0 are put back in tune when a project opens

v0.5.0 corrected how a zone's tuning is read and written, which left Synth-tab disks rendered
before it - and saved inside projects - playing out of tune: a 5-cent detune came back almost
a semitone flat. When a project opens with such a disk it is now rendered again from the Synth
settings saved beside it, which writes the tuning the right way and keeps every setting a
render keeps. It happens only for a disk carrying exactly the old bytes; tuning set by hand on
the Program tab is left alone. Checked in DiskEditCheck (an old disk spotted, a new one and a
hand-tuned one not) and on the standalone's own saved state, which came back in tune.

## v0.5.1 — 2026-09-27

High-density disks, and 48 kHz samples kept at 48 kHz on import.

### High-density disks

The S950 formats 1600K high-density floppies as well as 800K double-density ones, and both
the plugin and the Studio now read and write them.

- The layout is akaiutil's (`akai_flhhead_s`): 1600 blocks, 10 sectors a track, the same
  directory with the allocation table carried on to 1600 entries - which makes the header
  five blocks rather than four, so file data starts at block 5.
- .hfe images of either density decode: the sectors per track are read off the disk itself.
  The Studio writes HD .hfe at 500 kbps with ten sectors a track; DD output is unchanged.
- **File → New HD Disk Image (1600K)** in the Studio.
- Checked against akaiutil, built from source: it reads an HD disk written here - files up
  past block 1000 extracted byte for byte - and a disk it formatted with `formatfloppyh9`,
  with samples it put there, reads here with the audio intact.

### 48 kHz on import

The Studio's import assumed the sampler stopped at 44.1 kHz and brought every 48 kHz file down
to it. The S950 samples at up to 48 kHz: a 48 kHz file now keeps its rate, 48,000 Hz is in the
rate list, and only faster files come down - to 48 kHz. (Reading and playing 48 kHz samples
already worked.)

## v0.5.0 — 2026-09-27

The release where VirtualS950 became Mz950, grew a Synth tab and a Program tab, and was held
against a real S950 on ten test programmes - which found four places the engine read a disk
differently from the machine.

### Four readings corrected against the machine

A calibration round on the real S950 - five programmes chosen from the library for what they
exercise, each recorded on the hardware and set against the engine rendered offline from the
same disk - found the loops, the filter envelope, the velocity switches and the drums right,
and four things wrong. All four are fixed in both engines.

- **A zone's transpose and fine are one number, in sixteenths of a semitone.** Keygroup
  bytes 42/43 (and 64/65) are the low and high bytes of one signed 16-bit pitch offset, in
  the unit the sample header's own pitch uses - not transpose plus fine/256. DSKA0058 SEQ
  BASS carries (fine, transpose) of (192,0), (80,0), (0,0), (144,-1) and (64,-1); the machine
  plays them +12, +5, 0, -7 and -12 semitones, and so does the engine now, all ten notes
  within 1.7 cents. Before, some of its keygroups played five semitones out. 544 zones in the
  library use these bytes; read this way they land on -12, +24, +12, +7, -5 and 1/16-step
  detunes. The Program tab says what each byte is worth.
- **The top of the filter is a fixed 16.3 kHz, not a fraction of the playback rate.** It
  used to fall with the rate a sample plays at, which took the treble off every note played
  below its root - the grit the machine is known for. DSKA0077 ESQ BASS 1, three octaves
  down: up to 70 dB was missing; now every band with signal is within 2.5 dB at all thirteen
  keys tried. MOOG BASS2's filter envelope and GRAND1 are unchanged by it.
- **A one-shot stops at its end marker.** DSKA0004 DRUM-A's cowbell ends 100 ms before its
  audio does, and the machine stops there; the engine played on. 123 one-shots in the
  library are affected. (The machine fades over the last ~80 ms where this stops cleanly,
  about 33 dB below the hit.)

- **Past about 156 kHz a note drops by octaves.** The machine will not play a sample faster
  than somewhere between 151 and 161 kHz; asked to, it plays the note an octave lower - and
  again, until it is under. It is a rate limit, not a key limit: ESQ BASS 1 (30 kHz) folds
  from note 90, VLA W VLN (25 kHz) plays note 90 as it is. Every note of ESQ BASS 1 from 90 to
  127 now matches the machine within 9 cents, where they were one to four octaves out.
  156.25 kHz (10 MHz / 64) is the figure used - inside the measured bracket, but a guess
  within it. Notes below a keygroup's lowest key stay silent, as they do on the machine.

**The Synth tab and the sound library.** Both synth writers wrote detune and octave the old
way, so the bundled disks played octaves as +192 semitones on the hardware. The writers now
write sixteenths - the machine's finest step, 6.25 cents, so a detune of a few cents is one
step - and the five library disks are regenerated, their audio unchanged. Synth disks saved
before this will play out of tune and should be rendered again.

### The release starts 15 ms after the note-off, as on the machine

Measured on DSKA0039 GRAND1 from a clean A/B: a 368-note house-piano clip at 120 bpm played
from the same MIDI through a real S950 and through the plugin, recorded with headroom (peak
-6.8 dBFS, no flat runs). The MIDI replayed offline matched the plugin half of the take to
0.76 dB rms, so candidate changes could be tested against the hardware half directly.

- **The release rate was already right**: 185 dB/s on the hardware, 188 in the engine.
- **But every gap after a note-off had 2.0–3.2 dB less tail** than the hardware - heard as
  "not enough tail". A slower release can't fix that evenly: it leaves the early gaps short
  and pushes the later ones over. A release that **starts 15 ms later** fixes all six gaps at
  once: from a mean of +2.47 dB short to **+0.13 dB**, each gap within ±0.2 dB but one at +0.8.
- The **key** counts as up immediately - glide's hand-over, mono and voice stealing see it at
  once, and letting go mid-glide still stops the glide at once. Only the envelope waits.
  All-notes-off does not wait.
- Both engines: `cal::NoteOffLatencySeconds` in the plugin, `Cal.NoteOffLatencySeconds` in
  the Studio's. New conformance checks: the key is up at once, the level holds through the
  latency and then falls, all-notes-off is immediate.

One programme and one clip. The single-note run below re-measured it with the start marker
honoured: timed from each note's own attack, so any MIDI or recording delay cancels, the
engine's release reaches -20 dB within 7 ms of the machine's at 50, 150 and 500 ms gates.

### A note starts at the sample's start marker

Every sample header carries a start marker at 0x20. The Studio showed it and auditioned from
it, but neither engine played from it: every note started at word 0. Measured on DSKA0039
GRAND1 with `SingleNoteTest.mid` - note 60 struck twelve times at four gate lengths, recorded
from the machine and the plugin:

- Every GRAND sample has its marker at **1000 words** (33–40 ms), after 500–750 words of
  lead-in at -30 to -36 dB. **The machine skips it**: its attack goes straight from silence
  to within 6 dB of the peak. Played from word 0, the plugin spent **24 ms at about -30 dB**
  first, and over each note's first 50 ms it was 4 dB quieter than the machine.
- Played from the marker, the attack matches the machine's shape to within about 2 dB.
- The machine's attack also lands about **20 ms after the note-on**, steady to ±3 ms over the
  twelve notes. That covers the MIDI interface and the recording chain as well as the S950,
  and there is no telling them apart from here, so no note-on delay is added.
- 199 of the 1,075 sounds the library's programmes play have a marker past word 0.
- Both engines crop the sound at decode time and move the loop with it; a marker at or past
  the end of what would play is ignored, as the Studio's audition ignores it. The loop editor
  no longer resets the marker to 0 when a loop is set. DiskEditCheck holds every sound a
  programme plays to its sample's words from the marker on.

### A SYNTH tab: sounds with no disk, made as a disk

The plugin can now make its own sounds, and it makes them the S950's way: every oscillator
is drawn as a band-limited looped sample, every drum as a one-shot, and the lot is written
to a blank disk held in memory that then becomes the loaded disk. So a sound designed on
the Synth tab goes through the real voice (filter, envelopes, LFO, warp), the Program tab
edits it, Glide and Wide work on it, the set saves it, and *Save disk as…* puts it on a
floppy for a real S950.

- **Three oscillators**, each a keygroup layered across the keyboard: sine, triangle, saw,
  square, pulse, organ, glass, buzz, hollow, FM at 1:1, 1:2 and 1:7, ring modulation at ×2
  and ×3, a phase bend, Casio-style phase distortion, and white, pink and brown noise. Each
  has level, octave, fine tune (detune is two layers a few cents apart), start phase, a
  *shape* control whose meaning follows the kind, and a *sweep* that moves that shape across
  the loop and back — pulse-width modulation, FM sweeps and filter-like morphs, baked in.
- **A drum kit** on General MIDI's notes: kick 36, snare 38, clap 39, hats 42/46, ride 51,
  toms 41/43/45, each switchable, with tune (a change of sample rate), decay (the keygroup's
  VCA decay), tone (the zone filter) and level. The oscillators move up to E2 when it's on.
- **Ten presets** to start from, each carrying an envelope and filter that go to the disk.
- **Program-tab edits survive a re-render**: the renderer copies each keygroup's settings
  across from the keygroup playing the same sample, and writes only what its own knobs own.
- Rendering runs on a background thread, a quarter of a second after the last change; waves
  are cached so a level or an octave costs a repack, not a resynthesis.
- **A held note follows a re-render without a click.** Samples are fingerprinted, so a
  rebuilt programme reuses every sample whose bytes did not change and decodes only what
  did; a note held through a level, tune or decay change keeps its sample object and takes
  the new settings in place. A changed wave reaches the next note. The wait after the last
  knob move is 100 ms, and a render that changed no wave takes a few milliseconds.
- **Fixed: a note held through a programme change read freed memory.** A voice kept a
  pointer into the programme it came from, which is retired and freed when a new one
  arrives; a note still sounding on a sample the new programme did not have then read its
  envelope, filter and warp out of freed memory - clicks, or worse. Every voice now owns a
  copy of its keygroup. This predates the Synth tab; the Synth tab made it constant.
- Glide memory survives a re-render of the same shape, so a line keeps gliding through it.
- **Choosing a preset keeps the drums**, with every setting they had, when they are on.
- **Drums play outside mono and the polyphony limit.** A drum note (every keygroup answering
  it constant-pitch and one-shot, which is what a kit's are, on a library disk or the Synth
  tab's) takes the voices above the limit, never joins the held-key stack, and never triggers
  legato - so with Poly at 1 the kick never steals the lead, a snare key held down is not a
  key for mono to return to, and glide runs between the lead's notes only.

Under it, two ports held to the C# they came from:

- **The disk writer** (`Disk::blank`, `addSample`, `addProgram`, `setZoneSample`,
  `rebuildPointers`): the C# library's, with every rule the hardware turned out to care
  about — contiguous directory, the keygroup arena, the header restating it, zone pointers
  as positions. `DiskEditCheck` builds a disk from nothing every build and asserts each of
  them; `crosscheck.ps1` shows the C# library reads the result identically.
- **The synthesis** (`Synth.h`): the additive waveforms, wavetable sweeps, FM, ring, bend,
  phase distortion, noise and the whole drum kit from `AkaiS950Synth` — with the .NET
  `System.Random` ported exactly, so seeds mean the same thing. `synthcheck.ps1` renders 45
  waves and drums with both and compares them word for word: all 45 identical.

**Not yet done:** a synth disk has not been loaded on a real S950. The sample-writing path
it uses has been; the programme-writing path has not.

### A new look for the plugin window

Redrawn as a dark, panelled interface in the manner of Vital, OB-Xd and the TAL synths, with
the same design system behind every control (`Plugin/Source/Look.h`):

- **Panels.** Every group sits in a framed card with a small uppercase title and, in its
  corner, the MIDI controller numbers that reach it.
- **One knob.** A dark disc with a bright arc for the value, the value as plain text below and
  the name in small caps below that. Offset knobs draw their arc from the centre and keep a
  tick at zero, so a knob doing nothing looks like nothing.
- **Colour means something.** Amber is what's on the disk (the Program tab), cyan is an offset
  on top of it (the Perform tab, and the "→ sounding" readouts), violet is an extra the S950
  never had. The same three colours on the tabs, panels, knobs and switches.
- Pill switches, flat buttons, underline tabs, Segoe UI throughout. The window is 720 × 720,
  down from 720 × 840; the envelope graphs no longer swallow the height.
- **Tooltips now appear.** Every control had one; nothing had ever shown them.
- **The keygroup strip lights up as you play.** A keygroup that answers a note gets brighter,
  gains a light rim and a marker at the key it took, and stays lit while a voice plays it.
  The engine counts hits per keygroup, so a drum hit too short to be caught "sounding" still
  flashes. Brightness and outline only, never a change of hue, so it reads the same with any
  colour vision. The palette as a whole has no red or green in it, and nothing in the window
  relies on colour alone.

### VirtualS950 is now Mz950

The product no longer wears Akai's model name. Mz950 plays Akai S900/S950 disks and says so,
but it isn't Akai's product, and every place a user sees it now says that.

- **Renamed:** the plugin is **Mz950** (`Mz950.vst3`, standalone `Mz950.exe`). The editor is
  **Mz950 Studio** (`Mz950Studio.exe`). The installer is `Mz950-<version>-setup.exe`. The
  window titles, the About box, the tutorial and the READMEs all use the new names.
- **A disclaimer** ("independent, not affiliated with Akai or inMusic; Akai, S900 and S950 are
  trademarks of their respective owners") appears beside the plugin's name, in the Studio's
  About box, on the installer's first page, in the tutorial and in the README.
- **Existing Live sets keep working.** A host identifies the plugin by its two codes, `Smoz`
  and `Ak95`, which are unchanged, so sets saved with VirtualS950 open with Mz950.
- **Upgrading replaces the old install.** The installer's AppId is unchanged, so installing over
  VirtualS950 upgrades it and removes the files and shortcuts left under the old names. That
  includes the old `VirtualS950.vst3`, which carries the same plugin ID. The build's own
  install step removes it too.
- **The sound library is still found** after the upgrade: the plugin reads the new `Mz950`
  registry key first, then the old one.
- **Not renamed:** internal folder and namespace names such as `AkaiS950Engine`. They are not
  user-facing, and renaming them would be a large change for little benefit.

### The plugin edits programmes: PROGRAM and PERFORM tabs

Every keygroup setting can now be edited in the plugin, as absolute values in the S950's own
units, and written into the disk image. The window is split into two tabs, so it's always clear
which controls change the programme (**PROGRAM**, saved in the disk) and which play on top of it
(**PERFORM**: the offsets, glide, polyphony and Wide, never saved to the disk). Where an offset
is moving a programme value, the Program tab shows what is actually sounding (**→ 52**).

- **A keygroup strip** across the keyboard, with overlapping keygroups stacked. Click one to
  edit it, or edit *All keygroups* at once.
- **Six pages:** Envelopes, Filter, LFO, Velocity, Tuning, Keys & output. That covers all 38
  settings, with the encodings copied from the Studio's `KeygroupEditor`: signed values, the
  output port stored one lower, fine tune in 256ths of a semitone, and flags changed one bit at
  a time.
- **Edits are saved with the project,** and the host is told the project has changed. Held
  notes follow an edit, because a rebuilt programme reuses its samples. *Save disk as…* exports
  a plain `.img`.
- **One departure from the Studio:** setting a VCF amount on an S900 keygroup with a blank
  filter envelope writes a flat envelope first. Otherwise the amount would silently do nothing.
- **Checked:** `DiskEditCheck` (run by `build.ps1`) round-trips every setting on the six test
  disks, 165,627 checks, and confirms no stray byte changes. `crosscheck.ps1` shows the C# and
  C++ readers agree on edited images. The engine no longer overwrites a waiting programme when
  edits arrive fast.
- **Not yet done:** loading an edited programme on a real S950.

### Glide (portamento) — new, and not a feature of the S950

The plugin's first control that the machine never had. It is off by default, so nothing about
how a programme sounds changes until it is switched on. **CC 65** switches it (64 and up is
on) and **CC 5** sets the time. Both are General MIDI's portamento controllers. The switch
lands on the sample the host puts it at, so a sequenced line can toggle it between two notes.

- Constant time whatever the interval, straight in semitones. CC 5 at 0 is no glide and at
  127 is three seconds. The default is 120 ms.
- Only within a keygroup. A note glides from the last note played in its own keygroup, or
  from wherever that note had reached if it was still gliding. Crossing into another
  keygroup does not glide, and changing programme forgets every keygroup's last note.
- Letting go of a key mid-glide stops the slide where it stands, and the next note in that
  keygroup plays at its own pitch. A glide that had already arrived still leads on to the
  next note.
- Constant-pitch keygroups ignore it, as they ignore the key.
- A glide away from a note whose key is already up takes that note over: its release fades
  in 10 ms instead of ringing on. It used to leave two copies of one sample at nearly one
  pitch, heard as a unison thickening on lines of single notes whenever polyphony was above 1.
- Plugin only. The C# engine and AkaiS950Studio do not have it.

### Wide — new, and not the S950's

Every note as a detuned pair: flat and left, sharp and right, by the same amount, centred on
the true pitch and as loud as one voice (equal-power, −3 dB a half). **CC 107** on/off,
**108** detune (±0–50 cents, default 10), **110** spread (0–100%, default 70), **111**
offset start (the sharp half begins 7 ms in so the attack doesn't flange; on by default).
Mono and poly. At most **four notes**, since each note uses two of the S950's eight voices,
and pairs are stolen whole. Constant-pitch keygroups are left single.

### Polyphony limit and mono mode — new, and also not the S950's

**Polyphony** (1 to 8, on **CC 106** in bands of 16) caps how many notes can sound at once, so
at 1 no chord can be played. It is a limit, not a unison stack. At 1 the plugin is
mono and plays like a monosynth. Legato within a keygroup moves the sounding note without
restarting it, gliding if glide is on. Letting go returns to the newest key still held.
Detached notes and notes in another keygroup start fresh. A layered programme sounds only its
first keygroup in mono. Voices sounding above a lowered limit finish rather than being cut.
The editor's voice readout shows the limit.

## v0.4.0 — 2026-09-26

### Every calibration recording before this release was going through a limiter

This one comes first because it is why several numbers below moved, and because it is the
kind of mistake that hides inside good-looking data.

Every take from run 2 to run 17 was captured with a limiter pinning at −0.49 dBFS. In the
worst of them — runs 10, 11, 12, 15, 16, 17 — **20 to 30% of every sample in the file** sits
within 0.1 dB of that ceiling. The test is simple enough that it should always have been
run: a limiter stacks samples at its threshold and an honest recording does not.

    peak = max|x|;  count samples with |x| > peak × 0.9886

Under about 0.01% is clean. Over 1% is a limiter.

It cost a whole model. The positional crossfade was measured twice from limited audio and
came out as `cos(πx/2)^1.44` with the pair *dipping* 1.3 dB at the midpoint — "no standard
crossfade does that" was written in the notes at the time and treated as a curiosity rather
than as the symptom it was. Re-recorded clean, the answer is different by up to 4.75 dB.

Single-tone level readings survived nearly intact; what the limiter wrecked was the one
**two-tone** measurement, because a weak tone beside a limited strong one is dragged down by
the strong one's gain reduction. Frequency and timing measurements were barely touched — a
clipped sine's zero crossings do not move — which is why the filter's corner-derived times
could still be trusted as anchors when its levels could not.

### The positional crossfade, measured and modelled — new

Program header byte 21. **48 of the 390 library programmes have it on with overlapping
keygroups**, and they are the multi-sampled instruments: GRAND-PNO1 and 2 with nine keygroups
apiece, GRANDX, CB CEL VL. Every engine used to sound both keygroups at full level across the
overlap — about 6 dB too loud, with two different recordings of one note beating together.

A key's position in the overlap is `x = (i + 1) / (N + 1)`, and the attenuation comes from a
measured table rather than a formula. Seven overlap widths from 1 key to 21 agree to 0.1 dB
wherever two land on the same `x`, and two independent clean takes agree to 0.05 dB.

Every distinct level in the clean take is a whole number of **0.4 dB steps**, which turns out
to be the machine's own internal decibel step — the sustain plateau and zone loudness count
in it too, measured separately on different runs.

Three-deep overlaps multiply their pairwise fades (0.21 dB rms over thirteen readings, where
taking only the deepest single fade misses by 5.52). Two keygroups on *identical* keys are
not faded at all — 17 library pairs are exactly that, the ARP2600 layers, and they had been
playing 3.7 dB too quiet apiece.

### The envelope curve is a counter, and the low bit of the byte is ignored

Reading every setting from 45 to 56 one unit at a time:

| stored | 45 | 46 | 47 | 48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dB/s | 223.6 | 191.1 | 192.2 | 163.5 | 163.7 | 141.3 | 141.4 | 121.2 | 121.5 | 103.8 | 103.8 | 88.9 |

(46,47), (48,49), (50,51), (52,53), (54,55) — each pair identical to better than half a per
cent, where the step *between* pairs is fifteen. **Two stored units share each envelope
time.** So stored 51 plays what 50 plays, where the model used to interpolate something
between 50 and 52 and was up to 8% out.

Read by count rather than by byte the curve is geometric at **1.166 a step**, which explains
an alternation found earlier and left unexplained: five stored units is two counter steps or
three, and 1.166² = 1.360 against a measured 1.34, 1.166³ = 1.585 against a measured 1.60.

### `ENV_TIME`'s fifty-byte gap, measured

There was nothing measured between stored 0 and 50 — a hole holding **two in five of the VCA
releases on the real disks** — filled by interpolating between the ends. A release ladder and
a decay ladder across it agree rung for rung to 1.5%, so it is one curve, and the old guess
was out by up to 35%.

The cause was upstream of the gap: the table's **anchor at stored 50 was itself 16% slow**, so
everything interpolated below it inherited the error. `VcaReleaseDb` moved 40 → 42.5 with it.

### The VCA attack below stored 30, set as a byte

It had two points down there and *both* were reached sideways, through the velocity rule
rather than by setting the byte. Twelve settings measured directly:

| stored | 8 | 10 | 12 | 15 | 18 | 20 | 22 | 25 | 28 | 30 | 35 | 40 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 5.4/n | 300 | 208 | 142 | 93 | 68 | 56 | 45 | 35 | 28 | 24 | 17 | 14 |

The counter law holds all the way down — every one within 2% of `5.4/n` for whole n. Of the
two the table already had, stored 20 was right and **stored 8 was 11% out**.

### A sustain of 0 is silence, and both decays are rates

**1907 of the 1908 library keygroups set a decay and 723 decay to a sustain of 20 or less**,
so this is every plucked and struck sound on every disk. They used to stop dead 39.6 dB up
and sit there ringing. The plateau is a straight line in decibels from stored 99 down to 5 —
confirming `SustainDb` — but a stored 0 is not on that line at all: it falls straight past
and goes to silence.

The **amplitude decay is a rate, not a duration**: held at one setting across twelve sustain
depths it fell at 48.2 dB/s at every one. A duration would put every plateau at the same
moment whatever its depth.

The **filter's decay is a rate too**, which took a separate run to see because the first
attempt watched the filter's *release* while the amplitude was dying underneath it. Watched
against a steady level, `time = 0.354 + 0.235 × octaves` fits to within 0.008 s where a
duration is a flat line. So both envelopes are rates and the machine has one generator.

### Aftertouch — new

Keygroup byte 21, which every engine read and threw away, and which the plugin had no
channel-pressure handling for at all. It scales the LFO's depth from pressure exactly as
byte 22 does from the modwheel, and it turns out to be the same mechanism: **71.95 cents at
full against the wheel's 72.3**, the same proportional law (0.511 of full at byte 21 = 50,
where the wheel gave 0.509), and the two **add** — wheel alone 71.87 cents, wheel and pressure
together 149.79.

This was excused for a long time on the grounds that byte 21 is 0 in all 1908 keygroups of
the disks to hand. Those are one person's disks, and this reads anybody's.

### Constants corrected against clean recordings

| | was | now | |
|---|---|---|---|
| `LoudnessDbPerUnit` | 0.29 | **0.401** | one reading on a limited take; now ten rungs, 38% out |
| `EnvOctaves` | 8.5 | **8.3** | matched neither of two runs, nor its own comment |
| `WarpCentsPerUnit` | 6.25 | **6.44** | the first run ever to vary the depth byte |
| `VelDbPerStep` | 0.63 | **0.642** | measured directly for the first time |
| `VcfTimeScale` | 0.78 | **0.71** | was one filter decay against one amplitude decay |
| `VcaReleaseDb` | 40 | **42.5** | moves with `ENV_TIME`; their product is unchanged |

### The individual outputs stay in the main mix

Byte 19 sends a keygroup to ALL, to one of MONO 1–8, or hard LEFT or RIGHT. The eight
individual outputs were played centred, which was a labelled guess covering **253 library
keygroups** — if the machine dropped those voices from the main stereo pair, as many samplers
of that era do, every one of them should have been silent there. All eight read the same as
ALL to 0.4 dB. They stay.

### The browser app sounded only one keygroup per key

It returned the first keygroup whose range covered a note and stopped looking, so the page
played one sample where the hardware plays two — on exactly the multi-sampled programmes the
crossfade is about. It now sounds every keygroup that answers a key, and fades them.

### Velocity to attack is modelled, in all three engines

Keygroup byte 9. Every engine read it off the disk and threw it away, so 164 keygroups across
the 101 real disks played with no velocity response on their attack at all.

A harder strike makes the attack **shorter**, by plain subtraction from the attack byte before
the envelope counter ever sees it:

    effective attack byte = attack − (velocity / 127) × velToAttack     clamped 0..99

Measured on the hardware over eleven clips, a base attack of 70 and depths of 0, 30, 75 and 99.
Where the attack counter is itself measured, every clip lands within one counter step — which
is all the resolution a counter has:

| depth | vel | effective byte | measured n | the model |
|---|---|---|---|---|
| 99 | 1 | 69.2 | 4.0 | 4 |
| 99 | 16 | 57.5 | 7.0 | 6 |
| 99 | 32 | 45.1 | 11.0 | 12 |
| 99 | 48 | 32.6 | 22.0 | 23 |
| 30 | 127 | 40.0 | 14.1 | 15 |
| 0 | 1 / 127 | 70.0 | 4.0 / 4.0 | 4 |

The control passes exactly: at depth 0 the attack is 1.351 s at velocity 1 and 1.352 s at 127.

**There is no pivot.** Velocity to *filter* turns about 65, a soft strike going down where a hard
one goes up, and the obvious guess was that the attack matched it. It does not — at full depth
velocity 1 played 1.344 s against a base of 1.350, so a soft strike leaves the byte alone. A
pivot at 64 would have gated velocity 1 entirely, and the recording has a second of ramp on it.

v0.3.0 said this was not worth modelling because no keygroup in the bundled library set it — 0
in all 181. That library is generated by this repository, so all it ever said was what this
repository writes. The real disks say otherwise.

### The attack counter below stored 30, measured at last

A listed gap in v0.3.0: nothing reached below stored 30, so the stretch from the gate up to it
was one guessed number. The velocity rule reaches it sideways — a base of 70 struck hard enough
lands anywhere — and the guess was out by 3× and 6×:

| effective byte | 7.6 | 20.1 |
|---|---|---|
| measured n | 270 | 55 |
| the old guess | 1684 | 164 |

Entered at 8 and 20. This leans on the velocity rule, which is fair given it is confirmed against
seven clips in the region that *is* measured, but it sits a rung below the rest of the table, and
the 7.6 point is the weakest thing in it — 0.020 s is near the floor of what the analysis can time.

Stored 0 remains a hard gate. Extrapolating the new points downwards puts it near 7.6 ms, which
is the first evidence that ever bore on the question and is not enough to overturn how envelope
generators are built.

### The pitch wheel — new

The plugin now answers MIDI pitch bend, with a **Bend Range** parameter of 1 to 12 semitones,
which is the range the machine's own MIDI page offers. The browser has the same control beside
its MIDI input, and the desktop engine has `PitchBend` and `BendRange`.

An integer parameter because the panel's is: the machine offers whole semitones and nothing in
between, so a continuous control would invite settings the hardware cannot hold.

**The wheel reaches notes already sounding**, which is what separates it from velocity — that
is settled when the key goes down. It multiplies the playback rate alongside the LFO and warp,
recomputed once per control block, and therefore drags the filter with it exactly as any other
rate change does.

**The two halves of the wheel are not the same width.** There are 8192 steps below the centre
and 8191 above it, so dividing by 8192 in both directions leaves a full upward bend one step
short of the range — inaudible, and the kind of wrong that never gets found because nobody
measures a wheel at its stop. `bendRatio` divides by the right half either way, and the
conformance suite covers both stops deliberately.

Rendered end to end through the engine and measured with the calibration rig's pitch tracker,
every combination lands exactly: at range 12 a full bend down reads 500.0 Hz on a 1000 Hz tone
and a full bend up reads 2000.0 — an octave each way, to the digit. The suite grew from 1417
checks to **1477**.

**The range is not read off a disk**, because it is a setting of the machine rather than of a
programme. There is an `OVERALL SETTINGS` file on 99 of the 101 library disks, and byte 35 of
it is the only byte in the 1..12 band that varies — 7 in 80 disks, then 2, 4, 1 and 3. The one
real unit to hand is set to 7, which is consistent but is also exactly what a factory default
would look like. So it is a candidate and not a measurement, and it does not get to pick the
default; two does, because that is what almost everything defaults to.

### Time direction is not a playback setting — measured, and deliberately not implemented

Sample header byte 0x2B, `'N'` normal or `'R'` reverse, the setting next to the loop mode on
the sample page. Exactly one of the 1110 samples on the real disks is `'R'`: PHONE 3 on
DSKA0083.

**It does nothing at playback.** A sample whose data is written forwards, with 0x2B set to
`'R'`, plays forwards. Five clips say so: one-shot and looping, `'N'` against `'R'`, with a
struck note that falls 52 dB in half a second — played backwards it would climb those 52 dB
instead, and nothing else on the machine makes a sample swell. The traces are identical.

| clip | 0.02s | 0.26s | 0.42s | 0.66s |
|---|---|---|---|---|
| one-shot, N | 0.0 dB | −24.8 | −42.1 | −52.9 |
| one-shot, **R** | 0.0 dB | −23.7 | −41.1 | −52.1 |

The fifth clip puts the reversed sample under a VCA decay, where the three possibilities give
different slopes: −333 dB/s if nothing reverses, −125 if the audio reverses while the envelope
runs forward, +333 if the whole voice reverses. It measured **−398**, which is the first of
those and nowhere near the other two.

So Reverse is a **destructive edit**: the panel rewrites the sample data backwards and 0x2B is
a record of what was done. PHONE 3's audio is already backwards on its disk, and one in 1110 is
the rate you would expect for something a person did once on purpose.

**The right implementation is therefore none**, and that is what all three engines already do.
Getting this wrong is not neutral — reversing at playback would play PHONE 3 backwards from
correct. Checked separately: the byte survives an edit, so resaving a disk does not lose the
record. `rewriteSample` copies the header wholesale and overwrites only the length and loop
fields.

An earlier attempt at this, carried as a passenger in run 13, asked it of two *looping*
samples at a flat level and learned nothing: a loop can mask a reversal, and content with no
envelope has no order to reverse. Both faults were the same one — testing a setting without
asking what else has to be true for it to show.

### Alternating loops play alternating, and a loop of N frames is 2N long

Sample header byte 0x1A: `'O'` one-shot, `'L'` looping, `'A'` alternating. All three codebases
read it and the editor writes it back correctly — and every engine then collapsed `'A'` to a
plain forward loop, because the mode reached playback as a boolean and the direction was
dropped there.

**18 of the 1110 samples on the real disks are alternating**, and they are the ones where it
shows: CYMBAL 1, OP-HIHAT, CRASH 1, PIANO C3, CELLO B2 and B3, J STR C5 and C6, and a shelf of
ambient beds — WIND, RAIN, THUNDER, WATER, INSECTS, TRAFFIC, JET, ENGINE 2, LIGHTNING. The mode
exists to hide the seam on a long texture, so playing one forward-only puts back the click it
was chosen to avoid.

**A loop of N frames comes round every 2N, not 2N−2.** The frame at each end is played twice as
the direction turns rather than once, and both are ordinary ways to build a ping-pong. The
difference is a tenth of a semitone on a short loop, so it was measured rather than chosen: a
sawtooth of N frames a cycle, which a reversal turns into a triangle at half the pitch, played
at four loop lengths.

| loop | 2N−2 | 2N | measured | correlation |
|---|---|---|---|---|
| 20 | 38 | **40** | 40 | 0.9999 |
| 32 | 62 | **64** | 64 | 0.9999 |
| 50 | 98 | **100** | 100 | 0.9999 |
| 128 | 254 | **256** | 256 | 0.9996 |

The two forward controls read exactly N — 20 and 50 — which is what says the loop is where the
header puts it and the measurement is calibrated.

The C# and C++ voices reflect the read position about the loop end, which gives 2N with the end
frame played twice. The browser cannot: Web Audio's looper is forward-only with no ping-pong
mode, so the loop is written into the buffer twice, the second time reversed, and the native
looper runs forward over the pair. Same sound by construction, one buffer copy at note-on
instead of a ScriptProcessor for the life of the note. Verified to give period 2N.

### The output port, and the engines are stereo where the machine is

Keygroup byte 19, read off the panel: **0 ALL, 1–8 the individual MONO outputs, 9 LEFT,
10 RIGHT**, with the byte storing one lower so ALL sits at −1. Every one of the 1908 library
keygroups falls inside that range under the mapping, and 291 of them set it.

It was the last unexplained byte in the record, and it was audibly wrong. 38 keygroups across
four programmes are sent hard left or hard right on the hardware and came out dead centre in
all three engines. `TUBULAR 2` is the clearest case: four keygroups covering the whole keyboard
sent LEFT and four identical ranges sent RIGHT, a layered stereo-widening patch that collapsed
to mono. `PIZ-CHORUS` is two keygroups, one each side.

**LEFT and RIGHT are hard, not a pan law** — those are two mono sockets on the back of the
machine, so a keygroup sent to one is absent from the other.

**MONO 1–8 stay centred, and that is a placeholder rather than a measurement.** Those are eight
physical jacks, and what the main stereo pair does with a voice routed to one of them is a
question about hardware that no disk can answer. On many samplers of the era, assigning a voice
to an individual output *removes* it from the main mix — which would make 253 library keygroups
silent here rather than centred. Until someone plays one and listens to the main outs, centring
is the choice that cannot make anything worse: it is what all three engines already did with
everything.

The mono render paths are untouched and bit-identical — `render(buffer, count)` still exists and
still behaves exactly as it did, which is why every existing test passes unchanged. The stereo
overload is new and only the plugin and the browser use it. Verified across all eleven port
values: ALL and MONO 1–8 give equal energy both sides, LEFT gives 0.3536 / 0.0000 and RIGHT the
reverse.

### Warp — a pitch envelope, measured and modelled in all three engines

Keygroup bytes 12, 13 and 14, used by 98 of the 1908 keygroups on the real disks. Identified on
the panel long ago, read by the list tool, parsed by neither the web nor the C++ and modelled by
nothing. Three runs settled what they do.

**It is a pitch bend at note-on, decaying back to the nominal pitch:**

    bend in cents = 6.25 × byte13 × scale,   decaying as exp(−t / τ(byte14))

    scale = 1                                when byte 12 is 0
          = (byte12 / 99) × (velocity / 127) when byte 12 is above 0

Fitted across 25 clips from runs 11 and 12: **rms 7.3%, worst 17%.**

- **Byte 13 is the depth**, linear at about 6.2 cents a unit, and the signs mirror — ±50 gives
  ±3.1 semitones. 6.25 is used because it is 1/16 of a semitone exactly and fits as well as
  anything; the measurement cannot separate 6.0 from 6.5, so the round figure is a choice, not
  a reading.
- **Byte 12 is the velocity sensitivity of that depth, and 0 means off.** At 0 the bend is full
  however gently the key is struck — measured at velocities 1, 32, 64 and 127 as 364, 342, 320
  and 338 cents, no trend. At 99 it is proportional to velocity: flat at velocity 1, 155 cents
  at 64, 298 at 127. This matters for 22 real keygroups that set byte 13 with byte 12 at 0.
- **Byte 14 is the time**, on a curve of its own that looks nothing like `ENV_TIME`:

| byte 14 | 0 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 90 | 95 | 99 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| τ (ms) | 34.3 | 43.2 | 48.3 | 57.7 | 69.5 | 86.2 | 112.8 | 159.6 | 276.0 | 432.7 | 755.5 |

  Byte 14 = 50 comes from eight independent clips reading 68 to 70 ms; byte 14 = 99 from two
  clips in different runs reading 749 and 762. Note that 99 — 755 ms — is what **1529 of the
  1908** real keygroups carry.

**The decay is a true exponential**, holding to 2–3% from full depth down to a tenth of it.
Earlier fits reporting 30% departures were fitting the tail, where the bend is a few cents and
the measurement's own wobble is one or two.

**There is no key follow.** The same keygroup struck at keys 48, 60 and 72 — two octaves — gave
time constants of 64.2, 64.2 and 70.7 ms and depths within 8%. Both published descriptions of
Warp call byte 13 a key follow that shortens the decay as notes rise. It does not, and the
panel's name for it, ATTACK OFFSET, survives where theirs does not.

**Now implemented everywhere.** The bend multiplies the playback rate, recomputed once per
control block alongside the LFO — which drags the filter with it for free, since both the cutoff
and the anti-alias ceiling already derive from the rate the audio leaves at. The C# and C++
voices gained a `sinceOn` clock, because the existing one restarts at every envelope stage and
the bend is measured from the strike.

The browser schedules it as a value curve rather than `setTargetAtTime`. That is the obvious
tool and the wrong one: it decays the *rate* exponentially towards the target, where the machine
decays the bend exponentially in *cents*. The two agree only for small bends — at three
semitones they part by about seven cents one time constant in.

The conformance suite grew from 697 checks to **1417**, the new 720 covering warp across four
byte-12 values, five depths, three times, three velocities and four points along the decay.
Byte 12 = 0 is in that grid deliberately: it means "always full depth", not "no depth", and a
port treating it as a multiplier of zero would silence 22 real keygroups while passing any test
that only ever set it above zero.

### Every release in the model ran at twice the machine's speed

Not a velocity thing — this one reaches every note of every programme, and it had been there
since the engine was written.

A release ran the gain down to 1e-4 over `envSeconds(byte)`. That is 80 dB in one release time.
The hardware does **40**. At stored 70 the model reached −40 dB in 699 ms where the S950 takes
1366.

Run 9 is what exposed it, by timing sixteen releases across stored 20 to 95. Divide the 40 dB
each one took by the curve's time for its byte and the implied span is flat — twelve of the
sixteen give **41.0 dB, spread 0.7**, over a 200:1 range of release times. That is a constant
being measured, not a curve being fitted.

So the envelope table was right all along and the span was wrong, which is the better of the two
answers: the same table still serves the attack, the decay and the filter, and one number moves.

Two consequences beyond the rate:

- **It is now a rate in fact, not just in name.** The old code fell from wherever the key came up
  *to* 1e-4 over one release time, so a quiet note faded in the same wall-clock time as a loud
  one. It now falls 40 dB per release time from wherever it was, which is what "rate" means.
- **A release no longer ends on a timer.** Both engines also went idle at `t >= release`, which
  with a real rate chops the tail off however loud the note still is. The gain threshold alone
  decides now. A note at stored 99 takes about 20 s to free its voice where it used to take 10.7
  — which matches the hardware, whose −50 clip in run 7 was still ringing after 20 seconds.

Rendered back through the same analysis, the model now lands within 2–4% of the hardware at
every setting except four.

### Those four say the envelope curve is wrong around stored 45

The clips at stored 44.6 and 46 imply a 49.3 dB span where everything else says 41. They miss in
the same place, the same direction and by the same amount as the velocity-release fit's worst
misses, which is the signature of `ENV_TIME` being about 20% slow around stored 45 rather than of
either rule being wrong.

Left as measured. One bad setting in a nine-point table wants re-measuring, not a second constant
laid over the top.

### Velocity to release is modelled too, and it pivots

Keygroup byte 10, with bit 4 of the flags byte as its enable. Read off the disk and dropped by
every engine until now, and unlike the attack it turns about the middle of the keyboard:

    effective release byte = release + 2 × velToRelease × (velocity − 64) / 63    clamped 0..99

It took three runs to see, because the first two measured it with its own switch off. Setting
that bit and sweeping five velocities across four depths gave the whole shape. Time from key-up
to 40 dB down, base release 70:

| depth | vel 1 | vel 32 | vel 64 | vel 96 | vel 127 |
|---|---|---|---|---|---|
| +25 | 42 ms | 195 ms | 1349 ms | 8272 ms | 11162 ms |
| −25 | 11093 ms | 8325 ms | 1354 ms | 194 ms | 41 ms |
| +12 | 224 ms | — | 1365 ms | — | 6994 ms |
| −12 | 7081 ms | — | 1337 ms | — | 219 ms |
| 0 | 1366 ms | — | — | — | 1354 ms |

Four things come out of that, each of which had been an open question:

- **The pivot is 64.** Every clip at velocity 64 lands on the depth-0 value to within 15 ms
  whatever its depth. Fitted as a free parameter it comes out at 64 exactly; 60 and 68 are both
  clearly worse. Velocity to *attack* has no pivot at all, so this could not be assumed.
- **The sign simply negates.** Read the −25 row backwards against +25 forwards: 41/42, 194/195,
  1354/1349, 8325/8272. A mirror, not an approximation.
- **The depth is linear.** 12 gives half the slope of 25, to 2% — the first time any velocity
  depth in this model has had its linearity checked rather than assumed.
- **The multiplier is 2, not 1.** A depth of 25 swings the effective byte from 20 to 99, not
  from 45 to 95. Fitted freely it is 2.05, and 1.5 or 2.5 are far away.

The control passes: depth 0 reads 1366 ms at velocity 1 and 1354 ms at 127, so the enable bit on
its own does nothing to the release.

**With the switch off, every note is released as though its velocity were 1.** That is measured,
not assumed, and it is why the parameter looked inert for two runs: bit 0x10 is clear on all 1908
keygroups in the real library, and at velocity 1 a depth of −50 clamps to byte 99 and takes eleven
seconds while +50 clamps to 0 and is instant — which is exactly what the earlier runs read at
*both* velocities. Only the two extremes were tried with the switch off, so "treated as velocity
1" is the simplest thing that fits rather than the only thing that could.

One consequence worth noting: `EnvSeconds` in the C# engine took an `int`. Velocity moves the
release off the whole numbers, so it now takes a double, as the web and C++ versions always did.
An int would have truncated every velocity-shifted release silently.

### The velocity page's ON/OFF is bit 0x10 of the flags byte

Found by `kgdiff.js`, then confirmed by it a second time in the save described above. The
keygroup record is now down to thirteen unexplained bytes and four unexplained flag bits.

### The velocity switch was out by one, in all three engines

A keygroup holds two samples and byte 2 says where one hands over to the other. Every engine
read that byte as the *first* velocity of zone 2. It is the *last* velocity of zone 1.

Measured on the hardware, with a sine in zone 1 and white noise in zone 2 so that which one
answered needed no interpretation:

| switch | zone 1 through | zone 2 from |
|---|---|---|
| 1 | 1 | 2 |
| 64 | 64 | 65 |
| 90 | 90 | 91 |
| 127 | 127 | never |

The last line is what makes it certain rather than merely consistent. At a switch of 127 the
hard sample cannot be reached at all, which only follows if zone 2 begins at 128 — and that
is why the panel's range runs to 128 and why 128 means the switch is off. What used to be a
special case in the code saying so has gone: with zone 2 starting at `split + 1`, a split of
127 or 128 leaves it an empty range and the engines decline it on their own.

It is a switch and not a crossfade. Every clip read either 0.639 or 0.0001 of its energy at
the tone's frequency, with nothing in between anywhere near a boundary.

The practical effect is one velocity step at each switch point, which matters most where a
programme puts the switch low or high — and it is exactly the kind of error that survives
three implementations agreeing with each other.

## v0.3.0 — 2026-09-25

### The player's controls — new

Fifteen controls that sit on top of whatever programme is loaded, where before there was only
gain. Every one is an *offset* from what the disk says, reads zero until it is turned, and
double-clicks back to zero — zero meaning "play what is on the floppy". A programme carries
its own cutoff and envelope per keygroup, often quite different ones across the keyboard, and
an absolute control would flatten all of that the moment it was touched.

All fifteen are automatable by the host and all fifteen answer a MIDI controller:

| group | controls | CC | range |
|---|---|---|---|
| Sample | Filter | 74 | ±99 |
| VCF | Amnt | 70 | ±50 |
| VCA envelope | Attack, Decay, Sustain, Release | 73, 75, 79, 72 | ±99 |
| VCF envelope | Attack, Decay, Sustain, Release | 102, 103, 104, 105 | ±99 |
| LFO | Rate, Depth, Delay | 76, 77, 78 | 0..99 |
| Velocity | Freq, Loudness | 109, 112 | 0..99 |

72–79 are the General MIDI sound-controller numbers, so a keyboard with knobs already
labelled *cutoff*, *attack* or *vibrato rate* reaches the right ones unmapped. 102–105 and
109/112 are undefined numbers taken for the filter envelope and for velocity, which GM has no
assignments for.

The two envelopes are dragged as shapes rather than set as eight knobs, and the graph draws
the result for one representative keygroup — the programme's own values with the trim added —
so it shows what will be heard rather than an abstract curve.

Three behaviours worth knowing:

- **The LFO and velocity knobs only add**, 0..99, where the filter and envelope trims go both
  ways. Nearly every programme leaves the LFO switched off, so a symmetric knob spent its
  whole lower half asking for less than nothing.
- **Velocity → Loudness reaches the next note played**, not one already sounding: how hard a
  key was struck is settled when it goes down. Velocity → Freq does reach a sounding note.
- **The VCA attack steps rather than slides**, because on the hardware it is a counter. See
  below.

Not included: velocity to attack and to release. The keygroup carries both (bytes 9 and 10),
no engine models either, and no keygroup in the six-disk library sets either — 0 in all 181 —
so there is nothing to calibrate against and nothing that would play differently. Controls for
those would be controls over invented behaviour.

### The filter envelope has a release — new

It never had one. The filter fell back to the keygroup's own cutoff the instant a key came up,
whatever the release byte said.

### Measured corrections to the engine

**The envelope time curve was one measured point stretched over the whole range.** A VCA decay
at stored 80, and an assumed 10000:1 span that nothing had ever checked. The span was the part
that was wrong — it is nearer 1000:1 — so the old curve ran at about half the machine's speed
below stored 70 and nearly twice it above 85, and was right only at 80.

| stored | 50 | 55 | 60 | 65 | 70 | 80 | 85 | 90 | 95 |
|---|---|---|---|---|---|---|---|---|---|
| measured | 0.357 | 0.418 | 0.722 | 0.881 | 1.404 | 2.814 | 4.037 | 4.117 | 8.095 |
| was | 0.176 | 0.280 | 0.446 | 0.711 | 1.131 | 2.868 | 4.567 | 7.272 | 11.580 |

Attack, decay and release agree on this one curve to 1.07x where they overlap.

**The release is a rate, not a duration.** The release byte sets how fast the envelope falls,
so a release from half depth is over in half the time. The filter did the opposite — fall to
zero over the release time however far there was to go — while the amplitude in the same voice
did the right thing. Nothing had caught it because every release ever measured started from a
sustain of 99 and fell the whole depth, the one case where the two rules agree.

**The VCA attack is a counter.** Every one of thirteen measured settings is 5.4/n seconds for
a whole number n, to within 0.7%. That explains what no curve could: stored 70 and 75 return
identical attacks to four digits, as do 80 and 85, and 90 through 99 all saturate at 2.70 s
where the old model asked for 10.74. `AttackScale` is gone — it was a single multiplier on the
shared curve and no value of it could be right.

**Attack 0 is a hard gate.** Not measured — nothing reaches below stored 30 — but it is how
envelope generators are built, and a 30-unit extrapolation had put it at 40 ms, which is an
audible softening on every percussive sample.

**Three constants from an earlier run**: the envelope's full-amount depth 7.6 → 8.5 octaves,
the cutoff at stored 50 from 1878 → 2210 Hz, and a key-tracking pivot of note 62, which is new
— tracking does not pivot at middle C, and assuming it did quietly offset every cutoff read
from a keygroup with tracking on.

### The editor and the host

- Programmes are offered to the host as the plugin's programs, so a DAW's own program selector
  changes sound without opening the plugin window.
- A saved song carries the whole disk image, gzipped, rather than a path to it.
- Samples can be exported as WAV, and files copied between open images.
- A velocity fader beside the on-screen keyboard. Every click used to play at a fixed velocity
  of 100, which on the measured curve sits about 1.2 octaves above the pivot — so on any
  programme with velocity-to-filter the filter appeared to do nothing.
- A bundled drum kit, drawn rather than recorded.

### Known gaps

Measurement rather than code, and listed in `Plugin/README.md` in full:

1. `EnvOctaves` is 8.5 and two independent readings suggest nearer 7.8.
2. `SustainDb` says a decay bottoms out 39.6 dB down; the hardware falls at least 77, and
   "at least" is as far as the recording's noise floor allows.
3. Velocity to attack and to release are read off the disk and dropped.
4. Nothing measures the VCA attack below stored 30.
5. The filter attack may quantise like the VCA's; it was measured too coarsely to tell.

## v0.2.0 — 2026-09-21

The first release with the plugin in it.

## v0.1.0

The editor.
