"""Compare a pipeline Result against the scenario ground truth."""
import numpy as np

from .engine import CONFIANTE, OCIOSO, STATES

DT = 0.01
END_TOL = 0.3      # s of grace after a true end (note-off lands on the bar line, +/- tracking error)


def _truth_labels(truth, g):
    starts = np.array([s for s, e, l in truth["chords"]])
    labs = np.array([l for s, e, l in truth["chords"]], dtype=object)
    return labs[np.clip(np.searchsorted(starts, g, side="right") - 1, 0, len(labs) - 1)]


def _timeline(changes, g):
    lab = np.full(len(g), None, dtype=object)
    mode = np.full(len(g), None, dtype=object)
    if changes:
        times = np.array([c[0] for c in changes])
        j = np.searchsorted(times, g, side="right") - 1
        ok = j >= 0
        lab[ok] = np.array([c[1] for c in changes], dtype=object)[j[ok]]
        mode[ok] = np.array([c[2] for c in changes], dtype=object)[j[ok]]
    return lab, mode


def _runs(mask):
    """Lengths (in samples) of consecutive True runs."""
    d = np.diff(np.concatenate([[0], mask.astype(int), [0]]))
    return np.flatnonzero(d == -1) - np.flatnonzero(d == 1)


def evaluate(res, truth):
    g = np.arange(0, truth["duration"], DT)
    tl = _truth_labels(truth, g)
    lab, mode = _timeline(res.changes, g)
    li = np.clip(np.searchsorted(res.log["t"], g), 0, len(res.log["t"]) - 1)
    conf = res.log["level"][li] == CONFIANTE
    state = res.log["state"][li]
    tt, tb = np.array(truth["tempo"]).T
    bpm_true = np.interp(g, tt, tb)
    beat = 60.0 / bpm_true
    is_chord = np.array([l not in ("N", "P") for l in tl])
    sounding = np.array([m is not None for m in mode])
    correct = is_chord & (lab == tl) & (mode == "full")
    m = {}
    m["acc_total"] = correct[is_chord].mean()
    m["acc_confiante"] = correct[is_chord & conf].mean() if (is_chord & conf).any() else np.nan
    m["acc_nao_confiante"] = correct[is_chord & ~conf].mean() if (is_chord & ~conf).any() else np.nan
    m["cobertura"] = sounding[is_chord].mean()
    m["tempo_nao_confiante"] = (~conf)[is_chord].mean()

    # change latency
    lats, lat_beats, missed = [], [], 0
    for c in truth["changes"]:
        b = 60.0 / np.interp(c["t"], tt, tb)
        cand = [ct for ct, l, md in res.changes if c["t"] - b <= ct <= c["t"] + 2 * b and l == c["to"] and md == "full"]
        if not cand:
            missed += 1
            continue
        lats.append(cand[0] - c["t"])
        lat_beats.append((cand[0] - c["t"]) / b)
    lats = np.array(lats) * 1000
    m["lat_media_ms"] = lats.mean() if len(lats) else np.nan
    m["lat_p95_ms"] = np.percentile(lats, 95) if len(lats) else np.nan
    m["lat_media_pulso"] = np.mean(lat_beats) if lat_beats else np.nan
    m["trocas_perdidas"] = missed
    m["trocas_total"] = len(truth["changes"])

    # tempo
    bi = np.clip(np.searchsorted(g, res.log["t"]), 0, len(g) - 1)
    est = res.log["bpm"]
    play_log = is_chord[bi] & ~np.isnan(est)
    err = np.abs(est - bpm_true[bi])
    m["bpm_erro_medio"] = err[play_log].mean() if play_log.any() else np.nan
    first = next(s for s, e, l in truth["chords"] if l not in ("N", "P"))
    rel = np.where(np.isnan(est), 1.0, err / bpm_true[bi])
    lock = np.nan
    frames2s = int(2.0 / (res.log["t"][1] - res.log["t"][0]))
    for i in np.flatnonzero(res.log["t"] >= first):
        if np.all(rel[i:i + frames2s] < 0.04):
            lock = res.log["t"][i] - first
            break
    m["t_trava_tempo_s"] = lock

    # false triggers: sounding where the truth is silence (grace after each end)
    forbidden = np.array([l == "N" for l in tl])
    for e in truth["ends"]:
        forbidden &= ~((g >= e) & (g < e + END_TOL))
    m["falso_disparo_s"] = (sounding & forbidden).sum() * DT
    on_t = np.array([ev["t"] for ev in res.events if ev["type"] == "on"])
    m["falso_disparo_n"] = int(forbidden[np.clip((on_t / DT).astype(int), 0, len(g) - 1)].sum()) if len(on_t) else 0

    # confidently wrong (outside the first beat after each true change, which is latency)
    near = np.zeros(len(g), bool)
    for c in truth["changes"]:
        b = 60.0 / np.interp(c["t"], tt, tb)
        near |= (g >= c["t"]) & (g < c["t"] + b)
    wrong = is_chord & conf & (mode == "full") & (lab != tl) & ~near
    m["errado_confiante_n"] = int(np.sum(_runs(wrong) * DT >= 0.5 * np.median(beat)))

    # stopping: last sound after each end, relative to the end (bar line)
    stops, forgot = [], []
    for e in truth["ends"]:
        bar = 4 * 60.0 / np.interp(e, tt, tb)
        nxt = next((s for s, _, l in truth["chords"] if s > e and l not in ("N", "P")), truth["duration"])
        w = (g >= e - bar) & (g < nxt)
        idx = np.flatnonzero(w & sounding)
        stops.append((g[idx[-1]] + DT - e) if len(idx) else -bar)
        if nxt < truth["duration"]:
            forgot.append(bool(np.any(state[(g >= e) & (g < nxt)] == STATES.index(OCIOSO))))
    m["parada_s"] = max(stops) if stops else np.nan
    m["parou_no_compasso"] = bool(stops) and all(-0.25 <= s <= END_TOL + 0.05 for s in stops)
    m["esqueceu"] = all(forgot) if forgot else None

    # recovery after surprising changes
    rec = []
    for c in (c for c in truth["changes"] if c["surprise"]):
        b = 60.0 / np.interp(c["t"], tt, tb)
        seg_end = next(e for s, e, l in truth["chords"] if s <= c["t"] < e)
        w = np.flatnonzero((g >= c["t"]) & (g < seg_end) & (lab == c["to"]) & (mode == "full"))
        rec.append(((g[w[0]] if len(w) else seg_end) - c["t"]) / b)
    m["recuperacao_pulsos"] = max(rec) if rec else np.nan

    # dynamics: strum gain per bar vs. velocity of the notes started in that bar
    gains, vels = [], []
    ons = [ev for ev in res.events if ev["type"] == "on"]
    for t0, gain in truth["intensity"]:
        bar = 4 * 60.0 / np.interp(t0, tt, tb)
        v = [ev["vel"] for ev in ons if t0 <= ev["t"] < t0 + bar]
        if v:
            gains.append(gain)
            vels.append(np.mean(v))
    m["corr_dinamica"] = np.corrcoef(gains, vels)[0, 1] if len(gains) > 2 and np.std(gains) > 0 and np.std(vels) > 0 else np.nan

    return {k: (round(float(v), 3) if isinstance(v, (float, np.floating)) else v) for k, v in m.items()}
