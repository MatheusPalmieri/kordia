"""Live app: real-time audio + a local web UI shown in a chromeless Edge window.

Run: python -m acompanhante.app   (or double-click Acompanhante.bat)

The UI (acompanhante/ui/index.html) talks to this process over HTTP on 127.0.0.1:
  GET  /api/devices      input/output devices of the default host API
  POST /api/start        {input, output, tom, bpm, progressao, clique}
  POST /api/stop
  POST /api/live         {gain_db, volume, monitor, clique, click_volume, dinamica_fixa, fixar_bpm}
  POST /api/open         open the last session folder in Explorer
  GET  /api/events       server-sent events: live state ~20x per second
"""
import json
import logging
import os
import shutil
import subprocess
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

import numpy as np
import sounddevice as sd

from . import params as P
from . import theory
from .engine import ENCERRANDO, FALLBACK, OCIOSO, OUVINDO, TRAVADO
from .pipeline import Processor, RunConfig, run
from .session import ROOT, SessionRecorder

UI = Path(__file__).with_name("ui") / "index.html"
log = logging.getLogger("kordia")

STATE_TEXT = {
    OCIOSO: "Esperando você tocar",
    OUVINDO: "Ouvindo e seguindo",
    TRAVADO: "Padrão aprendido",
    FALLBACK: "Em dúvida — segurando a tônica",
    ENCERRANDO: "Você parou — terminando o compasso",
}


def list_devices():
    """Devices of the default host API.
    shortcut: default host API only (MME on Windows accepts 22050 Hz and resamples); WASAPI/ASIO low latency
    belongs to the C++/JUCE engine (docs/05-roadmap.md)."""
    api = sd.query_devices(sd.default.device[0] if sd.default.device[0] >= 0 else None)["hostapi"]
    ins, outs = [], []
    for i, d in enumerate(sd.query_devices()):
        if d["hostapi"] == api:
            if d["max_input_channels"] > 0:
                ins.append({"id": i, "name": d["name"]})
            if d["max_output_channels"] > 0:
                outs.append({"id": i, "name": d["name"]})
    return {"inputs": ins, "outputs": outs, "default_input": sd.default.device[0], "default_output": sd.default.device[1]}


def parse_config(d):
    """Validate the UI request (trust boundary) -> (RunConfig, input id, output id)."""
    devs = list_devices()
    dev_in, dev_out = int(d["input"]), int(d["output"])
    if dev_in not in [x["id"] for x in devs["inputs"]] or dev_out not in [x["id"] for x in devs["outputs"]]:
        raise ValueError("Dispositivo de áudio inválido.")
    key = d.get("tom") or None
    if key is not None and key not in theory.NOTE_NAMES:
        raise ValueError(f"Tom inválido: {key}")
    bpm = d.get("bpm")
    if bpm is not None:
        bpm = float(bpm)
        if not 30 <= bpm <= 250:
            raise ValueError("BPM deve estar entre 30 e 250.")
    prog = " ".join(str(d.get("progressao") or "").split()) or None
    if prog:
        for c in prog.split():
            try:
                theory.chord_index(c)
            except (KeyError, ValueError, IndexError):
                raise ValueError(f"Acorde não reconhecido: {c!r} (use C, Am, F#m, Bb...)") from None
    modo = "metronomo" if bpm else ("decorado" if prog else "livre")
    seed = d.get("bpm_inicial")
    seed = float(np.clip(float(seed), 30, 250)) if seed else None
    return (RunConfig(modo=modo, tom=key, bpm=bpm, progressao=prog, clique=bool(d.get("clique")),
                      manter_padrao=bool(d.get("manter_padrao", True)),
                      aprender_na_tonica=bool(d.get("aprender_na_tonica", True)), bpm_inicial=seed), dev_in, dev_out)


def _safe_key(eng):
    try:   # read from the HTTP thread while the audio thread appends to eng.heard
        return eng.current_key()
    except RuntimeError:
        return None


