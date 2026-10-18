#ifndef AppStage
  #define AppStage "..\out\package"
#endif
#ifndef DriverDir
  #define DriverDir "..\out\dependencies\VBCABLE"
#endif
#ifndef ReleaseVersion
  #define ReleaseVersion "0.4.2-rc.1"
#endif
[Setup]
AppId={{9240D5A8-051A-4D27-B83E-27A8E7069411}
AppName=TDSpeed Preview
AppVersion={#ReleaseVersion}
VersionInfoVersion=0.4.2.0
#ifdef TrustedSigning
SignTool=PublisherSign
SignedUninstaller=yes
#endif
SetupIconFile=..\Source\Assets\TDSpeed.ico
AppPublisher=Fyodor
DefaultDirName={localappdata}\Programs\TDSpeed Preview
DefaultGroupName=TDSpeed Preview
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\..\release
OutputBaseFilename=TDSpeed-Setup-{#ReleaseVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
InfoBeforeFile=ThirdPartyNotice.txt
UninstallDisplayIcon={app}\TDSpeedPreview.exe
CloseApplications=yes
RestartApplications=no
[Files]
Source: "{#AppStage}\TDSpeedPreview.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\Read me.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppStage}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AppStage}\TDSpeed-Source-{#ReleaseVersion}.zip"; DestDir: "{app}\Source"; Flags: ignoreversion
Source: "{#AppStage}\Licences\*"; DestDir: "{app}\Licences"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "ThirdPartyNotice.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DriverDir}\*"; DestDir: "{app}\Drivers\VB-CABLE"; Flags: ignoreversion recursesubdirs createallsubdirs
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
[Icons]
Name: "{group}\TDSpeed Preview"; Filename: "{app}\TDSpeedPreview.exe"
Name: "{group}\Restore normal audio"; Filename: "{app}\TDSpeedPreview.exe"; Parameters: "--restore"
Name: "{group}\Uninstall TDSpeed Preview"; Filename: "{uninstallexe}"
Name: "{userdesktop}\TDSpeed Preview"; Filename: "{app}\TDSpeedPreview.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\TDSpeedPreview.exe"; Description: "Open TDSpeed Preview"; Flags: nowait postinstall skipifsilent; Check: CanLaunch
[UninstallRun]
Filename: "{app}\TDSpeedPreview.exe"; Parameters: "--shutdown"; Flags: runhidden waituntilterminated; RunOnceId: "Shutdown"
Filename: "{app}\TDSpeedPreview.exe"; Parameters: "--restore"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreRouting"
Filename: "{app}\TDSpeedPreview.exe"; Parameters: "--remove-startup"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveStartup"
[Code]
var
  CableNeedsRestart: Boolean;
function CanLaunch: Boolean;
begin
  Result := not CableNeedsRestart;
end;
function NeedRestart: Boolean;
begin
  Result := CableNeedsRestart;
end;
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\TDSpeedPreview.exe')) then
    Exec(ExpandConstant('{app}\TDSpeedPreview.exe'), '--shutdown', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;
procedure CurStepChanged(CurStep: TSetupStep);
var
  State, Code: Integer;
  App, Driver: String;
begin
  if CurStep <> ssPostInstall then exit;
  App := ExpandConstant('{app}\TDSpeedPreview.exe');
  Exec(App, '--shutdown', '', SW_HIDE, ewWaitUntilTerminated, Code);
  if not Exec(App, '--check-cable', '', SW_HIDE, ewWaitUntilTerminated, State) then begin
    MsgBox('TDSpeed could not inspect your audio devices. Driver installation was skipped to protect existing devices. Open the app after setup to check audio configuration.', mbError, MB_OK);
    exit;
  end;
  if State = 1 then begin
    Log('A usable cable already exists; driver installation skipped.');
    exit;
  end;
  if State = 2 then begin
    Log('A cable is installed but unavailable; no reinstallation.');
    MsgBox('A virtual cable is already installed but is busy, disabled, disconnected, or incomplete. No driver was reinstalled. Close VSTHost and enable the cable in Windows sound settings, then refresh TDSpeed.', mbInformation, MB_OK);
    exit;
  end;
  if State <> 0 then begin
    MsgBox('Audio-device detection failed. Driver installation was skipped. See the included Read me file.', mbError, MB_OK);
    exit;
  end;
  Driver := ExpandConstant('{app}\Drivers\VB-CABLE\VBCABLE_Setup_x64.exe');
  if ShellExec('runas', Driver, '-i -h', ExtractFileDir(Driver), SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Log(Format('VB-CABLE setup exit code: %d', [Code]));
    if (Code = 0) or (Code = 1) or (Code = 3010) then begin
      CableNeedsRestart := True;
      MsgBox('VB-CABLE setup completed. Restart Windows before opening TDSpeed. Your existing audio drivers have not been removed.', mbInformation, MB_OK);
    end else
      MsgBox('VB-CABLE setup did not report successful installation. Run its included installer from the Drivers folder, or contact VB-Audio. TDSpeed is installed.', mbError, MB_OK);
  end else
    MsgBox('Driver installation was cancelled or could not start. TDSpeed is installed; run setup again to install the missing cable.', mbInformation, MB_OK);
end;
