"""Progression memory: confirmed chord sequence, cycle detection and next-chord prediction."""
import numpy as np

from . import params as P
from . import theory

N_IDX = 24
SLOTS_PER_BAR = P.BEATS_PER_BAR // P.SLOT_BEATS


def _same(a, b):
    """Same chord as far as the cycle goes: the third is what one reading gets wrong; the sum of the laps decides it."""
    return a != N_IDX and b != N_IDX and a % 12 == b % 12


class ProgressionMemory:
    def __init__(self, progression=None):
        """progression: decorated mode, e.g. 'C G Am F' (each chord = 1 bar)."""
        self.fixed = [theory.chord_index(c) for c in progression.split()] if progression else None
        self.T = theory.templates()
        self.reset()

    def reset(self):
        self.seq = []          # [[chord idx, start beat index]]
        self.known = list(self.fixed) if self.fixed else None   # cycle to re-lock onto
        self.lengths = [P.BEATS_PER_BAR] * len(self.known) if self.known else None
        self.locked = False
        self.pos = None        # index in `known` of the chord currently playing
        # click grid: one entry per half bar, oldest first
        self.slots, self.labels = [], []   # unit chroma (zeros: nothing played), and its chord
        self.last_slot = None  # index of the newest slot
        self.cycle = None      # chord of each slot of the lap, by slot index % len(cycle)
        self.misses = 0        # consecutive slots against the cycle

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

    # --- click grid ---
    def _read(self, chroma):
        return int(np.argmax(self.T @ chroma)) if chroma.any() else N_IDX

    def _append(self, chroma):
        if not self.slots and not chroma.any():
            return   # the song has not started: an empty lap would count against every cycle
        self.slots.append(chroma)
        self.labels.append(self._read(chroma))
        del self.slots[:-P.GRID_HISTORY], self.labels[:-P.GRID_HISTORY]

    def push_slot(self, slot, chroma, max_misses):
        """Half bar `slot` closed: returns the chord read in it; locks, corrects or drops the cycle."""
        if self.last_slot is not None:
            for _ in range(slot - self.last_slot - 1):
                self._append(np.zeros(12))
        norm = np.linalg.norm(chroma)
        self._append(chroma / norm if norm > 0 else chroma)
        self.last_slot = slot
        heard = self.labels[-1] if self.labels else N_IDX
        if self.locked:
            self.misses = self.misses + 1 if heard != N_IDX and not _same(heard, self.cycle[slot % len(self.cycle)]) else 0
            if self.misses >= max_misses:
                self.unlock()
            else:
                self._adopt(len(self.cycle))
        else:
            size = self._find_cycle()
            if size and self._adopt(size):
                self.locked, self.misses = True, 0
        return heard

    def _find_cycle(self):
        """Smallest lap of whole bars whose slots repeat in the lap after it, or None."""
        labels = self.labels
        for size in range(SLOTS_PER_BAR, P.GRID_MAX_SLOTS + 1, SLOTS_PER_BAR):
            tail = min(size, len(labels) - size)
            if tail < round((P.GRID_LOCK_LAPS - 1) * size):
                continue
            same = [_same(a, b) for a, b in zip(labels[-tail:], labels[-tail - size:-size])]
            roots = {c % 12 for c in labels[-size:] if c != N_IDX}
            if sum(same) >= P.GRID_MATCH * tail and all(same[-P.GRID_TAIL:]) and len(roots) > 1:
                return size
        return None

    def _adopt(self, size):
        """Chord of each slot = sum of its last laps. Fills known, lengths, pos and seq for the slot about to start.
        False if the lap has a single chord (nothing to follow)."""
        votes = np.zeros((size, 12))
        for back in range(min(P.GRID_LAPS * size, len(self.slots))):
            votes[(self.last_slot - back) % size] += self.slots[-1 - back]
        cycle = [self._read(v) for v in votes]
        for i in range(size):
            if cycle[i] == N_IDX:   # never played in these laps: the previous chord goes on
                cycle[i] = cycle[i - 1]
        if len(set(cycle)) < 2 or N_IDX in cycle:
            return False
        first = next(i for i in range(size) if cycle[i] != cycle[i - 1])
        now = self.last_slot + 1
        offset, start = (now - first) % size, 0
        self.known, self.lengths = [], []
        for k in range(size):
            c = cycle[(first + k) % size]
            if not self.known or self.known[-1] != c:
                self.known.append(c)
                self.lengths.append(0)
                if k <= offset:
                    self.pos, start = len(self.known) - 1, k
            self.lengths[-1] += P.SLOT_BEATS
        self.cycle = cycle
        self.seq = [[self.known[self.pos], (now - offset + start) * P.SLOT_BEATS]]
        return True

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
            # durations of the newest closed lap, rotated to `cyc`: the song's first chord may be cut short
            # (a two-bar tonic that closes the cycle shows up as one bar when the song starts on it)
            lens = self._lens(self.seq[-p - 1:])
            self.lengths = lens[-(k - 1):] + lens[:-(k - 1)]
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
