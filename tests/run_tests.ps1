$ErrorActionPreference = 'Stop'
$gcc = 'C:\msys64\mingw64\bin\gcc.exe'

& $gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude `
  src\sha256.c src\blockchain.c src\accounts_store.c src\session_store.c src\metrics.c src\detection.c src\mitigation.c src\evaluation.c src\analysis.c src\attack.c src\attack_51.c src\attack_selfish.c src\attack_withholding.c src\attack_sybil.c src\menu.c src\main.c -o tests\atelier2_test.exe -lm
if ($LASTEXITCODE -ne 0) { throw 'Compilation de atelier2_test.exe echouee.' }

& $gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude `
  src\sha256.c src\blockchain.c src\accounts_store.c src\session_store.c src\metrics.c src\detection.c src\mitigation.c src\evaluation.c src\analysis.c src\attack.c src\attack_51.c src\attack_selfish.c src\attack_withholding.c src\attack_sybil.c tests\test_engine.c -o tests\test_engine.exe -lm
if ($LASTEXITCODE -ne 0) { throw 'Compilation de test_engine.exe echouee.' }

& .\tests\test_engine.exe
if ($LASTEXITCODE -ne 0) { throw 'Les tests unitaires ont echoue.' }

$invalidCommands = @(
  @('--consensus', 'invalid'),
  @('--node', 'Alice:1:2'),
  @('--consensus', 'pow', '--node', 'Alice:0:1:10'),
  @('--node', 'Alice:1:11:10'),
  @('--node', 'Alice:1:1:10', '--tx', 'Alice:Bob:1')
)
foreach ($argumentsList in $invalidCommands) {
  & .\tests\atelier2_test.exe @argumentsList *> $null
  if ($LASTEXITCODE -eq 0) { throw "La commande invalide a ete acceptee : $($argumentsList -join ' ')" }
}

Write-Host 'Tests CLI invalides : OK'

$env:ATELIER2_ACCOUNTS_FILE = Join-Path $env:TEMP 'atelier2_menu_accounts.csv'
Remove-Item -LiteralPath $env:ATELIER2_ACCOUNTS_FILE -Force -ErrorAction SilentlyContinue
Get-Content tests\menu_scenario.txt | & .\tests\atelier2_test.exe | Set-Content "$env:TEMP\atelier2_menu_output.txt"
if ($LASTEXITCODE -ne 0) { throw 'Le scenario du menu a echoue.' }
if (-not (Select-String -Path "$env:TEMP\atelier2_menu_output.txt" -SimpleMatch 'Resultat : BLOCKCHAIN VALIDE')) {
  throw 'Le scenario du menu n a pas produit une blockchain valide.'
}
Write-Host 'Scenario menu : OK'
