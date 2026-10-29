"""Executor de tarefas (substitui o make no Windows): python tasks.py test|avaliar [versao]|gerar"""
import subprocess
import sys

cmd = sys.argv[1] if len(sys.argv) > 1 else "test"
py = sys.executable
if cmd == "test":
    sys.exit(subprocess.call([py, "-m", "pytest", "-q"]))
elif cmd == "avaliar":
    versao = sys.argv[2] if len(sys.argv) > 2 else "dev"
    sys.exit(subprocess.call([py, "-m", "acompanhante.cli", "avaliar", "--todos", "--versao", versao]))
elif cmd == "gerar":
    sys.exit(subprocess.call([py, "-m", "acompanhante.cli", "gerar", "--todos", "--saida", "dados"]))
sys.exit(f"tarefa desconhecida: {cmd}")
