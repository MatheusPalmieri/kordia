"""Live sessions, as the lab reads them. The app writes them (SessionWriter in cpp/app/LiveEngine.cpp).

Folder sessoes/<date-time>/:
  config.json     run config + devices + latency (replay: python -m acompanhante.cli rodar --sessao <folder>)
  entrada.wav     input exactly as the processor saw it (after input gain)
  quadros.csv     one row per block (~23 ms): level, heard chord, confidence, state, BPM, cost, audio failures
  decisoes.jsonl  every accompaniment change and every engine reason (lock, unlock, stop...) with its cause
  resumo.json     totals written at stop; relatorio.png is added by the offline replay right after
"""
import json
from pathlib import Path

import soundfile as sf


def load(folder):
    """(input audio, RunConfig dict, lookahead, analysis rate) of a recorded session (Python or C++ app)."""
    folder = Path(folder)
    cfg = json.loads((folder / "config.json").read_text(encoding="utf-8"))
    audio, sr = sf.read(folder / "entrada.wav")
    return audio, cfg["run"], cfg.get("lookahead", 0.0), sr
