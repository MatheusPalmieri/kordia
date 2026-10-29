@echo off
rem Abre o acompanhante ao vivo (duplo clique).
cd /d "%~dp0"
if not exist .venv\Scripts\pythonw.exe (
  echo Ambiente nao instalado. Rode: python -m venv .venv ^&^& .venv\Scripts\pip install -e .
  pause
  exit /b 1
)
start "" .venv\Scripts\pythonw.exe -m acompanhante.gui
