"""Streaming synthesizer (sample-accurate, stereo): worship pad, bass, metronome click; plus a MIDI writer.

Pad: 3 detuned band-limited saws per note (wavetable), open voicing, low-pass, slow attack, long release,
big stereo reverb (Freeverb-style). Bass: saturated sine (audible on small speakers). Soft limiter at the end.
"""
import struct

import numpy as np
from scipy.signal import butter, lfilter

from . import params as P

TABLE = 2048
_tables = {}


def _hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def _saw(f):
    """Band-limited saw wavetable for fundamental f (cached per harmonic count)."""
    nh = int(max(1, min(48, P.SR / 2 / f / 1.1)))
    if nh not in _tables:
        ph = np.arange(TABLE) / TABLE
        _tables[nh] = sum(np.sin(2 * np.pi * h * ph) / h for h in range(1, nh + 1)) * (2 / np.pi)
    return _tables[nh]


def _lookup(table, phase):
    x = (phase % 1.0) * TABLE
    i = x.astype(int)
    frac = x - i
    return table[i] * (1 - frac) + table[(i + 1) % TABLE] * frac


class _Comb:
    """y[n] = x[n] + g * lowpass(y[n-D]); processed in chunks <= D so it stays vectorized."""

    def __init__(self, D, g, damp):
        self.D, self.g, self.d = D, g, damp
        self.h = np.zeros(D + 1)          # y[-(D+1) .. -1]

    def __call__(self, x):
        out = []
        for s in range(0, len(x), self.D):
            xb = x[s:s + self.D]
            n = len(xb)
            yb = xb + self.g * ((1 - self.d) * self.h[1:n + 1] + self.d * self.h[:n])
            self.h = np.concatenate([self.h, yb])[-(self.D + 1):]
            out.append(yb)
        return np.concatenate(out)


class _Allpass:
    def __init__(self, D, g=0.5):
        self.D, self.g = D, g
        self.xh, self.yh = np.zeros(D), np.zeros(D)   # x[-D..-1], y[-D..-1]

    def __call__(self, x):
        out = []
        for s in range(0, len(x), self.D):
            xb = x[s:s + self.D]
            n = len(xb)
            yb = -self.g * xb + self.xh[:n] + self.g * self.yh[:n]
            self.xh = np.concatenate([self.xh, xb])[-self.D:]
            self.yh = np.concatenate([self.yh, yb])[-self.D:]
            out.append(yb)
        return np.concatenate(out)


class Reverb:
    """Freeverb-style stereo reverb (delays scaled from 44.1 kHz)."""
    COMBS = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
    ALLPASS = [556, 441, 341, 225]

    def __init__(self, room=P.REVERB_ROOM, damp=P.REVERB_DAMP, spread=23):
        k = P.SR / 44100
        self.ch = []
        for extra in (0, spread):
            combs = [_Comb(int((d + extra) * k), room, damp) for d in self.COMBS]
            aps = [_Allpass(int((d + extra) * k)) for d in self.ALLPASS]
            self.ch.append((combs, aps))

    def __call__(self, x):
        outs = []
        for combs, aps in self.ch:
            y = sum(c(x) for c in combs) * 0.12
            for a in aps:
                y = a(y)
            outs.append(y)
        return np.stack(outs, axis=1)


