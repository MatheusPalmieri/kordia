"""Prova: songs the engine was never tuned on, in all 12 keys, with the settings of the live app and the
imperfections measured in the real sessions. Answers one question: does the app end up playing what was played?"""
import csv
import math
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from functools import cache
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

from . import theory
from .engine import STATES, TRAVADO
from .metrics import DT, _runs, _timeline, _truth_labels
from .params import SR
from .pipeline import RunConfig, run
from .scenarios import Bar, final, render

# measured in sessoes/ (2026-10-29 and 30)
LOOKAHEAD = 0.0218    # s: 2 buffers of 480 samples at 44.1 kHz
LATE = 0.08           # s: strums reach the input this late against the click (median 54-92 ms)
JITTER = 0.025        # s: std of each strum against the grid (the click sessions spread more, but include 16ths)
CENTS = 5.0           # std of each string's tuning error (the sessions sit within +/-7 cents of the grid)
NOISE_DB = -32.0      # room noise relative to the playing level (-24 dB playing over -56 dB in the noisiest session)
LEVEL = 0.3           # 0.5 peak -> 0.15 peak: the sessions peak at 0.05-0.5, with p95 -24 to -39 dB
ROOM_SECONDS = 0.25   # decay of a small room; the input is a USB mic, not a pickup
TUNED_DOWN = -1       # the guitar is a half step down: shapes sound one semitone below their name

PATTERNS = ["DUDUDUDU", "D-DU-UDU", "DUxUDUxU"]
BPMS = [90, 72, 120, 104]
MIN_BARS, MIN_LAPS = 24, 4

# bars split by '|'; two chords in a bar change on beat 3. Written in C (or G: `FORMS`), transposed to each key.
PROGRESSIONS = {
    "pop": "C | G | Am | F",
    "sensivel": "Am | F | C | G",
    "worship": "C | F | Am | G",
    "tres_acordes": "C | F | G",
    "dois_acordes": "C | F",
    "duas_por_compasso": "C G | Am F",
    "dois_compassos": "C | C | G | G | Am | Am | F | F",
    "volta_na_tonica": "C | G | C | F",
    "seis_acordes": "C | G | Am | Em | F | G",
    "canone": "C | G | Am | Em | F | C | F | G",
    "falsa_volta": "C | G | C | G | Am | F",
    "menor": "Am | G | F | G",
    "ii_V_I": "Dm | G | C | Am",
    "emprestado": "C | Bb | F | C",
    "dominante_secundaria": "C | E | Am | F",
    "formas_worship": "G | D/F# | Em7 | Cadd9",
    "formas_sus": "G | G/B | Cadd9 | Dsus4",
}
FORMS = {"formas_worship", "formas_sus"}   # voicings that only exist as shapes: transposed with a capo
VARIANTS = ("clique", "tom_da_forma", "livre")

# the real guitar: bars of the live sessions, played on the click over I V vi IV (sessoes/ is not versioned).
# (folder, sounding key, first bar on chord I, end bar). The truth of a bar is its position in the cycle, not what
# the app heard, so a bar it misheard live (the Bbm barre) stays in the exam as a hard bar.
SESSIONS_DIR = Path("sessoes")
SESSIONS = [("2026-10-29_22-50-35", 11, 1, 25), ("2026-10-29_22-51-45", 1, 1, 11),
            ("2026-10-29_22-52-18", 6, 3, 23), ("2026-10-30_10-57-27", 6, 1, 37)]
DEGREES = ("C", "G", "Am", "F")
SESSION_BPM = 90.0
FADE = 0.005          # s: splice between two recorded bars
REAL_VARIANTS = ("real_clique", "real_livre")
# control: real sessions played straight, whole, with the interface's settings. (folder, cycle, first and end bar)
CONTROLS = [("2026-10-29_22-50-35", "B F# Abm E", 1, 25), ("2026-10-29_22-51-45", "C# Ab Bbm F#", 1, 11),
            ("2026-10-29_22-52-18", "F# C# Ebm B", 3, 23), ("2026-10-30_10-57-27", "F# C# Ebm B", 1, 37),
            ("2026-10-30_15-52-02", "F# C# Ebm B", 1, 20), ("2026-10-30_15-53-00", "F# C# Ebm B", 1, 21)]
REAL_PROGRESSIONS = [p for p, txt in PROGRESSIONS.items() if set(txt.replace("|", " ").split()) <= set(DEGREES)]


