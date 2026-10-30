"""The 12 synthetic test scenarios: WAV audio + ground-truth dict."""
from dataclasses import dataclass

import numpy as np
from scipy.signal import lfilter

from . import theory
from .guitar import Guitar
from .params import SR


@dataclass
class Bar:
    chord: str | None            # guitar shape ('C', 'Am/G'); None = silent bar (strings muted)
    pattern: str = "DUDUDUDU"    # 8 eighth-note slots: D down, U up, x muted chk, - ring, m mute, b single bass note
    bpm: float = 110.0           # tempo at bar start (interpolated to the next bar)
    capo: int = 0
    gain: float = 1.0
    shift: float = 0.0           # beats; the guitarist changes early (<0) or late (>0) vs the grid
    shape2: str | None = None    # different shape for the second half (same harmony, e.g. 'Am/G')
    bass_notes: tuple = ()       # MIDI notes consumed by 'b' slots
    surprise: bool = False
    wrong: str | None = None     # musician's mistake: this shape on the first half (truth stays `chord`)
    late_entry: bool = False     # musician's mistake: an 'm' here is a late entry, not a pause (truth stays `chord`)

    @property
    def label(self):
        return theory.N if self.chord is None else theory.transpose(theory.canonical(self.chord), self.capo)


PROG = ["C", "G", "Am", "F"]


def loop(prog, cycles, **kw):
    return [Bar(c, **kw) for _ in range(cycles) for c in prog]


def final(chord="C", **kw):
    return [Bar(chord, "D-------", **kw)]


def _pink(n, rng):
    return lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786],
                   [1, -2.494956002, 2.017265875, -0.522189400], rng.standard_normal(n))


def _voice(beats, beat_dur, rng):
    """'Singer': sine with vibrato on random C-major notes, one per beat."""
    out = []
    scale = [60, 62, 64, 65, 67, 69, 71, 72]
    for d in beat_dur:
        n = int(d * SR)
        f = 440 * 2 ** ((rng.choice(scale) - 69) / 12)
        t = np.arange(n) / SR
        ph = 2 * np.pi * np.cumsum(f * (1 + 0.017 * np.sin(2 * np.pi * 5 * t))) / SR
        env = np.minimum(1, t / 0.05) * np.minimum(1, (d - t) / 0.05)
        out.append(env * (np.sin(ph) + 0.4 * np.sin(2 * ph) + 0.2 * np.sin(3 * ph)))
    return np.concatenate(out)


