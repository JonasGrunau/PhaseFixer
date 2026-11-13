; Inno Setup script for the Windows installer (VST3).
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.0 /DArtefacts=<repo>\build\PhaseFixer_artefacts\Release /O<out-dir> packaging\windows\PhaseFixer.iss

#ifndef AppVersion
  #define AppVersion "0.3.2"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\PhaseFixer_artefacts\Release"
#endif

[Setup]
AppId={{1481BFDA-547B-48B0-9203-6594C4191CF9}
AppName=PhaseFixer
AppVersion={#AppVersion}
AppPublisher=Jonas Grunau
DefaultDirName={commoncf64}\VST3\PhaseFixer.vst3
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=PhaseFixer-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Files]
Source: "{#Artefacts}\VST3\PhaseFixer.vst3\*"; DestDir: "{commoncf64}\VST3\PhaseFixer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
