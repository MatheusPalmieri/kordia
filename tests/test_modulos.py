"""Unit tests: each module in isolation."""
import numpy as np

from acompanhante import params as P
from acompanhante import scenarios, theory
from acompanhante.chords import ChordTracker
from acompanhante.guitar import Guitar, pluck
from acompanhante.memory import ProgressionMemory
from acompanhante.onsets import Onset, OnsetDetector
from acompanhante.pipeline import RunConfig, _norm_mag, run
from acompanhante.synth import write_midi
from acompanhante.tempo import FixedGrid, TempoTracker


def _frames(audio):
    """Yield (t, onset_mag, chroma_mag, db) the way the pipeline does."""
    wo, wc = np.hanning(P.ONSET_WIN), np.hanning(P.CHROMA_WIN)
    buf = np.zeros(P.CHROMA_WIN)
    for k in range(len(audio) // P.HOP):
        x = audio[k * P.HOP:(k + 1) * P.HOP]
        buf = np.concatenate([buf[P.HOP:], x])
        yield (k + 1) * P.HOP / P.SR, _norm_mag(buf[-P.ONSET_WIN:], wo), _norm_mag(buf, wc), 10 * np.log10(np.mean(x ** 2) + 1e-12)


def test_pluck_tuning():
    for midi in (45, 52, 64):
        y = pluck(midi, 1.0)
        ac = np.correlate(y[2000:6000], y[2000:6000], "full")[3999:]
        lag = np.argmax(ac[20:400]) + 20
        assert abs(lag - P.SR / (440 * 2 ** ((midi - 69) / 12))) <= 1.0


def test_scenario_truth_is_coherent():
    audio, truth = scenarios.build("pausa_e_fim")
    segs = truth["chords"]
    assert segs[0][0] == 0 and abs(segs[-1][1] - truth["duration"]) < 1e-6
    assert all(abs(a[1] - b[0]) < 1e-6 for a, b in zip(segs, segs[1:]))
    assert any(l == "P" for _, _, l in segs)          # the short pause
    assert len(truth["ends"]) == 2                    # long silence + final end
    assert len(audio) == int(truth["duration"] * P.SR)


def test_onsets_count_and_timing():
    g = Guitar(5.0, seed=1)
    times = [0.5 + 0.5 * i for i in range(8)]
    for i, t in enumerate(times):
        g.strum(t, theory.voicing("G"), "D" if i % 2 == 0 else "U", 0.8)
    od = OnsetDetector()
    found = [o.t for t, mo, mc, db in _frames(g.out) if (o := od.process(mo, t - P.ONSET_WIN / 2 / P.SR, db))]
    assert len(found) == len(times)
    assert max(abs(f - t) for f, t in zip(found, times)) < 0.05


def test_chord_classifier_recognizes_triads():
    for label in ["C", "G", "Am", "F", "Em", "E", "Bb", "Dm", "D", "Bm"]:
        g = Guitar(1.5, seed=2)
        g.strum(0.1, theory.voicing(label), "D", 1.0)
        ct = ChordTracker()
        for t, mo, mc, db in _frames(g.out[: int(1.0 * P.SR)]):
            idx, conf, _ = ct.process(mc, db < P.SILENCE_DB, P.HMM_P_CHANGE_ONSET)
        assert theory.LABELS[idx] == label, label


def test_key_prior_never_blocks():
    w = theory.key_prior("C")
    assert w[theory.chord_index("E")] > 0 and w[theory.chord_index("C")] > w[theory.chord_index("E")]


def _feed_clicks(tracker, bpms, subdiv=2):
    """Eighth-note 'strums' (louder on the beat) with a tempo per beat."""
    t, events = 0.5, []
    for bpm in bpms:
        beat = 60 / bpm
        events += [(t, 1.0), (t + beat / 2, 0.5)][:subdiv]
        t += beat
    frame = P.HOP / P.SR
    ev = iter(events + [(1e9, 0)])
    nxt = next(ev)
    for k in range(int(t / frame)):
        now = (k + 1) * frame
        onset, flux = None, 0.0
        if nxt[0] <= now:
            onset, flux = Onset(nxt[0], -20, nxt[1] * 5), nxt[1] * 5
            nxt = next(ev)
        tracker.update(now, flux, onset)
    return tracker


def test_tempo_constant_and_eighths():
    tr = _feed_clicks(TempoTracker(), [100] * 32)
    assert abs(tr.bpm - 100) < 2
    assert tr.confidence > 0.8


def test_tempo_follows_ramp():
    tr = _feed_clicks(TempoTracker(), list(np.linspace(100, 125, 64)))
    assert abs(tr.bpm - 125) < 125 * 0.05


def test_fixed_grid():
    g = FixedGrid(120)
    g.update(1.0, 1.0, Onset(1.0, -20, 1))
    assert abs(g.time_of_beat(4) - 3.0) < 1e-9 and g.bar_pos(4) == 0


def test_memory_learns_cycle_and_predicts():
    m = ProgressionMemory()
    prog = [theory.chord_index(c) for c in "C G Am F".split()]
    for i, c in enumerate((prog * 2)[:5]):
        m.push(c, 4 * i)
    assert not m.locked          # C G Am F C: one chord of the second lap is not enough
    m.push(prog[1], 20)          # ... G: the lap came back
    assert m.locked
    assert m.predict() == (prog[2], 24)
    m.unlock()
    m.push(prog[2], 24)          # G -> Am are consecutive in the known cycle: re-lock
    assert m.locked and m.predict() == (prog[3], 28)


def test_memory_decorated_aligns_on_first_chord():
    m = ProgressionMemory("C G Am F")
    m.push(theory.chord_index("Am"), 0)
    assert m.locked and m.predict() == (theory.chord_index("F"), 4)


def test_silence_and_noise_play_nothing():
    rng = np.random.default_rng(0)
    res = run(rng.standard_normal(P.SR * 4) * 1e-3, RunConfig(tom="C"))
    assert not res.events or all(e["type"] == "expr" for e in res.events)


def test_pipeline_is_causal():
    """Changing the future must not change any output before it."""
    audio, truth = scenarios.build("estavel_120")
    audio = audio[: 14 * P.SR]
    cut = 8 * P.SR
    other = audio.copy()
    other[cut:] = np.random.default_rng(1).standard_normal(len(audio) - cut) * 0.1
    a, b = run(audio, RunConfig(tom="C")), run(other, RunConfig(tom="C"))
    tc = (cut // P.HOP) * P.HOP / P.SR    # last block boundary before the change
    ea = [e for e in a.events if e["t"] < tc]
    eb = [e for e in b.events if e["t"] < tc]
    assert ea == eb and len(ea) > 0
    n = int(tc * P.SR)
    assert np.array_equal(a.accomp[:n], b.accomp[:n])


def test_midi_file_is_valid(tmp_path):
    ev = [{"t": 0.0, "type": "on", "inst": "bass", "pitch": 36, "vel": 100},
          {"t": 1.0, "type": "off", "inst": "bass", "pitch": 36, "vel": 0},
          {"t": 0.5, "type": "expr", "inst": "all", "pitch": 0, "vel": 0, "value": 0.5}]
    p = tmp_path / "x.mid"
    write_midi(ev, p)
    data = p.read_bytes()
    assert data[:4] == b"MThd" and data[14:18] == b"MTrk"
    assert int.from_bytes(data[18:22], "big") == len(data) - 22
    assert data.endswith(b"\xFF\x2F\x00")


def test_lookahead_emits_predicted_changes_early():
    """Live mode: predicted changes must be decided `lookahead` before they are due, so they sound on time."""
    from acompanhante.engine import TRAVADO
    from acompanhante.pipeline import Processor
    audio, _ = scenarios.build("estavel_120")
    L = 0.12
    proc = Processor(RunConfig(tom="C"), keep_log=False, lookahead=L)
    early = []
    for k in range(len(audio) // P.HOP):
        n = len(proc.acc.changes) and proc.acc.changes[-1]
        proc.process(audio[k * P.HOP:(k + 1) * P.HOP].astype(np.float64))
        last = proc.acc.changes[-1] if proc.acc.changes else None
        if last and last != n and proc.engine.state == TRAVADO and last[2] == "full":
            early.append(last[0] - proc.s / P.SR)       # due time minus decision time
    assert len(early) >= 4
    assert np.median(early) >= L - 2 * P.HOP / P.SR


def test_click_follows_tempo_and_fixed_grid_starts_at_start():
    """Click: free mode clicks on the tracked beats once the tempo is known; with a fixed BPM + click the grid
    starts at Iniciar (the musician follows the click), not at the first strum."""
    from acompanhante.pipeline import Processor
    audio, _ = scenarios.build("estavel_120")
    proc = Processor(RunConfig(tom="C", clique=True), keep_log=False)
    for k in range(int(10 * P.SR) // P.HOP):
        proc.process(audio[k * P.HOP:(k + 1) * P.HOP].astype(np.float64))
    clicks = [e["t"] for e in proc.acc.events if e["inst"] == "click"]
    assert len(clicks) >= 8 and abs(np.median(np.diff(clicks)) - 0.5) < 0.02     # 120 BPM
    grid = Processor(RunConfig(modo="metronomo", bpm=100, clique=True), keep_log=False)
    for _ in range(int(2 * P.SR) // P.HOP):
        grid.process(np.zeros(P.HOP))                                              # silence: still clicks
    clicks = [e for e in grid.acc.events if e["inst"] == "click"]
    assert abs(clicks[0]["t"] - 0.5) < 1e-6 and clicks[0]["pitch"] == 77 and len(clicks) == 3


def test_session_records_and_replays(tmp_path):
    from acompanhante.session import SessionRecorder, load
    rec = SessionRecorder({"run": {"modo": "livre", "tom": "C"}, "lookahead": 0.1}, root=tmp_path)
    block = np.full(P.HOP, 0.1)
    rec.feed(block, {k: 0 for k in __import__("acompanhante.session", fromlist=["x"]).FIELDS}, [{"t": 0, "motivo": "x"}])
    folder = rec.stop({"ok": True})
    audio, cfg, la, sr = load(folder)
    assert len(audio) == P.HOP and abs(audio[0] - 0.1) < 1e-3 and cfg["tom"] == "C" and la == 0.1 and sr == P.SR
    assert (folder / "decisoes.jsonl").read_text(encoding="utf-8").strip()


def test_fallback_is_silent_when_the_key_does_not_fit():
    from acompanhante.engine import Engine
    eng = Engine(TempoTracker(), ProgressionMemory(), key="C")
    eng.recent.extend(theory.chord_index(c) for c in ["B", "F#", "G#m", "E"])
    out = eng._fallback_out(1.0)                                  # B major music: C tonic would be a wrong note
    assert (out is None or out.mode is None) and eng.cur == (None, None)
    eng2 = Engine(TempoTracker(), ProgressionMemory(), key="C")
    eng2.recent.extend(theory.chord_index(c) for c in ["C", "G", "Am", "F"])
    assert eng2._fallback_out(1.0).mode == "tonic"
