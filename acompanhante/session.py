"""Live session recorder: everything needed to understand and replay a live run.

Folder sessoes/<date-time>/:
  config.json     run config + devices + latency (replay: python -m acompanhante.cli rodar --sessao <folder>)
  entrada.wav     input exactly as the processor saw it (after input gain)
  quadros.csv     one row per block (~23 ms): level, heard chord, confidence, state, BPM, cost, audio failures
  decisoes.jsonl  every accompaniment change and every engine reason (lock, unlock, stop...) with its cause
  resumo.json     totals written at stop; relatorio.png is added by the offline replay right after

The audio callback only puts items on a queue; files are written by a background thread.
"""
import json
import queue
import threading
from datetime import datetime
from pathlib import Path

import numpy as np
import soundfile as sf

from . import params as P

FIELDS = ["t", "db", "onset", "bpm", "tempo_conf", "ouvido", "conf", "estado", "nivel", "tocando", "proc_ms", "falha"]
ROOT = Path("sessoes")


class SessionRecorder:
    def __init__(self, config, root=ROOT):
        self.dir = Path(root) / datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.dir.mkdir(parents=True, exist_ok=True)
        (self.dir / "config.json").write_text(json.dumps(config, indent=1, ensure_ascii=False), encoding="utf-8")
        self.q = queue.SimpleQueue()
        self.thread = threading.Thread(target=self._writer, daemon=True)
        self.thread.start()

    def feed(self, block, row, decisions):
        """Audio thread: queue only (no I/O here)."""
        self.q.put((np.asarray(block, np.float32).copy(), row, decisions))

    def _writer(self):
        with sf.SoundFile(self.dir / "entrada.wav", "w", P.SR, 1, "PCM_16") as wav, \
                open(self.dir / "quadros.csv", "w", encoding="utf-8") as csv, \
                open(self.dir / "decisoes.jsonl", "w", encoding="utf-8") as dec:
            csv.write(",".join(FIELDS) + "\n")
            while (item := self.q.get()) is not None:
                block, row, decisions = item
                wav.write(np.clip(block, -1, 1))
                csv.write(",".join("" if row[k] is None else str(row[k]) for k in FIELDS) + "\n")
                for d in decisions:
                    dec.write(json.dumps(d, ensure_ascii=False) + "\n")

    def stop(self, summary):
        self.q.put(None)
        self.thread.join(timeout=10)
        (self.dir / "resumo.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False), encoding="utf-8")
        return self.dir


def load(folder):
    """(input audio, RunConfig dict, lookahead, analysis rate) of a recorded session (Python or C++ app)."""
    folder = Path(folder)
    cfg = json.loads((folder / "config.json").read_text(encoding="utf-8"))
    audio, sr = sf.read(folder / "entrada.wav")
    return audio, cfg["run"], cfg.get("lookahead", 0.0), sr
