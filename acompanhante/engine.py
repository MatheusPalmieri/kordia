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
    def __init__(self, tracker, memory, key=None, metronome=False, lookahead=0.0):
        self.tracker, self.memory, self.key, self.metronome = tracker, memory, key, metronome
        self.lookahead = lookahead   # live: audio output latency; scheduled events are emitted this much earlier
        self.hold = False            # "manter o padrão": once locked, trust the pattern over the audio (live setting)
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
        self.contra = None
        self.contra_since = 0.0
        self.disagree_since = None   # hold mode: since when the audio confidently disagrees with the pattern
        self.root5_tc = None         # when a 'no third' chord started (its full version keeps that start)
        self.last_change_t = -np.inf
        self.recent = deque(maxlen=8)   # last chords played: does the configured key match the music?
        self.reason = ""
        self.stop_at = None
        self.memory.reset()
        self.tracker.reset()

    # --- helpers ---
    def _bar(self):
        return P.BEATS_PER_BAR * self.tracker.period if self.tracker.period else 2.0

    def _set(self, chord, mode, t, reason=None):
        if (chord, mode) == self.cur:
            return None
        self.cur = (chord, mode)
        if mode == "full":
            self.recent.append(chord)
        if reason is not None:
            self.reason = reason
        return Output(chord, mode, t)

    def _held(self):
        return self.hold and self.memory.locked

    def _stop_gap(self):
        if self._held() and self.tracker.period:
            return P.HOLD_STOP_BARS * self._bar()   # a late entry is not the end of the song
        ioi = min(max(self.iois, default=0.0), self._bar())
        base = P.STOP_BEATS * self.tracker.period if self.tracker.period else P.STOP_DEFAULT_SECONDS
        return max(base, P.STOP_IOI_FACTOR * ioi)

    def _change_time(self, t):
        """Musical time of a change heard now: the strum that started it (detection lags it)."""
        return self.last_onset if self.last_onset is not None and t - self.last_onset < 0.4 else t

    def _fallback_out(self, t):
        if self.key is None:
            return self._set(None, None, t, "perdido sem tom: silêncio")
        d = theory.diatonic(theory.note_pc(self.key))
        if sum(c in d for c in self.recent) * 2 < len(self.recent):
            # what we heard is mostly outside the configured key: its tonic would be a wrong note
            return self._set(None, None, t, "perdido; o tom configurado não bate com o que ouvi: silêncio")
        return self._set(None, "tonic", t, "perdido: pad na tônica")

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
                if gap > P.FORGET_BARS * self._bar():   # (> HOLD_STOP_BARS: a held pattern stops before it forgets)
                    out = self._set(None, None, t, "silêncio longo: fim")
                    self._forget()
                    self.reason = "silêncio longo: esqueci padrão e andamento"
                    return out
                if t + self.lookahead >= self.stop_at:
                    return self._set(None, None, max(t, self.stop_at), "você parou: fim do compasso")
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
        elif (self.doubt_since is not None and t - self.doubt_since > P.DOUBT_TO_FALLBACK_BARS * self._bar()
              and not (self._held() and self.state == TRAVADO)):   # a held pattern beats the tonic
            self.state = FALLBACK
            self.memory.unlock()
            self.reason = "dúvida por mais de 1 compasso"
            self.pending = None
            return self._fallback_out(t)

        if self.state == TRAVADO:
            return self._locked(t, idx)
        return self._reactive(t, idx, evidence)

    def _held_check(self, t, idx, late):
        """Hold mode: mistakes, late entries and doubts never break the pattern; only a confident disagreement
        sustained for HOLD_CONTRA_BARS (the musician really moved on) makes it relearn."""
        if self.level != CONFIANTE or idx == N_IDX:
            return None
        if (idx, "full") == self.cur or late:
            self.disagree_since = None
            return None
        if self.disagree_since is None:
            self.disagree_since = t
            return None
        if t - self.disagree_since < P.HOLD_CONTRA_BARS * self._bar():
            return None
        self.disagree_since = None
        self.memory.correct(idx)
        self.memory.unlock()
        self.state = OUVINDO
        self.tracker.observe_change(self._change_time(t))
        return self._set(idx, "full", t, f"você mudou de verdade (2 compassos diferentes do padrão): ouvi {theory.LABELS[idx]}, reaprendendo")

    def _reactive(self, t, idx, evidence):
        if self.pending and t + self.lookahead + FRAME >= self.pending[1]:
            chord, when = self.pending
            self.pending = None
            return self._set(chord, "full", max(t, when), "metrônomo: troca no tempo forte")
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
            self.root5_tc = self._change_time(t)
            return self._set(int(b), "root5", t, f"dúvida {theory.LABELS[a]}/{theory.LABELS[b]}: sem terça")
        return None

    def _reactive_change(self, t, idx):
        tc = self._change_time(t)
        if self.cur[1] == "root5" and self.cur[0] is not None and self.cur[0] % 12 == idx % 12 and self.root5_tc is not None:
            tc = self.root5_tc   # same chord, now with its third: it started when the root did
        if self.tracker.period:
            self.tracker.observe_change(tc)
            # changes land on (or just before) a beat; detection lag and off-beat strums only push them later
            blip = tc - self.last_change_t < self.tracker.period
            self.memory.push(idx, int(np.floor(self.tracker.beat_phase(tc) + 0.3)), blip)
            self.last_change_t = tc
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
                self.reason = f"metrônomo: {theory.LABELS[idx]} agendado para o tempo forte"
                return None
        self.pending = None
        reason = f"ouvi {theory.LABELS[idx]}" + (" — padrão reconhecido, travei" if self.state == TRAVADO else "")
        return self._set(idx, "full", t, reason)

    def _locked(self, t, idx):
        pred = self.memory.predict()
        period = self.tracker.period
        if pred:
            nxt, beat = pred
            tc = self.tracker.time_of_beat(beat)
            # the musician changed early: follow (except in metronome mode, where the grid rules)
            early = (not self.metronome and self.level == CONFIANTE and idx == nxt and self.cur[0] != nxt
                     and t >= self.confirm_at and t >= tc - period)
            if t + self.lookahead + FRAME >= tc or early:
                when = t if early else max(t, tc)
                if early:   # the musician is ahead of the stored grid: learn the real start (fixes a systematic offset)
                    beat = int(np.floor(self.tracker.beat_phase(self._change_time(t)) + 0.3))
                self.memory.advance(beat)
                self.confirm_at = when + P.LOCK_CONFIRM_BEATS * period + 0.1
                self.expected, self.prev_chord, self.changed_at = nxt, self.cur[0], when
                self.contra = None
                return self._set(nxt, "full", when, "previsto pelo padrão" + (" (você antecipou)" if early else ""))
        if t < self.confirm_at:
            return None
        # still hearing the previous chord shortly after a predicted change = musician is late, not a contradiction
        late = idx == self.prev_chord and t < self.changed_at + P.LOCK_LATE_BEATS * period
        if self.hold:
            return self._held_check(t, idx, late)
        if self.level == CONFIANTE and idx != N_IDX and (idx, "full") != self.cur and not late:
            # a confident disagreement must persist before it overrides the pattern (one bad frame is noise)
            if self.contra != idx:
                self.contra, self.contra_since = idx, t
            elif t - self.contra_since >= P.LOCK_CONTRA_BEATS * period:
                self.contra = None
                if self.cur[0] is not None and idx % 12 == self.cur[0] % 12:
                    # same root, other third (A vs Am): keep the pattern, drop the third
                    return self._set(self.cur[0], "root5", t, f"dúvida maior/menor ({theory.LABELS[idx]}): sem terça")
                # the audio wins: forget the lock and relearn
                self.memory.correct(idx)
                self.memory.unlock()
                self.state = OUVINDO
                self.tracker.observe_change(self._change_time(t))
                return self._set(idx, "full", t, f"contradição sustentada: ouvi {theory.LABELS[idx]}, destravei")
        else:
            self.contra = None
        if self.expected is not None:
            if idx == self.expected:
                self.misses = 0
            elif not late and idx != N_IDX and self.level != PERDIDO:
                self.misses += 1
                if self.misses >= P.LOCK_MAX_MISSES:
                    self.memory.unlock()
                    self.state = OUVINDO
                    self.reason = "várias discordâncias fracas: destravei"
            self.expected = None
        return None
