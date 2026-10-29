"""Accompaniment generator: engine outputs -> note events for bass and pad. Also dynamics."""
import numpy as np

from . import params as P
from . import theory


class Dynamics:
    """Strum loudness -> smoothed velocity; expression decays while the musician is not strumming."""

    def __init__(self):
        self.level = None

    def strum(self, db):
        self.level = db if self.level is None else self.level + P.DYN_SMOOTH * (db - self.level)

    def velocity(self):
        if self.level is None:
            return P.DYN_VEL_HIGH
        x = np.clip((self.level - P.DYN_DB_LOW) / (P.DYN_DB_HIGH - P.DYN_DB_LOW), 0, 1)
        return int(round(P.DYN_VEL_LOW + x * (P.DYN_VEL_HIGH - P.DYN_VEL_LOW)))

    def expression(self, t, last_onset, period):
        beat = period or 0.5
        if last_onset is None or t - last_onset < 1.5 * beat:
            return 1.0
        return float(np.exp(-(t - last_onset - 1.5 * beat) / P.DYN_SILENCE_DECAY))


def voice(chord, mode, key):
    """(bass MIDI note or None, pad MIDI notes) for an engine output."""
    if mode is None:
        return None, []
    if mode == "tonic":
        r = theory.note_pc(key)
        pcs, bass = [r, (r + 7) % 12], None
    else:
        pcs = theory.chord_pcs(chord)
        if mode == "root5":
            pcs = [pcs[0], pcs[2]]
        bass = P.BASS_LOW + pcs[0]
    return bass, [P.PAD_LOW + (pc - P.PAD_LOW) % 12 for pc in pcs]


class Accompanist:
    def __init__(self, key=None):
        self.key = key
        self.events = []     # dicts: t, type on/off/expr, inst, pitch, vel
        self.changes = []    # (t, chord label or None, mode)
        self.bass = None
        self.pad = []
        self.last_bass_t = -np.inf
        self.expr = 1.0

    def _ev(self, t, typ, inst, pitch=0, vel=0):
        self.events.append({"t": round(float(t), 5), "type": typ, "inst": inst, "pitch": int(pitch), "vel": int(vel)})
        return self.events[-1]

    def apply(self, out, vel):
        new = []
        for p in self.pad:
            new.append(self._ev(out.t, "off", "pad", p))
        if self.bass is not None:
            new.append(self._ev(out.t, "off", "bass", self.bass))
        self.bass, self.pad = voice(out.chord, out.mode, self.key)
        for p in self.pad:
            new.append(self._ev(out.t, "on", "pad", p, vel))
        if self.bass is not None:
            new.append(self._ev(out.t, "on", "bass", self.bass, vel))
            self.last_bass_t = out.t
        label = None if out.chord is None else theory.LABELS[out.chord]
        self.changes.append((out.t, label, out.mode))
        return new

    def beat(self, t, strong, period, vel):
        """Re-attack the bass on beats 1 and 3 (only called when the tempo is trusted)."""
        if self.bass is None or not strong or t - self.last_bass_t < 0.5 * period:
            return []
        self.last_bass_t = t
        return [self._ev(t, "off", "bass", self.bass), self._ev(t, "on", "bass", self.bass, vel)]

    def expression(self, t, value):
        if abs(value - self.expr) < 0.03 and not (value == 1.0 and self.expr != 1.0):
            return []
        self.expr = value
        ev = self._ev(t, "expr", "all")
        ev["value"] = round(value, 3)
        return [ev]
