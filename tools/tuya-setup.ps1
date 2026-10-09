# Configura a luz do quarto no JSON do fundo (uma vez): busca na API da Tuya a chave local do interruptor (precisa
# de um Cloud Project no platform.tuya.com com a conta do Smart Life ligada) e salva id + chave + IP criptografados
# para o usuário do Windows (DPAPI) em cache\tuya-quarto.bin. O Access Secret vem da área de transferência (copie
# antes de rodar) e não fica salvo; depois disso o FundoVivo fala direto com o interruptor pela rede (protocolo 3.3).
# Uso: .\tuya-setup.ps1 -ClientId <Access ID> -DeviceId <ID do aparelho> -Ip <IP na rede> [-Region us]
param([Parameter(Mandatory)] [string]$ClientId, [Parameter(Mandatory)] [string]$DeviceId, [Parameter(Mandatory)] [string]$Ip,
      [string]$Region = 'us')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Add-Type -AssemblyName System.Security

$Secret = (Get-Clipboard -Raw).Trim()
$Base = "https://openapi.tuya$Region.com"

# SHA-256 em hexadecimal.
function Get-Sha256Hex([string]$Text) {
    -join ([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('x2') })
}

# Assinatura HMAC-SHA256 da Tuya (hexadecimal maiúsculo).
function Get-Sign([string]$Text) {
    $hmac = [Security.Cryptography.HMACSHA256]::new([Text.Encoding]::UTF8.GetBytes($Secret))
    -join ($hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('X2') })
}

# GET assinado na API da Tuya; devolve o "result" ou para com o erro dela.
function Invoke-Tuya([string]$Path, [string]$Token = '') {
    $t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()
    $toSign = "GET`n$(Get-Sha256Hex '')`n`n$Path"
    $headers = @{ client_id = $ClientId; t = $t; sign_method = 'HMAC-SHA256'; nonce = ''; sign = (Get-Sign "$ClientId$Token$t$toSign") }
    if ($Token) { $headers.access_token = $Token }
    $r = Invoke-RestMethod "$Base$Path" -Headers $headers
    if (-not $r.success) { throw "Tuya $Path -> $($r.code) $($r.msg)" }
    $r.result
}

$token = (Invoke-Tuya '/v1.0/token?grant_type=1').access_token
$device = Invoke-Tuya "/v1.0/devices/$DeviceId" $token
"aparelho: $($device.name) ($($device.product_name)), online: $($device.online)"

$data = [Text.Encoding]::UTF8.GetBytes((@{ id = $DeviceId; key = $device.local_key; ip = $Ip } | ConvertTo-Json -Compress))
[IO.File]::WriteAllBytes((Join-Path $Root 'cache\tuya-quarto.bin'), [Security.Cryptography.ProtectedData]::Protect($data, $null, 'CurrentUser'))
Set-Clipboard -Value ' '
'pronto: cache\tuya-quarto.bin salvo (criptografado). Reinicie o FundoVivo (tools\reload-wallpaper.ps1).'
