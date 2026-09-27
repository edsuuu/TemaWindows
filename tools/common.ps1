# Funções usadas pelos scripts de recarregar (dot-source: . "$PSScriptRoot\common.ps1").
$Root = Split-Path $PSScriptRoot
$Bin = Join-Path $Root 'bin'

# Para tudo se o jogo estiver em tela cheia (janela cobrindo o monitor inteiro). Em janela, pode seguir.
function Assert-NoGame {
    $game = Get-Process UAGame -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
    if (-not $game) { return }

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type 'using System; using System.Runtime.InteropServices; public static class GameWindow {
        public struct RECT { public int Left, Top, Right, Bottom; }
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); }'
    $rect = New-Object GameWindow+RECT
    [void][GameWindow]::GetWindowRect($game.MainWindowHandle, [ref]$rect)
    $screen = [System.Windows.Forms.Screen]::FromHandle($game.MainWindowHandle).Bounds
    if ($rect.Left -le $screen.Left -and $rect.Top -le $screen.Top -and $rect.Right -ge $screen.Right -and $rect.Bottom -ge $screen.Bottom) {
        throw 'Jogo em tela cheia: não compilo nem reinicio nada.'
    }
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
