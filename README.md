# Mz950

An independent emulator for playing back **Akai S900/S950 floppy disk images**. It doesn't
record anything: it loads the images a sampler saved and plays the programmes on them
through an emulation of that machine's voice (the filter, both envelopes and the vibrato,
eight notes at a time) from a MIDI keyboard. It comes as a
VST3 plugin and a standalone player, plus **Mz950 Studio**, an editor that opens, edits and
writes the disks back out.

Every constant in that emulation was **measured off a real S950**, not guessed at. A run
was written for the purpose, recorded, and read back; where a number is still an
assumption the source says so.

It plays a disk exactly as the machine would, with a few optional extras the machine never
had: glide, a polyphony limit with a mono legato mode, and Wide (a detuned stereo pair per
note). Each is labelled "not on the S950" and is off by default.

> **Not affiliated with Akai.** Mz950 is an independent project by Simon Moscrop. It is not
> affiliated with, endorsed or sponsored by Akai Professional or inMusic. Akai, S900 and S950
> are trademarks of their respective owners, used here only to say which disks and which
> machine it is compatible with.
>
> The project was called *VirtualS950* until it was renamed to Mz950. Some folder names in the
> source (`AkaiS950Engine`, `AkaiS950Studio`, …) still carry the old naming. They are internal
> and describe what the code works with.

![The editor, on a program's first keygroup](AkaiS950Studio/screenshot-keygroups.png)

## Using it

[**docs/tutorial.html**](docs/tutorial.html) is the walk-through: open a disk, hear it,
change something, find a loop, slice a break, add your own sample, write an image for a
Gotek, play it from a MIDI keyboard, and use the plugin in a DAW. The editor opens it from
**Help → Tutorial**, or <kbd>F1</kbd>, and the installer puts it in the program group.

The plugin's half is also in [Plugin/README.md](Plugin/README.md#using-it) — how a disk is
loaded, how programmes reach the host's own selector, what a saved song remembers (all of it:
the whole image rides in the project), and the fifteen player's controls with the MIDI
controller numbers that reach them.

[**CHANGELOG.md**](CHANGELOG.md) is what changed between releases, and is where the notes for
a tag come from.

## The parts

| | |
|---|---|
| **`AkaiS950Engine`** | the instrument. No UI, no disk format, no dependencies. |
| **`AkaiS950Studio`** | the editor: open a disk, look at it, change it, write it back. |
| **`AkaiS950List`** | the format — HFE and raw images, the directory, samples, programs. |
| **`AkaiS950Tests`** | the checks. |
| **`AkaiS950Synth`** | a workshop tool, not part of the app: writes disks of synthesised sounds from nothing. |

`AkaiS950Engine` is deliberately sealed off. It knows nothing about WinForms, nothing
about disks, and nothing about where its notes come from; its entire interface is
`SetPatch`, three note methods and `Render(float[], int, int)`. It allocates nothing in
the render path and never locks — a garbage collection inside an audio callback is a
click, and a click in a plugin is somebody's ruined take.

