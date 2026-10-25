# Recarrega o TemaContexto (itens do "Mostrar mais opções" e Abrir WSL no menu novo do clique direito): compila se
# pedido, gera e registra de novo os pacotes (sem assinatura: precisa do Modo Desenvolvedor) e fecha os dllhost que
# estão com a DLL velha; inicia o vigia que deixa os dllhost abertos. Não reinicia o Explorer. -Remove tira tudo.
# Uso: .\reload-contextmenu.ps1 [-Build] [-Remove]
param([switch]$Build, [switch]$Remove)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

# Um pacote por posição do menu (o Windows junta num submenu os de mesmo nome): a classe do fundo de pasta
# e a de arquivos/pastas daquela posição. Por último, o do Abrir WSL. As classes batem com src\contextmenu\commands.cpp.
function Get-ContextPackages {
    $base = '6F3B2A90-1C4D-4E8A-B5F7-3D9E0C2A71'
    foreach ($i in 0..9) {
        [ordered]@{ Name = "Contexto$i"; Types = [ordered]@{ 'Directory\Background' = "${base}0$i"; '*' = "${base}1$i"; 'Directory' = "${base}1$i" } }
    }
    [ordered]@{ Name = 'ContextoWsl'; Types = [ordered]@{ 'Directory\Background' = "${base}20" } }
}

# Pasta do pacote em bin\contexto\<nome>\, com o manifesto tirado do modelo e o logo. Devolve o caminho do manifesto.
function Write-ContextPackage($Package) {
    $folder = Join-Path $Bin "contexto\$($Package.Name)"
    New-Item -ItemType Directory -Force $folder | Out-Null
    $verbs = $Package.Types.GetEnumerator() | ForEach-Object {
        "            <desktop5:ItemType Type=`"$($_.Key)`"><desktop5:Verb Id=`"Item`" Clsid=`"$($_.Value)`" /></desktop5:ItemType>"
    }
    $classes = $Package.Types.Values | Select-Object -Unique | ForEach-Object {
        "              <com:Class Id=`"$_`" Path=`"TemaContexto.dll`" ThreadingModel=`"STA`" />"
    }
    $manifest = (Get-Content "$Root\src\contextmenu\AppxManifest.xml" -Raw).Replace('{Name}', $Package.Name).
        Replace('{Verbs}', $verbs -join "`n").Replace('{Classes}', $classes -join "`n")
    Set-Content "$folder\AppxManifest.xml" $manifest -NoNewline
    Copy-Item "$Root\assets\contexto.png" $folder
    "$folder\AppxManifest.xml"
}

Assert-NoGame
Stop-Injector TemaContexto.dll
if ($Build) { Invoke-Build contextmenu }
# Com o dllhost aberto, tirar o pacote espera uns 30 s por ele.
Get-Process dllhost -ErrorAction SilentlyContinue | Where-Object { $_.Modules.ModuleName -contains 'TemaContexto.dll' } |
    Stop-Process -Force -ErrorAction SilentlyContinue
Get-AppxPackage TemaWindows.Contexto* | Remove-AppxPackage
if (-not $Remove) {
    Get-ContextPackages | ForEach-Object { Add-AppxPackage -Register (Write-ContextPackage $_) -ExternalLocation $Bin }
}

# O vigia mantém os dllhost abertos (sem ele o menu mostra "Carregando"); sobe com o Windows pelo Run.
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ($Remove) {
    Remove-ItemProperty $run TemaContexto -ErrorAction SilentlyContinue
} else {
    Set-ItemProperty $run TemaContexto "rundll32.exe `"$Bin\TemaContexto.dll`",Run"
    Start-Injector TemaContexto.dll
}
