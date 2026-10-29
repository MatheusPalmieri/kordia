"""CLI: gerar (scenarios), rodar (any WAV), avaliar (all scenarios -> metrics + reports)."""
import argparse
import csv
import json
import time
from pathlib import Path

import numpy as np
import soundfile as sf

from . import params as P
from . import scenarios
from .metrics import evaluate
from .pipeline import RunConfig, load_wav, run
from .report import plot
from .synth import write_midi


def _save_outputs(res, out_dir, name, truth=None, title="", audio_dir=None):
    out_dir.mkdir(parents=True, exist_ok=True)
    audio_dir = audio_dir or out_dir
    audio_dir.mkdir(parents=True, exist_ok=True)
    mix = res.audio + res.accomp
    sf.write(audio_dir / f"{name}_render.wav", (mix / max(1.0, np.max(np.abs(mix)))).astype(np.float32), P.SR)
    write_midi(res.events, audio_dir / f"{name}.mid")
    (audio_dir / f"{name}_eventos.json").write_text(json.dumps({"changes": res.changes, "events": res.events}))
    plot(res, out_dir / f"{name}.png", truth, title)


def cmd_gerar(a):
    out = Path(a.saida)
    out.mkdir(parents=True, exist_ok=True)
    for name in (scenarios.SCENARIOS if a.todos else [a.cenario]):
        audio, truth = scenarios.build(name)
        sf.write(out / f"{name}.wav", audio, P.SR)
        (out / f"{name}.json").write_text(json.dumps(truth, indent=1))
        print(f"{name}: {len(audio) / P.SR:.1f} s -> {out / name}.wav/.json")


def cmd_rodar(a):
    audio = load_wav(a.entrada)
    cfg = RunConfig(modo=a.modo, tom=a.tom, bpm=a.bpm, progressao=a.progressao)
    t0 = time.time()
    res = run(audio, cfg)
    name = Path(a.entrada).stem
    truth = json.loads(Path(a.verdade).read_text()) if a.verdade else None
    _save_outputs(res, Path(a.saida), name, truth, f"{name} — modo {a.modo}")
    print(f"{name}: {len(audio) / P.SR:.1f} s processados em {time.time() - t0:.1f} s; {len(res.changes)} trocas")
    if truth:
        print(json.dumps(evaluate(res, truth), indent=1))
    print(f"saídas em {Path(a.saida).resolve()}")


COLS = ["acc_total", "acc_confiante", "cobertura", "lat_media_ms", "lat_p95_ms", "lat_media_pulso", "lat_reativo_ms",
        "lat_travado_ms", "trocas_perdidas",
        "bpm_erro_medio", "t_trava_tempo_s", "falso_disparo_s", "errado_confiante_n", "parada_s", "esqueceu",
        "recuperacao_pulsos", "corr_dinamica"]


def evaluate_all(names, versao, save=True):
    rows = {}
    out = Path("resultados") / versao
    for name in names:
        audio, truth = scenarios.build(name)
        res = run(audio, RunConfig.from_dict(truth["run"]))
        rows[name] = evaluate(res, truth)
        if save:
            _save_outputs(res, out / "relatorios", name, truth, f"{name} — {truth['description']}", out / "audio")
    if save:
        out.mkdir(parents=True, exist_ok=True)
        (out / "metricas.json").write_text(json.dumps(rows, indent=1))
        keys = list(next(iter(rows.values())).keys())
        with open(out / "metricas.csv", "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["cenario"] + keys)
            for name, r in rows.items():
                w.writerow([name] + [r[k] for k in keys])
    return rows


def aggregate(rows):
    agg = {}
    for k in COLS:
        vals = [r[k] for r in rows.values() if isinstance(r[k], (int, float)) and not isinstance(r[k], bool) and not np.isnan(r[k])]
        agg[k] = round(float(np.mean(vals)), 3) if vals else None
    return agg


def print_table(rows):
    short = {"acc_total": "acc", "acc_confiante": "acc_conf", "cobertura": "cobert", "lat_media_ms": "lat_ms",
             "lat_p95_ms": "lat95", "lat_media_pulso": "lat_pul", "lat_reativo_ms": "lat_reat", "lat_travado_ms": "lat_trav", "trocas_perdidas": "perd", "bpm_erro_medio": "bpm_err",
             "t_trava_tempo_s": "trava_s", "falso_disparo_s": "falso_s", "errado_confiante_n": "err_conf",
             "parada_s": "parada", "esqueceu": "esqueceu", "recuperacao_pulsos": "recup", "corr_dinamica": "dyn"}
    head = f"{'cenario':22s}" + "".join(f"{short[c]:>9s}" for c in COLS)
    print(head)
    for name, r in list(rows.items()) + [("MÉDIA", aggregate(rows))]:
        cells = []
        for c in COLS:
            v = r[c]
            cells.append(f"{'-' if v is None or (isinstance(v, float) and np.isnan(v)) else (str(v) if isinstance(v, bool) else f'{v:.2f}'):>9s}")
        print(f"{name:22s}" + "".join(cells))


def cmd_avaliar(a):
    names = list(scenarios.SCENARIOS) if a.todos or not a.cenario else [a.cenario]
    rows = evaluate_all(names, a.versao)
    print_table(rows)
    print(f"\nresultados em {(Path('resultados') / a.versao).resolve()}")


def main(argv=None):
    p = argparse.ArgumentParser(prog="acompanhante")
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gerar", help="gera cenários sintéticos (WAV + verdade-base)")
    g.add_argument("--cenario", choices=list(scenarios.SCENARIOS))
    g.add_argument("--todos", action="store_true")
    g.add_argument("--saida", default="dados")
    r = sub.add_parser("rodar", help="roda o acompanhante num WAV")
    r.add_argument("--entrada", required=True)
    r.add_argument("--modo", choices=["livre", "metronomo", "decorado"], default="livre")
    r.add_argument("--bpm", type=float)
    r.add_argument("--tom")
    r.add_argument("--progressao")
    r.add_argument("--verdade", help="JSON de verdade-base (opcional) para calcular métricas")
    r.add_argument("--saida", default="saida")
    v = sub.add_parser("avaliar", help="roda os cenários e calcula métricas")
    v.add_argument("--todos", action="store_true")
    v.add_argument("--cenario", choices=list(scenarios.SCENARIOS))
    v.add_argument("--versao", default="dev")
    sub.add_parser("interface", help="abre a interface gráfica ao vivo (microfone -> baixo + pad)")
    a = p.parse_args(argv)
    if a.cmd == "interface":
        from .gui import main as gui_main
        return gui_main()
    if a.cmd == "gerar" and not (a.todos or a.cenario):
        p.error("gerar: use --cenario NOME ou --todos")
    {"gerar": cmd_gerar, "rodar": cmd_rodar, "avaliar": cmd_avaliar}[a.cmd](a)


if __name__ == "__main__":
    main()
