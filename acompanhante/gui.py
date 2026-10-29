"""Live GUI: pick input/output devices, set key / BPM / progression, press Iniciar and play.

Run: python -m acompanhante.gui
"""
import tkinter as tk
from tkinter import messagebox, ttk

import numpy as np
import sounddevice as sd

from . import params as P
from . import theory
from .engine import ENCERRANDO, FALLBACK, OCIOSO, OUVINDO, TRAVADO
from .pipeline import Processor, RunConfig

KEYS = ["(nenhum)"] + theory.NOTE_NAMES
STATE_TEXT = {
    OCIOSO: "Esperando você tocar",
    OUVINDO: "Seguindo o que ouve",
    TRAVADO: "Padrão aprendido: prevendo as trocas",
    FALLBACK: "Em dúvida: segurando a tônica",
    ENCERRANDO: "Você parou: terminando o compasso",
}
STATE_COLOR = {OCIOSO: "#888888", OUVINDO: "#2f7fd1", TRAVADO: "#2e9d4f", FALLBACK: "#d18b2f", ENCERRANDO: "#8a5cc2"}


def list_devices():
    """(inputs, outputs) as [(index, name)] from the default host API.
    shortcut: only the default host API (MME on Windows, which accepts 22050 Hz and resamples);
    WASAPI/ASIO low-latency paths belong to the C++/JUCE version."""
    api = sd.query_devices(sd.default.device[0] if sd.default.device[0] >= 0 else None)["hostapi"]
    ins, outs = [], []
    for i, d in enumerate(sd.query_devices()):
        if d["hostapi"] != api:
            continue
        if d["max_input_channels"] > 0:
            ins.append((i, d["name"]))
        if d["max_output_channels"] > 0:
            outs.append((i, d["name"]))
    return ins, outs


def playing_text(cur, key):
    chord, mode = cur
    if mode is None:
        return "—"
    if mode == "tonic":
        return f"{key} (tônica)"
    name = theory.LABELS[chord]
    return f"{name} (sem terça)" if mode == "root5" else name


