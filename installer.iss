#define MyAppVersion "1.0.0"
#define MyAppName "SamplerMode"
#define MyAppPublisher "SamplerMode"
#define MyAppURL "https://github.com/ggptshka-sys/samplermode"

[Setup]
AppId={{7A03D8F5-6E6B-42C3-9C48-77C91E17A641}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\SamplerMode
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=release
OutputBaseFilename=SamplerMode-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=LICENSE
CloseApplications=no
RestartApplications=no
UninstallDisplayName={#MyAppName}

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "build\package\sampler.dll"; DestDir: "{autopf}\VST Plugins"; Flags: ignoreversion
Source: "build\package\sampler.dll"; DestDir: "{autopf}\Common Files\VST3"; DestName: "sampler.vst3"; Flags: ignoreversion
Source: "build\package\sampler-diagnostic.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "Update.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "Update.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "example-library\*"; DestDir: "{userdocs}\SamplerMode\Example Library"; Flags: ignoreversion recursesubdirs createallsubdirs onlyifdoesntexist

[Icons]
Name: "{group}\Обновить SamplerMode"; Filename: "{app}\Update.cmd"; WorkingDir: "{app}"
Name: "{group}\Инструкция SamplerMode"; Filename: "{app}\README.md"
