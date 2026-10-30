"""Progression memory: confirmed chord sequence, cycle detection and next-chord prediction."""
from . import params as P
from . import theory


class ProgressionMemory:
    def __init__(self, progression=None):
        """progression: decorated mode, e.g. 'C G Am F' (each chord = 1 bar)."""
        self.fixed = [theory.chord_index(c) for c in progression.split()] if progression else None
        self.reset()

    def reset(self):
        self.seq = []          # [[chord idx, start beat index]]
        self.known = list(self.fixed) if self.fixed else None   # cycle to re-lock onto
        self.lengths = [P.BEATS_PER_BAR] * len(self.known) if self.known else None
        self.locked = False
        self.pos = None        # index in `known` of the chord currently playing

    def push(self, idx, beat, blip=False):
        """A chord was confirmed starting at beat index `beat`. blip: the previous one lasted < 1 beat."""
        if self.seq and (blip or beat - self.seq[-1][1] < 1) and not self.locked:
            self.seq.pop()   # the previous chord was a blip, not part of the progression
        if self.seq and self.seq[-1][0] == idx:
            return
        self.seq.append([idx, beat])
        del self.seq[:-256]   # only the tail is ever used (bounded for live sessions; same in C++)
        if not self.locked:
            self._try_lock()

    def correct(self, idx):
        """The last pushed (predicted) chord was actually `idx`."""
        if self.seq:
            self.seq[-1][0] = idx
            if len(self.seq) > 1 and self.seq[-2][0] == idx:
                self.seq.pop()   # it was the same chord all along

    def _lens(self, items):
        """Durations in beats of closed entries, rounded to half bars (min 2 beats)."""
        half = P.BEATS_PER_BAR // 2
        return [max(half, round((b[1] - a[1]) / half) * half) for a, b in zip(items, items[1:])]

    def _try_lock(self):
        labels = [s[0] for s in self.seq]
        # 1) re-lock onto a known cycle: last two chords are consecutive in it
        if self.known and len(labels) >= 2:
            n = len(self.known)
            for i in range(n):
                if self.known[i] == labels[-2] and self.known[(i + 1) % n] == labels[-1]:
                    self.locked, self.pos = True, (i + 1) % n
                    return
        # 2) decorated mode, first chord: align on it
        if self.fixed and len(labels) == 1 and labels[0] in self.known:
            self.locked, self.pos = True, self.known.index(labels[0])
            return
        # 3) learn: a lap of p chords came back (its first k chords repeat); smallest p wins
        for p in range(P.CYCLE_MIN, P.CYCLE_MAX + 1):
            k = max(P.CYCLE_MIN, (p + 1) // 2)   # how much of the second lap must match before locking
            if len(labels) < p + k or labels[-k:] != labels[-k - p:-p]:
                continue
            cyc = labels[-k - p:-k]               # one complete (closed) lap
            if len(set(cyc)) < 2:
                continue
            self.known = cyc
            self.lengths = self._lens(self.seq[-k - p:-k + 1])
            self.locked, self.pos = True, k - 1  # the current chord is cyc[k-1]
            return

    def predict(self):
        """(next chord idx, beat index where it starts) or None."""
        if not self.locked or not self.seq:
            return None
        nxt = (self.pos + 1) % len(self.known)
        return self.known[nxt], self.seq[-1][1] + self.lengths[self.pos]

    def advance(self, beat):
        """The predicted change happened at `beat`."""
        self.pos = (self.pos + 1) % len(self.known)
        self.seq.append([self.known[self.pos], beat])
        del self.seq[:-256]

    def unlock(self):
        self.locked, self.pos = False, None
