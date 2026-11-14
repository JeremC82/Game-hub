# Compile Game-hub (ESP32 classique) avec les memes bibliotheques que l'horloge
#   ./build.ps1              -> le jeu (games) et la manette (manette)
#   ./build.ps1 games        -> le jeu seulement
#   ./build.ps1 games -Ps4   -> le jeu en version manette PS4 Bluetooth, sans wifi (Games_PS4_V<version>.bin)
param([string[]]$Sketches = @("games", "manette"), [switch]$Ps4, [string]$Fqbn = "esp32:esp32:esp32")
$proj = $PSScriptRoot
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$yaml = "C:\Users\Jeremy\Documents\projet vscode\clockV1\build\arduino-cli.yaml"
$names = @{ games = @("Games", "GAMES_VERSION"); manette = @("Manette", "MANETTE_VERSION") }
foreach ($sk in $Sketches) {
  $out = "$proj\build\$sk"
  $extra = @()
  $prefix = $names[$sk][0]
  if ($Ps4 -and $sk -eq "games") {
    $out = "$proj\build\games_ps4"
    $extra = @("--build-property", "compiler.cpp.extra_flags=-DGAMES_PS4")
    $prefix = "Games_PS4"
  }
  & $cli --config-file $yaml compile --fqbn $Fqbn --warnings default @extra --output-dir $out "$proj\$sk" 2>&1 | Tee-Object "$proj\build\compile_$sk.log" | Select-String -Pattern "$sk\.ino:\d+:\d+: (warning|error)|error:|Le croquis|Error during|trop grand|too big"
  if ($LASTEXITCODE -ne 0) { "ECHEC compilation $sk"; exit 1 }
  $ver = (Select-String -Path "$proj\$sk\$sk.ino" -Pattern ('#define ' + $names[$sk][1] + ' "([^"]+)"')).Matches[0].Groups[1].Value
  $bin = "$proj\$($prefix)_V$ver.bin"
  Copy-Item "$out\$sk.ino.bin" $bin -Force
  "{0} : {1} octets" -f (Split-Path $bin -Leaf), (Get-Item $bin).Length
}
