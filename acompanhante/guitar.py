"""Synthetic acoustic guitar (Karplus-Strong). Only used to build test scenarios, so it may be offline."""
import numpy as np
from scipy.signal import lfilter

from .params import SR


def pluck(midi, dur, amp=1.0, bright=1.0, rng=None, t60=None):
    """One plucked string. Karplus-Strong with an allpass for exact tuning, as one IIR (lfilter)."""
    rng = rng or np.random.default_rng(0)
    f = 440.0 * 2 ** ((midi - 69) / 12)
    D = SR / f
    N = int(np.floor(D - 0.5 - 0.1))
    frac = D - 0.5 - N
    c = (1 - frac) / (1 + frac)
    t60 = t60 or float(np.interp(midi, [40, 76], [3.5, 1.2]))
    d = 0.001 ** (1.0 / (f * t60))
    # H = Da / (Da - d z^-N L Na), L = (1+z^-1)/2, allpass A = (c + z^-1)/(1 + c z^-1)
    a = np.zeros(N + 3)
    a[0], a[1] = 1.0, c
    a[N:N + 3] -= d * np.array([0.5 * c, 0.5 * c + 0.5, 0.5])
    n = int(dur * SR)
    x = np.zeros(n)
    burst = rng.uniform(-1, 1, min(N, n))
    burst -= burst.mean()  # DC would ring forever in the loop
    # softer strums are darker: one-pole lowpass on the excitation
    burst = lfilter([bright], [1, -(1 - bright)], burst)
    x[:len(burst)] = burst
    return amp * lfilter([1.0, c], a, x)


def muted_strum(dur=0.08, amp=1.0, rng=None):
    """Percussive 'chk': band-limited noise with a fast decay, no clear pitch."""
    rng = rng or np.random.default_rng(0)
    n = int(dur * SR)
    noise = rng.standard_normal(n)
    noise = lfilter([1, 0, -1], [1, -1.6, 0.7], noise)  # crude band-pass ~ 300-2500 Hz
    return amp * 0.15 * noise * np.exp(-np.arange(n) / (0.025 * SR))


class Guitar:
    """Renders strums into a buffer. Each string is cut when re-plucked or muted, like a real guitar."""

    def __init__(self, duration, seed=0, cents=0.0):
        """cents: std of each string's tuning error (own generator, so cents=0 keeps every scenario identical)."""
        self.out = np.zeros(int(duration * SR) + SR)
        self.rng = np.random.default_rng(seed)
        self.tuning = np.random.default_rng(seed + 1).normal(0, cents / 100, 6)
        self.ringing = {}  # string index -> (start sample, rendered array)

    def _cut(self, string, at):
        if string in self.ringing:
            s0, y = self.ringing.pop(string)
            k = max(0, at - s0)
            fade = min(220, len(y) - k)
            if fade > 0:
                self.out[s0 + k:s0 + k + fade] -= y[k:k + fade] * np.linspace(0, 1, fade)
                self.out[s0 + k + fade:s0 + len(y)] -= y[k + fade:]

    def mute(self, t):
        at = int(t * SR)
        for s in list(self.ringing):
            self._cut(s, at)

    def _add(self, string, t, y):
        at = int(t * SR)
        self._cut(string, at)
        y = y[:len(self.out) - at]
        self.out[at:at + len(y)] += y
        self.ringing[string] = (at, y)

    def strum(self, t, notes, direction="D", amp=1.0, spread=0.012):
        """direction 'D' = low to high over all strings; 'U' = high to low over the top 3-4."""
        strings = list(range(6 - len(notes), 6))
        pairs = list(zip(strings, notes))
        if direction == "U":
            pairs = pairs[::-1][: self.rng.integers(3, 5)]
        bright = float(np.clip(0.35 + 0.6 * amp, 0.2, 1.0))
        for i, (s, m) in enumerate(pairs):
            a = amp * (0.8 if direction == "U" else 1.0) * self.rng.uniform(0.85, 1.0)
            self._add(s, t + i * spread * self.rng.uniform(0.7, 1.3),
                      pluck(m + self.tuning[s], 4.0, 0.3 * a, bright, self.rng))

    def single(self, t, midi, amp=1.0):
        string = 0 if midi < 45 else 1
        self._add(string, t, pluck(midi, 3.0, 0.35 * amp, 0.8, self.rng))

    def chk(self, t, amp=1.0):
        self.mute(t)
        y = muted_strum(amp=amp, rng=self.rng)
        at = int(t * SR)
        self.out[at:at + len(y)] += y[:len(self.out) - at]
