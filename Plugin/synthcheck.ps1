<#
    Hold the C++ synthesis to the C# it was ported from.

        .\synthcheck.ps1

    Both SynthDumps render the same list of waves and drums - the plain shapes, the
    wavetable sweeps, FM, ring, noise, the shaken waves, and every drum in the kit - to
    raw 16-bit files, and this compares them word for word. Nothing needs a disk and
    nothing is kept: every wave is drawn from a formula and a seed.

    A render that differs is reported with how FAR it differs. One 12-bit step here and
    there is two libraries rounding a transcendental differently; hundreds of steps
    everywhere is a port that got the recipe wrong, and those are different findings.
#>
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root

$work = Join-Path $env:TEMP "s950synthcheck"
New-Item -ItemType Directory -Force -Path $work | Out-Null
foreach ($d in "cs", "cpp") { Remove-Item (Join-Path $work $d) -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force -Path (Join-Path $work $d) | Out-Null }

# ---------------------------------------------------------------- the C# dumper

$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { Write-Error "No .NET Framework 4.0 compiler found." }

$csDump = Join-Path $work "SynthDump.exe"
$csSrc  = @(Join-Path $repo "AkaiS950Tests\SynthDump.cs")
foreach ($f in "Waveforms", "Spectrum", "Modulation", "Noise", "Drums", "Patches") {
    $csSrc += Join-Path $repo "AkaiS950Synth\$f.cs"
}

& $csc /nologo /target:exe /main:SynthDump /out:$csDump /r:System.dll /r:System.Core.dll $csSrc
if ($LASTEXITCODE -ne 0) { Write-Error "the C# dumper did not build" }

# --------------------------------------------------------------- the C++ dumper

$cppDump = Join-Path $work "SynthDumpCpp.exe"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath
if (-not $vsPath) { Write-Error "No Visual Studio C++ tools found." }

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$include = Join-Path $root "Source\S950"

cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$work`" && cl /nologo /std:c++17 /EHsc /W4 /O2 /I `"$include`" `"$root\Tests\SynthDump.cpp`" `"$include\Synth.cpp`" /Fe:`"$cppDump`"" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "the C++ dumper did not build" }

# ------------------------------------------------------------------- the renders

$a = Join-Path $work "cs"
$b = Join-Path $work "cpp"

$csLines  = & $csDump  $a
if ($LASTEXITCODE -ne 0) { Write-Error "the C# dumper failed" }
$cppLines = & $cppDump $b
if ($LASTEXITCODE -ne 0) { Write-Error "the C++ dumper failed" }

Write-Host ""
Write-Host "  $($csLines.Count) renders, two libraries, compared word for word"
Write-Host ""

$same = 0; $near = 0; $far = @()

for ($i = 0; $i -lt $csLines.Count; $i++) {
    $name = ($csLines[$i] -split "\s+")[0..1] -join " "
    $name = ($csLines[$i].Substring(0, 10)).Trim()

    if ($i -lt $cppLines.Count -and $csLines[$i] -eq $cppLines[$i]) {
        $same++
        Write-Host ("  {0,-12} identical" -f $name) -ForegroundColor Green
        continue
    }

    $file = $name.Replace(" ", "_") + ".s16"
    $x = [System.IO.File]::ReadAllBytes((Join-Path $a $file))
    $y = if (Test-Path (Join-Path $b $file)) { [System.IO.File]::ReadAllBytes((Join-Path $b $file)) } else { @() }

    if ($x.Length -ne $y.Length) {
        $far += $name
        Write-Host ("  {0,-12} DIFFERENT LENGTH  {1} vs {2} bytes" -f $name, $x.Length, $y.Length) -ForegroundColor Red
        continue
    }

    $maxDiff = 0; $count = 0; $n = $x.Length / 2
    for ($k = 0; $k -lt $n; $k++) {
        $p = [BitConverter]::ToInt16($x, 2 * $k); $q = [BitConverter]::ToInt16($y, 2 * $k)
        $d = [Math]::Abs($p - $q)
        if ($d -gt 0) { $count++; if ($d -gt $maxDiff) { $maxDiff = $d } }
    }

    if ($maxDiff -le 1) {
        $near++
        Write-Host ("  {0,-12} within one step   {1} of {2} words differ by 1" -f $name, $count, $n) -ForegroundColor DarkYellow
    }
    else {
        $far += $name
        Write-Host ("  {0,-12} DIFFERS           {1} of {2} words, up to {3} steps" -f $name, $count, $n, $maxDiff) -ForegroundColor Red
    }
}

Write-Host ""
if ($far.Count -eq 0) {
    Write-Host "  $same identical, $near within a rounding step: the port is the tool" -ForegroundColor Green
} else {
    Write-Host ("  {0} identical, {1} within a step, {2} DIFFER: {3}" -f $same, $near, $far.Count, ($far -join ", ")) -ForegroundColor Red
    exit 1
}
