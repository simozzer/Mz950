<#
    Build the Studio application.

        .\build.ps1                 builds Mz950Studio.exe beside this script
        .\build.ps1 -Run            builds it and starts it
        .\build.ps1 -Out somewhere\ builds it there instead

    WHY csc AND NOT dotnet build

    There is no .NET SDK on the machine this was written on - only runtimes - so
    `dotnet build` has nothing to build with, and the csproj files are there for an IDE
    rather than for this. csc.exe from .NET Framework 4.0 is on every Windows install
    since about 2010 and needs nothing fetched, which suits a project whose whole point
    is that it has no dependencies.

    The cost is C# 5: no string interpolation, no expression-bodied members, no nameof.
    The source is written to that, deliberately.
#>
param(
    [string]$Out = "",
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) {
    $csc = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"
}
if (-not (Test-Path $csc)) {
    Write-Error "No .NET Framework 4.0 compiler found. Looked in $env:WINDIR\Microsoft.NET."
}

if ($Out -eq "") { $Out = Join-Path $root "Mz950Studio.exe" }

# Program.cs in the List project is the command-line tool's entry point, and two Mains
# in one assembly is an error rather than a choice.
$src = @()
$src += Get-ChildItem (Join-Path $root "AkaiS950Engine\*.cs") | ForEach-Object { $_.FullName }
$src += Get-ChildItem (Join-Path $root "AkaiS950Studio\*.cs") | ForEach-Object { $_.FullName }
$src += Get-ChildItem (Join-Path $root "AkaiS950List\*.cs") |
        Where-Object { $_.Name -ne "Program.cs" } | ForEach-Object { $_.FullName }

Write-Host "building $($src.Count) files -> $Out"

# /unsafe is for one loop in WasapiOut that writes the render buffer through a float*.
# Doing it with Marshal.Copy would mean a second buffer and a copy per callback.
# The icon is an artefact, built from the SVGs beside it by Icon\build-icon.ps1. A build
# without it is not worth failing over - the program runs perfectly well wearing the
# default - so it is passed only when it is there.
$icon = Join-Path $root "Icon\AkaiS950.ico"
$iconArg = if (Test-Path $icon) { "/win32icon:$icon" } else { "" }

if (-not (Test-Path $icon)) {
    Write-Host "no Icon\AkaiS950.ico - run Icon\build-icon.ps1 to make one"
}

& $csc /nologo /unsafe /target:winexe /out:$Out $iconArg `
    /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll `
    $src

if ($LASTEXITCODE -ne 0) { Write-Error "build failed" }

$size = (Get-Item $Out).Length
Write-Host "ok  -  $([math]::Round($size / 1024)) KB"

if ($Run) { & $Out }
