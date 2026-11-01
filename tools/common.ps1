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

# Retângulo arredondado como caminho (x, y, largura, altura e raio na grade de 256 dos ícones).
function New-RoundedRect([float]$X, [float]$Y, [float]$W, [float]$H, [float]$R) {
    Add-Type -AssemblyName System.Drawing
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc($X, $Y, 2 * $R, 2 * $R, 180, 90)
    $path.AddArc($X + $W - 2 * $R, $Y, 2 * $R, 2 * $R, 270, 90)
    $path.AddArc($X + $W - 2 * $R, $Y + $H - 2 * $R, 2 * $R, 2 * $R, 0, 90)
    $path.AddArc($X, $Y + $H - 2 * $R, 2 * $R, 2 * $R, 90, 90)
    $path.CloseFigure()
    $path
}

# .ico com um PNG por tamanho, cada um desenhado por `$Paint` (recebe o Graphics já na grade de 256 e o tamanho em px):
# cabeçalho, uma entrada de 16 bytes por imagem e os PNGs em seguida.
function Save-Icon([string]$Path, [int[]]$Sizes, [scriptblock]$Paint) {
    Add-Type -AssemblyName System.Drawing
    $images = foreach ($size in $Sizes) {
        $bitmap = New-Object Drawing.Bitmap $size, $size
        $g = [Drawing.Graphics]::FromImage($bitmap)
        $g.SmoothingMode = 'AntiAlias'
        $g.ScaleTransform($size / 256, $size / 256)
        & $Paint $g $size
        $stream = New-Object IO.MemoryStream
        $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose()
        $bitmap.Dispose()
        , $stream.ToArray()
    }

    $writer = New-Object IO.BinaryWriter ([IO.File]::Create($Path))
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$Sizes.Count)
    $offset = 6 + 16 * $Sizes.Count
    for ($i = 0; $i -lt $Sizes.Count; $i++) {
        $side = [byte]($Sizes[$i] % 256)
        $writer.Write($side); $writer.Write($side); $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
        $offset += $images[$i].Length
    }
    $images | ForEach-Object { $writer.Write($_) }
    $writer.Close()
}
