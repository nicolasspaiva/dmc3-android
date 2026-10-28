# Instala o Devil May Cry 3 num aparelho Android conectado (celular por USB/Wi-Fi ou emulador) e copia os dados para a
# pasta do jogo (Memória interna/DMC3, ver runtime/android_setup.cpp).
# Uso:  powershell -ExecutionPolicy Bypass -File android\install_device.ps1 [-Serial <id do adb>] [-Apk <arquivo>]
#                                                                           [-SemVideos] [-SemSave] [-SemDados]
#   -Serial     escolhe o aparelho quando há mais de um (veja "adb devices")
#   -Apk        APK a instalar (padrão: a versão completa de teste)
#   -SemVideos  não copia a pasta de vídeos (1,1 GB; para a versão leve)
#   -SemSave    não copia o save do PC (saves\SaveGame)
#   -SemDados   só instala o APK
param([string]$Serial = '', [string]$Apk = '', [switch]$SemVideos, [switch]$SemSave, [switch]$SemDados)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not $Apk) { $Apk = "$root\android\app\build\outputs\apk\completo\debug\app-completo-debug.apk" }
$a = @(); if ($Serial) { $a = @('-s', $Serial) }
$dst = '/sdcard/DMC3'

if (-not (Test-Path $Apk)) { throw "APK não encontrado: $Apk (compile antes com gradlew assembleCompletoDebug)" }
"Aparelho: " + (& $adb @a shell getprop ro.product.model) + " / " + (& $adb @a shell getprop ro.product.cpu.abi)

"1/3 instalando o APK"
& $adb @a install -r $Apk
# o "acesso a todos os arquivos" que o app pediria na primeira execução
& $adb @a shell appops set --uid org.dmc3port MANAGE_EXTERNAL_STORAGE allow

if (-not $SemDados) {
    "2/3 copiando os dados do jogo para $dst (pode levar alguns minutos)"
    & $adb @a shell mkdir -p "$dst/Media" "$dst/saves"
    & $adb @a push "$root\x360\DMC3.xex" "$dst/DMC3.xex"
    foreach ($f in 'DMC3-0.zip', 'DMC3-1.zip', 'DMC3-2.zip') { & $adb @a push "$root\x360\Media\$f" "$dst/Media/$f" }
    if (-not $SemVideos) { & $adb @a push "$root\x360\Media\wmv" "$dst/Media/" }
    if (-not $SemSave -and (Test-Path "$root\saves\SaveGame")) {
        "3/3 copiando o save do PC"
        & $adb @a push "$root\saves\SaveGame" "$dst/saves/"
    } else { "3/3 sem save do PC" }
}

"Pronto. Abrindo o jogo."
& $adb @a shell am start -n org.dmc3port/.DMC3Activity | Out-Null
"Log do jogo no aparelho: $dst/dmc3_log.txt  (adb pull para ler)"
