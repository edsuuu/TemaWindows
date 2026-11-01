; Instalador do Wallget: widgets no papel de parede animado (o FundoVivo.exe, instalado como Wallget.exe) e o app de
; configuração (FundoConfig.exe -> WallgetConfig.exe: cores, JSON, posições e cidade, com prévia). Por usuário e sem
; administrador; só o passo do HWiNFO (temperatura da CPU) pede elevação, e só aparece quando falta o HWiNFO ou a
; tarefa que reabre ele. Gerado por build\build-installer.bat em bin\Wallget-Setup.exe.
#define Version "1.0.0"

[Setup]
AppId=TemaWindows.Wallget
AppName=Wallget
AppVersion={#Version}
AppPublisher=edsuuu
AppPublisherURL=https://github.com/edsuuu/TemaWindows
DefaultDirName={autopf}\Wallget
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\bin
OutputBaseFilename=Wallget-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Wallget
UninstallDisplayIcon={app}\bin\Wallget.exe

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "hwinfo"; Description: "Instalar e configurar o HWiNFO para mostrar a temperatura da CPU (pede administrador)"; Check: NeedsHwinfo

[Files]
Source: "..\bin\FundoVivo.exe"; DestDir: "{app}\bin"; DestName: "Wallget.exe"; Flags: ignoreversion
Source: "..\bin\FundoConfig.exe"; DestDir: "{app}\bin"; DestName: "WallgetConfig.exe"; Flags: ignoreversion
Source: "..\assets\fundo.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\fundo-morph.bin"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\folder.ico"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\tools\hwinfo-setup.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Dirs]
Name: "{app}\cache"

[Icons]
Name: "{autoprograms}\Wallget"; Filename: "{app}\bin\WallgetConfig.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Wallget"; ValueData: """{app}\bin\Wallget.exe"""; Flags: uninsdeletevalue

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\hwinfo-setup.ps1"""; Verb: runas; Flags: shellexec runhidden waituntilterminated; Tasks: hwinfo; StatusMsg: "Configurando o HWiNFO..."
Filename: "{app}\bin\Wallget.exe"; Flags: nowait
Filename: "{app}\bin\WallgetConfig.exe"; Description: "Abrir o Wallget (cores, JSON, posições e cidade)"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im Wallget.exe /im WallgetConfig.exe"; Flags: runhidden; RunOnceId: "Parar"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\cache"

[Code]
// Falta o HWiNFO ou a tarefa que reabre ele a cada 11 h?
function NeedsHwinfo: Boolean;
var
  Code: Integer;
begin
  Result := not FileExists(ExpandConstant('{commonpf64}\HWiNFO64\HWiNFO64.EXE')) or
            not Exec(ExpandConstant('{sys}\schtasks.exe'), '/query /tn "HWiNFO (TemaWindows)"', '', SW_HIDE, ewWaitUntilTerminated, Code) or
            (Code <> 0);
end;

// Para o que estiver rodando antes de copiar os arquivos (o novo sobe no fim). Inclui os nomes de desenvolvimento
// (FundoVivo/FundoConfig): o papel de parede só roda um por vez, então um deles aberto impediria o novo de subir.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im Wallget.exe /im WallgetConfig.exe /im FundoVivo.exe /im FundoConfig.exe', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
  Result := '';
end;
