# Installer

One installer for both halves of Mz950: **Mz950 Studio**, which opens disk images and edits
programmes, and the **VST3 plugin**, which plays those same disks in a DAW. They share the
engine — the measured constants in `Cal.cs` and `Cal.h` agree to the digit — so they are one
instrument with two front ends rather than two programs.

## Building it

```
cd Installer
.\build-installer.ps1
```

That builds the editor, builds the plugin, checks that every file the script names actually
exists, and only then compiles the installer into `Output\`. A packaging step that quietly
ships a stale binary, or quietly leaves one out, is worse than one that stops.

```
.\build-installer.ps1 -CheckOnly    # just list what would be packaged, and when it was built
.\build-installer.ps1 -SkipBuild    # package what is already built
```

## What it needs

**Inno Setup 6.3 or newer** — free, about 6 MB, from <https://jrsoftware.org/isdl.php>. It is
the only thing in the way; everything else builds with what is already on the machine. 6.3
is the floor because the script uses the `x64compatible` architecture name.

## What it installs

| | Where | Chooseable |
|---|---|---|
| `Mz950Studio.exe` | `{autopf}\Mz950` | yes |
| `Mz950.vst3` | `{autocf}\VST3` | yes |
| `Mz950.exe` (standalone) | `{autopf}\Mz950` | yes |

An install of the old *VirtualS950* is upgraded in place, because the installer's AppId never
changed. It keeps its folder, and the files and shortcuts it had under the old names are
removed. That includes the old `VirtualS950.vst3`, which carries the same plugin ID as the new
bundle and must not be left beside it.

The installer asks at the start whether it is for everyone or just you, and both
destinations follow that answer. A VST3 has two homes on Windows — the machine-wide one
under Common Files, which needs administrator rights, and the per-user one under
LocalAppData, which does not — and Inno's `{autocf}` resolves to whichever was chosen. That
is why it asks rather than assuming: requiring administrator to install an audio plugin is a
poor trade when Windows offers a perfectly good folder that does not.

After a per-user install it says where the plugin went, because hosts differ on which
folders they scan and that one usually has to be pointed out once. In Ableton:
Preferences → Plug-Ins → VST3 Plug-In Custom Folder.

The editor needs the .NET Framework 4, which Windows 10 from 1903 and every Windows 11 ship
in the box. The installer checks anyway and offers to carry on with just the plugin, because
"nothing happens when I run it" is a miserable way to find out.

## What it deliberately does not install

**ffmpeg.** It is optional and it is not bundled.

The editor reads WAV and AIFF/AIFC itself — PCM at 8, 16, 24 or 32 bits, IEEE float at 32 or
64, any channel count or rate — and only reaches for ffmpeg on `PATH` for anything else, MP3
and FLAC and Ogg. Import already says so where it matters: files it cannot open are marked
"needs ffmpeg" in the browser, and the dialog says up front when none was found. The plugin
does not use it at all.

Bundling it would be the wrong trade twice over. It is tens of megabytes for a feature most
people will never reach, and its licence depends on how the build was configured — an
ffmpeg built as GPL2-only cannot be shipped alongside an AGPLv3 program at all. Calling it
as a separate process found on `PATH`, which is what the editor does, keeps it at arm's
length and leaves the choice of build to whoever wants the feature.

Anyone who does: any ffmpeg on `PATH` will do, and nothing needs configuring.

## Before giving it to anyone else

**Mz950 is AGPLv3** — see `LICENSE` at the root of the repository. That is not an
arbitrary choice: the plugin links JUCE, which since version 8 is AGPLv3 unless you have
bought a commercial licence, and matching it means the repository's terms and the JUCE
parts' terms are the same thing rather than two sets in one project.

What that obliges, in practice: anyone you give the installer to is entitled to the source
it was built from, under the same licence. The repository being public satisfies that, so
long as the version you hand out corresponds to a commit that is actually pushed.
