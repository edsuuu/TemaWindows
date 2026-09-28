# Login do Spotify para a "próxima música" do fundo (uma vez só). Abre o navegador na autorização do app do Spotify
# (o mesmo do PlaylistOrganizer: Client ID em HKCU\Software\TemaBarra\SpotifyClientId, fluxo PKCE, sem segredo, só o
# escopo de leitura do player), recebe o código no redirect http://127.0.0.1:43821/callback (precisa estar cadastrado
# no app, no painel do Spotify; a 8000 do app é do servidor de desenvolvimento) e troca pelo token. O refresh token
# fica em cache\spotify-token.bin, criptografado para o usuário do Windows (DPAPI); o FundoVivo renova sozinho depois.
# Uso: .\spotify-login.ps1
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Add-Type -AssemblyName System.Security

$Redirect = 'http://127.0.0.1:43821/callback'
$TokenFile = Join-Path $Root 'cache\spotify-token.bin'

# Bytes em base64 de URL (sem "=", "+" e "/"), como o PKCE pede.
function ConvertTo-Base64Url([byte[]]$Bytes) {
    [Convert]::ToBase64String($Bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

# Espera o navegador voltar no redirect (a porta 43821 fica aberta só enquanto isso), responde uma página simples e
# devolve o código da autorização, conferindo o state.
function Receive-Code([Net.Sockets.TcpListener]$Listener, [string]$State) {
    try {
        while ($true) {
            $client = $Listener.AcceptTcpClient()
            $stream = $client.GetStream()
            $line = (New-Object IO.StreamReader $stream).ReadLine()
            $callback = $line -match '^GET /callback\?(\S*)'
            $query = if ($callback) { [Web.HttpUtility]::ParseQueryString($Matches[1]) }

            $html = '<html><body style="background:#000;color:#ddd;font-family:Segoe UI;padding:40px">Pronto, pode fechar esta aba.</body></html>'
            $bytes = [Text.Encoding]::UTF8.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: text/html; charset=utf-8`r`nConnection: close`r`n`r`n$html")
            $stream.Write($bytes, 0, $bytes.Length)
            $client.Close()
            if (-not $callback) { continue }

            if ($query['error']) { throw "Autorização recusada: $($query['error'])" }
            if ($query['state'] -ne $State) { throw 'Resposta com state diferente; tente de novo.' }
            return $query['code']
        }
    } finally {
        $Listener.Stop()
    }
}

Assert-NoGame
$clientId = (Get-ItemProperty HKCU:\Software\TemaBarra -ErrorAction SilentlyContinue).SpotifyClientId
if (-not $clientId) { throw 'Falta o Client ID em HKCU\Software\TemaBarra\SpotifyClientId.' }

$verifier = ConvertTo-Base64Url ([Security.Cryptography.RandomNumberGenerator]::GetBytes(48))
$challenge = ConvertTo-Base64Url ([Security.Cryptography.SHA256]::HashData([Text.Encoding]::ASCII.GetBytes($verifier)))
$state = ConvertTo-Base64Url ([Security.Cryptography.RandomNumberGenerator]::GetBytes(16))
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 43821)
$listener.Start()
Start-Process ("https://accounts.spotify.com/authorize?client_id=$clientId&response_type=code" +
    "&redirect_uri=$([Uri]::EscapeDataString($Redirect))&code_challenge_method=S256&code_challenge=$challenge" +
    "&state=$state&scope=user-read-playback-state")

$code = Receive-Code $listener $state
$token = Invoke-RestMethod -Method Post -Uri 'https://accounts.spotify.com/api/token' -Body @{
    grant_type = 'authorization_code'; code = $code; redirect_uri = $Redirect; client_id = $clientId; code_verifier = $verifier
}
$protected = [Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($token.refresh_token), $null, 'CurrentUser')
[IO.File]::WriteAllBytes($TokenFile, $protected)
'Spotify conectado: em alguns segundos o fundo mostra a próxima música da fila.'
