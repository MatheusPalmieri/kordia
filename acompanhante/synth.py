"""Streaming bass/pad synthesizer (sample-accurate) and a minimal MIDI file writer."""
import struct

import numpy as np

from . import params as P

BASS_REL, PAD_ATT, PAD_REL = 0.12, 0.35, P.FADE_TIME


def _hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


class Synth:
    def __init__(self):
        self.voices = []      # dicts: inst, pitch, vel, on, off
        self.expr_from = self.expr_to = 1.0
        self.expr_t = 0.0

    def handle(self, ev):
        if ev["type"] == "on":
            self.voices.append({"inst": ev["inst"], "pitch": ev["pitch"], "vel": ev["vel"], "on": ev["t"], "off": None})
        elif ev["type"] == "off":
            for v in self.voices:
                if v["off"] is None and v["inst"] == ev["inst"] and v["pitch"] == ev["pitch"]:
                    v["off"] = ev["t"]
        elif ev["type"] == "expr":
            self.expr_from, self.expr_to, self.expr_t = self.expr_to, ev["value"], ev["t"]

    def render(self, t0, n):
        t = t0 + np.arange(n) / P.SR
        out = np.zeros(n)
        alive = []
        for v in self.voices:
            rel = BASS_REL if v["inst"] == "bass" else PAD_REL
            if v["off"] is not None and t0 > v["off"] + rel:
                continue
            alive.append(v)
            dt = t - v["on"]
            if v["inst"] == "bass":
                env = np.clip(dt / 0.005, 0, 1) * (0.55 + 0.45 * np.exp(-np.maximum(dt, 0) / 0.6))
                ph = 2 * np.pi * _hz(v["pitch"]) * t
                osc = 0.12 * (np.sin(ph) + 0.3 * np.sin(2 * ph) + 0.1 * np.sin(3 * ph))
            else:
                env = np.clip(dt / PAD_ATT, 0, 1)
                f = _hz(v["pitch"])
                osc = sum(0.012 / h * (np.sin(2 * np.pi * h * f * 1.002 * t) + np.sin(2 * np.pi * h * f * 0.998 * t))
                          for h in range(1, 6))
            if v["off"] is not None:
                env = env * np.clip(1 - (t - v["off"]) / rel, 0, 1)
            out += env * (v["vel"] / 127) * osc
        self.voices = alive
        # expression: smooth ramp towards the target over ~50 ms
        a = np.clip((t - self.expr_t) / 0.05, 0, 1)
        gain = self.expr_from + (self.expr_to - self.expr_from) * a
        if a[-1] >= 1:
            self.expr_from = self.expr_to
        return out * gain


def _vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def write_midi(events, path):
    """Type-0 MIDI: bass on channel 1 (finger bass), pad on channel 2 (warm pad), expression as CC11."""
    tpq, us_per_q = 480, 500000          # 120 BPM: 960 ticks per second
    ch = {"bass": 0, "pad": 1}
    msgs = [(0, bytes([0xC0, 33])), (0, bytes([0xC1, 89]))]
    for e in events:
        tick = int(round(e["t"] * tpq * 1e6 / us_per_q))
        if e["type"] == "on":
            msgs.append((tick, bytes([0x90 | ch[e["inst"]], e["pitch"], max(1, e["vel"])])))
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
