"""Streaming pipeline: wires every module and runs block by block, logging each frame."""
from dataclasses import dataclass, field

import numpy as np

from . import params as P
from . import theory
from .accomp import Accompanist, Dynamics
from .chords import ChordTracker
from .engine import STATES, TRAVADO, OUVINDO, Engine
from .memory import ProgressionMemory
from .onsets import OnsetDetector
from .synth import Synth
from .tempo import FixedGrid, TempoTracker


@dataclass
class RunConfig:
    modo: str = "livre"           # livre | metronomo | decorado
    tom: str | None = None        # main key, e.g. 'C'
    bpm: float | None = None      # metronome mode
    progressao: str | None = None # decorated mode, e.g. 'C G Am F'
    feedback: dict | None = None  # test only: {'gain': 0.5, 'delay': 0.03} closed loop output -> input

    @classmethod
    def from_dict(cls, d):
        return cls(**{k: v for k, v in d.items() if k in cls.__dataclass_fields__})


@dataclass
class Result:
    audio: np.ndarray             # input as heard (including feedback)
    accomp: np.ndarray            # synthesized bass + pad
    log: dict                     # per-frame arrays
    events: list
    changes: list                 # (t, chord label or None, mode)
    onsets: list = field(default_factory=list)


def _norm_mag(x, win):
    return np.abs(np.fft.rfft(x * win)) * 2 / win.sum()


def run(audio, cfg: RunConfig):
    if cfg.modo == "metronomo" and not cfg.bpm:
        raise ValueError("modo metronomo exige --bpm")
    if cfg.modo == "decorado" and not cfg.progressao:
        raise ValueError("modo decorado exige --progressao")
    audio = np.asarray(audio, dtype=np.float64).copy()
    tracker = FixedGrid(cfg.bpm) if cfg.modo == "metronomo" else TempoTracker()
    memory = ProgressionMemory(cfg.progressao if cfg.modo == "decorado" else None)
    engine = Engine(tracker, memory, cfg.tom, metronome=cfg.modo == "metronomo")
    onsets, chords = OnsetDetector(), ChordTracker(cfg.tom)
    acc, dyn, synth = Accompanist(cfg.tom), Dynamics(), Synth()
    wo, wc = np.hanning(P.ONSET_WIN), np.hanning(P.CHROMA_WIN)
    buf = np.zeros(P.CHROMA_WIN)
    n_blocks = len(audio) // P.HOP
    out = np.zeros(len(audio))
    fb_gain, fb_delay = (cfg.feedback["gain"], int(cfg.feedback["delay"] * P.SR)) if cfg.feedback else (0.0, 0)
    keys = ["t", "db", "flux", "onset", "bpm", "tempo_conf", "heard", "conf", "state", "level", "expr", "vel"]
    log = {k: [] for k in keys}
    onset_list = []
    last_onset_t = -np.inf
    last_onset_strong = False

    for k in range(n_blocks):
        s = k * P.HOP
        # output for this block was decided during the previous one (1 block of structural latency)
        out[s:s + P.HOP] = synth.render(s / P.SR, P.HOP)
        if fb_gain and s >= fb_delay:
            audio[s:s + P.HOP] += fb_gain * out[s - fb_delay:s - fb_delay + P.HOP]
        x = audio[s:s + P.HOP]
        buf = np.concatenate([buf[P.HOP:], x])
        t = (s + P.HOP) / P.SR
        db = 10 * np.log10(np.mean(x ** 2) + 1e-12)

        onset = onsets.process(_norm_mag(buf[-P.ONSET_WIN:], wo), t - P.ONSET_WIN / 2 / P.SR, db)
        beats = tracker.update(t, onsets.flux, onset)
        if onset is not None:
            onset_list.append((onset.t, onset.db))
            dyn.strum(onset.db)
            last_onset_t = onset.t
            last_onset_strong = bool(tracker.period) and tracker.is_strong(onset.t)
        if t - last_onset_t < P.HMM_ONSET_BOOST_TIME:
            p_change = P.HMM_P_CHANGE_STRONG if last_onset_strong else P.HMM_P_CHANGE_ONSET
        else:
            p_change = P.HMM_P_CHANGE
        idx, conf, _ = chords.process(_norm_mag(buf, wc), db < P.SILENCE_DB, p_change)

        decision = engine.step(t, onset, idx, conf, chords.evidence)
        vel = dyn.velocity()
        evs = []
        if decision is not None:
            evs += acc.apply(decision, vel)
        if engine.state in (OUVINDO, TRAVADO) and tracker.confidence >= 0.5:
            for bt, bi in beats:
                evs += acc.beat(t, tracker.bar_pos(bi) % 2 == 0, tracker.period, vel)
        expr = dyn.expression(t, engine.last_onset, tracker.period)
        evs += acc.expression(t, expr)
        for e in evs:
            synth.handle(e)

        for key, val in zip(keys, [t, db, onsets.flux, onset is not None, tracker.bpm or np.nan, tracker.confidence,
                                   idx, conf, STATES.index(engine.state), engine.level, expr, vel]):
            log[key].append(val)

    log = {k: np.asarray(v) for k, v in log.items()}
    return Result(audio, out, log, acc.events, acc.changes, onset_list)


def load_wav(path):
    """Any WAV -> mono float at SR. Resampling here is input preparation (an audio driver would do it live)."""
    import soundfile as sf
    from scipy.signal import resample_poly
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != P.SR:
        g = np.gcd(int(sr), P.SR)
        x = resample_poly(x, P.SR // g, int(sr) // g)
    return x

