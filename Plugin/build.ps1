<#
    Build the plugin, after proving the engine against the C# reference.

        .\build.ps1              conformance check, then the VST3 and the standalone
        .\build.ps1 -NoRun       compile the conformance check but do not run it
        .\build.ps1 -NoPlugin    conformance check only, for iterating on the engine

    IT USED TO BUILD ONLY THE CONFORMANCE CHECK, AND SAID "ok" WHILE THE PLUGIN WENT STALE

    That is the worst shape a build script can have. It compiles three files - the check
    itself, Voice.cpp and Engine.cpp - runs them, prints a green line and exits, and none
    of that touches the VST3. The plugin is a CMake build, and unless someone remembered
    to run CMake separately the .vst3 on disk stayed whatever it was.

    It caught us out twice. The second time the plugin was eleven hours behind the engine,
    across the whole velocity-to-attack and velocity-to-release work, with this script
    reporting success every time it ran. Nothing in the output hinted at it, because from
    the script's point of view nothing was wrong.

    So the conformance check now gates the plugin build rather than standing in for it: if
    the engine does not match the C# to nine decimal places, there is no point compiling a
    plugin around it, and if it does then the plugin is built from those exact sources in
    the same breath. -NoPlugin is there for the tight loop where only the engine is moving.

    WHY VCVARS AND NOT JUST "cl"

    cl.exe is never on the PATH. The installer leaves it off on purpose, because the
    compiler needs INCLUDE, LIB and PATH set for one particular target architecture, and
    vcvars64.bat is what sets them. Start menu -> "Developer PowerShell for VS 2026" does
    the same thing; this finds the batch file itself so the build works from any shell,
    including one driven by a script.

    vswhere reports where Visual Studio is rather than a path being written down here,
    because that path carries the edition and the major version in it - C:\Program Files\
    Microsoft Visual Studio\18\Community today - and hard-coding it would break on the
    next upgrade with an error that says nothing useful.
