; Mz950 - installer for the editor and the plugin.
;
; Inno Setup 6.3 or newer. build-installer.ps1 builds both products, checks every file
; named below actually exists, and then runs this.
;
; WHY BOTH IN ONE INSTALLER
;
; They are two halves of the same thing. The editor opens disk images, edits programmes and
; writes them back to something a real S950 will load; the plugin plays those same disks in
; a DAW. Both are driven by the same measured engine - the numbers in Cal.cs and Cal.h agree
; to the digit - so shipping them together is shipping one instrument with two front ends.
;
; WHY IT ASKS WHO IT IS FOR
;
; A VST3 has two homes on Windows: the machine-wide one under Common Files, which needs
; administrator rights, and the per-user one under LocalAppData, which does not. Inno's
; {autocf} resolves to whichever matches the choice made at the start, so the same script
; does both and neither needs explaining.
;
; THE RENAME
;
; This was VirtualS950 up to release 0.4.0 and is Mz950 now, so that the product no longer wears
; Akai's model name - it plays Akai's disks, and says so, but it is not Akai's. The AppId
; below did NOT change, and must not: it is how Inno knows an existing VirtualS950 install
; is this program, so installing over one upgrades it in place rather than leaving two
; entries in Apps & features. What the old version left under its old names is taken away
; in [InstallDelete] and [Registry] below.

#define AppName        "Mz950"
#define AppVersion     "0.5.3"
#define AppPublisher   "Simon Moscrop"
#define AppCopyright   "Copyright (C) 2026 Simon Moscrop"
#define AppURL         "https://github.com/simozzer/Mz950"

#define RepoRoot       ".."
#define PluginArtefacts RepoRoot + "\Plugin\build\VirtualS950_artefacts\Release"

