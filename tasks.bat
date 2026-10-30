@echo off
rem Tarefas do Kordia: tasks build [--app] ^| test [filtro] ^| avaliar [versao] ^| prova [versao] ^| gerar ^| atalho
setlocal
cd /d "%~dp0"
set "LAB=cpp\build\Release\kordia_lab.exe"
rem o CMake que vem com o Visual Studio instalado conhece essa versao; sem ele, usa o do PATH
set "CMAKE=cmake"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%p in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe 2^>nul`) do set "CMAKE=%%p"

if "%~1"=="build" goto build
if "%~1"=="" goto test
if "%~1"=="test" goto test
if "%~1"=="avaliar" goto avaliar
if "%~1"=="prova" goto prova
if "%~1"=="gerar" goto gerar
if "%~1"=="atalho" goto atalho
echo tarefa desconhecida: %~1
exit /b 2

:build
rem nucleo + laboratorio + testes (+ app JUCE); precisa do Visual Studio 2022 ou 2026 (Build Tools bastam)
set "APP=OFF"
if "%~2"=="--app" set "APP=ON"
rem sem -G: o CMake escolhe o Visual Studio mais novo instalado
"%CMAKE%" -S cpp -B cpp/build -A x64 -DKORDIA_APP=%APP% || exit /b 1
"%CMAKE%" --build cpp/build --config Release --parallel
exit /b %errorlevel%

:test
cpp\build\Release\kordia_tests.exe %2
exit /b %errorlevel%

:avaliar
set "VERSAO=%~2"
if "%VERSAO%"=="" set "VERSAO=dev"
%LAB% avaliar --todos --versao %VERSAO%
exit /b %errorlevel%

:prova
set "VERSAO=%~2"
if "%VERSAO%"=="" set "VERSAO=dev"
%LAB% prova --versao %VERSAO%
exit /b %errorlevel%

:gerar
%LAB% gerar --todos --saida dados
exit /b %errorlevel%

:atalho
rem um .bat nao tem icone, um atalho tem; guarda caminhos absolutos, entao e gerado, nao versionado
powershell -NoProfile -Command "$s = (New-Object -ComObject WScript.Shell).CreateShortcut('%CD%\Kordia.lnk'); $s.TargetPath = '%CD%\assets\abrir.bat'; $s.IconLocation = '%CD%\assets\kordia.ico'; $s.WorkingDirectory = '%CD%'; $s.WindowStyle = 7; $s.Description = 'Kordia'; $s.Save()"
exit /b %errorlevel%
