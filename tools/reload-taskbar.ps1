# Recarrega o TemaBarra: recompila se pedido, reinicia o Explorer (solta a DLL), injeta a de bin\, conserta a caixa de
# pesquisa e, se pedido, tira um print real da barra. ATENÇÃO: reiniciar o Explorer fecha as janelas do Explorador de
# Arquivos e às vezes um app já aberto perde o botão da barra (bug do Windows); rode tools\effects-test.exe antes
# quando mexer em efeitos.
# Uso: .\reload-taskbar.ps1 [-Build] [-Shot <saida.png>]
param([switch]$Build, [string]$Shot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

# Mata o Explorer e garante que ele volte. Testado o "Sair do Explorer" limpo (WM_USER+436): não evitou o bug dos
# apps soltos e numa das vezes a Calculadora fechou junto, então ficou o jeito à força.
function Restart-Explorer {
    Stop-Process -Name explorer -Force
    Start-Sleep 3
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
}

# Pastas abertas no Explorador agora (o reinício fecha as janelas; elas são reabertas depois, sem a posição).
function Get-FolderPaths {
    (New-Object -ComObject Shell.Application).Windows() | Where-Object LocationURL -like 'file:///*' |
        ForEach-Object { [Uri]::new($_.LocationURL).LocalPath }
}

# Bug do Windows: depois de reiniciar o Explorer à força a caixa de pesquisa some; alternar o modo 1 -> 2 a traz de volta.
function Repair-SearchBox {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Search'
    if ((Get-ItemProperty $key).SearchboxTaskbarMode -ne 2) { return }
    Set-ItemProperty $key SearchboxTaskbarMode 1
    Start-Sleep 3
    Set-ItemProperty $key SearchboxTaskbarMode 2
    Start-Sleep 3
}

# Print real (DXGI) do monitor principal, recortado nos 110 px de baixo, onde fica a barra.
function Save-TaskbarShot([string]$Path) {
    & "$PSScriptRoot\capture.exe" "$Path.full.png" 0 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'captura falhou (tela bloqueada?)' }
    Add-Type -AssemblyName System.Drawing
    $full = [Drawing.Image]::FromFile("$Path.full.png")
    $crop = New-Object Drawing.Bitmap $full.Width, 110
    [Drawing.Graphics]::FromImage($crop).DrawImage($full, (New-Object Drawing.Rectangle 0, 0, $full.Width, 110),
        (New-Object Drawing.Rectangle 0, ($full.Height - 110), $full.Width, 110), [Drawing.GraphicsUnit]::Pixel)
    $full.Dispose()
    $crop.Save($Path)
}

Assert-NoGame
$lock = New-Object System.Threading.Mutex($false, 'Global\TemaBarraRecarregar')
try { $null = $lock.WaitOne([TimeSpan]::FromMinutes(10)) } catch [System.Threading.AbandonedMutexException] {}
try {
    if ($Build) { Invoke-Build taskbar }
    $folders = @(Get-FolderPaths)
    Stop-Injector TemaBarra.dll
    Restart-Explorer
    Start-Injector TemaBarra.dll
    Start-Sleep 14
    Repair-SearchBox
    $folders | ForEach-Object { Start-Process explorer.exe "`"$_`"" }
    Get-Content "$Root\logs\TemaBarra.log" -Tail 8
    'Obs.: se algum app que já estava aberto ficar sem a barrinha ou sem minimizar no clique, feche e abra o app de novo.'
    if ($Shot) { Save-TaskbarShot $Shot }
} finally {
    $lock.ReleaseMutex()
}