def note_name(midi):
    return f"{theory.NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def playing(cur, key):
    chord, mode = cur
    if mode is None:
        return None, None
    if mode == "tonic":
        return key, "tônica"
    return theory.LABELS[chord], ("sem terça" if mode == "root5" else None)


class Live:
    def __init__(self):
        self.proc = self.stream = self.rec = None
        self.gain, self.volume, self.monitor = 1.0, 1.0, False
        self.error = None
        self.xruns = 0
        self.cost = deque(maxlen=400)       # ms per block, recent
        self.latency = 0.0
        self.session = None
        self.report = None                  # None | "gerando" | "pronto" | "erro"
        self.diary = deque(maxlen=12)       # recent decisions for the UI
        self._seen = 0
        self._reason = ""
        self.lock = threading.Lock()

    # --- control (HTTP threads) ---
    def start(self, d):
        with self.lock:
            if self.stream is not None:
                return
            cfg, dev_in, dev_out = parse_config(d)
            out_ch = min(2, sd.query_devices(dev_out)["max_output_channels"])
            stream = sd.Stream(device=(dev_in, dev_out), samplerate=P.SR, blocksize=P.HOP,
                               channels=(1, out_ch), dtype="float32", latency="low", callback=self._callback)
            try:
                self.latency = sum(stream.latency)
                self.proc = Processor(cfg, keep_log=False, lookahead=self.latency)
            except Exception:
                stream.close()
                raise
            self.stream = stream
            self.error, self.xruns, self._seen, self._reason = None, 0, 0, ""
            self.cost.clear()
            self.diary.clear()
            self.set_live(d)
            info = {"run": cfg.__dict__, "lookahead": self.latency,
                    "entrada": sd.query_devices(dev_in)["name"], "saida": sd.query_devices(dev_out)["name"],
                    "ajustes_iniciais": {k: d.get(k) for k in ("gain_db", "volume", "clique", "dinamica_fixa")}}
            self.rec = SessionRecorder(info)
            self.session, self.report = str(self.rec.dir), None
            self.t_start = time.time()
            self.stream.start()
            log.info("start %s -> %s cfg=%s latency=%.0f ms", info["entrada"], info["saida"], cfg, self.latency * 1000)

    def stop(self):
        with self.lock:
            if self.stream is None:
                return
            self.stream.stop()
            self.stream.close()
            self.stream = None
            cost = np.array(self.cost) if self.cost else np.zeros(1)
            summary = {"duracao_s": round(time.time() - self.t_start, 1), "falhas_de_audio": self.xruns,
                       "proc_ms_media": round(float(cost.mean()), 2), "proc_ms_max_recente": round(float(cost.max()), 2),
                       "orcamento_ms": round(P.HOP / P.SR * 1000, 1), "latencia_ms": round(self.latency * 1000),
                       "decisoes": self.proc.n_decisions}
            folder = self.rec.stop(summary)
            self.rec = None
            log.info("stop %s %s", folder, summary)
            threading.Thread(target=self._report, args=(folder,), daemon=True).start()

    def _report(self, folder):
        """Offline replay of the session -> relatorio.png (same code, same decisions)."""
        from .report import plot
        from .session import load
        self.report = "gerando"
        try:
            audio, cfg, lookahead, _ = load(folder)
            res = run(audio, RunConfig.from_dict(cfg), lookahead=lookahead)
            plot(res, folder / "relatorio.png", None, f"Sessão {folder.name}")
            self.report = "pronto"
        except Exception:
            log.exception("report failed")
            self.report = "erro"

    def set_live(self, d):
        if "gain_db" in d:
            self.gain = 10 ** (float(d["gain_db"]) / 20)
        if "volume" in d:
            self.volume = float(np.clip(float(d["volume"]), 0, 2))
        if "monitor" in d:
            self.monitor = bool(d["monitor"])
        p = self.proc
        if p is not None:
            if "clique" in d:
                p.click = bool(d["clique"])
            if "click_volume" in d:
                p.click_vel = int(np.clip(float(d["click_volume"]), 0, 1) * 127)
            if "dinamica_fixa" in d:
                p.dyn.fixed = bool(d["dinamica_fixa"])
            if "fixar_bpm" in d:
                p.freeze_tempo(d["fixar_bpm"])
            if "manter_padrao" in d:
                p.engine.hold = bool(d["manter_padrao"])
            if "aprender_na_tonica" in d:
                p.engine.learn = bool(d["aprender_na_tonica"])
            if isinstance(d.get("camadas"), list):
                p.synth.layers = [float(np.clip(float(g), 0, 2)) for g in d["camadas"]][:len(P.LAYERS)]
            if "baixo" in d:
                p.synth.bass_gain = float(np.clip(float(d["baixo"]), 0, 2))
            if d.get("bpm"):
                p.set_bpm(float(np.clip(float(d["bpm"]), 30, 250)))

    # --- audio thread ---
    def _callback(self, indata, outdata, frames, t, status):
        t0 = time.perf_counter()
        fail = bool(status)
        if fail:
            self.xruns += 1
        try:
            p = self.proc
            x = indata[:, 0].astype(np.float64) * self.gain
            y = p.process(x) * self.volume
            if self.monitor:
                y = y + x[:, None]
            y = np.clip(y, -1, 1)
            outdata[:] = y if outdata.shape[1] == 2 else y.mean(axis=1, keepdims=True)
            cost = (time.perf_counter() - t0) * 1000
            self.cost.append(cost)
            eng = p.engine
            new = []
            if p.n_decisions > self._seen:
                for dt, st, label, mode, reason in p.decisions[-(p.n_decisions - self._seen):]:
                    new.append({"t": dt, "estado": st, "acorde": label, "modo": mode, "motivo": reason})
                self._seen = p.n_decisions
                self._reason = eng.reason
            elif eng.reason != self._reason:
                self._reason = eng.reason
                new.append({"t": round(p.s / P.SR, 3), "estado": eng.state, "motivo": eng.reason})
            self.diary.extend(new)
            label, extra = playing(eng.cur, eng.key)
            row = {"t": round(p.s / P.SR, 3), "db": round(p.db, 1), "onset": int(p.last_onset_t > p.s / P.SR - P.HOP / P.SR),
                   "bpm": None if not p.tracker.bpm else round(p.tracker.bpm, 1), "tempo_conf": round(p.tracker.confidence, 2),
                   "ouvido": theory.LABELS[p.idx], "conf": round(p.conf, 2), "estado": eng.state, "nivel": eng.level,
                   "tocando": f"{label or '-'}{'(' + extra + ')' if extra else ''}", "proc_ms": round(cost, 2),
                   "falha": int(fail)}
            if self.rec is not None:
                self.rec.feed(x, row, new)
        except Exception as e:
            self.error = repr(e)
            outdata.fill(0)
            log.exception("audio callback")
            raise sd.CallbackAbort from e

    # --- UI state ---
    def snapshot(self):
        p = self.proc
        s = {"running": self.stream is not None, "error": self.error, "session": self.session, "report": self.report,
             "latency_ms": round(self.latency * 1000), "xruns": self.xruns,
             "cost_ms": round(float(np.mean(self.cost)), 2) if self.cost else None,
             "cost_max_ms": round(float(np.max(self.cost)), 1) if self.cost else None,
             "budget_ms": round(P.HOP / P.SR * 1000, 1), "diary": list(self.diary)[::-1]}
        if p is None:
            return s
        eng = p.engine
        label, extra = playing(eng.cur, eng.key)
        known = p.memory.known if p.memory.locked else None
        s.update({
            "state": eng.state, "state_text": STATE_TEXT[eng.state], "level": eng.level,
            "playing": label, "playing_extra": extra,
            "heard": None if p.idx == 24 else theory.LABELS[p.idx], "conf": round(p.conf, 2),
            "db": round(p.db, 1), "bpm": round(p.tracker.bpm, 1) if p.tracker.bpm else None,
            "beat": p.beat_in_bar() if eng.state in (OUVINDO, TRAVADO, FALLBACK) or p.click else None,
            "frozen": getattr(p.tracker, "frozen", False), "fixed_grid": p.engine.metronome,
            "pattern": [theory.LABELS[c] for c in known] if known else None,
            "pattern_pos": p.memory.pos if known else None, "next": p.next_chord(), "clique": p.click,
            "hold": p.engine.hold, "learning": p.engine.learn and eng.state != TRAVADO,
            "key_detected": theory.NOTE_NAMES[k] if (k := _safe_key(eng)) is not None else None,
            "out": {"bass_rms": p.synth.levels[0], "pad_rms": p.synth.levels[1],
                    "bass": note_name(p.acc.bass) if p.acc.bass is not None else None,
                    "pad": [note_name(n) for n in p.acc.pad]},
        })
        return s


