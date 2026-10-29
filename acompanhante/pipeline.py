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


class Processor:
    """The whole system for one block at a time. Used by `run` (files) and by the live GUI (audio callback)."""

    LOG_KEYS = ["t", "db", "flux", "onset", "bpm", "tempo_conf", "heard", "conf", "state", "level", "expr", "vel"]

    def __init__(self, cfg: RunConfig, keep_log=True, lookahead=0.0):
        """lookahead: live output latency (s). The synth renders that far ahead so predicted changes and
        bass beats sound on time; reactive changes are late anyway and simply start immediately."""
        if cfg.modo == "metronomo" and not cfg.bpm:
            raise ValueError("modo metronomo exige bpm")
        if cfg.modo == "decorado" and not cfg.progressao:
            raise ValueError("modo decorado exige progressao")
        self.tracker = FixedGrid(cfg.bpm) if cfg.modo == "metronomo" else TempoTracker()
        # a progression also works together with a fixed BPM (metronome + decorated)
        self.memory = ProgressionMemory(cfg.progressao)
        self.lookahead = lookahead
        self.engine = Engine(self.tracker, self.memory, cfg.tom, metronome=cfg.modo == "metronomo", lookahead=lookahead)
        self.onsets, self.chords = OnsetDetector(), ChordTracker(cfg.tom)
        self.acc, self.dyn, self.synth = Accompanist(cfg.tom), Dynamics(), Synth()
        self.wo, self.wc = np.hanning(P.ONSET_WIN), np.hanning(P.CHROMA_WIN)
        self.buf = np.zeros(P.CHROMA_WIN)
        self.s = 0                      # samples processed
        self.keep_log = keep_log
        self.log = {k: [] for k in self.LOG_KEYS}
        self.onset_list = []
        self.last_onset_t = -np.inf
        self.last_onset_strong = False
        self.db, self.idx, self.conf = -120.0, 24, 0.0   # latest values, read by the GUI

    def render(self):
        """Output for the current block, decided during the previous one (1 block of structural latency)."""
        return self.synth.render(self.s / P.SR + self.lookahead, P.HOP)

    def analyze(self, x):
        """Consume one input block of HOP samples."""
        tracker, engine, acc, dyn = self.tracker, self.engine, self.acc, self.dyn
        self.buf = np.concatenate([self.buf[P.HOP:], x])
        self.s += P.HOP
        t = self.s / P.SR
        db = 10 * np.log10(np.mean(x ** 2) + 1e-12)

        onset = self.onsets.process(_norm_mag(self.buf[-P.ONSET_WIN:], self.wo), t - P.ONSET_WIN / 2 / P.SR, db)
        tracker.update(t, self.onsets.flux, onset)
        if onset is not None:
            self.onset_list.append((onset.t, onset.db))
            dyn.strum(onset.db)
            self.last_onset_t = onset.t
            self.last_onset_strong = bool(tracker.period) and tracker.is_strong(onset.t)
        if t - self.last_onset_t < P.HMM_ONSET_BOOST_TIME:
            p_change = P.HMM_P_CHANGE_STRONG if self.last_onset_strong else P.HMM_P_CHANGE_ONSET
        else:
            p_change = P.HMM_P_CHANGE
        idx, conf, _ = self.chords.process(_norm_mag(self.buf, self.wc), db < P.SILENCE_DB, p_change)

        decision = engine.step(t, onset, idx, conf, self.chords.evidence)
        vel = dyn.velocity()
        evs = []
        if decision is not None:
            evs += acc.apply(decision, vel)
        if engine.state in (OUVINDO, TRAVADO) and tracker.confidence >= 0.5:
            # bass re-attack on the predicted beat that falls in the next rendered block
            bi = int(np.ceil(tracker.beat_phase(t + self.lookahead)))
            tb = tracker.time_of_beat(bi)
            if tb < t + self.lookahead + P.HOP / P.SR:
                evs += acc.beat(tb, tracker.bar_pos(bi) % 2 == 0, tracker.period, vel)
        expr = dyn.expression(t, engine.last_onset, tracker.period)
        evs += acc.expression(t, expr)
        for e in evs:
            self.synth.handle(e)

        self.db, self.idx, self.conf = db, idx, conf
        if self.keep_log:
            for key, val in zip(self.LOG_KEYS, [t, db, self.onsets.flux, onset is not None, tracker.bpm or np.nan,
                                                tracker.confidence, idx, conf, STATES.index(engine.state),
                                                engine.level, expr, vel]):
                self.log[key].append(val)
        else:
            del acc.events[:-64], acc.changes[:-64], self.onset_list[:-64]   # live: bounded memory

    def process(self, x):
        out = self.render()
        self.analyze(x)
        return out

    def beat_in_bar(self):
        """1..4 for the beat currently sounding, or None if the tempo is unknown."""
        if not self.tracker.period:
            return None
        return self.tracker.bar_pos(int(np.floor(self.tracker.beat_phase(self.s / P.SR)))) + 1


def run(audio, cfg: RunConfig):
    audio = np.asarray(audio, dtype=np.float64).copy()
    proc = Processor(cfg)
    out = np.zeros(len(audio))
    fb_gain, fb_delay = (cfg.feedback["gain"], int(cfg.feedback["delay"] * P.SR)) if cfg.feedback else (0.0, 0)
    for k in range(len(audio) // P.HOP):
        s = k * P.HOP
        out[s:s + P.HOP] = proc.render()
        if fb_gain and s >= fb_delay:
            audio[s:s + P.HOP] += fb_gain * out[s - fb_delay:s - fb_delay + P.HOP]
        proc.analyze(audio[s:s + P.HOP])
    log = {k: np.asarray(v) for k, v in proc.log.items()}
    return Result(audio, out, log, proc.acc.events, proc.acc.changes, proc.onset_list)

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

