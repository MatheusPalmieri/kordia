@echo off
rem Abre o Kordia ao vivo (chamado pelo atalho Kordia.lnk).
cd /d "%~dp0.."
if not exist "cpp\build\app\KordiaApp_artefacts\Release\Kordia.exe" (
  echo Kordia.exe nao encontrado. Compile com: tasks build --app
  pause
  exit /b 1
)
start "" "cpp\build\app\KordiaApp_artefacts\Release\Kordia.exe"
