"""'Which chord' layer: chroma -> template likelihoods -> causal HMM forward filter."""
import numpy as np

from . import params as P
from . import theory


class ChordTracker:
    def __init__(self, key=None):
        f = np.fft.rfftfreq(P.CHROMA_WIN, 1 / P.SR)
        self.band = (f >= P.CHROMA_FMIN) & (f <= P.CHROMA_FMAX)
        midi = np.round(69 + 12 * np.log2(f[self.band] / 440.0)).astype(int)
        self.midi = midi - midi.min()
        self.pc = (np.arange(self.midi.max() + 1) + midi.min()) % 12
        self.T = theory.templates()
        self.prior = theory.key_prior(key)
        self.post = self.prior.copy()
        self.evidence = np.full(25, 1 / 25)
        self.last_chroma = np.zeros(12)

    def chroma(self, mag):
        """12-bin chroma: max compressed magnitude per semitone, folded to pitch classes."""
        semis = np.zeros(self.midi.max() + 1)
        np.maximum.at(semis, self.midi, np.log1p(P.CHROMA_GAMMA * mag[self.band]))
        return np.bincount(self.pc, weights=semis, minlength=12)

    def scores(self, mag, silent=False):
        """Cosine score per state (24 chords + N)."""
        c = self.last_chroma = self.chroma(mag)
        norm = np.linalg.norm(c)
        s = np.zeros(25)
        if not silent and norm > 1e-6:
            s[:24] = self.T @ (c / norm)
        s[24] = P.HMM_N_SCORE
        return s

    def process(self, mag, silent=False, p_change=P.HMM_P_CHANGE):
        """Returns (chord index, confidence, posterior)."""
        s = self.scores(mag, silent)
        like = np.exp(P.HMM_BETA * (s - s.max()))
        pred = (1 - p_change) * self.post + p_change * self.prior
        post = pred * like
        self.post = post / post.sum()
        frame = np.exp(P.CONF_BETA * (s - s.max()))
        self.evidence = (1 - P.CONF_SMOOTH) * self.evidence + P.CONF_SMOOTH * frame / frame.sum()
        idx = int(np.argmax(self.post))
        return idx, float(self.evidence[idx]), self.post
