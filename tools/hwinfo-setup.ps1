# Prepara o HWiNFO para o FundoVivo ler a temperatura da CPU (como administrador; o instalador chama quando falta o
# HWiNFO ou a tarefa): instala pelo winget se faltar, liga a memória compartilhada (modo só sensores, minimizado) e
# agenda a tarefa "HWiNFO (TemaWindows)", que abre ele no logon e reabre a cada 11 h (na versão grátis a memória
# compartilhada desliga sozinha em 12 h).
$ErrorActionPreference = 'Stop'
$exe = "$env:ProgramFiles\HWiNFO64\HWiNFO64.EXE"
$task = 'HWiNFO (TemaWindows)'

if (-not (Test-Path $exe)) {
    winget install --id REALiX.HWiNFO --exact --silent --scope machine --accept-package-agreements --accept-source-agreements
    if (-not (Test-Path $exe)) { throw 'O HWiNFO não instalou.' }
}

$ini = Join-Path (Split-Path $exe) 'HWiNFO64.INI'
if (-not (Test-Path $ini)) {
    $settings = 'SensorsOnly=1', 'ShowWelcomeAndProgress=0', 'MinimalizeMainWnd=1', 'MinimalizeSensors=1', 'MinimalizeSensorsClose=1',
                'SensorsSM=1', 'AutoUpdate=0'
    Set-Content $ini (@('[Settings]') + $settings) -Encoding Ascii
} elseif (-not (Select-String $ini -Pattern '^SensorsSM=1' -Quiet)) {
    $text = (Get-Content $ini | Where-Object { $_ -notmatch '^SensorsSM=' }) -join "`r`n"
    $text = if ($text -match '(?m)^\[Settings\]') { $text -replace '(?m)^\[Settings\]', "[Settings]`r`nSensorsSM=1" } else { "[Settings]`r`nSensorsSM=1`r`n$text" }
    Set-Content $ini $text -Encoding Ascii
}

$user = "$env:USERDOMAIN\$env:USERNAME"
$restart = "Stop-Process -Name HWiNFO64 -Force -ErrorAction SilentlyContinue; Start-Sleep 2; Start-Process '$exe'"
$action = New-ScheduledTaskAction -Execute 'conhost.exe' -Argument "--headless powershell.exe -NoProfile -WindowStyle Hidden -Command `"$restart`""
$triggers = (New-ScheduledTaskTrigger -AtLogOn -User $user), (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Hours 11))
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$options = New-ScheduledTaskSettingsSet -ExecutionTimeLimit 0 -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
Register-ScheduledTask $task -Action $action -Trigger $triggers -Principal $principal -Settings $options -Force | Out-Null
Start-ScheduledTask $task
