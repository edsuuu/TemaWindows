# Gera assets\folder.ico: pasta com o degradê do logo do Iniciar (branco no topo à esquerda até o preto). Quem aplica
# nas pastas da área de trabalho é o FundoVivo (src\wallpaper\folder_icons.cpp).
# Uso: .\folder-icon.ps1
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Add-Type -AssemblyName System.Drawing

$Icon = Join-Path $Root 'assets\folder.ico'
$Sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

# Retângulo arredondado como caminho (x, y, largura, altura e raio na grade de 256).
function New-RoundedRect([float]$X, [float]$Y, [float]$W, [float]$H, [float]$R) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc($X, $Y, 2 * $R, 2 * $R, 180, 90)
    $path.AddArc($X + $W - 2 * $R, $Y, 2 * $R, 2 * $R, 270, 90)
    $path.AddArc($X + $W - 2 * $R, $Y + $H - 2 * $R, 2 * $R, 2 * $R, 0, 90)
    $path.AddArc($X, $Y + $H - 2 * $R, 2 * $R, 2 * $R, 90, 90)
    $path.CloseFigure()
    $path
}

# Fundo da pasta com a aba em cima à esquerda, no formato da pasta do Windows 11.
function New-BackPath {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc(20, 36, 24, 24, 180, 90)
    $path.AddLine(32, 36, 96, 36)
    $path.AddLine(96, 36, 118, 58)
    $path.AddArc(212, 58, 24, 24, 270, 90)
    $path.AddArc(212, 192, 24, 24, 0, 90)
    $path.AddArc(20, 192, 24, 24, 90, 90)
    $path.CloseFigure()
    $path
}

# Degradê do logo na diagonal da pasta, do branco (`$Light`) ao preto.
function New-Gradient([int]$Light) {
    New-Object Drawing.Drawing2D.LinearGradientBrush (New-Object Drawing.PointF 20, 36), (New-Object Drawing.PointF 236, 216),
        ([Drawing.Color]::FromArgb(255, $Light, $Light, $Light)), ([Drawing.Color]::Black)
}

# A pasta num PNG de `$Size` px: fundo com o degradê mais apagado, frente com o degradê inteiro e um fio claro na
# borda de cima da frente, para separar as duas peças onde o degradê chega no preto.
function Get-FolderPng([int]$Size) {
    $bitmap = New-Object Drawing.Bitmap $Size, $Size
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.ScaleTransform($Size / 256, $Size / 256)

    $g.FillPath((New-Gradient 150), (New-BackPath))
    $front = New-RoundedRect 20 78 216 138 14
    $g.FillPath((New-Gradient 255), $front)
    $g.DrawPath((New-Object Drawing.Pen ([Drawing.Color]::FromArgb(70, 255, 255, 255)), (256 / $Size)), $front)

    $stream = New-Object IO.MemoryStream
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bitmap.Dispose()
    , $stream.ToArray()
}

# .ico com um PNG por tamanho: cabeçalho, uma entrada de 16 bytes por imagem e os PNGs em seguida.
function Save-Icon([string]$Path) {
    $images = foreach ($size in $Sizes) { , (Get-FolderPng $size) }
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

Save-Icon $Icon
