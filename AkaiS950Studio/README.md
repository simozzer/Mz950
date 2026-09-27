# Mz950 Studio

A WinForms browser and editor for Akai S900/S950 floppy images. (Mz950 is independent and not
affiliated with Akai or inMusic; it was called Akai S950 Studio before the project was renamed.) Load, inspect,
edit parameters, and write the result to a new image — the file you opened is
never modified.

![Keygroup view](screenshot-keygroups.png)

## What it does

- Opens a single `.hfe` or raw `.img`, or a whole folder/drive of them
- **Open** replaces what is loaded; **Add Image...** appends to it, skipping anything
  already open. Same-named images from different folders are labelled with their folder
- Warns before an Open or Close All that would discard unsaved edits
- Tree of disks, grouped by file type
- Full field detail for every file type
- Per-keygroup sample references for programs, flagged when the sample lives on another disk
- A velocity strip on the right of the keyboard row, because how hard the key is struck
  is most of what the filter hears
- A piano keyboard above the keygroup list: every mapped range gets a pale wash, the
  selected keygroup's range is highlighted solid, and clicking a mapped key selects
  the keygroup that owns it **and plays it at that key's pitch**
- Waveform display with the start, loop and end markers drawn, and the loop region
  shaded — the loop is the tail running back from the end marker by the loop length,
  not the whole marked span
- Plays samples: select a sample, or a keygroup, and you hear it. The 12-bit
  split-packed disk data is decoded to 16-bit PCM and played at its recorded rate —
  no filter and no envelope, but trimmed to its start and end markers, and a looping
  sample sustains on its loop rather than stopping dead
- Adds samples to a disk from WAV or AIFF — and from MP3, FLAC, Ogg and M4A where
  `ffmpeg` is on the PATH — converting to the S950's 12-bit format
- Previews audio files as you click them, waveform and all, before you import one
- Slices a break into one-shots on detected onsets, mapping them to consecutive keys
- Creates and deletes programs, adds and removes keygroups, renames and deletes samples
- Stretches a sample to a new tempo, halves its rate, trims leading silence
- Finds a loop in a sustained sample, and says how clean the join is
- Edits several keygroups at once, and sets a key range by clicking the keyboard
- Writes the result as either `.hfe` or a raw `.img`, whichever the disk came from
- Copies a sample or a program onto another open disk. A program brings every sample its
  zones name; a name already taken by something else is renamed rather than overwritten
- Copies a single keygroup onto another program, on the same disk or another open one,
  bringing the samples its zones name with it
- Exports any file exactly as stored on the disk
- Exports a sample as a 16-bit WAV at its own rate, with the loop and the root note
  written into the file's `smpl` chunk rather than baked into the audio
- Opens on the disks that were open last time; the very first run opens the bundled
  sound library instead, so there is something to play with straight away
- Reports read integrity — bad-CRC and unreadable sectors, per disk

Decoding 101 images takes about 20 seconds; it runs on a background thread with a
progress bar, so the window stays responsive.

## Running it

```
Mz950Studio.exe                reopen the last session
Mz950Studio.exe E:\            load every image on the stick at startup
Mz950Studio.exe disk.hfe       load one image
```

### How hard it strikes

The narrow fader on the right of the keyboard row sets the velocity a click carries, 1 to
127, and it matters more than it looks. Velocity opens the filter: at the measured 8.34
octaves of full-scale velocity tracking, the pivot is 65, so the fixed strike of 100 the
keyboard used to play sat about 1.2 octaves above it. On a programme written with any
velocity-to-filter at all, that is the difference between hearing the filter and not.

The same programme, the same note, the cutoff a fifth of a second in:

```
  velocity  20   BASS/SQ BASS    847 Hz
  velocity  40   BASS/SQ BASS   1341 Hz
  velocity  64   BASS/SQ BASS   2329 Hz
  velocity 100   BASS/SQ BASS   5328 Hz
  velocity 127   BASS/SQ BASS   9911 Hz
```

Three and a half octaves of filter, and none of it reachable while the keyboard struck at
one fixed velocity. It still starts at 100, so nothing sounds different until the fader is
moved; drag it, or roll the wheel over it for one step at a time, ten with Ctrl held.

It is drawn rather than a TrackBar, which is forty-odd pixels of chrome before it draws
anything and disagrees with itself about which end of a vertical one is the maximum. This
is twenty pixels wide, reads high at the top the way a fader does, and says its number
underneath. `VelocityCheck` drives it through its own mouse handlers, the way WinForms
would, and finds the ends of the groove by feel rather than by arithmetic so that it tests
the mapping rather than the layout.

### A filter that seems to do nothing

If it does, the programme is probably written wide open rather than the filter being
broken. The cutoff ceiling is `0.37 x the rate the audio leaves at`, and the curve reaches
it well before the stored value reaches 99: on a 40 kHz sample, a zone filter of 84, 86,
88 and 90 all land on exactly 17600 Hz. Two thirds of the shipped library is written at or
past that point, which is why `ORGAN` and `GLASS BEL` cannot be filtered at all while the
`BASS` programmes, written at 34 to 52, obviously can.

So: a dark strike on a BASS programme is the filter working. A hard strike on an organ is
the filter having nothing left to close.

### What it opens with

A path on the command line is an instruction and beats anything remembered. With no path,
it reopens whatever was loaded when it last closed.

The very first run has no such history, so it opens `BASS.hfe` from the bundled library
rather than meeting a newcomer with an empty window and a file dialog. Closing everything
before quitting is remembered too: that was a decision, so the next launch opens empty
rather than putting the library back.

