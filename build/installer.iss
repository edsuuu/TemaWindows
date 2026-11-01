; Instalador do Wallget: widgets no papel de parede animado (o FundoVivo.exe, instalado como Wallget.exe), o app de
; configuração (FundoConfig.exe -> WallgetConfig.exe: cores, widgets, JSON, layout, cidade, fundo e barra, com prévia)
; e, se escolhida, a barra de tarefas (TemaBarra.dll: blur/transparente e o logo do Iniciar em degradê). Por usuário e
; sem administrador; só o passo do HWiNFO (temperatura da CPU) pede elevação, e só aparece quando falta o HWiNFO ou a
; tarefa que reabre ele. Gerado por build\build-installer.bat em bin\Wallget-Setup.exe.
#define Version "1.1.1"

[Setup]
AppId=TemaWindows.Wallget
AppName=Wallget
AppVersion={#Version}
AppPublisher=edsuuu
AppPublisherURL=https://github.com/edsuuu/Wallget
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
Name: "barra"; Description: "Barra de tarefas: fundo com blur ou transparente e o logo do Iniciar em degradê (muda o Explorer)"; Flags: unchecked; Check: TaskbarFree
Name: "hwinfo"; Description: "Instalar e configurar o HWiNFO para mostrar a temperatura da CPU (pede administrador)"; Check: NeedsHwinfo

[Files]
Source: "..\bin\FundoVivo.exe"; DestDir: "{app}\bin"; DestName: "Wallget.exe"; Flags: ignoreversion
Source: "..\bin\FundoConfig.exe"; DestDir: "{app}\bin"; DestName: "WallgetConfig.exe"; Flags: ignoreversion
Source: "..\bin\TemaBarra.dll"; DestDir: "{app}\bin"; Flags: ignoreversion uninsrestartdelete; Tasks: barra
Source: "..\assets\fundo.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\fundo-morph.bin"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\folder.ico"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\tools\hwinfo-setup.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Dirs]
Name: "{app}\cache"
Name: "{app}\logs"; Tasks: barra

[Icons]
Name: "{autoprograms}\Wallget"; Filename: "{app}\bin\WallgetConfig.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Wallget"; ValueData: """{app}\bin\Wallget.exe"""; Flags: uninsdeletevalue; Check: WantsStartup
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "TemaBarra"; ValueData: "rundll32.exe ""{app}\bin\TemaBarra.dll"",Run"; Flags: uninsdeletevalue; Tasks: barra

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\hwinfo-setup.ps1"""; Verb: runas; Flags: shellexec runhidden waituntilterminated; Tasks: hwinfo; StatusMsg: "Configurando o HWiNFO..."
Filename: "{sys}\rundll32.exe"; Parameters: """{app}\bin\TemaBarra.dll"",Run"; Flags: nowait; Tasks: barra; Check: FreshTaskbar
Filename: "{app}\bin\Wallget.exe"; Flags: nowait
Filename: "{app}\bin\WallgetConfig.exe"; Description: "Abrir o Wallget (cores, widgets, fundo e barra)"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im Wallget.exe /im WallgetConfig.exe"; Flags: runhidden; RunOnceId: "Parar"
Filename: "powershell.exe"; Parameters: "{code:StopTaskbarCommand}"; Flags: runhidden; RunOnceId: "PararBarra"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\cache"
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\bin\TemaBarra.dll.old"

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

// A barra pode ser instalada? Não quando já há outra TemaBarra.dll iniciando com o Windows (a de desenvolvimento, em
// outra pasta): duas no mesmo Explorer brigariam.
function TaskbarFree: Boolean;
var
  Command: String;
begin
  Result := not RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TemaBarra', Command) or
            (Pos(Lowercase(ExpandConstant('{app}\bin\TemaBarra.dll')), Lowercase(Command)) > 0);
end;

// Inicia com o Windows, a não ser que o app tenha desligado (IniciarComWindows = 0).
function WantsStartup: Boolean;
var
  Value: Cardinal;
begin
  Result := not RegQueryDWordValue(HKCU, 'Software\TemaBarra', 'IniciarComWindows', Value) or (Value <> 0);
end;

// Comando que para o rundll32 que injeta a TemaBarra.dll desta pasta (os outros rundll32 ficam).
function StopTaskbarCommand(Param: String): String;
begin
  Result := '-NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq ''rundll32.exe'' -and $_.CommandLine -like ''*' +
            ExpandConstant('{app}') + '\bin\TemaBarra.dll*'' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"';
end;

var
  TaskbarWasInstalled: Boolean;

// A barra entra agora (injetando no Explorer aberto) só na primeira instalação; numa atualização o Explorer já tem a
// DLL velha carregada, e a nova entra no próximo login.
function FreshTaskbar: Boolean;
begin
  Result := not TaskbarWasInstalled;
end;

// Para o que estiver rodando antes de copiar os arquivos (o novo sobe no fim). Inclui os nomes de desenvolvimento
// (FundoVivo/FundoConfig): o papel de parede só roda um por vez, então um deles aberto impediria o novo de subir. A
// TemaBarra.dll antiga continua carregada no Explorer: ela é renomeada (dá para renomear uma DLL em uso) para a nova
// entrar no lugar, e o Explorer passa a usar a nova no próximo login. Com a barra desmarcada agora, ela só deixa de
// iniciar com o Windows (a DLL fica, carregada no Explorer até o próximo login).
// ponytail: a .old de uma atualização só sai na seguinte; apagar no login se juntar muitas.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
  Dll: String;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im Wallget.exe /im WallgetConfig.exe /im FundoVivo.exe /im FundoConfig.exe', '', SW_HIDE,
       ewWaitUntilTerminated, Code);
  Dll := ExpandConstant('{app}\bin\TemaBarra.dll');
  TaskbarWasInstalled := FileExists(Dll);
  if TaskbarWasInstalled then begin
    Exec('powershell.exe', StopTaskbarCommand(''), '', SW_HIDE, ewWaitUntilTerminated, Code);
    if WizardIsTaskSelected('barra') then begin
      DeleteFile(Dll + '.old');
      RenameFile(Dll, Dll + '.old');
    end else if TaskbarFree then
      RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TemaBarra');
  end;
  Result := '';
end;