[Setup]
; Never change this - see THE RENAME above.
AppId={{7A1D5C4E-9B62-4E8B-9F3A-0C51A4D9E7B2}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright={#AppCopyright}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}

; Shown before anything is installed, and required rather than decorative: conveying a
; binary under the AGPL means conveying the licence with it. It is installed beside the
; programs as well, because a licence somebody clicked past a year ago is not a copy they
; have got.
LicenseFile={#RepoRoot}\LICENSE

; An upgrade keeps the folder and Start menu group it already had (Inno's default for a
; known AppId), so an old install stays in its VirtualS950 folder. Only a new one gets these.
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=Output
OutputBaseFilename=Mz950-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

; The setup program wears the same face as what it installs. Built by Icon\build-icon.ps1
; from the drawings beside it, so this is an artefact and may not be there on a clean
; checkout - which is what check-installer.ps1 will say if it is not.
SetupIconFile={#RepoRoot}\Icon\AkaiS950.ico

; The plugin is 64-bit only, and so is the editor's build.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Ask rather than assume. A per-user install needs no administrator and puts the VST3
; where a host will still find it; a machine-wide one is what a studio machine expects.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Mz950Studio.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; What it is and what it is not, on the first page anybody sees.
WelcomeLabel2=This will install [name/ver] on your computer.%n%nMz950 plays and edits Akai S900/S950 floppy disk images. It is an independent project: it is not affiliated with, endorsed or sponsored by Akai Professional or inMusic. Akai, S900 and S950 are trademarks of their respective owners.%n%nIt is recommended that you close all other applications before continuing.

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "studio";     Description: "Mz950 Studio - open, edit and save S900/S950 disk images"; Types: full custom; Flags: checkablealone
Name: "vst3";       Description: "VST3 plugin - play disks in a DAW";                          Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone player - the plugin without a DAW";               Types: full custom; Flags: checkablealone
Name: "sounds";     Description: "Sound library - five disks of synthesised sounds";           Types: full custom; Flags: checkablealone

[InstallDelete]
;
; What a VirtualS950 install left under its old names. Every one of these is replaced by a
; file of the new name below, so leaving them would mean two of everything - and for the
; VST3 it is worse than untidy, because the old bundle carries the SAME plugin ID as the new
; one (the ID comes from codes that did not change), and a host finding two copies of one
; plugin loads whichever it likes.
;
Type: files;          Name: "{app}\AkaiS950Studio.exe"
Type: files;          Name: "{app}\VirtualS950.exe"
Type: filesandordirs; Name: "{autocf}\VST3\VirtualS950.vst3"
Type: files;          Name: "{group}\Akai S950 Studio.lnk"
Type: files;          Name: "{group}\VirtualS950 Standalone.lnk"
Type: files;          Name: "{group}\Uninstall VirtualS950.lnk"
Type: files;          Name: "{autodesktop}\Akai S950 Studio.lnk"

[Files]
Source: "{#RepoRoot}\Mz950Studio.exe"; DestDir: "{app}"; Components: studio; Flags: ignoreversion

Source: "{#PluginArtefacts}\Standalone\Mz950.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; A VST3 is a folder, not a file - the binary lives at Contents\x86_64-win inside it - so
; the whole bundle is copied and the structure kept.
Source: "{#PluginArtefacts}\VST3\Mz950.vst3\*"; DestDir: "{autocf}\VST3\Mz950.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

;
; The sound library: five disk images, written by AkaiS950Synth during packaging rather
; than carried in the repository - the same bargain the icon makes, and for a better
; reason, since a .hfe is 2 MB of binary nobody can review.
;
; They matter more than their size suggests. Without them somebody who has just installed
; this owns no S950 floppies and has nothing whatever to open, which makes a working
; player look like a broken one. Ten megabytes of images compress to a few hundred
; kilobytes inside the setup - the disks are mostly zeroes and lzma2 is told to pack them
; as one solid block - so this costs the download almost nothing.
;
; Nothing is decompressed afterwards: the installer writes them out as it installs, the
; way it writes every other file.
;
Source: "{#RepoRoot}\Installer\staging\Disks\*.hfe"; DestDir: "{app}\Disks"; \
    Components: sounds; Flags: ignoreversion

; Not optional and not attached to a component: every install gets it, because the licence
; travels with the binaries whichever of them were chosen.
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

Source: "{#RepoRoot}\README.md";        DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#RepoRoot}\Plugin\README.md"; DestDir: "{app}"; DestName: "README-plugin.md"; Flags: ignoreversion

; What changed since the last version. Beside the READMEs rather than shown during setup:
; somebody upgrading wants to find it afterwards, and nobody reads a wall of text standing
; between them and an install.
Source: "{#RepoRoot}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion

; The walk-through, which Help -> Tutorial opens from exactly here. Not attached to a
; component either: the plugin's section of it is as much use to somebody who installed
; only the plugin.
Source: "{#RepoRoot}\docs\tutorial.html"; DestDir: "{app}\docs"; Flags: ignoreversion

; The screenshots the walk-through shows. Beside it, at the path it links them by.
Source: "{#RepoRoot}\docs\img\*.png"; DestDir: "{app}\docs\img"; Flags: ignoreversion

[Registry]
;
; Where things went, for the parts of this that are not in {app} and cannot ask.
;
; The VST3 is loaded by the host, from a folder shared with every other plugin, and has no
; idea where its installer put anything. Without this its file browser opens on Documents,
; which for somebody who has just installed this and owns no floppies is an empty room. So
; the installer writes down the two paths and the plugin reads them.
;
; HKA follows the install mode - HKCU for a per-user install, HKLM for a machine-wide one -
; and the plugin looks in that order. uninsdeletekey takes the lot away again.
;
Root: HKA; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "InstallPath"; \
    ValueData: "{app}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "DiskLibrary"; \
    ValueData: "{app}\Disks"
Root: HKA; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "Tutorial"; \
    ValueData: "{app}\docs\tutorial.html"

; The key a VirtualS950 install wrote. The plugin still reads it as a fallback, for a machine
; with the old version on it; this install has just written the new one, so the old can go.
Root: HKA; Subkey: "Software\VirtualS950"; Flags: deletekey dontcreatekey

[Icons]
Name: "{group}\Mz950 Studio";            Filename: "{app}\Mz950Studio.exe"; Components: studio
Name: "{group}\Tutorial";                Filename: "{app}\docs\tutorial.html"
Name: "{group}\Mz950 Standalone";        Filename: "{app}\Mz950.exe";       Components: standalone
Name: "{group}\Uninstall {#AppName}";    Filename: "{uninstallexe}"

Name: "{autodesktop}\Mz950 Studio";      Filename: "{app}\Mz950Studio.exe"; Components: studio; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the editor"; \
    GroupDescription: "Shortcuts:"; Components: studio; Flags: unchecked

[UninstallDelete]
;
; Uninstall removes the files it put inside the bundle, but not the bundle itself - Inno
; only prunes directories it is sure it created, and this one may have been there already.
; That leaves an empty Mz950.vst3 sitting in a folder the host scans, which is worse than
; untidy: a bundle with no binary in it is something a DAW has to decide what to do about,
; and different ones decide differently.
;
; Found by installing and uninstalling this for real - the binary was gone and the folder
; was not, which is also why the first check for it reported the plugin still present.
;
Type: filesandordirs; Name: "{autocf}\VST3\Mz950.vst3"

[Run]
Filename: "{app}\Mz950Studio.exe"; Description: "Open Mz950 Studio"; \
    Components: studio; Flags: nowait postinstall skipifsilent

[Code]
//
// The editor is a .NET Framework 4 program. Windows 10 from 1903 and every Windows 11 ship
// 4.8 in the box, so this will almost never fire - but "nothing happens when I run it" is a
// miserable way to discover otherwise, and the check costs one registry read.
//
function HasDotNetFramework4: Boolean;
var
  Release: Cardinal;
begin
  Result := RegQueryDWordValue (HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full',
                                'Release', Release);
end;

function InitializeSetup: Boolean;
begin
  Result := True;

  if not HasDotNetFramework4 then
    Result := MsgBox ('The editor needs the .NET Framework 4, which does not appear to be '
                      + 'installed.' + #13#10#13#10
                      + 'The plugin does not need it, so installing only that will work. '
                      + 'Carry on?',
                      mbConfirmation, MB_YESNO) = IDYES;
end;

//
// Say where the VST3 went. Hosts differ on which folders they scan, and the per-user one
// is the one they are most likely to have to be told about once.
//
procedure CurStepChanged (CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsComponentSelected ('vst3') and not IsAdminInstallMode then
    MsgBox ('The plugin was installed for you only, at:' + #13#10#13#10
            + ExpandConstant ('{autocf}\VST3') + #13#10#13#10
            + 'If your DAW does not find it, add that folder to its VST3 search paths. '
            + 'In Ableton: Preferences, Plug-Ins, VST3 Plug-In Custom Folder.',
            mbInformation, MB_OK);
end;