class Handler(BaseHTTPRequestHandler):
    live: Live = None
    clients = [0, time.time()]   # open event streams, last time one was open

    def log_message(self, *a):
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            body = UI.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/devices":
            self._json(list_devices())
        elif path == "/api/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            Handler.clients[0] += 1
            try:
                while True:
                    self.wfile.write(f"data: {json.dumps(self.live.snapshot(), ensure_ascii=False)}\n\n".encode())
                    self.wfile.flush()
                    time.sleep(0.05)
            except (BrokenPipeError, ConnectionError, OSError):
                pass
            finally:
                Handler.clients[0] -= 1
                Handler.clients[1] = time.time()
        else:
            self._json({"error": "not found"}, 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            d = json.loads(self.rfile.read(n) or b"{}")
            path = urlsplit(self.path).path
            if path == "/api/start":
                self.live.start(d)
            elif path == "/api/stop":
                self.live.stop()
            elif path == "/api/live":
                self.live.set_live(d)
            elif path == "/api/open" and self.live.session:
                os.startfile(Path(self.live.session).resolve())
            else:
                return self._json({"error": "not found"}, 404)
            self._json({"ok": True})
        except (ValueError, KeyError, TypeError) as e:
            self._json({"ok": False, "error": str(e)}, 400)
        except sd.PortAudioError as e:
            self._json({"ok": False, "error": f"Não consegui abrir o áudio: {e}"}, 500)
        except Exception as e:
            log.exception("POST %s", self.path)
            self._json({"ok": False, "error": repr(e)}, 500)


def _edge():
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"), os.environ.get("LOCALAPPDATA")):
        if base and (p := Path(base) / "Microsoft" / "Edge" / "Application" / "msedge.exe").exists():
            return str(p)
    return shutil.which("msedge")


def main(open_window=True, port=0):
    ROOT.mkdir(exist_ok=True)
    logging.basicConfig(filename=ROOT / "app.log", level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    live = Live()
    Handler.live = live
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    Handler.clients[1] = time.time() + 20   # grace period for the window to open
    log.info("ui at %s", url)
    if open_window:
        edge = _edge()
        if edge:
            profile = Path(os.environ.get("LOCALAPPDATA", ".")) / "kordia" / "edge"
            subprocess.Popen([edge, f"--app={url}", "--window-size=1240,860", f"--user-data-dir={profile}",
                              "--no-first-run", "--disable-features=Translate"])
        else:
            webbrowser.open(url)

        def watchdog():   # the window was closed: stop audio and exit
            while True:
                time.sleep(2)
                if Handler.clients[0] == 0 and time.time() - Handler.clients[1] > 6:
                    live.stop()
                    server.shutdown()
                    return
        threading.Thread(target=watchdog, daemon=True).start()
    print(f"Kordia rodando em {url} (feche a janela para sair)")
    try:
        server.serve_forever()
    finally:
        live.stop()


if __name__ == "__main__":
    main()
