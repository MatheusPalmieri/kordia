@echo off
rem Abre o Kordia ao vivo (chamado pelo atalho Kordia.lnk). Usa o app C++/JUCE se estiver compilado; senao, o app Python.
cd /d "%~dp0.."
if exist "cpp\build\app\KordiaApp_artefacts\Release\Kordia.exe" (
  start "" "cpp\build\app\KordiaApp_artefacts\Release\Kordia.exe"
  exit /b 0
)
if not exist ".venv\Scripts\pythonw.exe" (
  echo Ambiente nao instalado. Rode: python -m venv .venv ^&^& .venv\Scripts\pip install -e .
  pause
  exit /b 1
)
start "" ".venv\Scripts\pythonw.exe" -m acompanhante.app
