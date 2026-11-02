; Orpheus — installeur Windows (Inno Setup 6).
;
; Compilé par .github/workflows/installers.yml :
;   iscc /DAppVersion=1.0.1 /DSourceDir=<dossier déployé> /DOutputDir=<sortie> orpheus.iss
;
; Installation par utilisateur (sans droits administrateur) dans %LOCALAPPDATA%\Programs\Orpheus,
; ou pour tous les utilisateurs si on le choisit au démarrage. Les données (morceaux, playlists)
; restent dans %LOCALAPPDATA%\orpheus : une mise à jour ne les touche pas.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\deploy"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build"
#endif

[Setup]
; Identifiant fixe : les nouvelles versions remplacent l'ancienne au lieu de s'installer à côté.
AppId={{06EAEF75-1EFB-4D2B-BE29-3C63362DD6AA}
AppName=Orpheus
AppVersion={#AppVersion}
AppVerName=Orpheus {#AppVersion}
AppPublisher=killex8569
AppPublisherURL=https://github.com/killex8569/Orpheus
AppSupportURL=https://github.com/killex8569/Orpheus/issues
AppUpdatesURL=https://github.com/killex8569/Orpheus/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Orpheus
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809+ : curl.exe (utilisé pour télécharger yt-dlp) est fourni avec le système.
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Orpheus-{#AppVersion}-windows-x64-setup
SetupIconFile=orpheus.ico
UninstallDisplayIcon={app}\orpheus.exe
UninstallDisplayName=Orpheus
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Ferme Orpheus s'il tourne pendant une mise à jour ou une désinstallation.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.DeleteData=Supprimer aussi vos données Orpheus (morceaux téléchargés, playlists, statistiques, réglages) ?%n%n%1%n%nChoisissez « Non » pour les garder en vue d'une réinstallation.
english.DeleteData=Also delete your Orpheus data (downloaded tracks, playlists, statistics, settings)?%n%n%1%n%nChoose "No" to keep them for a later reinstall.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Modules Qt/QML d'une version précédente : évite de mélanger d'anciens fichiers avec les nouveaux.
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Orpheus"; Filename: "{app}\orpheus.exe"; Comment: "Lecteur de musique (YouTube, Spotify, cache local)"
Name: "{autodesktop}\Orpheus"; Filename: "{app}\orpheus.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\orpheus.exe"; Description: "{cm:LaunchProgram,Orpheus}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    exit;
  DataDir := ExpandConstant('{localappdata}\orpheus');
  if DirExists(DataDir) and not UninstallSilent then
    if MsgBox(FmtMessage(CustomMessage('DeleteData'), [DataDir]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(DataDir, True, True, True);
end;