That is not tidiness. It is what would let a VST3 or CLAP wrapper host the same engine
without a line of it changing. See **A plugin, later** in
[the Studio's README](AkaiS950Studio/README.md#a-plugin-later) for what is missing and
what it would take.

## Building it

```powershell
.\build.ps1          # Mz950Studio.exe, beside the script
.\build.ps1 -Run     # and start it
```

No SDK, no package restore, no build system. `build.ps1` calls `csc.exe` from .NET
Framework 4.0, which has been on every Windows install for fifteen years. The cost is C#
5 — no string interpolation, no expression-bodied members — and the source is written to
that deliberately.

The `.csproj` files are there for an IDE. There is no .NET SDK on the machine this was
written on, so `dotnet build` has nothing to build with and the `net10.0-windows` in them
is aspirational.

The disk generator builds on its own, and deliberately not with the rest:

```powershell
.\AkaiS950Synth\build.ps1 -List            # the library it would write
.\AkaiS950Synth\build.ps1 -To D:\disks     # write it
```

It fills S950 floppies with waveforms worked out from their harmonics — nothing is
recorded and nothing is sampled. It has no window and no audio, it is not part of the
Studio and is not installed with it, and the only thing it shares is `AkaiS950List`,
because it has to write the same format.

Five of the six disks are tuned sounds: a few cycles of a waveform, looped, with the
machine's envelope and filter making an instrument of them. The sixth, `DRUMS`, is not.
A drum's envelope is the sound rather than a setting, so those samples are one-shots
with the whole shape baked in — three kicks, three snares, three toms, closed, pedal and
open hats, a clap, a rimshot, a clave, a ride, its bell and a crash, laid out close to
General MIDI. `Drums.cs` says how each one is drawn and why.

Drums are judged by ear or not at all, and a disk image cannot be played:

```powershell
.\AkaiS950Synth\AkaiS950Synth.exe C:\temp\drums --wav   # every one-shot as a WAV
```

## Checking it

```powershell
.\test.ps1                                   # the maths and the loop joins
.\test.ps1 -Images D:\disks                  # ...and every programme in a disk library
.\test.ps1 -Audio                            # ...and open the sound card and play a chord
```

Seven checks, answering different questions:

**`EngineCheck`** — the maths. Every mapping is compared against the number the web
version's `audio.js` prints for the same input, so the two implementations cannot quietly
drift apart. Then it renders and measures what actually comes out: noise through the
filter to find the corner and the slope, a decaying note to check the fall really is
straight in decibels, and a vibrato demodulated back out of the audio the same way the
calibration run measures it off the hardware.

It reads the vibrato back at 7.140 Hz and 151.1 cents where the law says 7.135 and 151.2,
and the decay in 6.9 dB steps reaching the floor at 2.87 s against a measured 2.868.

**`LoopClickCheck`** — the loop join, as a number. A click is a step, so the measurement
is the largest sample-to-sample jump in the rendered note against the largest one the
waveform makes on its own. A loop spliced a quarter-cycle out scores **6.0×**; snapping
its ends to zero crossings brings it to 1.2×; the crossfade brings it to **1.0×**, which
is to say there is no longer a join to hear.

**`WavCheck`** — the WAV export, against every sample on every disk in `disks\`. The
header is the easy half; what it really pins down is that the exported loop lands exactly
where the player puts it, worked out independently rather than by calling the same code.
That is not a formality — 37 of the 61 samples declare a loop longer than the sample they
belong to, so the clamping is the common path rather than the edge case:

```
  6 disks, 61 samples, 43 looping, 37 with a loop longer than the sample
  991 checks, ALL PASSED
```

The web version writes the same file from the same disks, and all 61 come out **byte for
byte identical** from the two separate implementations.

**`CopyCheck`** — copying between disks, across every pair in `disks\`. This one writes,
so most of what it asserts is about what a copy must *not* disturb: every file already on
the target is compared byte for byte afterwards, and the rebuilt image is reloaded with
`RebuildPointers` run over it, which must find nothing left to fix. The rename and skip
paths cannot occur in the corpus, so they are constructed:

```
  6 disks: 61 sample copies, 122 program copies, 16 already present
  8470 checks, ALL PASSED
```

The web version does the same work from the same disks; `test\copytest.js` there is the
other half.

**`KeygroupCopyCheck`** — the same for a keygroup, which is not a file. The target program
grows by 70 bytes in place and the keygroup arena moves under every program on the disk, so
the pointer check is the point rather than a formality, and the keygroups already in the
target must still say exactly what they said. Both directions, across disks and between two
programs of one disk:

```
  122 keygroup copies across disks, 122 within one disk, 118 sample(s) brought along
  10064 checks, ALL PASSED
```

**`PatchCheck`** — real Akai programmes, which is where the surprises are:

```
  101 disks, 390 programmes, 1908 keygroups
  1756 notes sounded, 152 were silent, 0 were wrong
```

The silent ones are not a fault: 164 of the library's keygroups name a sample that is not
on their own disk, which the format document already records as *1744 of 1908 match*.

**`AudioCheck`** — the only one that can tell you whether the WASAPI interop is right.
Whether a COM vtable is in the right order is not a question a buffer can answer.

## What it sounds like

Everything the machine does, from the measured numbers:

- the **filter**, 36 dB per octave, swept by its own envelope, with key tracking and
  velocity — running *after* the varispeed as on the machine, so its cutoff is a fixed
  number of hertz whichever key is held;
- both **envelopes**, the decay a straight line in decibels rather than in amplitude,
  because that is what the hardware was measured doing;
- the **LFO**: a sine, linear in rate, 1.53 cents of depth per unit, and a delay that
  fades the wobble in rather than waiting. Voices share one oscillator or run their own
  according to the desync bit — measured by watching two notes drift for six seconds,
  not assumed;
- **velocity** to loudness and to filter, **zone** loudness and tuning, **constant
  pitch**, **one-shot**, **looping**, eight voices with both zones of a keygroup
  sounding, and **MIDI in** including the modwheel.

### One deliberate departure

The machine splices its loops: it plays to the loop end and jumps back, and if the two
points do not match it clicks — once every time round, forever. This does not. The loop
ends are pulled onto matching zero crossings and the join is crossfaded over a few
milliseconds.

That is the only place the instrument knowingly sounds better than the hardware, and it
is a setting rather than a fact:

```csharp
instrument.LoopCrossfadeMs = 0;       // splice it, as the machine does
instrument.SnapLoopsToZero = false;
```

## Latency

Shared-mode WASAPI with an event callback — 22 ms at 44.1 kHz on the machine this was
written on. Playable, and two orders of magnitude better than the `SoundPlayer` it
replaced, but not what an interface is capable of. Exclusive mode reaches three or four
milliseconds and is the obvious next thing.

## The other half

The same format work, and the same measured constants, exist as a browser tool:
[**AkaiS950Web**](https://github.com/simozzer/AkaiS950Web) — which is also where the
calibration apparatus lives, and where the S950 disk format is written up in sixteen
pages. The two are checked against each other on purpose.

## Licence

AGPLv3 — see [`LICENSE`](LICENSE).

That follows from the plugin rather than being a preference: it links JUCE, which since
version 8 is AGPLv3 unless you have bought a commercial licence. Licensing this the same way
means the repository's terms and the JUCE parts' terms are one thing rather than two sets
living in one project. The editor uses no JUCE at all, but one licence across the repository
is simpler than a rule about which half you are looking at.

In practice: anyone given a binary is entitled to the source it was built from, under the
same licence, which a public repository satisfies.

```
Copyright (C) 2026 Simon Moscrop

This program is free software: you can redistribute it and/or modify it under the terms
of the GNU Affero General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this
program. If not, see <https://www.gnu.org/licenses/>.
```
