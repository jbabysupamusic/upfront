; Inno Setup script - builds "UPFRONT-Setup.exe" (done automatically by GitHub)
#define AppVersion GetEnv("UPFRONT_VERSION")
#if AppVersion == ""
  #define AppVersion "1.1.0"
#endif

[Setup]
AppName=UPFRONT Harmonic EQ
AppVersion={#AppVersion}
AppPublisher=UPFRONT Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={autopf64}\UPFRONT
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=UPFRONT-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "..\build\UPFRONT_artefacts\Release\VST3\UPFRONT.vst3\*"; DestDir: "{commoncf64}\VST3\UPFRONT.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=UPFRONT is installed.%n%nIn FL Studio: Options > Manage plugins > Find installed plugins.%nThen add UPFRONT to any mixer insert.