def render(bars, seed=0, lead=1.0, tail=3.0, jitter=0.010, noise_db=None, voice_db=None):
    rng = np.random.default_rng(seed)
    # tempo map: one entry per beat
    beats, beat_dur, downbeats = [], [], []
    t = lead
    for i, bar in enumerate(bars):
        nxt = bars[i + 1].bpm if i + 1 < len(bars) else bar.bpm
        downbeats.append(t)
        for b in range(4):
            bpm = bar.bpm + (nxt - bar.bpm) * b / 4
            beats.append(t)
            beat_dur.append(60.0 / bpm)
            t += 60.0 / bpm
    end_t = t
    duration = end_t + tail
    eff = [downbeats[i] + bar.shift * beat_dur[4 * i] for i, bar in enumerate(bars)]

    g = Guitar(duration, seed)
    cur_shape = None
    segments = [[0.0, lead, theory.N]]
    for i, bar in enumerate(bars):
        bar_end = downbeats[i + 1] if i + 1 < len(bars) else end_t
        if bar.chord is None:
            if cur_shape is not None:
                g.mute(downbeats[i])
            cur_shape = None
            segments.append([downbeats[i], bar_end, theory.N])
            continue
        label, seg_start = bar.label, downbeats[i]
        bass = list(bar.bass_notes)
        for k, c in enumerate(bar.pattern):
            bi = 4 * i + k // 2
            ts_grid = beats[bi] + (k % 2) * 0.5 * beat_dur[bi]
            ts = ts_grid + rng.normal(0, jitter)
            j = max(jj for jj in range(len(bars)) if eff[jj] <= ts_grid + 1e-9 or jj == 0)
            src = bars[j] if bars[j].chord is not None else bar
            shape = src.shape2 if (src.shape2 and src is bar and k >= 4) else src.chord
            if src is bar and bar.wrong and k < 4:
                shape = bar.wrong
            if c in "DU":
                if shape != cur_shape:
                    g.mute(ts)
                    cur_shape = shape
                accent = 1.0 if (k % 2 == 0 and c == "D") else 0.7
                g.strum(ts, theory.voicing(shape, src.capo), c, bar.gain * accent * rng.uniform(0.85, 1.15))
            elif c == "x":
                g.chk(ts, bar.gain * rng.uniform(0.8, 1.2))
            elif c == "b":
                g.single(ts, bass.pop(0), bar.gain)
            elif c == "m":
                g.mute(ts)
                cur_shape = None
                if not bar.late_entry:
                    segments.append([seg_start, ts_grid, label])
                    label, seg_start = "P", ts_grid
        segments.append([seg_start, bar_end, label])
    g.mute(end_t)
    segments.append([end_t, duration, theory.N])

    audio = g.out[: int(duration * SR)]
    audio = audio / (np.max(np.abs(audio)) + 1e-9) * 0.5
    playing = np.zeros(len(audio), bool)
    for s, e, lab in segments:
        if lab not in (theory.N, "P"):
            playing[int(s * SR):int(e * SR)] = True
    ref = np.sqrt(np.mean(audio[playing] ** 2))
    if noise_db is not None:
        n = _pink(len(audio), rng)
        audio = audio + n / np.sqrt(np.mean(n ** 2)) * ref * 10 ** (noise_db / 20)
    if voice_db is not None:
        v = _voice(beats, beat_dur, rng)
        v = np.concatenate([np.zeros(int(lead * SR)), v])[: len(audio)]
        v = np.pad(v, (0, len(audio) - len(v))) * playing
        audio = audio + v / np.sqrt(np.mean(v[playing] ** 2)) * ref * 10 ** (voice_db / 20)

    # merge segments and derive changes / ends
    merged = []
    for s, e, lab in segments:
        if e - s <= 1e-6:
            continue
        if merged and merged[-1][2] == lab:
            merged[-1][1] = e
        else:
            merged.append([s, e, lab])
    surprise_at = {round(downbeats[i], 6) for i, b in enumerate(bars) if b.surprise}
    changes, ends, last = [], [], None
    for k, (s, e, lab) in enumerate(merged):
        if lab in (theory.N, "P"):
            if lab == theory.N and k > 0 and merged[k - 1][2] != theory.N:
                ends.append(s)
                last = None
            continue
        if lab != last:
            changes.append({"t": s, "from": last, "to": lab, "surprise": round(s, 6) in surprise_at})
        last = lab
    truth = {
        "duration": duration, "beats": beats, "downbeats": downbeats,
        "tempo": [[b, 60.0 / d] for b, d in zip(beats, beat_dur)],
        "chords": merged, "changes": changes, "ends": ends,
        "intensity": [[downbeats[i], b.gain] for i, b in enumerate(bars) if b.chord is not None],
    }
    return audio.astype(np.float32), truth


def _metro_bars(rng):
    bars = loop(PROG, 4, bpm=110)
    for b in bars[1:]:
        b.shift = float(rng.choice([-0.5, -0.25, 0.25, 0.4]))
    return bars + final(bpm=110)


def _pausa_e_fim():
    a = loop(PROG, 2, bpm=110)
    a[7].pattern = "DUDUm---"
    b = loop(PROG, 2, bpm=110)
    c = loop(["Am", "F", "C", "G"], 2, bpm=110)
    return a + b + final(bpm=110) + [Bar(None, bpm=110) for _ in range(3)] + c + final("Am", bpm=110)


def _nota_de_passagem():
    bars = loop(PROG, 4, bpm=100)
    bars[6].pattern, bars[6].bass_notes = "DUDUbUDU", (43,)
    bars[10].shape2 = "Am/G"
    bars[13].pattern, bars[13].bass_notes = "DUDUDUbb", (45, 43)
    bars[15].pattern, bars[15].bass_notes = "DUDUDUbb", (43, 47)
    return bars + final(bpm=100)


def _troca_de_tom():
    c = loop(PROG, 3, bpm=110) + final(bpm=110) + [Bar(None, bpm=110) for _ in range(2)]
    d = loop(PROG, 3, bpm=100, capo=2)  # C-shapes with capo 2 = D A Bm G
    e = loop(["E", "B", "C#m", "A"], 3, bpm=100)
    for b in d[:4] + e[:4]:
        b.surprise = True
    return c + d + e + final("E", bpm=100)


def _acorde_fora_do_tom():
    bars = loop(PROG, 2, bpm=110)
    bars += [Bar("C", bpm=110), Bar("G", bpm=110), Bar("Em", bpm=110, surprise=True), Bar("F", bpm=110)]
    bars += [Bar("C", bpm=110), Bar("E", bpm=110, surprise=True), Bar("Am", bpm=110), Bar("F", bpm=110)]
    bars += [Bar("C", bpm=110), Bar("Bb", bpm=110, surprise=True), Bar("Am", bpm=110), Bar("F", bpm=110)]
    return bars + loop(PROG, 2, bpm=110) + final(bpm=110)


