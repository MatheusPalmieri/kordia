# Acompanhante automático para violão (baixo + pad)

Um sistema que **ouve o violão** e toca junto um baixo e um pad que seguem os acordes **que estão soando**, não uma cifra pré-programada. Pensado para banda de igreja, onde não há clique, o tom muda na hora e o andamento oscila.

**Status:** passo zero, um protótipo offline em Python, causal (streaming), testado em áudio sintético com verdade-base. Baseline: 88 % do tempo com o acorde certo, zero falso disparo, trocas a ~30 ms do tempo quando o ciclo está travado (~260 ms reagindo). Detalhes e falhas conhecidas em [`docs/resultados.md`](docs/resultados.md).

## Instalação

```bash
python -m venv .venv
.venv/Scripts/pip install -e .        # Windows (Git Bash)
# .venv/bin/pip install -e .          # Linux/macOS
```

Requer Python 3.11+ (testado com 3.15), numpy, scipy, soundfile, matplotlib, pytest.

## Uso

```bash
# testes (unitários + regressão dos cenários)
python tasks.py test

# gerar um cenário sintético (WAV + verdade-base JSON)
python -m acompanhante.cli gerar --cenario acelerando --saida dados/

# rodar o acompanhante num WAV qualquer (inclusive gravação de celular)
python -m acompanhante.cli rodar --entrada minha_gravacao.wav --modo livre --tom C
python -m acompanhante.cli rodar --entrada gravacao.wav --modo metronomo --bpm 110 --tom C
python -m acompanhante.cli rodar --entrada gravacao.wav --modo decorado --progressao "C G Am F" --tom C

# avaliar todos os cenários (tabela de métricas + relatórios PNG)
python -m acompanhante.cli avaliar --todos --versao baseline
```

`rodar` gera, ao lado da saída escolhida (`--saida`, padrão `saida/`):
- `<nome>_render.wav`: violão original + baixo + pad;
- `<nome>.mid` e `<nome>_eventos.json`: os eventos do acompanhamento;
- `<nome>.png`: relatório visual (forma de onda, onsets, BPM, acordes, estado).

`avaliar` salva tudo em `resultados/<versao>/`.

## Estrutura

```
acompanhante/   código (um módulo por camada; parâmetros em params.py)
tests/          pytest
docs/           visão, requisitos, arquitetura, riscos, testes, roadmap, decisões, resultados, glossário
dados/          cenários gerados (ignorado pelo git)
resultados/     métricas e relatórios por versão (só o CSV/JSON vai para o git)
```

## Documentação

1. [Visão](docs/00-visao.md) · 2. [Requisitos](docs/01-requisitos.md) · 3. [Arquitetura](docs/02-arquitetura.md) · 4. [Riscos](docs/03-riscos.md) · 5. [Plano de testes](docs/04-plano-de-testes.md) · 6. [Roadmap](docs/05-roadmap.md) · [Decisões](docs/decisoes.md) · [Resultados](docs/resultados.md) · [Glossário](docs/glossario.md)
