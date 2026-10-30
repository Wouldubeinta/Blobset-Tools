; Blobset Tools installer (Inno Setup 6.3 or newer).
;
; Don't compile this by hand: run installer\build_installer.ps1, which gathers the release build (exe, Qt runtime,
; codec DLLs, QML modules, game list, pictures, defaults, Vortex extensions) into installer\stage and then calls
; ISCC with the version from CMakeLists.txt.
;
; The install folder only gets read-only files. Everything Blobset Tools writes (Settings.ini, games\..., backups,
; mods, cli.log) goes to Documents\Blobset Tools, made on first launch (src/core/app_paths.h). Uninstalling
; leaves that folder alone.
;
; Games page: the player ticks the games they own and setup writes GameSelectionInfo.xml with only those
; (Blobset Tools lists only them, seeds only their profiles and sets up Vortex only for them). Run setup again to
; change the list. Silent installs: /GAMES="Cricket 24|AFL 26" (the <FolderName>s), otherwise the previous choice.

#ifndef AppVersion
  #define AppVersion "2.1.10.55"
#endif
#ifndef StageDir
  #define StageDir "stage"
#endif
#ifndef SourceDir
  #define SourceDir ".."
#endif

#define AppName "Blobset Tools"
#define AppExe  "Blobset-Tools.exe"

