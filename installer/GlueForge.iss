; Inno Setup script for GlueForge (Windows VST3 installer).
; Build with:  ISCC GlueForge.iss /DMyVersion=0.1.0
; (run scripts\package.ps1 first so dist\GlueForge-<version>\VST3 exists)

#define MyAppName "GlueForge"
#ifndef MyVersion
  #define MyVersion "0.1.0"
#endif

[Setup]
AppName={#MyAppName}
AppVersion={#MyVersion}
AppPublisher=GlueForge
DefaultDirName={commoncf}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=GlueForge-Setup-{#MyVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayName={#MyAppName} {#MyVersion}

[Files]
; Install the whole VST3 bundle into the system VST3 folder.
Source: "..\dist\GlueForge-{#MyVersion}\VST3\GlueForge.vst3\*"; \
    DestDir: "{commoncf}\VST3\GlueForge.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\GlueForge.vst3"

[Messages]
WelcomeLabel2=This will install the {#MyAppName} VST3 plugin (v{#MyVersion}) into your system VST3 folder. Rescan plugins in your DAW afterwards.
