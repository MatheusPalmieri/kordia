# Acompanhante automático para violão (baixo + pad)

Um sistema que **ouve o violão** e toca junto um baixo e um pad que seguem os acordes **que estão soando**, não uma cifra pré-programada. Pensado para banda de igreja, onde não há clique, o tom muda na hora e o andamento oscila.

**Status:** passo zero, um protótipo offline em Python, causal (streaming), testado em áudio sintético com verdade-base. Baseline: 88 % do tempo com o acorde certo, zero falso disparo, trocas a ~30 ms do tempo quando o ciclo está travado (~260 ms reagindo). Detalhes e falhas conhecidas em [`docs/resultados.md`](docs/resultados.md).

## Instalação

```bash
python -m venv .venv
.venv/Scripts/pip install -e .        # Windows (Git Bash)
# .venv/bin/pip install -e .          # Linux/macOS
```

Requer Python 3.11+ (testado com 3.15), numpy, scipy, soundfile, matplotlib, pytest, sounddevice (áudio ao vivo) e tkinter (vem com o Python).

## Uso ao vivo (interface)

Dê dois cliques em **`Acompanhante.bat`** (ou rode `python -m acompanhante.gui`).

1. Escolha a **entrada** (microfone, interface de áudio, saída da mesa) e a **saída** (já vem o alto-falante padrão).
2. Toque um pouco e ajuste o **ganho da entrada** até a barra de nível ficar verde e passar da marca cinza quando você bate.
3. Opcional: **tom principal**, **BPM fixo** (se a igreja usa metrônomo) e **progressão conhecida** (ex.: `C G Am F`). Sem nada disso ele só ouve e segue.
4. Clique em **Iniciar** e toque. A janela mostra o acorde que ele está tocando, o que está ouvindo (com a confiança), o estado, o BPM com o pulso do compasso e o padrão aprendido.

Volume do acompanhamento e "ouvir também o violão na saída" mudam com ele tocando; tom, BPM e progressão valem a partir do próximo Iniciar.

> **Importante:** com microfone e alto-falante no mesmo ambiente, o sistema ouve o próprio baixo/pad e se confunde (é o pior cenário medido, ver resultados). Prefira **fone de ouvido** ou o violão **ligado direto** (cabo/interface).

A latência do driver de áudio é medida ao iniciar e compensada nas trocas previstas e no pulso do baixo. As trocas reativas (antes de ele aprender o padrão) continuam chegando ~½ pulso atrasadas.

## Uso em arquivos e avaliação

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