class Synth:
    def __init__(self):
        self.voices = []      # dicts: inst, pitch, vel, on, off, rel
        self.expr_from = self.expr_to = 1.0
        self.expr_t = 0.0
        self.b_lp, self.a_lp = butter(2, P.PAD_CUTOFF / (P.SR / 2))
        self.zi = np.zeros(2)
        self.reverb = Reverb()

    def handle(self, ev):
        if ev["type"] == "on":
            off = ev["t"] + 0.06 if ev["inst"] == "click" else None
            self.voices.append({"inst": ev["inst"], "pitch": ev["pitch"], "vel": ev["vel"], "on": ev["t"],
                                "off": off, "rel": None})
        elif ev["type"] == "off":
            for v in self.voices:
                if v["off"] is None and v["inst"] == ev["inst"] and v["pitch"] == ev["pitch"]:
                    v["off"], v["rel"] = ev["t"], ev.get("rel")
        elif ev["type"] == "expr":
            self.expr_from, self.expr_to, self.expr_t = self.expr_to, ev["value"], ev["t"]

    def _release(self, v):
        if v["rel"] is not None:
            return v["rel"]
        return {"bass": P.BASS_RELEASE, "pad": P.PAD_RELEASE, "click": 0.0}[v["inst"]]

    def render(self, t0, n):
        """(n, 2) stereo block starting at time t0."""
        t = t0 + np.arange(n) / P.SR
        bass, pad, click = np.zeros(n), np.zeros(n), np.zeros(n)
        alive = []
        for v in self.voices:
            rel = self._release(v)
            if v["off"] is not None and t0 > v["off"] + rel:
                continue
            alive.append(v)
            dt = t - v["on"]
            f = _hz(v["pitch"])
            amp = v["vel"] / 127
            if v["inst"] == "click":
                env = np.where((dt >= 0) & (dt < 0.06), np.exp(-np.maximum(dt, 0) / 0.012), 0.0)
                click += P.CLICK_LEVEL * amp * env * np.sin(2 * np.pi * (1500 if v["pitch"] == 77 else 1000) * dt)
                continue
            if v["inst"] == "bass":
                env = np.clip(dt / 0.008, 0, 1) * (0.7 + 0.3 * np.exp(-np.maximum(dt, 0) / 0.5))
                ph = 2 * np.pi * f * t
                osc = np.tanh(2.2 * np.sin(ph)) / np.tanh(2.2) + 0.25 * np.sin(2 * ph)
                target = bass
                level = P.BASS_LEVEL
            else:
                a = np.clip(dt / P.PAD_ATTACK, 0, 1)
                env = 0.5 - 0.5 * np.cos(np.pi * a)          # smooth swell
                tab = _saw(f)
                osc = sum(_lookup(tab, f * c * t) for c in (0.996, 1.0, 1.004)) / 3
                target = pad
                level = P.PAD_LEVEL
            if v["off"] is not None:
                r = np.clip((t - v["off"]) / max(rel, 1e-3), 0, 1)
                env = env * (1 - r) ** 2
            target += level * amp * env * osc
        self.voices = alive

        pad, self.zi = lfilter(self.b_lp, self.a_lp, pad, zi=self.zi)
        wet = self.reverb(pad)
        mix = (bass + P.PAD_DRY * pad)[:, None] + P.PAD_WET * wet
        a = np.clip((t - self.expr_t) / 0.05, 0, 1)
        gain = self.expr_from + (self.expr_to - self.expr_from) * a
        if a[-1] >= 1:
            self.expr_from = self.expr_to
        mix = np.tanh(mix * gain[:, None])                  # soft limiter: loud without harsh clipping
        return mix + click[:, None]


def _vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def write_midi(events, path):
    """Type-0 MIDI: bass ch 1 (finger bass), pad ch 2 (warm pad), click ch 10 (wood block), CC11 = expression."""
    tpq, us_per_q = 480, 500000          # 120 BPM: 960 ticks per second
    ch = {"bass": 0, "pad": 1, "click": 9}
    msgs = [(0, bytes([0xC0, 33])), (0, bytes([0xC1, 89]))]
    for e in events:
        tick = int(round(e["t"] * tpq * 1e6 / us_per_q))
        if e["type"] == "on":
            msgs.append((tick, bytes([0x90 | ch[e["inst"]], e["pitch"], max(1, e["vel"])])))
            if e["inst"] == "click":
                msgs.append((tick + 24, bytes([0x89, e["pitch"], 0])))
        elif e["type"] == "off":
            msgs.append((tick, bytes([0x80 | ch[e["inst"]], e["pitch"], 0])))
        else:
            v = int(round(127 * e["value"]))
            msgs += [(tick, bytes([0xB0, 11, v])), (tick, bytes([0xB1, 11, v]))]
    msgs.sort(key=lambda m: m[0])  # stable: offs emitted before ons at the same tick stay first
    data, last = bytearray(b"\x00\xFF\x51\x03" + us_per_q.to_bytes(3, "big")), 0
    for tick, m in msgs:
        data += _vlq(tick - last) + m
        last = tick
    data += b"\x00\xFF\x2F\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq) + b"MTrk" + struct.pack(">I", len(data)) + data)
