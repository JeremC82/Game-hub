# Compile Game hub avec Bluepad32 (branche bluepad32) : jusqu'a 4 manettes Bluetooth (PS4, PS5, Xbox...).
# Carte "ESP32 + Bluepad32" (esp32-bluepad32:esp32, installee avec build\bp32\arduino-cli.yaml), partition Huge APP.
#   GameHub_V<version>_bp32.bin         -> a flasher a 0x10000 (sur une carte deja en Huge APP)
#   GameHub_V<version>_bp32_complet.bin -> 1er flash (demarrage + table des partitions + programme), a 0x0
param([string]$Fqbn = "esp32-bluepad32:esp32:esp32:PartitionScheme=huge_app,DebugLevel=error")
$proj = $PSScriptRoot
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$yaml = "$proj\build\bp32\arduino-cli.yaml"
$core = "$env:LOCALAPPDATA\Arduino15\packages\esp32-bluepad32\hardware\esp32\4.1.0"
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\4.6\esptool.exe"
$out = "$proj\build\games_bp32"
& $cli --config-file $yaml compile --fqbn $Fqbn --warnings default --output-dir $out "$proj\games" 2>&1 | Tee-Object "$proj\build\compile_bp32.log" | Select-String -Pattern "games\.ino:\d+:\d+: (warning|error)|error:|Le croquis|Les variables|Error during"
if ($LASTEXITCODE -ne 0) { "ECHEC compilation"; exit 1 }
$ver = (Select-String -Path "$proj\games\games.ino" -Pattern '#define GAMES_VERSION "([^"]+)"').Matches[0].Groups[1].Value
$bin = "$proj\GameHub_V${ver}_bp32.bin"
$full = "$proj\GameHub_V${ver}_bp32_complet.bin"
Copy-Item "$out\games.ino.bin" $bin -Force
& $esptool --chip esp32 merge_bin -o $full --flash_mode dio --flash_freq 80m --flash_size 4MB `
  0x1000 "$out\games.ino.bootloader.bin" 0x8000 "$out\games.ino.partitions.bin" `
  0xe000 "$core\tools\partitions\boot_app0.bin" 0x10000 $bin | Out-Null
if ($LASTEXITCODE -ne 0) { "ECHEC fusion du bin complet"; exit 1 }
"{0} : {1} octets (a 0x10000)" -f (Split-Path $bin -Leaf), (Get-Item $bin).Length
"{0} : {1} octets (1er flash, a 0x0)" -f (Split-Path $full -Leaf), (Get-Item $full).Length
