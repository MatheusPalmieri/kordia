"""Regression tests on the 12 synthetic scenarios. Thresholds start loose; tighten them as the system improves
(and say why in the commit)."""
import pytest

from acompanhante import scenarios
from acompanhante.cli import evaluate_all


@pytest.fixture(scope="module")
def rows():
    return evaluate_all(list(scenarios.SCENARIOS), "test", save=False)


@pytest.mark.parametrize("name", list(scenarios.SCENARIOS))
def test_never_plays_when_it_should_not(rows, name):
    r = rows[name]
    assert r["falso_disparo_s"] <= 2.0
    assert r["parada_s"] <= 3.0          # ~1 bar + 1 s at the slowest scenario tempo
    assert r["acc_total"] >= 0.5
    # known problems (docs/resultados.md): #1 feedback (the long pad tail + reverb comes back in the input),
    # #3 tempo lost in a strong ritardando
    assert r["errado_confiante_n"] <= (4 if name in ("desacelerando_final", "realimentacao") else 3)


def test_stable(rows):
    r = rows["estavel_120"]
    assert r["acc_total"] >= 0.85 and r["falso_disparo_s"] <= 1.0 and r["bpm_erro_medio"] <= 5
    assert r["parou_no_compasso"]


def test_prediction_beats_reaction(rows):
    r = rows["estavel_120"]
    assert r["lat_travado_ms"] < r["lat_reativo_ms"] / 2


def test_metronome_aligns_to_grid(rows):
    assert abs(rows["modo_metronomo"]["lat_media_pulso"]) <= 0.5


def test_forgets_after_long_silence(rows):
    assert rows["pausa_e_fim"]["esqueceu"] and rows["troca_de_tom"]["esqueceu"]


def test_follows_dynamics(rows):
    assert rows["dinamica"]["corr_dinamica"] >= 0.7


def test_accelerating(rows):
    assert rows["acelerando"]["bpm_erro_medio"] <= 5 and rows["acelerando"]["acc_total"] >= 0.85
