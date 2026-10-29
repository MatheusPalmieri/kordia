"""Causal onset (strum) detector: spectral flux with an adaptive threshold."""
from collections import deque
from dataclasses import dataclass

import numpy as np

from . import params as P


@dataclass
class Onset:
    t: float        # seconds
    db: float       # loudness of the strum (frame RMS, dBFS)
    flux: float


class OnsetDetector:
    def __init__(self):
        f = np.fft.rfftfreq(P.ONSET_WIN, 1 / P.SR)
        self.band = (f >= P.ONSET_FMIN) & (f <= P.ONSET_FMAX)
        self.prev = None
        self.hist = deque(maxlen=P.ONSET_MEDIAN_FRAMES)
        self.last_t = -np.inf
        self.prev_flux = 0.0
        self.flux = 0.0

    def process(self, mag, t, rms_db):
        """mag: normalized magnitude spectrum of the last ONSET_WIN samples. Returns Onset or None."""
        lm = np.log1p(P.CHROMA_GAMMA * mag[self.band])
        flux = 0.0 if self.prev is None else float(np.sum(np.maximum(lm - self.prev, 0.0))) / 10.0
        self.prev = lm
        thr = (np.median(self.hist) if self.hist else 0.0) * P.ONSET_K + P.ONSET_DELTA
        self.hist.append(flux)
        rising = flux > self.prev_flux
        self.prev_flux, self.flux = flux, flux
        if flux > thr and rising and rms_db > P.SILENCE_DB and t - self.last_t >= P.ONSET_REFRACTORY:
            self.last_t = t
            return Onset(t, rms_db, flux)
        return None