@dataclass
class Case:
    prog: str
    key: int        # sounding key (pitch class)
    variant: str    # clique: metronome + automatic key; tom_da_forma: key set to the shape's name (a half step
                    # above what sounds, as in the 2026-10-30 session); livre: no click, automatic key

    @property
    def name(self):
        return f"{self.prog}/{theory.NOTE_NAMES[self.key]}/{self.variant}"


def song(case):
    """Bars of the song and its tempo: pattern and BPM rotate with the key, so the matrix stays one song per cell."""
    pattern, bpm = PATTERNS[case.key % len(PATTERNS)], BPMS[case.key % len(BPMS)]
    forms = case.prog in FORMS
    home = theory.note_pc("G" if forms else "C")
    up = (case.key - TUNED_DOWN - home) % 12
    capo, shape = (up + TUNED_DOWN, str) if forms else (TUNED_DOWN, lambda c: theory.transpose(c, up))
    lap = []
    for bar in PROGRESSIONS[case.prog].split("|"):
        first, *second = bar.split()
        lap.append(Bar(shape(first), pattern, bpm=bpm, capo=capo, half=shape(second[0]) if second else None))
    laps = max(MIN_LAPS, math.ceil(MIN_BARS / len(lap)))
    return lap * laps + final(lap[0].chord, bpm=bpm, capo=capo), bpm


def _room(x, rng):
    tail = rng.standard_normal(int(ROOM_SECONDS * SR)) * np.exp(-np.arange(int(ROOM_SECONDS * SR)) / (ROOM_SECONDS / 6.9 * SR))
    ir = np.concatenate([[1.0], 0.04 * tail])
    return np.fft.irfft(np.fft.rfft(x, len(x) + len(ir)) * np.fft.rfft(ir, len(x) + len(ir)))[: len(x)]


@cache
def _takes():
    """[(source key, {degree: [recorded bars]})], one entry per session: a song never mixes two sessions, whose
    levels differ by 12 dB (no real take jumps like that on every bar)."""
    from .session import load
    takes = []
    for folder, key, first, end in SESSIONS:
        audio, cfg, lookahead, sr = load(SESSIONS_DIR / folder)
        assert sr == SR and cfg["bpm"] == SESSION_BPM, folder
        bar, bars = 4 * 60.0 / SESSION_BPM, {}
        for i in range(first, end):
            a = int((0.5 + lookahead + LATE - 0.03 + i * bar) * SR)
            bars.setdefault(DEGREES[(i - first) % 4], []).append(np.asarray(audio[a:a + int(bar * SR)]))
        takes.append((key, bars))
    return takes