The list is one path per line in `%APPDATA%\AkaiS950Studio\session.txt`. Delete it to
start as though for the first time. Its existence is also how the first run is recognised,
which is what separates "never been here" from "was here and closed the disks on purpose".

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open disk image |
| `Ctrl+Shift+O` | Open folder |
| `Ctrl+E` | Export selected file |
| `Ctrl+W` | Export the selected sample as a WAV |
| `Ctrl+I` | Add a sample to the selected disk |
| `Ctrl+N` | New program on the selected disk |
| `Ctrl+L` | Slice the selected sample into one-shots |
| `Ctrl+Delete` | Delete the selected sample or program |
| `Ctrl+P` | Play the selected sample or keygroup |
| `Ctrl+.` | Stop playback |

Double-clicking a sample in the tree, or a keygroup row, also plays it. **Play > Play on
Selection** turns the automatic playback off if you would rather only hear it on demand.

### Pitch

Clicking a key on the keyboard plays the keygroup's sample at that key's pitch, the way the
sampler does it — by varispeed, so duration moves with pitch rather than being stretched.
The shift is `(key - nominal pitch) + zone transpose + zone fine / 256`, where the sample's
nominal pitch is the tuning word's whole-semitone part and is the key at which it sounds at
its recorded rate. A keygroup with **constant pitch** set ignores the key entirely and plays
at the transpose offset alone, which is what a drum keygroup wants.

Selecting a keygroup *row* still plays the sample as recorded, unpitched — the row is for
auditioning the raw sample, the keyboard for hearing it in place.

Extreme keys can call for a rate outside what the audio stack will take; past roughly 4 kHz
to 96 kHz the audio is converted instead so the pitch still comes out right.

## Building

The included `Mz950Studio.exe` was produced without an SDK, by compiling against
.NET Framework 4.x. It runs as-is on Windows.

For a modern build, install a .NET SDK and:

```
dotnet build -c Release
```

The project targets `net10.0-windows`, matching the WindowsDesktop runtime already
present on this machine. The `.csproj` shares `Hfe.cs` and `AkaiDisk.cs` with the
command-line tool in `..\AkaiS950List` rather than duplicating them, so a fix to the
decoder benefits both.

## Layout

| File | Contents |
|---|---|
| `Program.cs` | entry point; passes command-line paths to the form |
| `MainForm.cs` | the whole UI — tree, detail pane, keygroup pane, loading, export, save |
| `Editors.cs` | PropertyGrid adapters for programs, keygroups and samples |
| `PianoKeyboard.cs` | the keyboard strip that maps keygroups onto the keys |
| `VelocitySlider.cs` | how hard the keyboard strikes, drawn as a narrow fader beside it |
| `WaveformView.cs` | waveform envelope, markers and time ruler |
| `SamplePlayer.cs` | wraps decoded PCM in a WAV header and plays it |
| `WavFile.cs` | the WAV writer, including the `smpl` loop chunk — used by the export and by the player |
| `Session.cs` | what was open last time, and the disk a first-time user starts on |
| `AudioImport.cs` | WAV/AIFF decoding, rate conversion, 12-bit quantising, onset detection |
| `AudioFileDialog.cs` | the file browser that previews what you click |
| `ImportDialog.cs` | the Add Sample dialog, including the capacity check |
| `StructureEdits.cs` | the commands that add or remove whole files, and their dialogs |
| `SliceDialog.cs` | the slice preview: live cut markers and the cost of the plan |
| `..\AkaiS950List\Hfe.cs` | HFE parsing and MFM decode |
| `..\AkaiS950List\HfeWrite.cs` | MFM re-encode, in-place sector patching, and building an HFE from nothing |
| `..\AkaiS950List\AkaiDisk.cs` | directory, FAT, file reads, sample headers, keygroups |
| `..\AkaiS950List\AkaiDiskEdit.cs` | the edits that resize or reorder files: delete, programs, slicing |
| `..\AkaiS950List\AkaiDiskCopy.cs` | copying a sample, a program or a keygroup onto another disk, planned before it is written |

Disk format notes are in `..\AkaiS950List\S950-Disk-Format.pdf`.

## The working area

The tree is down the left. Everything else is arranged the way a keygroup is thought
about: the **keyboard** across the top, since a keygroup is a range of keys before it is
anything else; under it a narrow **column of keygroup numbers** on the left, with the
**editor and both envelopes** filling the rest of the row; and the **waveform along the
bottom**, so it stays visible whichever keygroup is being worked on.

The number column carries only the number. Every other column it used to have — key
range, envelopes, filter, zone samples — is a field in the grid beside it, so the list
stays narrow and the editor gets the width. The row tooltip carries the detail (*Keys 48
- 59   AMEN175*), and a keygroup whose sample is not on this disk stays marked in amber.

A sample has no keygroups and no key ranges, so the keyboard and the number column stand
down for one and the editor takes the width. A disk has neither, and the waveform goes
too.

Selecting a program picks its first keygroup for you, so the keyboard shows a range and
the waveform a sample — but the grid keeps the program's own properties until a number is
clicked, since that is what asks for the keygroup instead.

## Editing

Select a program or sample in the tree, or a keygroup number in the list, and the
**Edit** tab shows its parameters. Changes are written straight into the loaded
disk image; the title bar and File menu mark the image as having unsaved changes.

**File > Save Disk Image As...** (Ctrl+S) writes a new image. It will not let you
overwrite the file you opened.

### Containers

Both are read; both can be written, and the extension you choose decides which — the
filter starts on the one the disk arrived in, but typing `.img` is enough to get one.

- **HFE** — the Gotek/HxC container. When the disk was opened from an HFE, that file is
  the template: its sectors are patched in place, so the MFM bitstream, gaps and sync
  marks around them are left exactly as they were.
- **IMG** — the bare 800K sector image: 80 cylinders x 2 sides x 5 sectors x 1024 bytes,
  819,200 bytes. This is what the editor has been working on all along, so writing it is
  a copy.

Converting an `.img` to `.hfe` works too: with no bitstream to patch, the container is
built from scratch — header, track list, and for every one of the 160 track/sides the
gaps, sync fields, index and address marks, CRCs and clock bits.

