; Inno Setup script for GlueForge (Windows installer).
; Build with:  ISCC GlueForge.iss /DMyVersion=0.2.0
; Run scripts\package.ps1 first so dist\GlueForge-<version>\{VST3,Standalone} exist.

#define MyAppName "GlueForge"
#ifndef MyVersion
  #define MyVersion "0.2.0"
#endif

[Setup]
AppName={#MyAppName}
AppVersion={#MyVersion}
AppPublisher=Nightshift Audio
DefaultDirName={autopf}\Nightshift Audio\{#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=GlueForge-Setup-{#MyVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayName={#MyAppName} {#MyVersion}
WizardStyle=modern

[Types]
Name: "full"; Description: "Full (VST3 + Standalone)"
Name: "vst3"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin (Ableton and other hosts)"; Types: full vst3 custom; Flags: fixed
Name: "standalone"; Description: "Standalone application";                 Types: full

[Files]
; VST3 bundle -> system VST3 folder (where DAWs scan).
Source: "..\dist\GlueForge-{#MyVersion}\VST3\GlueForge.vst3\*"; DestDir: "{commoncf}\VST3\GlueForge.vst3"; \
    Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
; Standalone app -> Program Files\GlueForge.
Source: "..\dist\GlueForge-{#MyVersion}\Standalone\GlueForge.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\GlueForge.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\GlueForge.vst3"

[Messages]
WelcomeLabel2=This installs the {#MyAppName} VST3 plugin (v{#MyVersion}) into your system VST3 folder, plus an optional standalone app. Rescan plug-ins in your DAW afterwards.
