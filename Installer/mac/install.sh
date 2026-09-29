#!/bin/bash
#
# Mz950 for macOS: put everything where it belongs, for this user, without an installer.
#
# Run it through bash rather than double-clicking it:
#
#     bash ~/Downloads/Mz950-*-macOS/install.sh
#
# (type "bash ", drag this file into the Terminal window, press Return). Going through bash
# is what lets it run at all: macOS refuses to open a downloaded script or plugin that has
# not been through Apple's notarization, and this project does not go through Apple.
#
# What it does, all of it inside your own home folder - nothing needs an administrator:
#
#   Mz950.vst3        ->  ~/Library/Audio/Plug-Ins/VST3
#   Mz950.component   ->  ~/Library/Audio/Plug-Ins/Components   (the Audio Unit, for Logic)
#   Mz950.app         ->  /Applications, or ~/Applications if that is not writable
#   Disks/*.hfe       ->  ~/Library/Application Support/Mz950/Disks  (never over your own)
#
# and it takes the "downloaded from the internet" quarantine flag off each of them, which is
# the one thing standing between an unnotarized plugin and a host that will load it.
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

vst3_dir="$HOME/Library/Audio/Plug-Ins/VST3"
au_dir="$HOME/Library/Audio/Plug-Ins/Components"
disk_dir="$HOME/Library/Application Support/Mz950/Disks"

if [ -w /Applications ]; then app_dir="/Applications"; else app_dir="$HOME/Applications"; fi

mkdir -p "$vst3_dir" "$au_dir" "$disk_dir" "$app_dir"

# One bundle into place: the old one out first, so nothing stale survives inside it.
place () {
    local what="$1" where="$2"
    if [ ! -e "$here/$what" ]; then
        echo "  (no $what in this download - skipped)"
        return
    fi
    rm -rf "$where/$what"
    ditto "$here/$what" "$where/$what"
    xattr -dr com.apple.quarantine "$where/$what" 2>/dev/null || true
    echo "  $what  ->  $where"
}

echo "Installing Mz950"
place "Mz950.vst3"      "$vst3_dir"
place "Mz950.component" "$au_dir"
place "Mz950.app"       "$app_dir"

# The library, copied only where a disk of that name is not already there: those disks are
# yours to edit, and an update should not undo the edits.
if [ -d "$here/Disks" ]; then
    copied=0
    for disk in "$here"/Disks/*.hfe; do
        [ -e "$disk" ] || continue
        name="$(basename "$disk")"
        if [ ! -e "$disk_dir/$name" ]; then
            cp "$disk" "$disk_dir/$name"
            copied=$((copied + 1))
        fi
    done
    xattr -dr com.apple.quarantine "$disk_dir" 2>/dev/null || true
    echo "  $copied new disk(s)  ->  $disk_dir"
fi

# macOS caches the list of Audio Units; this makes it look again, so Logic finds Mz950
# without a restart. Harmless if nothing is running.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done. In your DAW, rescan plug-ins; Mz950 is under simozzer."
echo "Load disk... opens on the sound library the first time."
