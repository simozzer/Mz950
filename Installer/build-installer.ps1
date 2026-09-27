<#
    Build both products and package them.

        .\build-installer.ps1              build everything, then the installer
        .\build-installer.ps1 -SkipBuild   package whatever is already built
        .\build-installer.ps1 -CheckOnly   just say whether the pieces are there

    The order matters: the editor and the plugin are built from source, every file the
    Inno script names is checked to exist, and only then is the installer compiled. A
    packaging step that silently ships a stale binary - or quietly omits one - is worse
    than one that fails.
#>
param(
    [switch]$SkipBuild,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $here

function Say($text, $colour = "Gray") { Write-Host $text -ForegroundColor $colour }

<#
    Did a build step do what it promised?

    Not "$LASTEXITCODE -ne 0" on its own, which is what this used to ask. That variable is
    only ever set by a NATIVE command, and build-icon.ps1 ends on Write-Host - so in a shell
    where nothing native has run yet it is still $null, and $null -ne 0 is TRUE. The release
    build then stopped with "the icon did not build" immediately after printing the size of
    the icon it had just built. It passed only when some earlier command happened to leave a
    zero lying around, which is not a thing to rest a release on.

    So: the exit code when there actually is one, and then the artefact, which is the part
    that cannot be faked by a stale variable. $ErrorActionPreference is Stop, so anything
    that throws inside the called script has already taken us out before this runs.
#>
function Built($path, $what) {
    if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) {
        Write-Error "$what did not build - exit code $LASTEXITCODE"
    }
    if (-not (Test-Path $path)) {
        Write-Error "$what did not build - nothing at $path"
    }
}

# ------------------------------------------------------------------ build the two halves

if (-not $SkipBuild -and -not $CheckOnly) {
    Say ""
    Say "  drawing the icon" "Cyan"
    & (Join-Path $repo "Icon\build-icon.ps1")
    Built (Join-Path $repo "Icon\AkaiS950.ico") "the icon"

    Say ""
    Say "  building the editor" "Cyan"
    & (Join-Path $repo "build.ps1")
    Built (Join-Path $repo "Mz950Studio.exe") "the editor"

    Say ""
    Say "  building the plugin" "Cyan"

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products * -property installationPath
    $cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

    if (-not (Test-Path $cmake)) {
        $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    }
    if (-not $cmake) { Write-Error "No cmake found - install the C++ workload, or CMake." }

    $pluginDir = Join-Path $repo "Plugin"
    $buildDir  = Join-Path $pluginDir "build"

    & $cmake -S $pluginDir -B $buildDir | Out-Null
    & $cmake --build $buildDir --config Release --parallel

    # A host holding the old plugin open stops it being INSTALLED, not built, and the
    # installer packages what was built - so that is not a reason to stop here.
    if (-not (Test-Path (Join-Path $buildDir "VirtualS950_artefacts\Release\VST3\Mz950.vst3"))) {
        Write-Error "the plugin did not build"
    }
}

# ------------------------------------------------------------------- the sound library
#
# Written every time, -SkipBuild or not.
#
# These are inputs to the packaging rather than a product being tested, they take a couple
# of seconds to make, and a stale one is undetectable: five files of the right size and
# shape, carrying whatever the patch library said a month ago. Regenerating them is cheaper
# than ever having to wonder.
#
$staging = Join-Path $here "staging\Disks"
New-Item -ItemType Directory -Force -Path $staging | Out-Null
Remove-Item (Join-Path $staging "*.hfe") -ErrorAction SilentlyContinue

Say ""
Say "  writing the sound library" "Cyan"

& (Join-Path $repo "AkaiS950Synth\build.ps1") -To $staging | Select-String -Pattern "->|zones sound"
Built $staging "the sound library"

$disks = Get-ChildItem (Join-Path $staging "*.hfe")
if ($disks.Count -lt 1) { Write-Error "no disk images turned up in $staging" }

# --------------------------------------------------------- check what the script will ship

$artefacts = Join-Path $repo "Plugin\build\VirtualS950_artefacts\Release"

$wanted = @(
    @{ What = "the editor";            Path = Join-Path $repo "Mz950Studio.exe" },
    @{ What = "the standalone";        Path = Join-Path $artefacts "Standalone\Mz950.exe" },
    @{ What = "the VST3 bundle";       Path = Join-Path $artefacts "VST3\Mz950.vst3" },
    @{ What = "the plugin binary";     Path = Join-Path $artefacts "VST3\Mz950.vst3\Contents\x86_64-win\Mz950.vst3" },
    @{ What = "the readme";            Path = Join-Path $repo "README.md" },
    @{ What = "the plugin readme";     Path = Join-Path $repo "Plugin\README.md" },
    @{ What = "the tutorial";          Path = Join-Path $repo "docs\tutorial.html" },
    @{ What = "the icon";              Path = Join-Path $repo "Icon\AkaiS950.ico" },
    @{ What = "the sound library";     Path = Join-Path $staging "BASS.hfe" }
)

Say ""
Say "  what would be packaged" "Cyan"
Say ""

$missing = @()
foreach ($w in $wanted) {
    if (Test-Path $w.Path) {
        $item = Get-Item $w.Path
        $when = $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm")
        Say ("    {0,-20} {1}  {2}" -f $w.What, $when, $w.Path) "Green"
    } else {
        $missing += $w.What
        Say ("    {0,-20} MISSING  {1}" -f $w.What, $w.Path) "Red"
    }
}

if ($missing.Count -gt 0) {
    Say ""
    Write-Error ("not ready to package: " + ($missing -join ", "))
}

if ($CheckOnly) { Say ""; Say "  everything the installer needs is present" "Green"; Say ""; exit 0 }

# ------------------------------------------------------------------------- find Inno Setup

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source

if (-not $iscc) {
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                     "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}

if (-not $iscc) {
    Say ""
    Say "  Inno Setup is not installed, so the installer cannot be compiled." "Yellow"
    Say ""
    Say "  It is free and about 6 MB:  https://jrsoftware.org/isdl.php" "Gray"
    Say "  Version 6.3 or newer - the script uses the x64compatible architecture name." "Gray"
    Say ""
    Say "  Everything it would package is built and listed above, so this is the only" "Gray"
    Say "  thing in the way." "Gray"
    Say ""
    exit 1
}

# ----------------------------------------------------------------------------- package it

Say ""
Say "  compiling the installer with $iscc" "Cyan"

& $iscc (Join-Path $here "VirtualS950.iss")
if ($LASTEXITCODE -ne 0) { Write-Error "the installer did not compile" }

$out = Get-ChildItem (Join-Path $here "Output\*.exe") -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1

Say ""
if ($out) {
    Say ("  {0}  -  {1:N0} KB" -f $out.FullName, ($out.Length / 1KB)) "Green"
} else {
    Say "  compiled, but no .exe turned up in Output" "Yellow"
}
Say ""