#>
param(
    [switch]$NoRun,
    [switch]$NoPlugin,
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Out -eq "") { $Out = Join-Path $env:TEMP "s950build" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "No Visual Studio found. Install VS Community with 'Desktop development with C++'."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath
if (-not $vsPath) {
    Write-Error "Visual Studio is installed but the C++ tools are not. Add the 'Desktop development with C++' workload."
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { Write-Error "No vcvars64.bat under $vsPath." }

$sources = @(
    (Join-Path $root "Tests\ConformanceCheck.cpp"),
    (Join-Path $root "Source\S950\Voice.cpp"),
    (Join-Path $root "Source\S950\Engine.cpp")
)

$include = Join-Path $root "Source\S950"
$exe = Join-Path $Out "ConformanceCheck.exe"

Write-Host "building $($sources.Count) files -> $exe"

# One cmd session: vcvars sets the environment, and it only lasts for that session.
$quoted = ($sources | ForEach-Object { "`"$_`"" }) -join " "
cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$Out`" && cl /nologo /std:c++17 /EHsc /W4 /I `"$include`" $quoted /Fe:`"$exe`""

if ($LASTEXITCODE -ne 0) { Write-Error "build failed" }

Write-Host "ok  -  $([math]::Round((Get-Item $exe).Length / 1KB)) KB" -ForegroundColor Green

if ($NoRun) { exit 0 }

& $exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "the engine does not match the C# reference - not building a plugin around it"
}

<#
    THE DISK WRITER, ON REAL DISKS

    The Program tab writes keygroup settings into the disk image, and DiskEditCheck holds
    that writer to the reader: every setting of every keygroup, round-tripped, with every
    byte of the image compared so an edit that touched anything else fails. It needs real
    disks, which stay out of the repository for the reason crosscheck.ps1 gives - so it runs
    on whatever is in ..\disks, and says so plainly when there is nothing there.
#>
$disks = Join-Path (Split-Path -Parent $root) "disks"
$images = @(Get-ChildItem $disks -Include *.hfe, *.img -File -Recurse -ErrorAction SilentlyContinue)

if ($images.Count -eq 0) {
    Write-Host "no disks in $disks - the disk writer was NOT checked" -ForegroundColor DarkYellow
}
else {
    $editCheck = Join-Path $Out "DiskEditCheck.exe"
    $diskSources = @(
        (Join-Path $root "Tests\DiskEditCheck.cpp"),
        (Join-Path $root "Source\S950\Disk.cpp"),
        (Join-Path $root "Source\S950\Hfe.cpp")
    )
    $quotedDisk = ($diskSources | ForEach-Object { "`"$_`"" }) -join " "
    cmd /c "call `"$vcvars`" >nul 2>&1 && cd /d `"$Out`" && cl /nologo /std:c++17 /EHsc /W4 /O2 /I `"$include`" $quotedDisk /Fe:`"$editCheck`"" | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "the disk edit check did not build" }

    Write-Host ""
    Write-Host "  the disk writer, on $($images.Count) disk(s)"
    foreach ($img in $images) {
        $result = & $editCheck $img.FullName | Select-Object -Last 1
        Write-Host ("  {0,-20} {1}" -f $img.Name, $result.Trim())
        if ($LASTEXITCODE -ne 0) {
            Write-Error "the disk writer failed on $($img.Name) - not building a plugin around it"
        }
    }
}

if ($NoPlugin) { exit 0 }

# ---------------------------------------------------------------- the plugin itself

<#
    CMake, because JUCE is a CMake project and the .vst3 is assembled by it - a bundle of
    a directory, a moduleinfo.json and the binary, not just a compile.

    cmake is looked for on the PATH first and inside Visual Studio second. VS ships one at
    a path that carries the edition and major version, which is exactly the kind of thing
    vswhere exists to avoid writing down, so $vsPath from above is reused rather than
    guessed at.
#>
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source

if (-not $cmake) {
    $bundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $bundled) { $cmake = $bundled }
}

if (-not $cmake) {
    Write-Error ("No cmake on the PATH and none inside Visual Studio. The conformance check " +
                 "passed, so the ENGINE is fine - but the plugin was not rebuilt.")
}

$buildDir = Join-Path $root "build"

# A tree that has never been configured has no cache to build from. Configuring needs JUCE,
# and CMakeLists says where it expects to find it if it is missing.
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host ""
    Write-Host "configuring $buildDir (first time)"
    & $cmake -S $root -B $buildDir
    if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure failed" }
}

Write-Host ""
Write-Host "building the VST3 and the standalone"

& $cmake --build $buildDir --config Release --target VirtualS950_VST3 VirtualS950_Standalone |
    Where-Object { $_ -match "error|warning C|-> |installed to" }

if ($LASTEXITCODE -ne 0) { Write-Error "the plugin build failed" }

<#
    Report what is on disk rather than that the build returned zero.

    The whole point of this addition is that a green line is not evidence, so the green
    line at the end names the artefacts and when they were written. A timestamp older than
    the sources is then visible rather than implied.
#>
$artefacts = @(
    (Join-Path $buildDir "VirtualS950_artefacts\Release\VST3\Mz950.vst3\Contents\x86_64-win\Mz950.vst3"),
    (Join-Path $buildDir "VirtualS950_artefacts\Release\Standalone\Mz950.exe")
)

$newest = Get-ChildItem (Join-Path $root "Source") -Recurse -Include *.cpp,*.h |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1

Write-Host ""
foreach ($a in $artefacts) {
    if (-not (Test-Path $a)) { Write-Error "expected $a and it is not there" }

    $f = Get-Item $a
    $stale = $f.LastWriteTime -lt $newest.LastWriteTime

    Write-Host ("ok  -  {0,-18} {1,7} KB   {2}{3}" -f $f.Name,
                [math]::Round($f.Length / 1KB), $f.LastWriteTime,
                $(if ($stale) { "   *** OLDER THAN $($newest.Name) ***" } else { "" })) `
               -ForegroundColor $(if ($stale) { "Red" } else { "Green" })

    if ($stale) { Write-Error "an artefact is older than the sources it was built from" }
}

exit 0
