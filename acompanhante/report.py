"""One PNG per run: waveform + onsets, tempo, chords (truth / heard / played), state, dynamics."""
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from . import params as P
from . import theory
from .engine import STATES

_CMAP = plt.get_cmap("tab20")


def _color(label):
    if label in (None, "N", "P"):
        return "#dddddd"
    return _CMAP(theory.chord_index(label) % 20)


def _lane(ax, y, segs):
    for s, e, lab in segs:
        if e <= s:
            continue
        ax.broken_barh([(s, e - s)], (y - 0.4, 0.8), color=_color(lab), edgecolor="white", linewidth=0.3)
        if lab not in (None, "N") and e - s > 0.8:
            ax.text((s + e) / 2, y, lab, ha="center", va="center", fontsize=6)


def _segments(times, labels, end):
    segs = []
    for i, (t, l) in enumerate(zip(times, labels)):
        nxt = times[i + 1] if i + 1 < len(times) else end
        segs.append((t, nxt, l))
    return segs


def plot(res, path, truth=None, title=""):
    t = res.log["t"]
    end = t[-1]
    fig, ax = plt.subplots(5, 1, figsize=(14, 10), sharex=True, gridspec_kw={"height_ratios": [1.2, 1, 1.4, 0.8, 0.8]})
    # 1 waveform + onsets
    hop = max(1, len(res.audio) // 4000)
    env = np.abs(res.audio[: len(res.audio) // hop * hop]).reshape(-1, hop).max(axis=1)
    ax[0].fill_between(np.arange(len(env)) * hop / P.SR, -env, env, color="#888", lw=0)
    for ot, _ in res.onsets:
        ax[0].axvline(ot, color="tab:red", lw=0.4, alpha=0.6)
    if truth:
        for d in truth["downbeats"]:
            ax[0].axvline(d, color="tab:blue", lw=0.6, ls=":")
    ax[0].set_ylabel("áudio\n(onsets)")
    # 2 tempo
    ax[1].plot(t, res.log["bpm"], label="estimado", color="tab:orange")
    if truth:
        tt, tb = np.array(truth["tempo"]).T
        ax[1].plot(tt, tb, label="real", color="k", lw=1, ls="--")
    ax[1].set_ylabel("BPM")
    ax[1].legend(fontsize=7, loc="upper left")
    # 3 chords: truth / heard / played
    heard_changes = np.flatnonzero(np.diff(np.concatenate([[-1], res.log["heard"]])))
    heard = _segments(t[heard_changes], [theory.LABELS[i] for i in res.log["heard"][heard_changes]], end)
    names = {"full": "{}", "root5": "{}5"}   # root5 = no third; 'tonic' fallback and silence are drawn grey
    played = _segments([c[0] for c in res.changes],
                       [names[c[2]].format(c[1]) if c[2] in names else None for c in res.changes], end)
    lanes = [(2, "verdade", [tuple(c) for c in truth["chords"]] if truth else []), (1, "ouvido", heard), (0, "tocado", played)]
    for y, name, segs in lanes:
        _lane(ax[2], y, segs)
    for c in res.changes:
        ax[2].axvline(c[0], ymax=0.33, color="k", lw=0.5)
    ax[2].set_yticks([0, 1, 2], ["tocado", "ouvido", "verdade"])
    ax[2].set_ylim(-0.6, 2.6)
    # 4 state machine + confidence
    ax[3].step(t, res.log["state"], where="post", color="tab:purple")
    ax[3].set_yticks(range(len(STATES)), STATES, fontsize=7)
    ax3b = ax[3].twinx()
    ax3b.plot(t, res.log["conf"], color="tab:green", lw=0.5, alpha=0.7)
    ax3b.set_ylim(0, 1.05)
    ax3b.set_ylabel("confiança", fontsize=7)
    # 5 dynamics
    ax[4].plot(t, res.log["vel"] / 127, label="velocity", color="tab:brown")
    ax[4].plot(t, res.log["expr"], label="expressão", color="tab:gray", lw=0.8)
    if truth and truth["intensity"]:
        it, ig = np.array(truth["intensity"]).T
        ax[4].step(it, ig, where="post", label="intensidade real", color="k", lw=0.8, ls="--")
    ax[4].set_ylim(0, 1.1)
    ax[4].legend(fontsize=7, loc="lower left")
    ax[4].set_xlabel("tempo (s)")
    fig.suptitle(title, fontsize=10)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