def _dinamica():
    gains = list(np.linspace(0.25, 1.0, 8)) + list(np.linspace(1.0, 0.25, 8))
    bars = loop(PROG, 4, bpm=100)
    for b, gn in zip(bars, gains):
        b.gain = float(gn)
    return bars + final(bpm=100, gain=0.25)


def _erros_com_padrao():
    """Third live session (2026-10-29): with the pattern learned, the musician's slips must not break it.
    Truth is the intended progression, not the slips."""
    bars = loop(PROG, 6, bpm=100)
    bars[9].wrong = "Dm"                                  # half a bar of a wrong chord (F expected... here G bar)
    bars[13].shift = 1.25                                 # changes to G 1.25 beats late
    bars[18].pattern, bars[18].late_entry = "m---DUDU", True   # comes in 2 beats late on Am
    bars[21].wrong = "Em"                                 # wrong chord again on G
    return bars + final(bpm=100)


LIVRE_C = {"modo": "livre", "tom": "C"}

# name -> (description, bars builder(rng), render kwargs, run config)
SCENARIOS = {
    "estavel_120": ("C-G-Am-F em loop, 120 BPM constante", lambda r: loop(PROG, 4, bpm=120) + final(bpm=120), {}, LIVRE_C),
    "acelerando": ("C-G-Am-F acelerando de 100 a 125 BPM",
                   lambda r: [Bar(c, bpm=100 + 25 * i / 19) for i, c in enumerate(PROG * 5)] + final(bpm=125), {}, LIVRE_C),
    "desacelerando_final": ("110 BPM com ritardando até ~65 no fim",
                            lambda r: loop(PROG, 3, bpm=110) + [Bar(c, bpm=v) for c, v in zip(PROG, [100, 90, 80, 70])] + final(bpm=65),
                            {}, LIVRE_C),
    "dinamica": ("crescendo e diminuendo", lambda r: _dinamica(), {}, LIVRE_C),
    "pausa_e_fim": ("pausa de meio compasso, fim com silêncio longo, outra progressão depois", lambda r: _pausa_e_fim(), {}, LIVRE_C),
    "nota_de_passagem": ("Am com baixo em Sol e notas de passagem que não são troca", lambda r: _nota_de_passagem(), {}, LIVRE_C),
    "troca_de_tom": ("Dó -> silêncio -> Ré (capotraste 2) -> Mi sem silêncio", lambda r: _troca_de_tom(), {}, LIVRE_C),
    "acorde_fora_do_tom": ("Em, E e Bb no lugar do esperado, com progressão decorada", lambda r: _acorde_fora_do_tom(), {},
                           {"modo": "decorado", "tom": "C", "progressao": "C G Am F"}),
    "batida_abafada": ("palhetadas abafadas nos tempos 2 e 4", lambda r: loop(PROG, 4, bpm=110, pattern="DUxUDUxU") + final(bpm=110),
                       {}, LIVRE_C),
    "ruido": ("ruído rosa + 'voz' senoidal", lambda r: loop(PROG, 4, bpm=100) + final(bpm=100), {"noise_db": -20, "voice_db": -6}, LIVRE_C),
    # gain 0.1: with the accompaniment ~14 dB above the guitar, the return arrives ~6 dB below the guitar
    "realimentacao": ("a saída do sistema volta na entrada (30 ms, ~6 dB abaixo do violão)",
                      lambda r: loop(PROG, 4, bpm=110) + final(bpm=110), {},
                      {"modo": "livre", "tom": "C", "feedback": {"gain": 0.1, "delay": 0.03}}),
    "erros_com_padrao": ("padrão travado; depois o músico erra um acorde, atrasa uma troca e entra atrasado",
                         lambda r: _erros_com_padrao(), {}, {"modo": "livre", "tom": "C", "manter_padrao": True}),
    # first live session (2026-10-29): I-V-vi-IV in B (guitar a half step down) with the key set to C
    "tom_errado": ("B-F#-G#m-E tocado com o tom configurado em Dó", lambda r: loop(["B", "F#", "G#m", "E"], 4, bpm=100)
                   + final("B", bpm=100), {}, LIVRE_C),
    "modo_metronomo": ("trocas adiantadas/atrasadas contra um clique fixo de 110 BPM", _metro_bars, {"jitter": 0.02},
                       {"modo": "metronomo", "tom": "C", "bpm": 110}),
}


def build(name, seed=None):
    desc, builder, kw, run = SCENARIOS[name]
    seed = seed if seed is not None else sum(map(ord, name))
    audio, truth = render(builder(np.random.default_rng(seed)), seed=seed, **kw)
    truth.update({"name": name, "description": desc, "seed": seed, "run": run})
    return audio, truth
