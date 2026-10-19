#!/bin/bash
#
# Mz950 for Linux: put everything where hosts look for it, for this user, without an installer.
#
#     bash install.sh
#
# What it does, all of it inside your own home folder - nothing needs a password:
#
#   Mz950.vst3    ->  ~/.vst3
#   Mz950.lv2     ->  ~/.lv2
#   Mz950         ->  ~/.local/bin                  (the standalone app)
#   Disks/*.hfe   ->  ~/.local/share/Mz950/Disks    (never over your own)
#
# Those plug-in folders are the ones every Linux host - Ardour, Bitwig, Reaper, Carla,
# Qtractor, Zrythm - searches; the disk folder is where Mz950's Load disk... opens.
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
disk_dir="$HOME/.local/share/Mz950/Disks"

# One bundle into place, if this download has it: the old one out first, so nothing stale
# survives inside it.
place () {
    local what="$1" where="$2"
    if [ ! -e "$here/$what" ]; then
        echo "  (no $what in this download - skipped)"
        return
    fi
    mkdir -p "$where"
    rm -rf "${where:?}/$what"
    cp -r "$here/$what" "$where/$what"
    echo "  $what  ->  $where"
}

echo "Installing Mz950"
place "Mz950.vst3" "$HOME/.vst3"
place "Mz950.lv2"  "$HOME/.lv2"
place "Mz950"      "$HOME/.local/bin"

# The library, copied only where a disk of that name is not already there: those disks are
# yours to edit, and an update should not undo the edits.
if [ -d "$here/Disks" ]; then
    mkdir -p "$disk_dir"
    copied=0
    for disk in "$here"/Disks/*.hfe; do
        [ -e "$disk" ] || continue
        name="$(basename "$disk")"
        if [ ! -e "$disk_dir/$name" ]; then
            cp "$disk" "$disk_dir/$name"
            copied=$((copied + 1))
        fi
    done
    echo "  $copied new disk(s)  ->  $disk_dir"
fi

echo
echo "Done. In your DAW, rescan plug-ins; Mz950 is under simozzer."
echo "Load disk... opens on the sound library the first time."