The layout was not assumed from the IBM System 34 spec but measured from the library,
where all 101 disks agree exactly: one identical 512-byte header, 25000 bytes per track,
sectors 1..5 with no skew, the first ID address mark at bit 4928 of every side, and the
gap fill running on through the 44 bytes of padding at the end of each side's last
256-byte chunk. Because of that the encoder is held to reproduction rather than mere
validity: rebuilding every disk in the corpus from its sectors alone reproduces the
original file **byte for byte**, on all 101.

### How writing works

Every Akai sector is 1024 bytes, so an edited sector occupies exactly the same
span of MFM cells as the original. The writer therefore patches each sector's
data field and CRC where it already sits, and never touches sync marks, ID
fields, gaps or track timing. Re-encoding an unchanged sector reproduces its
bits exactly - verified across a whole image, 800 sectors, every track
bit-identical - so patching every sector is safe.

After writing, the new image is immediately loaded back and checked. If any
sector fails CRC the save is reported as failed and you are told not to use the
image. A clean save reports the file count and zero bad sectors.

## Adding a sample

**File > Add Sample to Disk...** (Ctrl+I) reads an audio file, converts it to the
sampler's format and writes it into the selected disk image.

### Hearing it before you choose it

The file browser previews what you click. Selecting a file decodes it, draws its waveform
and plays it, so a folder of takes can be auditioned where you are choosing between them
rather than one import at a time. **Play on selection** turns the automatic playback off if
you would rather only hear it on demand, mirroring the same option in the Play menu.

This is why it is not the standard Windows file dialog: that one offers no managed hook for
"the selection changed", so hearing a file before committing to it means listing the folder
ourselves. Double-click opens a folder or accepts a file, Backspace goes up, and typing a
path — of a folder, or of a file — goes there.

Decoding runs off the UI thread, because an ffmpeg decode is a subprocess and a temp file
rather than an instant. Clicking down a list faster than the decodes finish is the normal
case rather than the exception, so each carries a generation number and a result that
arrives after the selection has moved on is dropped. A file that will not decode says why
on the spot and leaves **Add** disabled, rather than failing after you have committed to it.

The audio is decoded once: the picker hands what it read to the conversion screen instead
of the file being read a second time.

### Formats

Read natively: **WAV** and **AIFF/AIFC**, PCM at 8, 16, 24 or 32 bits or IEEE float at
32 or 64, any channel count, any sample rate. **MP3, FLAC, Ogg, M4A and the rest** are
decoded only if an `ffmpeg` happens to be on the PATH — when there is none, the browser
says so rather than leaving you wondering why a file will not open. Stereo is mixed to mono.

The conversion is: mix to mono, resample (windowed-sinc, with the low-pass widened when
converting downwards so it does not alias), optionally normalise, then quantise to signed
12-bit. Anything above 44.1 kHz is brought down to it, since that is the sampler's ceiling.

The rate list offers **12,500 Hz up to 44,100 Hz**, plus "keep the source rate" — the values
the corpus actually contains, minus the one-off oddities that look like varispeed. Dropping
the rate is the most effective way to fit more on a disk: the same free space that holds 1.86
seconds at 44.1 kHz holds 6.59 seconds at 12,500 Hz.

**It cannot overfill a disk.** The dialog knows the free block count and shows what the
sample will cost as you change the rate or length; the Add button stays disabled until it
fits, and **Trim to Fit** sets the length to the longest that will. Duplicate and empty
names are refused the same way. The disk-writing layer checks independently, so a bad call
throws rather than corrupting an image.

Nothing already on the disk is moved or rewritten. The new file takes free blocks and a
directory slot after the last sample, which is what keeps the derived RAM fields correct.

## Copying between disks

Right-click a sample or a program and **Copy to** lists every other disk that is open.
Nothing is written until the whole set fits, and a box first shows exactly what would
land and what it costs.

A program is never copied alone. Its zones name their samples by name, so every sample
they name comes with it - otherwise it would arrive silent. That makes a copy quietly
larger than it looks, which is why the confirmation itemises it.

### Nothing on the target is replaced

A name already taken by a *different* file is renamed: `BASSLOOP` arrives as `BASSLOOP2`,
and the copied program's zones are repointed at the new name so it still plays what it
came with. The file already on the target is left exactly as it was, because replacing it
would silently change how that disk's other programs sound.

A name taken by the *same* file - same bytes, ignoring the fields that belong to the disk
rather than the file - is recognised and skipped. Copying a program twice therefore costs
nothing the second time and writes no duplicate samples.

### What a copy has to recompute

Two numbers in a sample's header belong to the disk it was living on rather than to the
sample: where it sits in the sampler's RAM (`0x36..0x38`) and where its loop descriptors
sit (`0x28`). `RebuildPointers` does not touch either - it rebuilds the keygroup arena,
not the sample table - so `AkaiDiskCopy` recomputes them from the last sample already on
the target, exactly as `AddSample` derives them for a new one. A program also takes a free
program number, since two programs sharing one is a conflict the sampler settles by
playing whichever it reaches first.

Everything else is carried over untouched, which is the point of copying the file rather
than re-adding it from its audio: the rate, the tuning, the loop markers, the loop mode
and direction and the loudness all survive.

### One keygroup, onto another program

Right-click a keygroup row and **Copy Keygroup N to...** lists every program that is open
except the one it came from - copying a keygroup onto its own program is what **Add
Keygroup** already does. Unlike a file copy this may end on the disk it started on:
moving a keygroup between two programs of the same disk is an ordinary thing to want, and
the samples it names are then already there, which the plan works out for itself.

The keygroup lands at the end of the target program. Its place in the chain carries
nothing the sampler reads - the key range decides what sounds - so there is nothing to be
gained by inserting it anywhere else. Only the zones of that one keygroup are consulted,
so it brings the one or two samples it actually names rather than the whole program's.

It is confirmed only when something beyond the keygroup is written. A keygroup landing on
a program whose disk already holds its samples costs one record and is undoable, and
stopping to confirm that is friction rather than safety.

### How it is checked

