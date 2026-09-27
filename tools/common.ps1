# Funções usadas pelos scripts de recarregar (dot-source: . "$PSScriptRoot\common.ps1").
$Root = Split-Path $PSScriptRoot
$Bin = Join-Path $Root 'bin'

# Para tudo se o jogo estiver aberto: nada de compilar nem reiniciar nada com ele rodando.
function Assert-NoGame {
    if (Get-Process UAGame -ErrorAction SilentlyContinue) { throw 'Jogo aberto: não compilo nem reinicio nada.' }
}

# Roda build\build-<nome>.bat e, se falhar, mostra só as linhas que interessam.
function Invoke-Build([string]$Name) {
    $out = & cmd.exe /c "`"$Root\build\build-$Name.bat`"" 2>&1
    if ($LASTEXITCODE -ne 0) {
        $out | Select-String -NotMatch 'vswhere|reconhecido|ou externo|diretório é inválido' | Select-Object -First 20
        throw "build-$Name falhou"
    }
}

# Para o vigia (rundll32) de uma DLL, rodando de bin\ ou de um caminho antigo.
function Stop-Injector([string]$Dll) {
    Get-CimInstance Win32_Process -Filter "Name='rundll32.exe'" | Where-Object CommandLine -like "*$Dll*" |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
}

# Inicia o vigia de uma DLL de bin\.
function Start-Injector([string]$Dll) {
    Start-Process rundll32.exe -ArgumentList "`"$Bin\$Dll`",Run"
}
