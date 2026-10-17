; Inno Setup script for the Windows installer (VST3).
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.0 /DArtefacts=<repo>\build\PhaseFucker_artefacts\Release /O<out-dir> packaging\windows\PhaseFucker.iss

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\PhaseFucker_artefacts\Release"
#endif

[Setup]
AppId={{BA788C6A-D5B9-4531-A590-0FFA1712DAE2}
AppName=PhaseFucker
AppVersion={#AppVersion}
AppPublisher=Jonas Grunau
DefaultDirName={commoncf64}\VST3\PhaseFucker.vst3
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=PhaseFucker-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Files]
Source: "{#Artefacts}\VST3\PhaseFucker.vst3\*"; DestDir: "{commoncf64}\VST3\PhaseFucker.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