`CopyCheck` copies every sample and every program across every pair of disks in the
library - 183 copies - and asserts rather more about what must *not* change than about
what does:

```
6 disks: 61 sample copies, 122 program copies, 16 already present
8470 checks, ALL PASSED
```

Every file already on the target is compared byte for byte afterwards; the copied audio
is compared against the source; and the rebuilt image is reloaded and `RebuildPointers`
run on it, which must find **nothing left to fix**. A pointer this code failed to
recompute shows up there as a non-zero count.

The corpus never collides, so the rename and skip paths are set up by hand: a sample is
planted on the target under a name the incoming program needs, holding different audio,
and the copy must rename what it brings, repoint the program's zones, and leave the
planted file alone.

`KeygroupCopyCheck` does the same for keygroups, in both directions - across disks and
between two programs of one disk:

```
122 keygroup copies across disks, 122 within one disk, 118 sample(s) brought along, 5 renamed
10064 checks, ALL PASSED
```

A keygroup is not a file, so it writes differently: the target program grows by 70 bytes
in place and the arena moves under every program on the disk. That makes the pointer check
the point rather than a formality, and it checks something the file copy does not need to -
that the keygroups already in the target program still say exactly what they said, since a
record spliced onto the end must not disturb the ones in front of it.

The web version does all of this too - `test\copytest.js` and `test\keygrouptest.js` there
are the other halves. Both report the same counts from the same disks.

## Taking a sample out

**Export Sample as WAV...** (`Ctrl+W`, or right-click a sample) writes a 16-bit mono WAV
at the sample's own rate. It is the other half of **Export Selected File**, which writes
the file exactly as the disk holds it — the right thing for putting back on another disk,
and no use at all in a DAW.

Everything the sample stores comes out. The audio is not trimmed to the markers and the
loop is not baked into it; both travel in the file's `smpl` chunk instead, together with
the root note taken from the sample's tuning. A sampler or DAW that reads that chunk —
most do — picks the loop up by itself, and one that does not still gets the whole sound
and plays it through. Nothing is discarded either way, so the markers can still be moved
afterwards: the audio outside them is still there.

### Where the loop lands

The loop is the tail running back from the **end** marker by the loop length, not the
whole marked span — the same reading the waveform display and the player use. The
clamping that goes with it matters more than it looks:

```
6 disks, 61 samples, 43 looping, 37 with a loop longer than the sample
```

Thirty-seven of the sixty-one declare a loop length longer than the sample it belongs to.
`BASS.hfe`'s `SQUARE` is 1222 samples long and declares a loop length of 1223, so a loop
start worked out by subtraction alone lands before the start of the sound. `WavFile.cs`
clamps exactly the way `SamplePlayer.ApplyMarkers` does, and `WavCheck` works out where
the loop should land independently rather than by calling the same code — two pieces of
code agreeing because they are the same code proves nothing.

`smpl` names the last frame *inside* the loop, where the Akai end marker is one past it,
which is the other off-by-one the check pins down.

### The same file from both programs

The web version writes this file too, from the same disks, and the two are separate
implementations. All 61 samples in the shipped library come out **byte for byte
identical** from both — `test\wavtest.js` in the web repository is the other half of
`WavCheck`.

## Adding and deleting keygroups

Right-click the keygroup list for **Add Keygroup** and **Delete Keygroup**. A new keygroup is
copied from the row you right-clicked, so it arrives playable rather than blank; edit it from
there. A program must keep at least one, and cannot exceed 64.

Both change the program's length — `38 + 70 x keygroups` — so the file is rewritten and its
block chain grown or shrunk, taking a block from the free pool or handing one back as needed.

### The keygroup chain

Keygroups are a linked list. Bytes 68–69 of each record hold the RAM address of the next one,
70 bytes on, and the last record holds zero — true of all 392 programs in the corpus without
exception. Adding or deleting relinks the whole chain from the program's base address.

That base is not stored anywhere. Each record points at the *next*, so the first record's
address is the first pointer minus 70. A program with a single keygroup stores no pointer at
all, so its base is inferred from the program before it, and failing that from `0xC5F6`, where
87 of the 96 multi-keygroup disks begin.

Those pointers sit in a RAM arena shared with the sample descriptor table that zone pointers
index, laid out in directory order. A program changing size therefore moves everything above
it, and both the later programs' chain pointers and the zone pointers are shifted to match.
Shifting rather than re-deriving preserves the irregular spacings the corpus shows in 16 of
156 consecutive program pairs.

**None of it turns out to matter to the sampler** — see the next section. It is done anyway so
that a disk written here is structurally what an S950 would have written.

### How that was established

**14 September 2026.** A disk was written with every keygroup chain pointer deliberately
corrupted to `0x5A5A` and everything else left intact, then loaded into an S950. Its programs
played correctly. The sampler rebuilds the RAM arena when it loads a disk and does not read
the stored pointers, so the arena arithmetic above — including the inferred base address for
single-keygroup programs — cannot affect whether a disk works.

The command that produced those corrupted images has been removed; it was a diagnostic, and
keeping a "break this disk" button in a disk editor is not worth the accident it invites.

## Deleting a sample

**Delete** on a sample, from the tree's right-click menu or Ctrl+Delete, behind a
confirmation that names every keygroup zone that will be emptied. That list is the point
of the confirmation: a sample is referenced by name from programs that may not be on
screen, and the S950 gives no hint that a zone has gone quiet.

Three things move together, or the disk is left inconsistent:

- zones naming the sample are cleared to the unused placeholder;
- zones naming a **later** sample have their pointer pulled back one record — those
  pointers are a sample's position in directory order, and every later sample has just
  moved down one;
- later samples shift down in sample RAM by the space this one occupied.

Then its blocks return to the free pool and its directory slot is cleared, and the
directory is closed up behind it. That last step matters: every one of the 100 factory
images holds its files in slots 0..n-1 with no holes, so the sampler is entitled to read
the directory until the first empty slot and stop. A gap left by a delete would make
everything past it invisible.

