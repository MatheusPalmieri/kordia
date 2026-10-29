"""Decision engine: state machine, confidence ladder, stop rules, prediction scheduling."""
from collections import deque
from dataclasses import dataclass

import numpy as np

from . import params as P
from . import theory

OCIOSO, OUVINDO, TRAVADO, FALLBACK, ENCERRANDO = "OCIOSO", "OUVINDO", "TRAVADO", "FALLBACK", "ENCERRANDO"
STATES = [OCIOSO, OUVINDO, TRAVADO, FALLBACK, ENCERRANDO]
CONFIANTE, DUVIDA, PERDIDO = "CONFIANTE", "DUVIDA", "PERDIDO"
N_IDX = 24
FRAME = P.HOP / P.SR


@dataclass
class Output:
    chord: int | None   # vocabulary index (None with mode 'tonic' or silence)
    mode: str | None    # 'full' | 'root5' | 'tonic' | None = silence
    t: float            # when it takes effect


class Engine:
    def __init__(self, tracker, memory, key=None, metronome=False):
        self.tracker, self.memory, self.key, self.metronome = tracker, memory, key, metronome
        self.cur = (None, None)
        self._forget()

    def _forget(self):
        self.state = OCIOSO
        self.level = PERDIDO
        self.last_onset = None
        self.iois = deque(maxlen=8)
        self.pending = None          # metronome mode: (chord, time)
        self.doubt_since = None
        self.confirm_at = 0.0
        self.expected = self.prev_chord = None
        self.changed_at = 0.0
        self.misses = 0
        self.stop_at = None
        self.memory.reset()
        self.tracker.reset()

    # --- helpers ---
    def _bar(self):
        return P.BEATS_PER_BAR * self.tracker.period if self.tracker.period else 2.0

    def _set(self, chord, mode, t):
        if (chord, mode) == self.cur:
            return None
        self.cur = (chord, mode)
        return Output(chord, mode, t)

    def _stop_gap(self):
        ioi = min(max(self.iois, default=0.0), self._bar())
        base = P.STOP_BEATS * self.tracker.period if self.tracker.period else P.STOP_DEFAULT_SECONDS
        return max(base, P.STOP_IOI_FACTOR * ioi)

    def _change_time(self, t):
        """Musical time of a change heard now: the strum that started it (detection lags it)."""
        return self.last_onset if self.last_onset is not None and t - self.last_onset < 0.4 else t

    def _fallback_out(self, t):
        if self.key is None:
            return self._set(None, None, t)
        return self._set(None, "tonic", t)

    # --- main step ---
    def step(self, t, onset, idx, conf, evidence):
        """Called once per block. Returns an Output when the accompaniment must change, else None."""
        if idx == N_IDX or conf < P.CONF_LOW:
            self.level = PERDIDO if conf < P.CONF_LOW else DUVIDA
        else:
            self.level = CONFIANTE if conf >= P.CONF_HIGH else DUVIDA
        if onset is not None:
            if self.last_onset is not None:
                self.iois.append(onset.t - self.last_onset)
            self.last_onset = onset.t

        if self.state == OCIOSO:
            if self.level == CONFIANTE and self.last_onset is not None and t - self.last_onset < 1.0:
                self.state = OUVINDO
                return self._reactive_change(t, idx)
            return None

        gap = t - self.last_onset
        if self.state != ENCERRANDO and gap > self._stop_gap():
            self.state = ENCERRANDO
            self.pending = None
            end = self.tracker.next_downbeat(self.last_onset) if self.tracker.period else t
            self.stop_at = max(t, end)
        if self.state == ENCERRANDO:
            if onset is None:
                if gap > P.FORGET_BARS * self._bar():
                    out = self._set(None, None, t)
                    self._forget()
                    return out
                if t >= self.stop_at:
                    return self._set(None, None, max(t, self.stop_at))
                return None
            self.state = TRAVADO if self.memory.locked else OUVINDO   # resumed: it was a pause
            self.confirm_at = t + P.LOCK_CONFIRM_BEATS * self.tracker.period if self.tracker.period else t

        # confidence ladder
        if self.level == CONFIANTE:
            self.doubt_since = None
        elif self.doubt_since is None:
            self.doubt_since = t
        if self.state == FALLBACK:
            if self.level != CONFIANTE:
                return None
            self.state = OUVINDO
        elif self.doubt_since is not None and t - self.doubt_since > P.DOUBT_TO_FALLBACK_BARS * self._bar():
            self.state = FALLBACK
            self.memory.unlock()
            self.pending = None
            return self._fallback_out(t)

        if self.state == TRAVADO:
            return self._locked(t, idx)
        return self._reactive(t, idx, evidence)

    def _reactive(self, t, idx, evidence):
        if self.pending and t + FRAME >= self.pending[1]:
            chord, when = self.pending
            self.pending = None
            return self._set(chord, "full", max(t, when))
        if self.level == CONFIANTE:
            # a chord change is born from a strum: ignore drifts between strums (noise, voice, decay)
            fresh = self.last_onset is not None and t - self.last_onset < P.CHANGE_AFTER_ONSET
            if fresh and self.cur != (idx, "full") and not (self.pending and self.pending[0] == idx):
                return self._reactive_change(t, idx)
            return None
        # major/minor ambiguity on the same root: play root + fifth, no third
        # (if we already play one of the two, holding it is the safer choice and avoids retriggering)
        a, b = np.argsort(evidence[:24])[-2:]
        if (self.level == DUVIDA and a % 12 == b % 12 and evidence[a] + evidence[b] >= P.CONF_HIGH
                and self.cur[0] not in (a, b)):
            return self._set(int(b), "root5", t)
        return None

    def _reactive_change(self, t, idx):
        tc = self._change_time(t)
        if self.tracker.period:
            self.tracker.observe_change(tc)
            self.memory.push(idx, int(round(self.tracker.beat_phase(tc))))
            if self.memory.locked:
                self.state = TRAVADO
                self.confirm_at = t + P.LOCK_CONFIRM_BEATS * self.tracker.period
                self.expected = None
                self.misses = 0
        if self.metronome and self.tracker.period:
            ph = self.tracker.beat_phase(tc)
            k = int(np.floor(ph))
            while self.tracker.bar_pos(k) % 2:
                k -= 1
            if ph - k > P.METRO_LATE_BEATS:     # early (or mid-bar): wait for the next strong beat
                k += 2
                self.pending = (idx, self.tracker.time_of_beat(k))
                return None
        self.pending = None
        return self._set(idx, "full", t)

    def _locked(self, t, idx):
        pred = self.memory.predict()
        period = self.tracker.period
        if pred:
            nxt, beat = pred
            tc = self.tracker.time_of_beat(beat)
            # the musician changed early: follow (except in metronome mode, where the grid rules)
            early = (not self.metronome and self.level == CONFIANTE and idx == nxt and self.cur[0] != nxt
                     and t >= self.confirm_at)
            if t + FRAME >= tc or early:
                when = t if early else max(t, tc)
                self.memory.advance(beat)
                self.confirm_at = when + P.LOCK_CONFIRM_BEATS * period + 0.1
                self.expected, self.prev_chord, self.changed_at = nxt, self.cur[0], when
                return self._set(nxt, "full", when)
        if t < self.confirm_at:
            return None
        # still hearing the previous chord shortly after a predicted change = musician is late, not a contradiction
        late = idx == self.prev_chord and t < self.changed_at + P.LOCK_LATE_BEATS * period
        if self.level == CONFIANTE and (idx, "full") != self.cur and not late:
            # strong disagreement: the audio wins, forget the lock and relearn
            self.memory.correct(idx)
            self.memory.unlock()
            self.state = OUVINDO
            self.tracker.observe_change(self._change_time(t))
            return self._set(idx, "full", t)
        if self.expected is not None:
            if idx == self.expected:
                self.misses = 0
            elif not late:
                self.misses += 1
                if self.misses >= P.LOCK_MAX_MISSES:
                    self.memory.unlock()
                    self.state = OUVINDO
            self.expected = None
        return None
