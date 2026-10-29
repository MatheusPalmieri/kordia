# CLAUDE.md — instruções permanentes

Projeto: acompanhante automático que **ouve o violão** e toca baixo + pad seguindo os acordes. Leia `docs/00-visao.md` e `docs/02-arquitetura.md` antes de mexer no código.

## Princípios (não negociáveis)

1. **Errar tocando pouco > errar tocando nota errada.** Na dúvida: fundamental sem terça, pad na tônica, ou silêncio. Falso disparo é o erro mais caro.
2. **Causalidade/streaming.** Todo processamento é bloco a bloco e só usa o passado, inclusive no harness offline. Proibido: funções que olham o áudio inteiro (ex.: `librosa.beat.beat_track`, normalizar pelo máximo global, filtfilt).
3. **"Qual acorde" × "quando trocar" são camadas separadas** (`chords.py` × `tempo.py`), testadas separadamente. O áudio confirma o acorde; o tempo diz quando.
4. **O áudio manda.** Tom principal e progressão decorada são só prior/palpite; acorde claro fora do tom tem que ser aceito.
5. **Parar quando o músico para**: termina o compasso, fade, esquece após silêncio longo.

## Convenções

- Documentação em português do Brasil; código e identificadores em inglês.
- Todos os parâmetros numéricos em `acompanhante/params.py`, com unidade e motivo (é a especificação para o port em C++/JUCE).
- Algoritmos simples e explícitos; nada de excesso de engenharia. Dependências: numpy, scipy, soundfile, matplotlib, pytest, sounddevice (+ tkinter da stdlib).
- Commits pequenos, mensagens claras.

## Comandos

```bash
python -m venv .venv && .venv/Scripts/pip install -e .   # ou .venv/bin/pip no Linux/macOS
python -m acompanhante.gui            # interface ao vivo (ou Acompanhante.bat / python tasks.py interface)
python tasks.py test                  # pytest (unitários + regressão dos cenários)
python tasks.py avaliar [versao]      # roda os 12 cenários, salva métricas e relatórios
python -m acompanhante.cli gerar --cenario acelerando --saida dados/
python -m acompanhante.cli rodar --entrada arquivo.wav --modo livre|metronomo|decorado [--bpm 120] [--tom C] [--progressao "C G Am F"]
python -m acompanhante.cli avaliar --todos [--versao v1]
```

## Ao mudar algo relevante

- Rode `python tasks.py avaliar <nova_versao>` e **atualize `docs/resultados.md`** com a tabela e uma análise honesta (inclusive o que piorou).
- Registre decisões novas ou revistas em **`docs/decisoes.md`** (data, decisão, motivo, alternativas).
- Se a arquitetura ou um requisito mudar, atualize `docs/02-arquitetura.md` / `docs/01-requisitos.md`.
- Ao apertar ou afrouxar um limiar em `tests/test_cenarios.py`, diga o porquê no commit.

## Como pensar neste projeto

- Antes de otimizar um número, pergunte: "isso deixa o sistema mais propenso a tocar nota errada?" Se sim, não vale.
- Áudio sintético é otimista. Ganho só no sintético e não explicável musicalmente é suspeito de sobreajuste.
- O equilíbrio "seguir o que ouve × seguir o que aprendeu" é o principal parâmetro; mexa nele com cenários de variação (`acorde_fora_do_tom`, `troca_de_tom`).

## Ao vivo × arquivo

`pipeline.Processor` é o sistema inteiro bloco a bloco; `run()` (arquivos, métricas) e `gui.py` (callback de áudio) usam o mesmo objeto. Nunca duplique lógica entre os dois. O callback de áudio não pode bloquear: nada de I/O, prints ou alocação grande lá dentro.
