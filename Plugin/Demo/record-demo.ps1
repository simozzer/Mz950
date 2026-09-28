<#
    Record the Mz950 demo: run a script in the standalone, film its window, and put the
    film and the plugin's own audio together - in sync, with captions.

        .\record-demo.ps1                          demo.txt, into .\out
        .\record-demo.ps1 -Script my.txt -Out D:\video

    WHAT COMES OUT, in -Out:

        demo-captioned.mp4   the finished clip, captions burned in
        demo-clean.mp4       the same without them, for your own editing
        demo.srt             the captions on their own, timed to both
        demo.wav             the audio as the plugin rendered it (32-bit float, before
                             the videos' audio is brought to a -1 dB peak)

    HOW IT STAYS IN SYNC. The audio is not taken from the sound card: the standalone writes
    its own output to a WAV as it renders, and the script's MIDI goes into it at exact
    sample positions (see Plugin/Source/Demo.h). At the instant that WAV starts, the window
    flashes white. This finds the flash in the film, starts the film there, and trims the
    flash off both. Every step of the script runs on the audio clock too, so the picture
    and the sound stay together for the whole run.

    Needs ffmpeg on the PATH (winget install Gyan.FFmpeg). Close other windows titled
    "Mz950" first - the film is taken by window title.
#>
param(
    [string]$Script = (Join-Path $PSScriptRoot "demo.txt"),
    [string]$Out    = (Join-Path $PSScriptRoot "out"),
    [string]$Exe    = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\VirtualS950_artefacts\Release\Standalone\Mz950.exe"),
    [int]$Fps       = 60,
    [double]$Trim   = 0.25          # seconds cut from the start: the flash, and a breath
)

$ErrorActionPreference = "Stop"
function Say($t, $c = "Gray") { Write-Host $t -ForegroundColor $c }

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { throw "ffmpeg is not on the PATH (winget install Gyan.FFmpeg)" }
if (-not (Test-Path $Exe))    { throw "no standalone at $Exe - build the plugin first" }
if (-not (Test-Path $Script)) { throw "no script at $Script" }

New-Item -ItemType Directory -Force $Out | Out-Null
$Script = (Resolve-Path $Script).Path
$stem   = [IO.Path]::GetFileNameWithoutExtension($Script)
$wavIn  = [IO.Path]::ChangeExtension($Script, ".wav")      # the standalone writes these beside the script
$srtIn  = [IO.Path]::ChangeExtension($Script, ".srt")
$raw    = Join-Path $Out "$stem-raw.mkv"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class DemoWin {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

# ---------------------------------------------------------------- run and film it
Get-Process -Name "Mz950" -ErrorAction SilentlyContinue | ForEach-Object { throw "Mz950 is already running (pid $($_.Id)) - close it first" }

Say "starting the standalone with $Script"
$app = Start-Process $Exe -ArgumentList "--demo", "`"$Script`"" -PassThru
$deadline = (Get-Date).AddSeconds(20)
do { Start-Sleep -Milliseconds 200; $app.Refresh() } until ($app.MainWindowHandle -ne 0 -or (Get-Date) -gt $deadline)
if ($app.MainWindowHandle -eq 0) { throw "the standalone's window did not appear" }
$hwnd = $app.MainWindowHandle

# Pinned on top for the run: the film is taken from the screen where the window is (JUCE
# draws with Direct2D, which gdigrab cannot capture by window title), so nothing may cover it.
[DemoWin]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0040) | Out-Null   # TOPMOST, NOSIZE|NOMOVE|SHOW
[DemoWin]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

$wr = New-Object DemoWin+RECT; [DemoWin]::GetWindowRect($hwnd, [ref]$wr) | Out-Null
$cr = New-Object DemoWin+RECT; [DemoWin]::GetClientRect($hwnd, [ref]$cr) | Out-Null
$pt = New-Object DemoWin+POINT; [DemoWin]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
$clientW = $cr.R - $cr.L; $clientH = $cr.B - $cr.T

# The plugin itself is the 640 x 672 at the bottom of the client area; the standalone draws
# its own title strip (the Options button) above it. That strip is cropped off.
$scale   = [DemoWin]::GetDpiForWindow($hwnd) / 96.0
$editorW = [int][math]::Round(640 * $scale)
$editorH = [int][math]::Round(672 * $scale)
$cropX   = [int][math]::Floor(($clientW - $editorW) / 2)
$cropY   = $clientH - $editorH
Say ("window at {0},{1}, client {2}x{3}, plugin {4}x{5} at +{6},+{7}" -f $pt.X, $pt.Y, $clientW, $clientH, $editorW, $editorH, $cropX, $cropY)

Say "filming the window at $Fps fps"
$ff = New-Object System.Diagnostics.Process
$ff.StartInfo.FileName = $ffmpeg
$ff.StartInfo.Arguments = "-y -hide_banner -loglevel error -f gdigrab -framerate $Fps -draw_mouse 0 " +
                          "-offset_x $($pt.X) -offset_y $($pt.Y) -video_size ${clientW}x${clientH} -i desktop " +
                          "-c:v libx264 -preset ultrafast -crf 10 -pix_fmt yuv444p `"$raw`""
$ff.StartInfo.UseShellExecute = $false
$ff.StartInfo.RedirectStandardInput = $true
$ff.Start() | Out-Null

Say "the demo is running - leave the window alone until it closes"
$app.WaitForExit(15 * 60 * 1000) | Out-Null
Start-Sleep -Milliseconds 500
try { $ff.StandardInput.Write("q"); $ff.StandardInput.Close() } catch { }
$ff.WaitForExit(30000) | Out-Null
if (-not (Test-Path $raw)) { throw "ffmpeg filmed nothing" }

if (-not (Test-Path $wavIn)) { throw "the standalone wrote no audio ($wavIn)" }
Move-Item $wavIn (Join-Path $Out "$stem.wav") -Force
$wav = Join-Path $Out "$stem.wav"

# ---------------------------------------------------------------- find the flash
Say "finding the sync flash"
$stats = Join-Path $Out "$stem-yavg.txt"
& $ffmpeg -hide_banner -loglevel error -i $raw -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG:file='$($stats -replace '\\','/' -replace ':','\:')'" -f null -
$flash = $null; $time = $null
foreach ($line in Get-Content $stats) {
    if ($line -match "pts_time:([0-9.]+)") { $time = [double]$Matches[1] }
    elseif ($line -match "YAVG=([0-9.]+)" -and [double]$Matches[1] -gt 200 -and $null -eq $flash) { $flash = $time }
}
if ($null -eq $flash) { throw "no white flash found in the film" }
Say ("flash at {0:0.000} s into the film" -f $flash)

# ---------------------------------------------------------------- the captions, moved by the trim
$srt = Join-Path $Out "$stem.srt"
if (Test-Path $srtIn) {
    $shift = [TimeSpan]::FromSeconds($Trim)
    $text = (Get-Content $srtIn -Raw) -replace '(\d\d:\d\d:\d\d,\d\d\d)', {
        $t = [TimeSpan]::ParseExact($_.Groups[1].Value, "hh\:mm\:ss\,fff", $null) - $shift
        if ($t -lt [TimeSpan]::Zero) { $t = [TimeSpan]::Zero }
        $t.ToString("hh\:mm\:ss\,fff")
    }
    [IO.File]::WriteAllText($srt, $text, (New-Object Text.UTF8Encoding $false))
    Remove-Item $srtIn
}

# ---------------------------------------------------------------- put them together
# ---------------------------------------------------------------- the level
# The WAV is 32-bit float, so nothing in it has clipped; the video's audio is brought to a
# peak of -1 dB. (The script's own "set gain" steps balance the sections against each other.)
$peak = 0.0
$vd = & $ffmpeg -hide_banner -i $wav -af volumedetect -f null - 2>&1 | Select-String "max_volume: (-?[0-9.]+) dB"
if ($vd) { $peak = [double]$vd.Matches[0].Groups[1].Value }
$lift = "{0:0.00}" -f (-1.0 - $peak)
Say ("audio peaks at {0:0.0} dB; the video's audio moves by {1} dB" -f $peak, $lift)

$start = "{0:0.000}" -f ($flash + $Trim)
$crop  = "crop=${editorW}:${editorH}:${cropX}:${cropY}"
$even  = "pad=ceil(iw/2)*2:ceil(ih/2)*2"
$style = "FontName=Segoe UI,FontSize=11,Bold=1,PrimaryColour=&H00FFFFFF,BackColour=&H99000000,BorderStyle=3,Outline=6,Shadow=0,MarginV=14"

Push-Location $Out
try {
    Say "writing $stem-clean.mp4"
    & $ffmpeg -y -hide_banner -loglevel error -ss $start -i $raw -ss $Trim -i $wav -map 0:v -map 1:a `
        -vf "$crop,$even" -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -r $Fps `
        -af "volume=${lift}dB" -c:a aac -b:a 320k -shortest "$stem-clean.mp4"

    if (Test-Path $srt) {
        Say "writing $stem-captioned.mp4"
        & $ffmpeg -y -hide_banner -loglevel error -ss $start -i $raw -ss $Trim -i $wav -map 0:v -map 1:a `
            -vf "$crop,$even,subtitles=$stem.srt:force_style='$style'" -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -r $Fps `
            -af "volume=${lift}dB" -c:a aac -b:a 320k -shortest "$stem-captioned.mp4"
    }
}
finally { Pop-Location }

Remove-Item $stats -ErrorAction SilentlyContinue
Say "done - $Out" Green
Get-ChildItem $Out -Filter "$stem*" | Where-Object Name -notlike "*-raw.mkv" | Select-Object Name, @{n="MB";e={[math]::Round($_.Length/1MB,1)}}