Checked against the whole corpus: a sample deleted from the middle of every image, and
the disk verified afterwards — every other sample byte-identical and still readable, the
free count exactly right, no zone left naming a missing sample, and the result surviving
a save and reload.

```
disks tested      : 99
zones emptied     : 168
pointers adjusted : 784
blocks recovered  : 5856
problems          : 0
```

## Programs

**New Program** (Ctrl+N) creates a program holding one empty keygroup across the whole
keyboard, ready to have its zones pointed at samples. The MIDI program number defaults
to the lowest the disk is not already using, so two programs do not answer to one program
change by accident.

Rather than invent a program from nothing, the header and keygroup it starts from are the
byte-by-byte mode of the library's 388 programs and 1902 keygroups, so a program this tool
creates is indistinguishable from one the Akai wrote except in the fields that must differ.
It is placed after the last program rather than at the end of the directory, because
programs come before the overall settings, the drum set and the samples — 98 of the 100
library disks are exactly that order.

**Delete Program** is the one structural delete with no references to chase. Nothing on a
disk refers to a program — the drum set's voice indexes are just 0..7, and no field
anywhere holds a program address — so it only has to free the blocks, close the directory
and rebuild the arena around one fewer program. The samples it played stay where they are.

Across the corpus, creating a program and deleting it again leaves every file on the disk
reading back identically, on all 101 images.

## Slicing a break into one-shots

**Slice into One-shots** (Ctrl+L) on a sample cuts it on its detected onsets and writes
each piece as its own sample, leaving the original alone. Point it at a program and it
also adds one keygroup per slice from a root key upward, each tuned to the key it sits on,
so the break arrives playable across the keyboard.

The detector is a peak envelope on a ~4 ms hop, tracked by a decaying peak-follower. A
slice starts where the envelope jumps above the follower by more than the rise ratio, is
loud enough in absolute terms, and is far enough from the previous slice. No FFT: on a
mono 12-bit break, an energy rise is what the ear is calling a hit anyway.

**Sensitivity** is one dial from 0 to 100 that moves two things at once — how big a jump
has to be, and how quiet a hit may be — because separating them gives two dials that only
make sense together. 50 lands on the eighths and sixteenths of a breakbeat. It is cheap
enough to rerun on every twitch of the slider, so the cut markers on the waveform and the
cost below it are always live.

The cost matters, because slicing is the one operation that can exhaust three limits at
once — directory slots, disk blocks and the sampler's own memory — and the sampler's
memory is not something the disk can tell us, so it is a setting rather than a fact. The
whole plan is worked out and reported before anything is written, and the button stays
disabled until it fits.

## Editing several keygroups at once

The keygroup list takes a multiple selection — ctrl-click adds one, shift-click takes the
run — and every control then writes to all of them: the property grid, both envelopes and
the VCF amount. That is how one filter setting goes across a whole drum kit without
typing it eight times.

**The list is the only place a selection is built.** Clicking a key on the keyboard is
playing a note, not picking a keygroup, so it moves the selection to whichever keygroup
owns that key and drops the rest — the keyboard has no modifiers to say anything else
with. Right-clicking a row outside the selection picks it; right-clicking inside one
leaves it alone, so *Set Key Range of 5 Keygroups* still means five.

The selection stays visible when the focus moves to the grid, the envelopes or the
keyboard. A list view hides it by default, which would leave the one thing you need to
see before typing a value invisible at the moment you type it.

The grid shows the **focused** row, the one clicked last, and the keyboard marks it solid
with the rest of the selection a shade behind it. The status line says what is about to
happen — *Keygroup 3 shown; edits reach all 5 selected keygroups* — and it is one undo
however many were touched, since the baseline is the whole image either way.

A flag is the one bit, not the whole byte. Writing the byte would carry the focused
keygroup's other flags onto the rest of the selection, which is not what setting *One
shot* on five keygroups means.

## Setting a key range by pointing at it

Two boxes wanting MIDI numbers are a poor way to say something the keyboard says better.
**Set Key Range from the Keyboard**, on the keygroup list's right-click menu, arms it: the
next click on the keyboard is the low key and the one after it the high, with the keys
shaded warm as you move between them and the caption naming the range. The same key twice
gives a one-key group, the two clicks work in either order, and Esc or a right-click calls
it off. It writes to the whole selection under a single undo.

### How that was checked

`..\AkaiS950Tests\MultiCheck.cs` exercises the write paths directly: that a value typed
once lands in every selected keygroup and in no others, that a flag sets its bit while
each keygroup keeps its own other flags, and that a single selection still edits exactly
one. `KeygroupEditor` is internal to the Studio assembly, so it is compiled together with
the Studio sources and given its own entry point:

```powershell
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$src = @("C:\Users\simon\AkaiS950Tests\MultiCheck.cs")
$src += @(Get-ChildItem "C:\Users\simon\AkaiS950Studio\*.cs" | % { $_.FullName })
$src += @(Get-ChildItem "C:\Users\simon\AkaiS950List\*.cs" |
          ? { \$_.Name -ne 'Program.cs' } | % { \$_.FullName })
& $csc /nologo /target:exe /main:MultiCheck /out:"$env:TEMP\MultiCheck.exe" `
    /r:System.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Core.dll $src