def _spliced(takes, bars, rng):
    n = len(takes[DEGREES[0]][0])
    fade = np.linspace(0, 1, int(FADE * SR))
    out, chords = [], []
    for tokens in bars:
        for c in tokens:
            pick = takes[c][rng.integers(len(takes[c]))]
            x = pick[: n // len(tokens)].copy()
            x[:len(fade)] *= fade
            x[-len(fade):] *= fade[::-1]
            out.append(x)
            chords.append((c, len(x)))
    return np.concatenate(out), chords


def build_real(case):
    """A new song cut from real bars, moved to the key by the shortest shift (which also moves the tempo)."""
    rng = np.random.default_rng(sum(map(ord, case.name)))
    shift = lambda src: (case.key - src + 6) % 12 - 6
    nearest = min(abs(shift(k)) for k, _ in _takes())
    options = [t for t in _takes() if abs(shift(t[0])) == nearest]
    src, bars = options[rng.integers(len(options))]
    ratio = 2 ** (shift(src) / 12)
    bpm = SESSION_BPM * ratio
    lap = [b.split() for b in PROGRESSIONS[case.prog].split("|")]
    body, chords = _spliced(bars, lap * max(MIN_LAPS, math.ceil(MIN_BARS / len(lap))) + [lap[0][:1]], rng)
    body = resample_poly(body, 1000, round(1000 * ratio))
    click = case.variant == "real_clique"
    lead = 0.5 + LOOKAHEAD + 8 * 60.0 / bpm + LATE - 0.03 if click else 1.0
    audio = np.concatenate([np.zeros(int(lead * SR)), body, np.zeros(3 * SR)])
    t, segments, changes, last = lead, [[0.0, lead, theory.N]], [], None
    for c, n in chords:
        label, end = theory.transpose(c, case.key), t + n / SR / ratio
        if label != last:
            changes.append({"t": t, "to": label})
        segments.append([t, end, label])
        t, last = end, label
    segments.append([t, len(audio) / SR, theory.N])
    truth = {"duration": len(audio) / SR, "tempo": [[0.0, bpm], [len(audio) / SR, bpm]], "chords": segments, "changes": changes}
    cfg = RunConfig(modo="metronomo" if click else "livre", bpm=bpm if click else None, clique=click, contagem=click,
                    manter_padrao=True, aprender_na_tonica=True)
    return audio.astype(np.float32), truth, cfg


def build(case):
    if case.variant in REAL_VARIANTS:
        return build_real(case)
    bars, bpm = song(case)
    seed = sum(map(ord, case.name))
    click = case.variant != "livre"
    # with the click the musician enters on a downbeat two bars after Iniciar, late by the measured offset
    lead = 0.5 + LOOKAHEAD + 8 * 60.0 / bpm + LATE if click else 1.0
    audio, truth = render(bars, seed=seed, lead=lead, jitter=JITTER, noise_db=NOISE_DB, cents=CENTS)
    audio = LEVEL * _room(audio, np.random.default_rng(seed))
    cfg = RunConfig(modo="metronomo" if click else "livre", bpm=bpm if click else None, clique=click, contagem=click,
                    tom=theory.NOTE_NAMES[(case.key - TUNED_DOWN) % 12] if case.variant == "tom_da_forma" else None,
                    manter_padrao=True, aprender_na_tonica=True)
    return audio.astype(np.float32), truth, cfg


def score(res, truth, key):
    """lock_s: from the first strum to the pattern locking. acc: share of the time after the lock with the exact
    chord. wrong_s: seconds of harmony that clashes (after the first beat of a change, runs of 1/2 beat or more)."""
    g = np.arange(0, truth["duration"], DT)
    tl = _truth_labels(truth, g)
    lab, mode = _timeline(res.changes, g)
    tt, tb = np.array(truth["tempo"]).T
    beat = 60.0 / np.median(tb)
    is_chord = np.array([l not in ("N", "P") for l in tl])
    first = next(s for s, e, l in truth["chords"] if l not in ("N", "P"))
    locked = res.log["t"][res.log["state"] == STATES.index(TRAVADO)]
    lock = float(locked[0]) if len(locked) else None

    right = (mode == "full") & (lab == tl)
    near = np.zeros(len(g), bool)
    for c in truth["changes"]:
        near |= (g >= c["t"]) & (g < c["t"] + beat)
    root = lambda labels: np.array([theory.parse_chord(l)[0] if l not in (None, "N", "P") else -1 for l in labels])
    clash = (mode == "full") & (lab != tl) | (mode == "root5") & (root(lab) != root(tl))
    key_chords = theory.diatonic(key)
    clash |= (mode == "tonic") & np.array([l not in (None, "N") and theory.chord_index(l) not in key_chords for l in lab])
    wrong = is_chord & clash & ~near
    after = is_chord & (g >= lock) if lock is not None else np.zeros(len(g), bool)
    return {"lock_s": None if lock is None else round(lock - first, 2),
            "lock_compassos": None if lock is None else round((lock - first) / (4 * beat), 1),
            "acc": round(float(right[after].mean()), 3) if after.any() else 0.0,
            "wrong_s": round(float(_runs(wrong)[_runs(wrong) * DT >= beat / 2].sum() * DT), 2),
            "played": _last_lap(res, truth)}


def _last_lap(res, truth):
    """Chords the app played over the last lap, for reading a failure at a glance."""
    n = len({l for _, _, l in truth["chords"] if l not in ("N", "P")})
    return " ".join(str(c[1]) for c in res.changes if c[1] and c[2] == "full")[-(6 * max(n, 4)):].split(" ", 1)[-1]


def verdict(s):
    if s["lock_s"] is None:
        return "nao_travou"
    return "ok" if s["acc"] >= 0.90 and s["wrong_s"] <= 2.0 else "errou"


def play(case, backend="cpp"):
    audio, truth, cfg = build(case)
    s = score(run(audio, cfg, lookahead=LOOKAHEAD if cfg.clique else 0.0, backend=backend), truth, case.key)
    return {"caso": case.name, "progressao": case.prog, "tom": theory.NOTE_NAMES[case.key], "variante": case.variant,
            "veredito": verdict(s), **s}


def play_control(control, backend="cpp"):
    """A real click session as it was played, truth = position in the cycle, automatic key as in the interface."""
    from .session import load
    folder, cycle, first, end = control
    audio, c, lookahead, sr = load(SESSIONS_DIR / folder)
    cfg = RunConfig.from_dict(c)
    cfg.tom, cfg.manter_padrao, cfg.aprender_na_tonica = None, True, True
    cycle, bar, t0 = cycle.split(), 4 * 60.0 / cfg.bpm, 0.5 + lookahead + LATE
    bars = [(t0 + b * bar, t0 + (b + 1) * bar, cycle[(b - first) % len(cycle)]) for b in range(first, end)]
    duration = bars[-1][1]
    truth = {"duration": duration, "tempo": [[0.0, cfg.bpm], [duration, cfg.bpm]],
             "chords": [[0.0, bars[0][0], theory.N]] + [list(b) for b in bars], "changes": [{"t": a, "to": l} for a, _, l in bars]}
    res = run(audio[: int(duration * sr)], cfg, lookahead=lookahead, backend=backend, sr=sr)
    s = score(res, truth, theory.note_pc(cycle[0]))
    return {"sessao": folder, "veredito": verdict(s), **s}


def cases(progs=None, keys=None, variants=None):
    has_real = all((SESSIONS_DIR / s[0]).is_dir() for s in SESSIONS)
    variants = variants or VARIANTS + (REAL_VARIANTS if has_real else ())
    return [Case(p, k, v) for p in (progs or PROGRESSIONS) for v in variants for k in (range(12) if keys is None else keys)
            if v not in REAL_VARIANTS or p in REAL_PROGRESSIONS]


def run_exam(selection, backend="cpp"):
    with ProcessPoolExecutor() as pool:
        return list(pool.map(play, selection, [backend] * len(selection)))


def run_controls(backend="cpp"):
    present = [c for c in CONTROLS if (SESSIONS_DIR / c[0]).is_dir()]
    with ProcessPoolExecutor() as pool:
        return list(pool.map(play_control, present, [backend] * len(present)))


def print_controls(rows):
    for r in rows:
        print(f"  {r['sessao']}  {r['veredito']:10s} acc {r['acc']:.2f} errado {r['wrong_s']:5.1f} s trava {r['lock_compassos']}")
    if rows:
        print(f"  média: acc {np.mean([r['acc'] for r in rows]):.2f}, errado {np.mean([r['wrong_s'] for r in rows]):.1f} s")


def print_exam(rows):
    """One line per progression, one column per variant: passed songs and the failures that matter."""
    columns = [v for v in VARIANTS + REAL_VARIANTS if any(r["variante"] == v for r in rows)]
    print(f"{'progressão':24s}" + "".join(f"{v:>14s}" for v in columns) + f"{'acc':>7s}{'trava(comp)':>13s}{'errado_s':>10s}")
    for prog in PROGRESSIONS:
        rs = [r for r in rows if r["progressao"] == prog]
        if not rs:
            continue
        cells = []
        for v in columns:
            vs = [r for r in rs if r["variante"] == v]
            cells.append(f"{sum(r['veredito'] == 'ok' for r in vs)}/{len(vs)}" if vs else "-")
        lock = [r["lock_compassos"] for r in rs if r["lock_compassos"] is not None]
        print(f"{prog:24s}" + "".join(f"{c:>14s}" for c in cells) + f"{np.mean([r['acc'] for r in rs]):7.2f}"
              + f"{np.median(lock) if lock else float('nan'):13.1f}{np.mean([r['wrong_s'] for r in rs]):10.1f}")
    ok = sum(r["veredito"] == "ok" for r in rows)
    print(f"\n{ok}/{len(rows)} músicas certas ({ok / len(rows):.0%})")
    for r in rows:
        if r["veredito"] != "ok":
            print(f"  {r['veredito']:10s} {r['caso']:42s} acc {r['acc']:.2f} errado {r['wrong_s']:5.1f} s trava {r['lock_compassos']}  [{r['played']}]")


def save_exam(rows, versao):
    out = Path("resultados") / versao
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "prova.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    return out / "prova.csv"
