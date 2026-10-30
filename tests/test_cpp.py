"""The C++ core (cpp/) must be the same algorithm as the Python reference: same parameters, same decisions,
same metrics on every scenario. Skipped when the module is not built (`python tasks.py build`)."""
import numpy as np
import pytest

from acompanhante import params as P
from acompanhante import scenarios
from acompanhante.metrics import evaluate
from acompanhante.pipeline import RunConfig, run

kordia_core = pytest.importorskip("acompanhante.kordia_core")


def test_params_match():
    cpp = kordia_core.params()
    for name, value in cpp.items():
        assert getattr(P, name) == pytest.approx(value), name


@pytest.mark.parametrize("name", list(scenarios.SCENARIOS))
def test_same_decisions_as_python(name):
    audio, truth = scenarios.build(name)
    cfg = RunConfig.from_dict(truth["run"])
    py, cpp = run(audio, cfg), run(audio, cfg, backend="cpp")
    key = lambda changes: [(round(t, 6), label, mode) for t, label, mode in changes]
    assert key(py.changes) == key(cpp.changes)
    assert np.array_equal(py.log["state"], cpp.log["state"]) and np.array_equal(py.log["heard"], cpp.log["heard"])
    assert np.abs(py.accomp - cpp.accomp).max() < 1e-9
    nan_free = lambda m: {k: None if isinstance(v, float) and np.isnan(v) else v for k, v in m.items()}
    assert nan_free(evaluate(py, truth)) == nan_free(evaluate(cpp, truth))


def test_live_lookahead_and_click_match():
    audio, _ = scenarios.build("estavel_120")
    cfg = RunConfig(tom="C", clique=True)
    py, cpp = run(audio, cfg, lookahead=0.12), run(audio, cfg, lookahead=0.12, backend="cpp")
    assert [e for e in py.events if e["inst"] == "click"] == [e for e in cpp.events if e["inst"] == "click"]
    assert [c[:3] for c in py.changes] == [c[:3] for c in cpp.changes]