class App:
    def __init__(self, root):
        self.root = root
        self.proc = None
        self.stream = None
        self.error = None
        self.xruns = 0
        self.gain = 1.0
        self.volume = 0.8
        self.monitor = False
        root.title("Acompanhante — baixo + pad")
        root.minsize(560, 0)
        self.ins, self.outs = list_devices()
        self._build()
        self._tick()

    # --- layout ---
    def _build(self):
        pad = {"padx": 8, "pady": 4}
        audio = ttk.LabelFrame(self.root, text="Áudio")
        audio.pack(fill="x", padx=10, pady=(10, 4))
        audio.columnconfigure(1, weight=1)

        ttk.Label(audio, text="Entrada:").grid(row=0, column=0, sticky="w", **pad)
        self.in_var = tk.StringVar()
        self.in_box = ttk.Combobox(audio, textvariable=self.in_var, state="readonly", values=[n for _, n in self.ins])
        self.in_box.grid(row=0, column=1, sticky="ew", **pad)
        ttk.Label(audio, text="Saída:").grid(row=1, column=0, sticky="w", **pad)
        self.out_var = tk.StringVar()
        self.out_box = ttk.Combobox(audio, textvariable=self.out_var, state="readonly", values=[n for _, n in self.outs])
        self.out_box.grid(row=1, column=1, sticky="ew", **pad)
        for box, items, default in ((self.in_box, self.ins, sd.default.device[0]), (self.out_box, self.outs, sd.default.device[1])):
            idx = [i for i, _ in items]
            if items:
                box.current(idx.index(default) if default in idx else 0)

        ttk.Label(audio, text="Ganho da entrada:").grid(row=2, column=0, sticky="w", **pad)
        self.gain_db = tk.DoubleVar(value=0.0)
        ttk.Scale(audio, from_=-12, to=30, variable=self.gain_db, command=self._live_settings).grid(row=2, column=1, sticky="ew", **pad)
        ttk.Label(audio, text="Nível:").grid(row=3, column=0, sticky="w", **pad)
        self.meter = tk.Canvas(audio, height=14, bg="#eeeeee", highlightthickness=0)
        self.meter.grid(row=3, column=1, sticky="ew", **pad)
        ttk.Label(audio, text="Volume do acompanhamento:").grid(row=4, column=0, sticky="w", **pad)
        self.vol = tk.DoubleVar(value=80)
        ttk.Scale(audio, from_=0, to=100, variable=self.vol, command=self._live_settings).grid(row=4, column=1, sticky="ew", **pad)
        self.mon_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(audio, text="Ouvir também o violão na saída", variable=self.mon_var,
                        command=self._live_settings).grid(row=5, column=1, sticky="w", **pad)

        music = ttk.LabelFrame(self.root, text="Música (opcional — sem nada, ele só ouve e segue)")
        music.pack(fill="x", padx=10, pady=4)
        ttk.Label(music, text="Tom principal:").grid(row=0, column=0, sticky="w", **pad)
        self.key_var = tk.StringVar(value="C")
        self.key_box = ttk.Combobox(music, textvariable=self.key_var, state="readonly", values=KEYS, width=10)
        self.key_box.grid(row=0, column=1, sticky="w", **pad)
        self.bpm_on = tk.BooleanVar(value=False)
        self.bpm_chk = ttk.Checkbutton(music, text="BPM fixo (metrônomo):", variable=self.bpm_on, command=self._toggle)
        self.bpm_chk.grid(row=1, column=0, sticky="w", **pad)
        self.bpm_var = tk.StringVar(value="110")
        self.bpm_entry = ttk.Spinbox(music, from_=40, to=220, textvariable=self.bpm_var, width=8)
        self.bpm_entry.grid(row=1, column=1, sticky="w", **pad)
        self.prog_on = tk.BooleanVar(value=False)
        self.prog_chk = ttk.Checkbutton(music, text="Progressão conhecida:", variable=self.prog_on, command=self._toggle)
        self.prog_chk.grid(row=2, column=0, sticky="w", **pad)
        self.prog_var = tk.StringVar(value="C G Am F")
        self.prog_entry = ttk.Entry(music, textvariable=self.prog_var, width=30)
        self.prog_entry.grid(row=2, column=1, sticky="w", **pad)
        self._toggle()

        self.start_btn = ttk.Button(self.root, text="▶  Iniciar", command=self._start_stop)
        self.start_btn.pack(pady=8, ipadx=20, ipady=6)

        live = ttk.LabelFrame(self.root, text="Ao vivo")
        live.pack(fill="both", expand=True, padx=10, pady=(4, 10))
        live.columnconfigure(1, weight=1)
        ttk.Label(live, text="Tocando:").grid(row=0, column=0, sticky="nw", **pad)
        self.playing = tk.Label(live, text="—", font=("Segoe UI", 36, "bold"), anchor="w")
        self.playing.grid(row=0, column=1, sticky="w", **pad)
        ttk.Label(live, text="Ouvindo:").grid(row=1, column=0, sticky="w", **pad)
        self.heard = ttk.Label(live, text="—", font=("Segoe UI", 12))
        self.heard.grid(row=1, column=1, sticky="w", **pad)
        ttk.Label(live, text="Estado:").grid(row=2, column=0, sticky="w", **pad)
        self.state = tk.Label(live, text="Parado", font=("Segoe UI", 12, "bold"), fg="#888888", anchor="w")
        self.state.grid(row=2, column=1, sticky="w", **pad)
        ttk.Label(live, text="Andamento:").grid(row=3, column=0, sticky="w", **pad)
        row = ttk.Frame(live)
        row.grid(row=3, column=1, sticky="w", **pad)
        self.bpm_lbl = ttk.Label(row, text="— BPM", font=("Segoe UI", 12), width=9)
        self.bpm_lbl.pack(side="left")
        self.beats = tk.Canvas(row, width=120, height=24, highlightthickness=0)
        self.beats.pack(side="left", padx=8)
        self.dots = [self.beats.create_oval(6 + 28 * i, 4, 22 + 28 * i, 20, fill="#dddddd", outline="") for i in range(4)]
        ttk.Label(live, text="Padrão:").grid(row=4, column=0, sticky="w", **pad)
        self.pattern = ttk.Label(live, text="—")
        self.pattern.grid(row=4, column=1, sticky="w", **pad)
        self.status = ttk.Label(self.root, foreground="#a05a00", wraplength=540, justify="left",
                                text="Dica: com microfone e alto-falante juntos o sistema ouve a si mesmo. "
                                     "Prefira fone de ouvido ou o violão ligado direto (cabo/interface).")
        self.status.pack(fill="x", padx=12, pady=(0, 10))

    def _toggle(self):
        running = self.stream is not None
        for chk, on, entry in ((self.bpm_chk, self.bpm_on, self.bpm_entry), (self.prog_chk, self.prog_on, self.prog_entry)):
            chk.state(["disabled"] if running else ["!disabled"])
            entry.state(["!disabled"] if on.get() and not running else ["disabled"])
        for box in (self.in_box, self.out_box, self.key_box):
            box.state(["disabled"] if running else ["readonly"])

    def _live_settings(self, *_):
        self.gain = 10 ** (self.gain_db.get() / 20)
        self.volume = self.vol.get() / 100
        self.monitor = self.mon_var.get()

    # --- start / stop ---
    def _config(self):
        key = None if self.key_var.get() == KEYS[0] else self.key_var.get()
        bpm = prog = None
        if self.bpm_on.get():
            bpm = float(self.bpm_var.get().replace(",", "."))
            if not 30 <= bpm <= 250:
                raise ValueError("BPM deve estar entre 30 e 250.")
        if self.prog_on.get():
            prog = " ".join(self.prog_var.get().split())
            if not prog:
                raise ValueError("Escreva a progressão, ex.: C G Am F")
            for c in prog.split():
                try:
                    theory.chord_index(c)
                except (KeyError, ValueError, IndexError):
                    raise ValueError(f"Acorde não reconhecido: {c!r} (use C, Am, F#m, Bb...)") from None
        modo = "metronomo" if bpm else ("decorado" if prog else "livre")
        return RunConfig(modo=modo, tom=key, bpm=bpm, progressao=prog)

    def _start_stop(self):
        if self.stream is not None:
            self._stop()
            return
        if not self.ins or not self.outs:
            messagebox.showerror("Áudio", "Nenhum dispositivo de entrada/saída encontrado.")
            return
        try:
            cfg = self._config()
        except ValueError as e:
            messagebox.showerror("Configuração", str(e))
            return
        dev_in = self.ins[self.in_box.current()][0]
        dev_out = self.outs[self.out_box.current()][0]
        out_ch = min(2, sd.query_devices(dev_out)["max_output_channels"])
        self.error, self.xruns = None, 0
        self._live_settings()
        try:
            self.stream = sd.Stream(device=(dev_in, dev_out), samplerate=P.SR, blocksize=P.HOP, channels=(1, out_ch),
                                    dtype="float32", latency="low", callback=self._callback)
            latency = sum(self.stream.latency)
            self.proc = Processor(cfg, keep_log=False, lookahead=latency)
            self.status.config(text=f"Latência do áudio: {latency * 1000:.0f} ms (compensada nas trocas previstas "
                                    "e no pulso do baixo). Com microfone + alto-falante o sistema ouve a si mesmo: "
                                    "prefira fone ou o violão ligado direto.")
            self.stream.start()
        except Exception as e:  # PortAudio errors: device busy, unsupported rate...
            self.stream = None
            messagebox.showerror("Áudio", f"Não consegui abrir o áudio:\n{e}")
            return
        self.start_btn.config(text="■  Parar")
        self._toggle()

    def _stop(self):
        if self.stream is not None:
            self.stream.stop()
            self.stream.close()
        self.stream = None
        self.start_btn.config(text="▶  Iniciar")
        self._toggle()

    def _callback(self, indata, outdata, frames, time, status):
        if status:
            self.xruns += 1
        try:
            x = indata[:, 0].astype(np.float64) * self.gain
            y = self.proc.process(x) * self.volume
            if self.monitor:
                y = y + x
            outdata[:] = np.clip(y, -1, 1)[:, None]
        except Exception as e:
            self.error = e
            outdata.fill(0)
            raise sd.CallbackAbort from e

    # --- display ---
    def _tick(self):
        p = self.proc
        running = self.stream is not None and p is not None
        w = self.meter.winfo_width()
        self.meter.delete("all")
        if running:
            db = p.db
            frac = float(np.clip((db + 60) / 60, 0, 1))
            color = "#2e9d4f" if db < -6 else "#d13b2f"
            self.meter.create_rectangle(0, 0, w * frac, 14, fill=color, outline="")
            self.meter.create_line(w * (P.SILENCE_DB + 60) / 60, 0, w * (P.SILENCE_DB + 60) / 60, 14, fill="#999")
            eng = p.engine
            self.playing.config(text=playing_text(eng.cur, eng.key))
            heard = "—" if p.idx == 24 else f"{theory.LABELS[p.idx]}  ({p.conf:.0%} de confiança)"
            self.heard.config(text=heard)
            self.state.config(text=STATE_TEXT[eng.state], fg=STATE_COLOR[eng.state])
            bpm = p.tracker.bpm
            self.bpm_lbl.config(text=f"{bpm:.0f} BPM" if bpm else "— BPM")
            beat = p.beat_in_bar() if eng.state in (OUVINDO, TRAVADO, FALLBACK) else None
            for i, d in enumerate(self.dots):
                self.beats.itemconfig(d, fill=("#2f7fd1" if i == 0 else "#7fb2e5") if beat == i + 1 else "#dddddd")
            known = p.memory.known if p.memory.locked else None
            self.pattern.config(text=" → ".join(theory.LABELS[c] for c in known) if known else "ainda aprendendo")
        if self.error is not None:
            err, self.error = self.error, None
            self._stop()
            messagebox.showerror("Áudio", f"O processamento parou:\n{err}")
        elif running and self.xruns:
            self.status.config(text=f"Aviso: {self.xruns} falhas de áudio (o computador não deu conta a tempo).")
        self.root.after(50, self._tick)


def main():
    root = tk.Tk()
    app = App(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app._stop(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()
