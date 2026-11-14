# Compile Game-hub (ESP32 classique) avec les memes bibliotheques que l'horloge
#   ./build.ps1            -> le jeu (games) et la manette (manette)
#   ./build.ps1 games      -> le jeu seulement
param([string[]]$Sketches = @("games", "manette"), [string]$Fqbn = "esp32:esp32:esp32")
$proj = $PSScriptRoot
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$yaml = "C:\Users\Jeremy\Documents\projet vscode\clockV1\build\arduino-cli.yaml"
$names = @{ games = @("Games", "GAMES_VERSION"); manette = @("Manette", "MANETTE_VERSION") }
foreach ($sk in $Sketches) {
  $out = "$proj\build\$sk"
  & $cli --config-file $yaml compile --fqbn $Fqbn --warnings default --output-dir $out "$proj\$sk" 2>&1 | Tee-Object "$proj\build\compile_$sk.log" | Select-String -Pattern "$sk\.ino:\d+:\d+: (warning|error)|error:|Le croquis|Error during"
  if ($LASTEXITCODE -ne 0) { "ECHEC compilation $sk"; exit 1 }
  $n = $names[$sk]
  $ver = (Select-String -Path "$proj\$sk\$sk.ino" -Pattern ('#define ' + $n[1] + ' "([^"]+)"')).Matches[0].Groups[1].Value
  $bin = "$proj\$($n[0])_V$ver.bin"
  Copy-Item "$out\$sk.ino.bin" $bin -Force
  "{0} : {1} octets" -f (Split-Path $bin -Leaf), (Get-Item $bin).Length
}
