@echo off
setlocal
cd /d "%~dp0\.."
set "GCC=C:\msys64\mingw64\bin\gcc.exe"
rmdir /s /q "tests\_generated" >nul 2>&1

"%GCC%" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude src\sha256.c src\blockchain.c src\accounts_store.c src\session_store.c src\metrics.c src\detection.c src\mitigation.c src\evaluation.c src\analysis.c src\attack.c src\attack_51.c src\attack_selfish.c src\attack_withholding.c src\attack_sybil.c src\menu.c src\main.c -o tests\atelier2_test.exe -lm
if errorlevel 1 goto :fail

"%GCC%" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude src\sha256.c src\blockchain.c src\accounts_store.c src\session_store.c src\metrics.c src\detection.c src\mitigation.c src\evaluation.c src\analysis.c src\attack.c src\attack_51.c src\attack_selfish.c src\attack_withholding.c src\attack_sybil.c tests\test_engine.c -o tests\test_engine.exe -lm
if errorlevel 1 goto :fail

tests\test_engine.exe
if errorlevel 1 goto :fail

tests\atelier2_test.exe --consensus invalid >nul 2>&1
if not errorlevel 1 goto :invalid_accepted
tests\atelier2_test.exe --node Alice:1:2 >nul 2>&1
if not errorlevel 1 goto :invalid_accepted
tests\atelier2_test.exe --consensus pow --node Alice:0:1:10 >nul 2>&1
if not errorlevel 1 goto :invalid_accepted
tests\atelier2_test.exe --node Alice:1:11:10 >nul 2>&1
if not errorlevel 1 goto :invalid_accepted
tests\atelier2_test.exe --node Alice:1:1:10 --tx Alice:Bob:1 >nul 2>&1
if not errorlevel 1 goto :invalid_accepted
tests\atelier2_test.exe --consensus pow --difficulty 0 --seed 7 --node Alice:70:70:100 --node Bob:30:30:100 --attack 51 --attacker Alice --spender Alice --merchant Bob --private-recipient Alice --amount 10 --confirmations 1 --rounds 30 >nul
if errorlevel 1 goto :fail
tests\atelier2_test.exe --consensus pow --difficulty 0 --seed 9 --node Alice:70:70:100 --node Bob:30:30:100 --attack selfish --attacker Alice --rounds 12 --gamma 100 >nul
if errorlevel 1 goto :fail
tests\atelier2_test.exe --consensus pow --difficulty 0 --seed 4 --node Alice:70:70:100 --node Bob:30:30:100 --attack withholding --attacker Alice --pool-member Alice --pool-member Bob --rounds 10 >nul
if errorlevel 1 goto :fail
tests\atelier2_test.exe --consensus pow --difficulty 0 --seed 2 --node Alice:70:70:100 --node Bob:30:30:100 --attack sybil --sybil-victim Bob --sybils 3 --victim-degree 3 --rounds 3 >nul
if errorlevel 1 goto :fail

set "ATELIER2_ACCOUNTS_FILE=%TEMP%\atelier2_menu_accounts.csv"
del /q "%ATELIER2_ACCOUNTS_FILE%" "%ATELIER2_ACCOUNTS_FILE%.tmp" >nul 2>&1
del /q "%TEMP%\session\session.active" >nul 2>&1
tests\atelier2_test.exe < tests\menu_scenario.txt > "%TEMP%\atelier2_menu_output.txt"
if errorlevel 1 goto :fail
findstr /C:"Resultat : BLOCKCHAIN VALIDE" "%TEMP%\atelier2_menu_output.txt" >nul
if errorlevel 1 goto :menu_failed
findstr /C:"Prevalidation : PRET A MINER : la selection et les soldes sont valides." "%TEMP%\atelier2_menu_output.txt" >nul
if errorlevel 1 goto :menu_failed
findstr /C:"BLOC 1 AJOUTE APRES MINAGE" "%TEMP%\atelier2_menu_output.txt" >nul
if errorlevel 1 goto :menu_failed
findstr /C:"Mises a jour de ressources appliquees" "%TEMP%\atelier2_menu_output.txt" >nul
if errorlevel 1 goto :menu_failed

rmdir /s /q "tests\_generated" >nul 2>&1
rmdir /s /q "tests\data" >nul 2>&1
del /q "tests\atelier2_test.exe" "tests\test_engine.exe" >nul 2>&1
echo Tests CLI invalides : OK
echo Scenario menu : OK
exit /b 0

:invalid_accepted
echo Une commande CLI invalide a ete acceptee.
exit /b 1

:menu_failed
echo Le scenario du menu n'a pas produit une blockchain valide.
exit /b 1

:fail
echo La compilation ou les tests du moteur ont echoue.
exit /b 1
