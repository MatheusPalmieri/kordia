"""'When to change' layer: adaptive beat tracker (ACF + PLL) and a fixed metronome grid.

Both expose the same interface: period, confidence, update(), beat_phase(), time_of_beat(),
next_downbeat(), is_strong(), observe_change(), reset().
"""
from collections import Counter, deque

import numpy as np

from . import params as P

FRAME = P.HOP / P.SR


class _Grid:
    """Shared beat-grid bookkeeping: next_beat time, its integer index, bar offset."""

    def reset(self):
        self.period = None
        self.next_beat = None
        self.next_index = 0
        self.bar_offset = 0
        self.first_onset = None
        self.onsets = deque(maxlen=64)
        self.hits = deque(maxlen=P.TEMPO_CONF_BEATS)
        self.pending = deque()       # beats waiting to be scored as hit/miss
        self.change_votes = deque(maxlen=8)

    @property
    def bpm(self):
        return 60.0 / self.period if self.period else None

    @property
    def confidence(self):
        return sum(self.hits) / len(self.hits) if self.hits else 0.0

    def beat_phase(self, t):
        """Continuous beat count at time t (integer = on a beat)."""
        return self.next_index - (self.next_beat - t) / self.period

    def time_of_beat(self, idx):
        return self.next_beat + (idx - self.next_index) * self.period

    def bar_pos(self, idx):
        return (idx - self.bar_offset) % P.BEATS_PER_BAR

    def next_downbeat(self, t):
        idx = int(np.floor(self.beat_phase(t) + 0.25)) + 1   # a strum slightly early still belongs to its beat
        while self.bar_pos(idx) != 0:
            idx += 1
        return self.time_of_beat(idx)

    def is_strong(self, t, tol=0.25):
        """t is within tol beats of beat 1 or 3."""
        ph = self.beat_phase(t)
        idx = int(round(ph))
        return abs(ph - idx) <= tol and self.bar_pos(idx) % 2 == 0

    def observe_change(self, t):
        """Chord changes tend to land on beat 1: use them to fix the bar phase."""
        if self.period is None:
            return
        ph = self.beat_phase(t)
        idx = int(round(ph))
        if abs(ph - idx) > 0.3:
            return
        self.change_votes.append((idx - self.bar_offset) % P.BEATS_PER_BAR)
        pos, n = Counter(self.change_votes).most_common(1)[0]
        if pos != 0 and n >= 3:
            self.bar_offset = (self.bar_offset + pos) % P.BEATS_PER_BAR
            self.change_votes.clear()

    def _advance(self, t):
        """Move the grid past t; score beats once their window has passed. Returns crossed beats [(time, index)]."""
        crossed = []
        while self.next_beat is not None and t >= self.next_beat:
            crossed.append((self.next_beat, self.next_index))
            self.pending.append(self.next_beat)
            self.next_beat += self.period
            self.next_index += 1
        win = P.PLL_WINDOW * self.period if self.period else 0
        while self.pending and t >= self.pending[0] + win:
            b = self.pending.popleft()
            self.hits.append(any(abs(o - b) <= win for o in self.onsets))
        return crossed

    def _start(self, t0, period):
        self.period = period
        k = int(np.ceil((self.now - t0) / period))
        self.next_beat = t0 + k * period
        self.next_index = int(round((self.next_beat - self.first_onset) / period))


class TempoTracker(_Grid):
    def __init__(self):
        self.reset()

    def reset(self):
        super().reset()
        self.env = deque(maxlen=int(P.TEMPO_ENV_SECONDS / FRAME))
        self.frame = 0
        self.disagree = 0
        self.now = 0.0

    def update(self, t, flux, onset=None):
        self.now = t
        self.env.append(flux)
        self.frame += 1
        if onset is not None:
            if self.first_onset is None:
                self.first_onset = onset.t
            self.onsets.append(onset.t)
            if self.period:
                self._pll(onset.t)
        if self.frame % P.TEMPO_ACF_EVERY == 0 and len(self.onsets) >= P.TEMPO_MIN_ONSETS:
            self._acf()
        return self._advance(t)

    def _pll(self, to):
        prev = self.next_beat - self.period
        nearest = prev if abs(to - prev) < abs(to - self.next_beat) else self.next_beat
        e = to - nearest
        if abs(e) > P.PLL_WINDOW * self.period:
            return  # off-beat strum: ignored
        self.next_beat += P.PLL_ALPHA * e
        self.period = float(np.clip(self.period + P.PLL_BETA * e, 60 / P.TEMPO_BPM_MAX, 60 / P.TEMPO_BPM_MIN))

    def _acf(self):
        """Tempo from autocorrelation of the onset envelope, weighted by a log-normal prior."""
        e = np.asarray(self.env)
        recent = [o for o in self.onsets if o > self.now - P.TEMPO_ENV_SECONDS]
        if len(recent) < P.TEMPO_MIN_ONSETS:
            return
        e = e - e.mean()
        n = len(e)
        lmin = int(60 / P.TEMPO_BPM_MAX / FRAME)
        lmax = min(int(60 / P.TEMPO_BPM_MIN / FRAME), n // 2)
        if lmax <= lmin + 2:
            return
        acf = np.array([np.dot(e[:n - lag], e[lag:]) for lag in range(lmin, lmax + 1)])
        lags = np.arange(lmin, lmax + 1)
        bpm = 60 / (lags * FRAME)
        w = np.exp(-0.5 * (np.log2(bpm / P.TEMPO_PRIOR_BPM) / P.TEMPO_PRIOR_OCTAVES) ** 2)
        score = np.maximum(acf, 0) * w
        i = int(np.argmax(score))
        if score[i] <= 0:
            return
        # parabolic interpolation on the raw ACF for sub-frame lag
        lag = float(lags[i])
        if 0 < i < len(acf) - 1:
            a, b, c = acf[i - 1], acf[i], acf[i + 1]
            denom = a - 2 * b + c
            if denom < 0:
                lag += 0.5 * (a - c) / denom
        period = lag * FRAME
        if self.period is None:
            self._start(self._best_phase(recent, period), period)
        elif abs(period - self.period) / self.period > P.TEMPO_RESET_DEV:
            self.disagree += 1
            if self.disagree >= 3:
                self.period, self.disagree = period, 0
        else:
            self.disagree = 0

    def _best_phase(self, recent, period):
        """Onset that best explains the others as lying on its beat grid (downstrokes are on the beat)."""
        r = np.asarray(recent)
        best, best_s = r[0], -1
        for o in r:
            d = (r - o) / period
            s = np.sum(np.abs(d - np.round(d)) < 0.12)
            if s > best_s:
                best, best_s = o, s
        return best


class FixedGrid(_Grid):
    """Metronome mode: fixed BPM, grid starts at the first onset (musician starts on beat 1)."""

    def __init__(self, bpm):
        self.fixed_period = 60.0 / bpm
        self.reset()

    def reset(self):
        super().reset()
        self.now = 0.0

    @property
    def confidence(self):
        return 1.0 if self.period else 0.0

    def update(self, t, flux, onset=None):
        self.now = t
        if onset is not None:
            if self.first_onset is None:
                self.first_onset = onset.t
                self._start(onset.t, self.fixed_period)
            self.onsets.append(onset.t)
        return self._advance(t)
