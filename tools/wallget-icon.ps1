# Gera assets\wallget.ico (Wallget.exe, WallgetConfig.exe e o ícone da bandeja): um anel dos widgets em 3/4, branco,
# sobre um quadrado escuro arredondado.
# Uso: .\wallget-icon.ps1
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Save-Icon (Join-Path $Root 'assets\wallget.ico') @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256) {
    param($g, $size)
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 22, 22, 24))), (New-RoundedRect 8 8 240 240 56))
    $width = [Math]::Max(30, 2.4 * 256 / $size)  # pelo menos ~2 px nos tamanhos pequenos
    $g.DrawEllipse((New-Object Drawing.Pen ([Drawing.Color]::FromArgb(70, 255, 255, 255)), $width), 52, 52, 152, 152)
    $ring = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(255, 242, 242, 242)), $width
    $ring.StartCap = 'Round'
    $ring.EndCap = 'Round'
    $g.DrawArc($ring, 52, 52, 152, 152, -90, 270)
}