& "$env:TEMP\MultiCheck.exe"
```

## Finding a loop

The S950 holds a loop as an **end point and a length**, and plays `end - length .. end`
round and round. The library bears that out: of its 324 looped samples the loop *start*
field is simply 0 in 250 of them, and `start == end - length` in only 8, so the length is
what says where the loop begins. The only thing to find is how far back it restarts.

**Find Loop...** on a sample looks for it. The join the ear hears runs from the loop end
back to that point, so what it looks for is the point whose approach matches the approach
to the end — a normalised cross-correlation, coarse over a decimated copy and then exact
around the best few candidates. Two things the finder cannot know are asked for in the
dialog: how short a loop is still musical, and whether the loop runs to the end of the
sample or to the end marker the sample already carries.

It reports the **match**, −1 to 1, and says what to expect of it: inaudible above 0.95,
audible below about 0.8, and hopeless on material with no repeating part in it. Applause
and running water have no clean loop, and the number says so rather than pretending.

Two mistakes are easy here, and both were caught by measuring rather than by listening:
decimating the coarse pass by *stride* aliases bright material and hides good loops, and
taking the best coarse candidates without keeping them apart gathers up the neighbours of
one peak, so only a single region ever gets examined closely.

### The loop mode moves the descriptor table

A sample's loop mode is not just a byte in its header. It decides how many 10-byte
descriptors the sample takes in the table that follows the keygroup arena — a looping
sample takes one more than a one-shot, an alternating one more again — so changing it
moves the descriptor pointer of **every sample after it**. The library bears the chain
out: 1,002 of its 1,011 consecutive samples point exactly where the one before them ends.

The property grid used to write `0x1A` and nothing else, which broke that chain on 95 of
the 96 library disks it was tried on. `LoopMode` now goes through `AkaiDisk.SetLoopMode`,
which shifts the pointers with it, exactly as renaming goes through the disk rather than
poking the name.

### How that was checked

`..\AkaiS950Tests\LoopCheck.cs` runs the finder over every looped sample in the corpus
and scores its answer against the loop the library shipped with, using the same join
measure for both, and flips a loop mode on every disk to confirm the descriptor chain
survives it. It has its own `Main`, so it is kept outside both projects rather than
breaking their builds:

```powershell
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$src = @("C:\Users\simon\AkaiS950Tests\LoopCheck.cs")
$src += @(Get-ChildItem "C:\Users\simon\AkaiS950List\*.cs" |
          ? { \$_.Name -ne 'Program.cs' } | % { \$_.FullName })
& $csc /nologo /target:exe /out:"$env:TEMP\LoopCheck.exe" /r:System.dll /r:System.Core.dll `
    /r:System.Drawing.dll $src
& "$env:TEMP\LoopCheck.exe" C:\Users\simon\AkaiS950Images
```

Across the library it averages **0.881 where the shipped loops average 0.793** — better on
110 samples, as good on 158, worse on 30, and those 30 are noise where both answers score
near zero. The web version reports the same figures from `looptest.js`, which is where the
algorithm was worked out; the two implementations agree number for number.

## Playing it: the engine

Until recently this played a sample by wrapping it in a WAV header and handing it to
`SoundPlayer`. That is one sample at a time, at its recorded pitch, with no filter, no
envelope, no loop and a latency measured in hundreds of milliseconds. It was honest about
it — the class comment said "deliberately dumb" — but it meant the C# version could show
you a program without ever letting you hear one.

There is now a real engine: **`AkaiS950Engine`**, a separate project with no reference to
WinForms, to the disk format, or to anything else.

| | |
|---|---|
| `Cal.cs` | the measured constants, and the mappings from a stored 0..99 to hertz and seconds |
| `Filter.cs` | the 6th-order Butterworth, three biquads, retunable a sample at a time |
| `Voice.cs` | one note: playback, loop, both envelopes, the LFO |
| `Engine.cs` | eight voices, a lock-free note queue, and `Render(float[], int, int)` |
| `Patch.cs` | what a voice needs, with no notion of where it came from |
| `WasapiOut.cs` | the sound card, event driven |
| `MidiIn.cs` | `winmm` short messages |

`Instrument.cs`, in this project, is the only file that knows about both halves: it turns
an `AkaiDisk` programme into an engine `Patch` and owns the output and the MIDI port.

That separation is not tidiness. It is what makes a plugin possible later without any of
this being rewritten — see below.

### What it does now

Everything the web version does, from the same measured numbers:

- the **filter**, 36 dB per octave, swept by its own envelope, with key tracking and
  velocity — and running *after* the varispeed, as on the machine, so the cutoff is a
  fixed number of hertz whichever key is held;
- both **envelopes**, with the decay a straight line in decibels rather than in amplitude,
  which is what the machine was measured doing;
- the **LFO**: a sine, linear in rate, 1.53 cents of depth per unit, and a delay that fades
  the wobble in rather than waiting. Voices share one oscillator or run their own
  depending on the desync bit, which was measured rather than guessed;
- **velocity** to loudness and to filter, **zone loudness**, **zone tuning**, **constant
  pitch**, **one-shot**;
- **looping**, playing `end - length .. end` as the machine does;
- **eight voices**, stealing the oldest, with both zones of a keygroup sounding;
- **MIDI in**, including the modwheel, which adds vibrato in proportion to byte 22.

### Latency

Shared-mode WASAPI with an event callback. On the machine this was written on that is a
22 ms buffer at 44.1 kHz — playable, and two orders of magnitude better than what it
replaced. Exclusive mode would get to three or four milliseconds and is the obvious next
step if this is ever played rather than auditioned.

The engine allocates nothing in `Render` and never locks: notes go through a lock-free
ring and are picked up at the top of the next callback. A garbage collection inside an
audio callback is a click.

### Checking it

Three programs, because they answer different questions.

`EngineCheck` proves the maths. Every mapping is compared against the number `audio.js`
prints for the same input — the two implementations are meant to agree to the digit, the
way `LoopCheck` and `looptest.js` already do. Then it renders and measures what comes out:
noise through the filter to find the corner and the slope, a decaying note to check the
fall really is straight in decibels, and a vibrato demodulated back out of the audio the
same way the calibration run measures it off the machine.

```powershell
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$src = @("C:\Users\simon\AkaiS950Tests\EngineCheck.cs")
$src += (Get-ChildItem "C:\Users\simon\AkaiS950Engine\*.cs" | % { $_.FullName })
& $csc /nologo /unsafe /target:exe /main:EngineCheck /out:"$env:TEMP\EngineCheck.exe" `
    /r:System.dll /r:System.Core.dll $src
