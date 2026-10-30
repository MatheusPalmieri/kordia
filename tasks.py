"""Executor de tarefas (substitui o make no Windows): python tasks.py build [--app]|interface|test|avaliar [versao]|gerar"""
import subprocess
import sys

cmd = sys.argv[1] if len(sys.argv) > 1 else "test"
py = sys.executable
CMAKE = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
if cmd == "build":   # C++ core + Python module (+ JUCE app); needs Visual Studio Build Tools
    import os, shutil
    cmake = shutil.which("cmake") or CMAKE
    app = "ON" if "--app" in sys.argv else "OFF"
    ok = subprocess.call([cmake, "-S", "cpp", "-B", "cpp/build", "-G", "Visual Studio 17 2022", "-A", "x64",
                          f"-DKORDIA_APP={app}", f"-DPython_EXECUTABLE={py}"]) == 0
    sys.exit(0 if ok and subprocess.call([cmake, "--build", "cpp/build", "--config", "Release", "--parallel"]) == 0 else 1)
if cmd == "interface":
    sys.exit(subprocess.call([py, "-m", "acompanhante.app"]))
if cmd == "test":
    sys.exit(subprocess.call([py, "-m", "pytest", "-q"]))
elif cmd == "avaliar":
    versao = sys.argv[2] if len(sys.argv) > 2 else "dev"
    sys.exit(subprocess.call([py, "-m", "acompanhante.cli", "avaliar", "--todos", "--versao", versao]))
elif cmd == "gerar":
    sys.exit(subprocess.call([py, "-m", "acompanhante.cli", "gerar", "--todos", "--saida", "dados"]))
sys.exit(f"tarefa desconhecida: {cmd}")
