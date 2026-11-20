; Inno Setup script for KeyForge (Windows x64).
;
;   ISCC.exe /DAppVersion=1.0.0 packaging\windows\keyforge.iss
;
; Per-user install: no admin prompt, no Program Files, no registry-wide
; entries.  The bundle is complete - Python, numpy, SoundCard, hidapi.dll and
; wchisp.exe all ship inside - so the target machine needs nothing installed.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "KeyForge"
#define AppPublisher "Velvet Mech"
#define AppURL "https://github.com/coqfu/velvet-mech"
#define ExeName "KeyForge.exe"

[Setup]
AppId={{6C1C3B1E-7D2A-4E0B-9C4E-2B7A5F0D8E31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist\windows
OutputBaseFilename=KeyForge-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName}
LicenseFile=..\..\LICENSE
CloseApplications=yes
RestartApplications=no
AllowNoIcons=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "..\..\dist\windows\KeyForge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "wchisp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "wchisp-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\WINDOWS.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\..\docs\INSTALL.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#ExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Description: "Start KeyForge"; Flags: nowait postinstall skipifsilent