& "$env:TEMP\EngineCheck.exe"
```

It reads the vibrato back at 7.140 Hz and 151.1 cents where the law says 7.135 and 151.2,
and the decay in dead-straight 6.9 dB steps reaching the floor at 2.87 s against a
measured 2.868.

`AudioCheck` proves the WASAPI interop, which nothing else can: whether a vtable is in the
right order is not a question a buffer can answer. It opens the card, plays a chord and
reports how much audio the callback was asked for. `AudioCheck quiet` does it silently.

`PatchCheck` plays the library. Every programme on every disk, a note in the middle of
each keygroup, looking for a NaN, a filter that has blown up, or a programme that builds a
patch and then makes no sound:

```
  101 disks, 390 programmes, 1908 keygroups
  1756 notes sounded, 152 were silent, 0 were wrong
  32 reached full scale - a hot sample plus a zone trim, which is what the master gain is for
```

The 152 silent ones are not a fault: 164 of the library’s keygroups name a sample that is
not on their own disk, which the format document already records as *1744 of 1908 match*.

## A plugin, later

The aim is a low-latency instrument, standalone and as a plugin. The standalone part is
done. The plugin part **cannot be built on this machine**, and it is worth writing down
why so the decision can be made rather than rediscovered.

There is no .NET SDK here — only runtimes — so `dotnet build` does not work and the
`net10.0-windows` in the csproj is aspirational. Everything is compiled by calling
`csc.exe` from .NET Framework 4.0 directly, which means C# 5. And there is no C or C++
compiler of any kind: no `cl`, no `gcc`, no `clang`.

That rules out both routes:

| Route | Needs | Missing |
|---|---|---|
| VST3 or CLAP wrapper | a C++ toolchain and the SDK | Visual Studio Build Tools |
| Pure C# VST3 (NPlug) | .NET SDK 8+, NativeAOT, the MSVC linker | all three |

Neither is a coding problem. The engine is already shaped for either: it has no UI, no file
access and no static state, its render path allocates nothing, and its entire interface is
`SetPatch`, three note methods and `Render(float[], int, int)`. A wrapper would hold one
`Engine`, translate the host’s note events into the same queue `MidiIn` uses, and call
`Render` from the host’s callback. Nothing in `AkaiS950Engine` would change.

VST2 is not worth considering: Steinberg stopped issuing licences and the SDK is not
available.

## Undo, and not losing edits

Every edit goes into the loaded image, so **Edit > Undo** (Ctrl+Z) and **Redo** (Ctrl+Y) keep
24 steps of history — a whole disk image per step, which at 800 KB each is cheap enough not to
think about. The menu names the step: *Undo trim CONGA 2*, *Undo VCA envelope*.

A gesture is one step. Dragging an envelope corner through fifty mouse-moves, or nudging the
amount slider repeatedly, collapses into a single undo rather than fifty.

Because an edit can make a disk dirty without the tree being rebuilt, **modified disks are
marked in the tree** with a `*` and coloured, and the title bar counts them. That matters with
a whole stick loaded: *Save Disk Image As* writes the selected disk only, so editing five and
saving one is otherwise an easy and silent mistake.

**File > Save All Modified...** (Ctrl+Shift+S) writes every modified image into a folder you
choose, under its own filename, verifying each one after writing and refusing to overwrite the
file it came from. Anything that fails to verify is listed and left marked as unsaved.

## Envelopes

Selecting a keygroup puts its two envelopes beside the property grid on the **Edit** tab: VCA
above, VCF below, with the filter envelope's **amount** on a vertical slider beside it — that
value only means anything next to the envelope it scales, and up is more.

Drag the corners. Three handles each: the peak sets attack, the corner sets decay and sustain
together, and the last sets release. All four values are the 0..99 the S950 stores. The eight
numeric ADSR properties and `VcfAmount` are no longer listed in the grid, since the shape says
more than the numbers and having both invites them to disagree.

Edits go straight into the loaded image and update the keygroup list's VCA and VCF columns as
you drag.

## Renaming

Right-click a program or sample for **Rename**, or edit the **Name** property in the Edit
tab — both go the same way. A filename is held in two places, the directory entry and the
first ten bytes of the file itself, and a rename writes both; leaving them disagreeing would
give the disk two names for one file.

Renaming a *sample* also rewrites every keygroup zone that referred to it, since zones name
their sample directly. Without that, renaming a sample would quietly orphan every program
using it. Duplicate and blank names are refused.

## Fitting a sample to a tempo

Right-click a sample for **Fit to Tempo**. Give it the tempo the sample is at and the tempo
you want; **Work out BPM** derives the first from the sample's length if you tell it how many
beats are in it, which a loop usually makes obvious.

Two ways to get there, and the checkbox picks between them:

- **Time stretch** (default) keeps the pitch and changes the length. Frames are overlap-added
  at a different spacing than they were taken, each nudged within a search window to the
  offset that best continues the waveform already written — WSOLA, which is what stops the
  joins phasing. Good on drums and percussion, less convincing on sustained material.
- **Varispeed** rewrites the playback rate only. No audio is touched, the file does not change
  size, and the pitch moves with the tempo — which is what a hardware sampler does, and often
  what you want for drums anyway. On this machine it costs two bytes.

Slowing a sample down makes its file bigger, which is the one edit here that needs to *take*
blocks rather than give them back. The dialog shows how many it needs against how many are
free and will not let you apply a stretch that does not fit; the disk layer refuses
independently.

## Trimming leading silence

Right-click a sample for **Trim Leading Silence**. It finds the first word above the noise
floor, drops everything before it, and hands back whole blocks of the space. The confirmation
says how much silence there is, in words and seconds, and how many blocks it will free.

Markers move with the audio — what was at word 1000 is at word 1000 − trimmed afterwards — so
start, end and loop points still point at the same sound. Only the start is trimmed: the tail
is where a loop lives, and where a decay fades below any threshold worth picking.

The threshold is 8 of the 12-bit range, about −48 dB, low enough to leave a quiet attack
alone. Across 30 images, 312 of 496 samples carry some leading silence, though usually only
milliseconds of it; about 146 blocks were recoverable in total.

## Halving a sample's rate

Right-click a sample in the tree for **Halve Sample Rate**. It resamples the audio down by
two and rewrites the file at half the rate, which halves the disk space it occupies. Pitch
and duration are unchanged; what goes is the top octave of bandwidth.

The resample low-passes properly. Dropping every other sample would fold everything above
the new Nyquist back into the audible band — a 15 kHz tone reappearing at 5 kHz at full
strength. Measured on the real code path, that alias comes out at zero while a 440 Hz tone
passes at full level.

The freed blocks go back to the free pool and can be used by the next sample you add. Later
samples shift down in sample RAM by exactly the space given back; earlier samples, and every
program on the disk, are untouched. Programs are safe because a keygroup zone references a
sample by an index-based pointer (`base + 70 x sample index`), not by its RAM address, so
resizing a sample cannot invalidate them.

Sample rates across the 101-image corpus run from **11,773 Hz to 44,329 Hz**, with the common
values on a 1,250 Hz grid and 40,000 Hz accounting for 36% of all samples. Halving below the
lowest rate the library contains is allowed but the confirmation says so, since the sampler's
own documented floor is not known here.

### What the derived fields assume

A sample header carries two values that describe where it will sit in the *sampler's*
memory, not on disk: the sample RAM address at `0x36` and the loop-descriptor pointer at
`0x28`. Both are computed from the sample before it in directory order:

- **RAM address** — `previous + ceil(2 x words / 16) x 16`. Exact for all 1011 consecutive
  pairs in the 101-image corpus.
- **Loop descriptors** — 10 bytes per record, 2 records for a one-shot and 3 for a loop.
  Exact for all 893 pairs where the preceding sample is under 128 KB of RAM.

Above 128 KB the record count is not a function of the header at all — two 280,000-byte
one-shots in the corpus take 3 and 4 records — so if the last sample on the target disk is
that large, the pointer written for the new one is the documented estimate rather than a
certainty. Both fields are consistent within each disk, as if written by a whole-disk save,
which suggests the sampler recomputes them on load.

### Confirmed on hardware

**14 September 2026** — a sample added by this program, saved to a `.hfe` and loaded into a
real Akai S950 from a Gotek, loads and plays. That settles the parts of the write path that
could not be checked from the images alone: the 12-bit split packing, the synthesised 60-byte
header, the directory entry, the FAT chain, and the derived RAM fields — at minimum they do
not stop the sampler loading the disk.

Also confirmed the same day: **halving a sample's rate**, saved and reloaded, plays. That
covers the shrinking path — rewriting a file smaller, truncating its chain and returning the
tail blocks to the free pool, updating the directory length, and rewriting the header with
markers scaled to the new word count.

And the case that exercises the RAM shift: a sample **added after** another, with the earlier
one then halved, still loads. That is the arrangement where every following sample's RAM
address and loop pointer are rewritten, so the shift arithmetic is at worst harmless and the
disk stays loadable.

**A time-stretched sample plays correctly too**, which covers the one direction the others do
not: growing a file. `SNARE #4` on DSKA0014 is second of 31 samples, so slowing it down
allocated blocks and moved 29 samples *up* in RAM. Checked with a MIDI pattern containing the
drums either side of it — the sample below it in RAM does not move and acts as a control, the
ones above it all do. Everything sounded as it should.

