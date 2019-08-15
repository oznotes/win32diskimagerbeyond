; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!
#define MyAppSourceDir AddBackslash(SourcePath) + "Release"
#define MyAppExeName "Win32DiskImager.exe"
#define MyAppExeFile AddBackslash(MyAppSourceDir) + MyAppExeName
#define MyAppName GetStringFileInfo(MyAppExeFile,ORIGINAL_FILENAME)
#define MyAppVersion GetStringFileInfo(MyAppExeFile,PRODUCT_VERSION)
#define MyAppPublisher "ImageWriter Developers"
#define MyAppURL "http://win32diskimager.sourceforge.net"


[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{3DFFA293-DF2C-4B23-92E5-3433BDC310E1}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={pf32}\ImageWriter
DefaultGroupName=Image Writer
LicenseFile=License.txt
OutputBaseFilename={#MyAppName}-setup-{#MyAppVersion}
SetupIconFile=src\images\setup.ico
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 0,6.1

[Files]
Source: "Release\Win32DiskImager.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "Changelog.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "GPL-2"; DestDir: "{app}"; Flags: ignoreversion
Source: "LGPL-2.1"; DestDir: "{app}"; Flags: ignoreversion
Source: "Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Release\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "Release\translations\*.qm"; DestDir: "{app}\translations"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent runascurrentuser
