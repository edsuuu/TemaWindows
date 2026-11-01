# Gera assets\folder.ico: pasta com o degradê do logo do Iniciar (branco no topo à esquerda até o preto). Quem aplica
# nas pastas da área de trabalho é o FundoVivo (src\wallpaper\folder_icons.cpp).
# Uso: .\folder-icon.ps1
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$Icon = Join-Path $Root 'assets\folder.ico'
$Sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

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

# A pasta: fundo com o degradê mais apagado, frente com o degradê inteiro e um fio claro na borda de cima da
# frente, para separar as duas peças onde o degradê chega no preto.
Save-Icon $Icon $Sizes {
    param($g, $size)
    $g.FillPath((New-Gradient 150), (New-BackPath))
    $front = New-RoundedRect 20 78 216 138 14
    $g.FillPath((New-Gradient 255), $front)
    $g.DrawPath((New-Object Drawing.Pen ([Drawing.Color]::FromArgb(70, 255, 255, 255)), (256 / $size)), $front)
}