And the question those two left open is now answered directly. A disk with **every keygroup
chain pointer deliberately corrupted to `0x5A5A`** still plays its programs correctly, so the
sampler rebuilds the RAM arena when it loads a disk rather than reading what is stored.

That was the last structural unknown. The derived RAM fields — keygroup chain pointers, sample
RAM addresses, loop-descriptor pointers — are written to match what the hardware would have
written, but nothing reads them back, so an approximation there cannot break a disk. In
particular the loop-descriptor record count for samples over 128 KB, which no formula
predicts, does not matter.

Strictly, only the chain pointers were corrupted in that test; the sample RAM address and the
loop-descriptor pointer are the same class of field in the same arena, recorded by the same
whole-disk save, but were not themselves scrambled.

What remains untested on hardware is everything that edits a *program* rather than a sample:
parameter edits from the grid and the envelopes, renaming, and adding or deleting keygroups.
The mechanisms they rest on are all proven — in-place byte writes, and the file resize that
growing a sample now demonstrates — but the disks themselves have not been played.

The same goes for the operations added since: deleting a sample, creating and deleting
programs, and slicing. They are checked structurally against the whole corpus — every disk
verified after the edit and after a save and reload — and they rest on the same three
mechanisms that are already proven on hardware: in-place byte writes, file resize, and the
directory and FAT bookkeeping that adding a sample exercises. A disk one of them produced
has not yet been put in front of an S950.

An HFE *built from nothing* rather than patched has not been played either, though it has
the strongest evidence of anything here that it is right: rebuilt from the sectors alone,
it reproduces the original file byte for byte on all 101 disks in the library, so the
bitstream a Gotek would see is the same one it already reads.

### Limits

The program load address, the sample RAM address, the loop-descriptor pointer and the
keygroup chain pointers are all *derived* values. Anything that resizes or reorders files
has to recompute them — which is why every operation here that changes the number of
samples, programs or keygroups rebuilds the whole arena from its layout afterwards rather
than shifting the stored values by a delta. The formulas are in `S950-Disk-Format.pdf`.

Rebuilding is checked against the library the only way that means anything: doing it to a
disk that has not been edited changes nothing at all, on all 101 images and every pointer
in them.

A raw `.img` must be 800K or 1600K, and playback is still a fire-and-forget `SoundPlayer`,
so a looping sample sustains for a few seconds rather than until you let go of the key.