[Setup]
AppId={{BC1791D2-F714-4786-8453-5D35576232E2}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Wouldy Mods
AppPublisherURL=https://github.com/Wouldubeinta/Blobset-Tools
AppSupportURL=https://github.com/Wouldubeinta/Blobset-Tools/issues
AppUpdatesURL=https://github.com/Wouldubeinta/Blobset-Tools/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Ask whether to install for everyone (Program Files) or just this user (no admin needed).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=BlobsetTools-Setup-{#AppVersion}
SetupIconFile={#SourceDir}\assets\blobset_tools.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile=WizardImageFile.png
WizardSmallImageFile=WizardSmallImageFile.png
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
UsePreviousTasks=yes


[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "vortex"; Description: "Set up Vortex Mod Manager support for the PC games I picked (Vortex must be installed)"; GroupDescription: "Mod managers:"; Flags: unchecked

[InstallDelete]
; Left over by older builds (the exe only uses the FFmpeg DLLs in lib).
Type: files; Name: "{app}\avcodec-61.dll"
Type: files; Name: "{app}\avformat-61.dll"
Type: files; Name: "{app}\avutil-59.dll"
Type: files; Name: "{app}\swresample-5.dll"
Type: files; Name: "{app}\swscale-8.dll"
; The game profiles shipped as defaults are refreshed as a whole (the user's copies are in Documents).
Type: filesandordirs; Name: "{app}\defaults"

[Files]
; The exe writes the game list once it's in place (WriteGameList), before the Vortex step runs.
Source: "{#StageDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion; AfterInstall: WriteGameList
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\{#AppExe},\GameSelectionInfo.xml"; Flags: ignoreversion recursesubdirs createallsubdirs
; The full game list: read by the Games page, never installed as is.
Source: "{#StageDir}\GameSelectionInfo.xml"; Flags: dontcopy

[UninstallDelete]
; Written by setup, so the uninstaller doesn't know it otherwise.
Type: files; Name: "{app}\GameSelectionInfo.xml"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; As the signed-in user, so the extensions land in their %APPDATA%\Vortex\plugins (not the admin's).
Filename: "{app}\{#AppExe}"; Parameters: "--install-vortex-extensions --quiet"; StatusMsg: "Setting up the Vortex extensions..."; Tasks: vortex; Flags: runhidden waituntilterminated runasoriginaluser
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer.%n%nYour settings, game profiles, mods and backups are kept in Documents\Blobset Tools (Options > Open Data Folder in Blobset Tools). Run setup again any time to change your games.

[Code]
var
  GamesPage: TWizardPage;
  GameList: TNewCheckListBox;
  GameXml: TArrayOfString;      // each <Game> element of the full list, as written in GameSelectionInfo.xml
  ModFolder: TArrayOfString;   // its <ModFolder>
  FolderName: TArrayOfString;  // its <FolderName>
  GameName: TArrayOfString;     // its <Name>

function IsWanted(const ModF, FolderF, Wanted: String): Boolean;
begin
  Result := (Pos('|' + Lowercase(ModF) + '|', '|' + Lowercase(Wanted) + '|') > 0) or
            (Pos('|' + Lowercase(FolderF) + '|', '|' + Lowercase(Wanted) + '|') > 0);
end;

procedure LoadGames;
var
  Doc, Nodes, Node: Variant;
  I, Count: Integer;
  Loaded: Boolean;
  Text: String;
begin
  ExtractTemporaryFile('GameSelectionInfo.xml');
  Doc := CreateOleObject('Msxml2.DOMDocument.6.0');
  Doc.async := False;
  Doc.preserveWhiteSpace := True;
  Loaded := Doc.load(ExpandConstant('{tmp}\GameSelectionInfo.xml'));
  if not Loaded then
  begin
    Text := Doc.parseError.reason;
    RaiseException('The game list in setup is damaged: ' + Text);
  end;
  Nodes := Doc.selectNodes('/GameSelectionInfo/Game');
  Count := Nodes.length;
  SetArrayLength(GameXml, Count);
  SetArrayLength(ModFolder, Count);
  SetArrayLength(FolderName, Count);
  SetArrayLength(GameName, Count);
  for I := 0 to Count - 1 do
  begin
    Node := Nodes.item(I);
    Text := Node.xml;
    GameXml[I] := Text;
    Text := Node.selectSingleNode('ModFolder').text;
    ModFolder[I] := Trim(Text);
    Text := Node.selectSingleNode('FolderName').text;
    FolderName[I] := Trim(Text);
    Text := Node.selectSingleNode('Name').text;
    GameName[I] := Trim(Text);
  end;
end;

procedure SelectAllClick(Sender: TObject);
var
  I: Integer;
begin
  for I := 0 to GameList.Items.Count - 1 do
    GameList.Checked[I] := True;
end;

procedure ClearAllClick(Sender: TObject);
var
  I: Integer;
begin
  for I := 0 to GameList.Items.Count - 1 do
    GameList.Checked[I] := False;
end;

procedure InitializeWizard;
var
  Wanted: String;
  SelectAll, ClearAll: TNewButton;
  I: Integer;
begin
  LoadGames;

  GamesPage := CreateCustomPage(wpSelectDir, 'Your games',
    'Tick the BigAnt games you own. Blobset Tools will only list these.');

  SelectAll := TNewButton.Create(GamesPage);
  SelectAll.Parent := GamesPage.Surface;
  SelectAll.Caption := 'Select all';
  SelectAll.Left := 0;
  SelectAll.Top := 0;
  SelectAll.Width := ScaleX(90);
  SelectAll.Height := ScaleY(23);
  SelectAll.OnClick := @SelectAllClick;

  ClearAll := TNewButton.Create(GamesPage);
  ClearAll.Parent := GamesPage.Surface;
  ClearAll.Caption := 'Clear';
  ClearAll.Left := SelectAll.Left + SelectAll.Width + ScaleX(8);
  ClearAll.Top := 0;
  ClearAll.Width := ScaleX(90);
  ClearAll.Height := ScaleY(23);
  ClearAll.OnClick := @ClearAllClick;

  GameList := TNewCheckListBox.Create(GamesPage);
  GameList.Parent := GamesPage.Surface;
  GameList.Left := 0;
  GameList.Top := SelectAll.Top + SelectAll.Height + ScaleY(8);
  GameList.Width := GamesPage.SurfaceWidth;
  GameList.Height := GamesPage.SurfaceHeight - GameList.Top;
  GameList.Anchors := [akLeft, akTop, akRight, akBottom];

  Wanted := ExpandConstant('{param:GAMES|}');
  if Wanted = '' then
    Wanted := GetPreviousData('Games', '');
  for I := 0 to GetArrayLength(GameName) - 1 do
    GameList.AddCheckBox(GameName[I], '', 0, IsWanted(ModFolder[I], FolderName[I], Wanted), True, False, False, nil);
end;

function PickedCount: Integer;
var
  I: Integer;
begin
  Result := 0;
  for I := 0 to GameList.Items.Count - 1 do
    if GameList.Checked[I] then
      Result := Result + 1;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = GamesPage.ID) and (PickedCount = 0) then
  begin
    SuppressibleMsgBox('Please tick at least one game.', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PickedFolders: String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to GameList.Items.Count - 1 do
    if GameList.Checked[I] then
    begin
      if Result <> '' then
        Result := Result + '|';
      Result := Result + FolderName[I];
    end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Games', PickedFolders);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
var
  I: Integer;
  Games: String;
begin
  Games := '';
  for I := 0 to GameList.Items.Count - 1 do
    if GameList.Checked[I] then
      Games := Games + Space + GameName[I] + NewLine;
  Result := MemoDirInfo + NewLine + NewLine + 'Your games:' + NewLine + Games;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + MemoTasksInfo + NewLine;
  Result := Result + NewLine + 'Settings, game profiles, mods and backups:' + NewLine + Space +
    'Documents\Blobset Tools (made the first time Blobset Tools starts)';
end;

// GameSelectionInfo.xml with only the ticked games, in the order of the full list.
procedure WriteGameList;
var
  Lines: TArrayOfString;
  I, N: Integer;
begin
  if PickedCount = 0 then
    SelectAllClick(nil);
  SetArrayLength(Lines, PickedCount + 3);
  Lines[0] := '<?xml version="1.0" encoding="UTF-8"?>';
  Lines[1] := '<GameSelectionInfo>';
  N := 2;
  for I := 0 to GameList.Items.Count - 1 do
    if GameList.Checked[I] then
    begin
      Lines[N] := '    ' + GameXml[I];
      N := N + 1;
    end;
  Lines[N] := '</GameSelectionInfo>';
  if not SaveStringsToUTF8File(ExpandConstant('{app}\GameSelectionInfo.xml'), Lines, False) then
    RaiseException('Could not write ' + ExpandConstant('{app}\GameSelectionInfo.xml'));
end;
