"""The exam (acompanhante/exam.py): new songs in other keys, with the live app's settings. The full matrix is
`python -m acompanhante.cli prova`; here, what must not get worse and the parts of the harness itself."""
import numpy as np
import pytest

from acompanhante import exam, theory
from acompanhante.pipeline import run


def test_song_sounds_a_half_step_below_the_shapes():
    bars, _ = exam.song(exam.Case("pop", theory.note_pc("B"), "clique"))
    assert [b.chord for b in bars[:4]] == ["C", "G", "Am", "F"] and [b.label for b in bars[:4]] == ["B", "F#", "Abm", "E"]


def test_forms_keep_their_shapes_and_use_a_capo():
    bars, _ = exam.song(exam.Case("formas_worship", theory.note_pc("A"), "clique"))
    assert [b.chord for b in bars[:4]] == ["G", "D/F#", "Em7", "Cadd9"]
    assert [b.label for b in bars[:4]] == ["A", "E", "F#m", "D"]


def test_two_chords_in_a_bar_are_in_the_truth():
    _, truth, _ = exam.build(exam.Case("duas_por_compasso", 0, "clique"))
    played = [l for _, _, l in truth["chords"] if l != "N"]
    assert played[:4] == ["C", "G", "Am", "F"]
    assert truth["chords"][2][0] - truth["chords"][1][0] == pytest.approx(2 * 60 / exam.BPMS[0], abs=0.01)


def test_cents_zero_keeps_the_old_scenarios():
    from acompanhante.guitar import Guitar
    assert not Guitar(1.0, 7).tuning.any() and Guitar(1.0, 7, 5.0).tuning.std() > 0


def test_score_catches_a_wrong_pattern():
    """The score must see what the app played, not trust the lock: a confident wrong chord counts."""
    truth = {"duration": 8.0, "tempo": [[0, 120], [8, 120]], "changes": [{"t": 0.0}, {"t": 4.0}],
             "chords": [[0.0, 4.0, "C"], [4.0, 8.0, "G"]]}

    class Res:
        changes = [(0.0, "C", "full"), (4.0, "Am", "full")]
        log = {"t": np.arange(0, 8, 0.1), "state": np.full(80, 2)}

    s = exam.score(Res, truth, 0)
    assert s["acc"] == pytest.approx(0.5, abs=0.01) and s["wrong_s"] == pytest.approx(3.5, abs=0.02)
    assert exam.verdict(s) == "errou"


def test_the_loop_the_musician_plays_most_in_every_key():
    pytest.importorskip("acompanhante.kordia_core")
    rows = exam.run_exam(exam.cases(["pop"], None, ["clique", "tom_da_forma"]))
    assert np.mean([r["acc"] for r in rows]) >= 0.80
    assert sum(r["veredito"] == "ok" for r in rows) >= 15


def test_cycle_that_closes_on_its_first_chord():
    """C | Bb | F | C with the click: was 0/24, the pattern learned a two-bar tonic as one bar."""
    pytest.importorskip("acompanhante.kordia_core")
    rows = exam.run_exam(exam.cases(["emprestado"], range(0, 12, 3), ["clique"]))
    assert min(r["acc"] for r in rows) >= 0.8, rows   # (the bug scored 0.42-0.57; 0.82 in F# has no wrong chord)


def test_recorded_takes_rebuild_the_real_session():
    """Control of the splice: the good G-shape session keeps its score when cut and put back in order."""
    pytest.importorskip("acompanhante.kordia_core")
    if not (exam.SESSIONS_DIR / exam.SESSIONS[3][0]).is_dir():
        pytest.skip("sessoes/ não é versionado")
    rows = exam.run_exam([exam.Case("pop", theory.note_pc("F#"), "real_clique")])
    assert rows[0]["acc"] >= 0.9


def test_exam_settings_match_in_python_and_cpp():
    pytest.importorskip("acompanhante.kordia_core")
    audio, _, cfg = exam.build(exam.Case("duas_por_compasso", theory.note_pc("F#"), "tom_da_forma"))
    audio = audio[: 22050 * 25]
    key = lambda changes: [(round(t, 6), label, mode) for t, label, mode in changes]
    py, cpp = run(audio, cfg, lookahead=exam.LOOKAHEAD), run(audio, cfg, lookahead=exam.LOOKAHEAD, backend="cpp")
    assert key(py.changes) == key(cpp.changes)
    assert np.array_equal(py.log["state"], cpp.log["state"])
