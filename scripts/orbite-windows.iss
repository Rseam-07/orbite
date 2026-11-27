#ifndef AppVersion
  #error "Provide AppVersion on the command line."
#endif
#ifndef StageDir
  #error "Provide StageDir on the command line."
#endif

[Setup]
AppId={{795B14B5-881E-4DD7-8937-44D3A92A895B}
AppName=Orbite
AppVersion={#AppVersion}
AppPublisher=Orbite
AppPublisherURL=https://github.com/Rseam-07/orbite
DefaultDirName={localappdata}\Programs\Orbite
DefaultGroupName=Orbite
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
SetupIconFile=..\assets\Orbite.ico
UninstallDisplayIcon={app}\Orbite.exe
OutputBaseFilename=Orbite-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\Orbite"; Filename: "{app}\Orbite.exe"

[Run]
Filename: "{app}\Orbite.exe"; Description: "Launch Orbite"; Flags: nowait postinstall skipifsilent
