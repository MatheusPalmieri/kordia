# 02 — Arquitetura

## Visão geral

Tudo roda em **streaming**: o áudio entra em blocos de 512 amostras (22 050 Hz, ≈ 23 ms) e cada módulo só usa o passado. A saída de um bloco vale a partir do bloco seguinte (1 bloco de latência estrutural), como seria em tempo real.

```mermaid
flowchart LR
    A[Entrada de áudio<br/>blocos de 512] --> F[Extração de features<br/>FFT 1024 e 4096]
    F --> O[Detector de onsets<br/>fluxo espectral]
    F --> H[Analisador harmônico<br/>chroma + templates + HMM]
    O --> T[Rastreador de tempo<br/>ACF + PLL / grade fixa]
    O --> D[Dinâmica]
    O --> E
    T --> E[Motor de decisão<br/>máquina de estados]
    H --> E
    M[Memória de progressão<br/>ciclos + previsão] <--> E
    D --> G
    E --> G[Gerador de acompanhamento<br/>eventos baixo/pad]
    G --> S[Sintetizador streaming]
    G --> MID[Arquivo .mid / JSON]
    S --> W[WAV renderizado]
    S -. realimentação<br/>(cenário de teste) .-> A
```

## Módulos (`acompanhante/`)

| Arquivo | Responsabilidade | Interface principal |
|---|---|---|
| `params.py` | todos os parâmetros numéricos, comentados | constantes |
| `theory.py` | nomes de acordes, voicings de violão, templates, prior por tom | `parse_chord`, `chord_pcs`, `key_prior` |
| `guitar.py` | síntese Karplus-Strong e batidas (só para gerar cenários) | `strum(...)` |
| `scenarios.py` | os 12 cenários, WAV + verdade-base JSON | `build(nome) -> (audio, truth)` |
| `onsets.py` | fluxo espectral, limiar adaptativo, intensidade | `OnsetDetector.process(mag, rms, t) -> Onset/None` |
| `chords.py` | chromagram, templates, filtro HMM causal | `ChordTracker.process(mag, t, ...) -> posterior` |
| `tempo.py` | rastreador adaptativo (ACF + PLL) e grade fixa | `.update(t, onset)`, `.bpm`, `.confidence`, `.beat_at(t)` |
| `memory.py` | sequência de acordes, detecção de ciclo, previsão | `ProgressionMemory.push/predict/unlock` |
| `engine.py` | máquina de estados, escada de confiança, parada, dinâmica | `Engine.step(...) -> decisão` |
| `accomp.py` | transforma decisões em eventos de nota (baixo/pad) | `Accompanist.update(...) -> [Event]` |
| `synth.py` | sintetizador streaming de baixo/pad; escrita MIDI | `Synth.render(n)`, `write_midi` |
| `pipeline.py` | `Processor`: o sistema inteiro, um bloco por vez; `run()` para arquivos | `Processor.process(bloco) -> saída`; `run(audio, cfg) -> Result` |
| `metrics.py` | compara resultado com verdade-base | `evaluate(result, truth) -> dict` |
| `report.py` | figura PNG por cenário | `plot(result, truth, path)` |
| `cli.py` | `gerar`, `rodar`, `avaliar`, `interface` | — |
| `gui.py` | interface ao vivo (tkinter) + fluxo de áudio duplex (sounddevice) | `python -m acompanhante.gui` |

## Fluxo por bloco

1. Buffer circular recebe o bloco. Calcula-se FFT de 1024 (onsets) e de 4096 (harmonia) sobre as últimas amostras.
2. **Onsets:** fluxo espectral (log-magnitude, diferença positiva, 150 Hz–5 kHz) comparado com limiar adaptativo (mediana móvel × fator + delta), com período refratário. Cada onset sai com instante e intensidade (RMS em dB).
3. **Tempo:**
   - *Livre:* a envoltória de fluxo dos últimos ~8 s passa por autocorrelação a cada ~0,25 s; o pico ponderado por um prior log-normal (centrado em ~105 BPM) dá o período. Um PLL corrige fase e período a cada onset que cai perto de um tempo previsto (onsets de contratempo são ignorados). A **posição no compasso** (4/4) começa no primeiro onset e é corrigida pelas trocas de acorde confirmadas (as trocas tendem a cair no tempo 1). A confiança é a fração dos últimos tempos que tiveram onset alinhado.
   - *Metrônomo:* grade fixa com o BPM dado, iniciada no primeiro onset.
4. **Harmonia:** chroma de 12 classes (60 Hz–2,5 kHz, compressão log) → similaridade de cosseno com templates de 24 acordes (maior/menor, com harmônicos) + estado "N" (sem acorde) → verossimilhança `exp(β·cos)` → **filtro HMM causal** (algoritmo forward). A probabilidade de troca por quadro é baixa (persistência) e aumenta logo após onsets em tempo forte (peso por posição). O prior de transição favorece acordes diatônicos do tom principal. A **confiança** não é o posterior do HMM (que satura em ~1): é um softmax por quadro suavizado, lido no acorde que o HMM escolheu.
5. **Motor de decisão** (abaixo) escolhe o que tocar e quando.
6. **Acompanhamento:** baixo na fundamental (oitava 2) atacando nos tempos 1 e 3 quando o tempo está confiável, sustentado caso contrário; pad com a tríade (oitava 4) sustentada até a troca. Velocity vem da dinâmica.
7. **Síntese:** sintetizador aditivo simples, streaming, com precisão de amostra.

## Máquina de estados

```mermaid
stateDiagram-v2
    [*] --> OCIOSO
    OCIOSO --> OUVINDO: onsets com acorde confiável
    OUVINDO --> TRAVADO: ciclo repetido detectado<br/>(ou progressão decorada alinhada)
    TRAVADO --> OUVINDO: contradição forte ou repetida
    OUVINDO --> FALLBACK: dúvida > 1 compasso
    TRAVADO --> FALLBACK: dúvida > 1 compasso
    FALLBACK --> OUVINDO: acorde confiável de novo
    OUVINDO --> ENCERRANDO: músico parou
    TRAVADO --> ENCERRANDO: músico parou
    FALLBACK --> ENCERRANDO: músico parou
    ENCERRANDO --> OUVINDO: voltou a tocar (pausa)
    ENCERRANDO --> TRAVADO: voltou a tocar com ciclo travado
    ENCERRANDO --> OCIOSO: silêncio ≥ 2 compassos<br/>(esquece progressão e tempo)
```

- **OCIOSO:** não toca nada. Memória vazia.
- **OUVINDO:** reativo. Troca o acompanhamento quando o acorde ouvido tem confiança alta **e** houve palhetada nos últimos 0,35 s. No modo metrônomo a troca espera o próximo tempo forte (1 ou 3) da grade.
- **TRAVADO:** previsão. A troca é agendada no próximo tempo previsto para o acorde seguinte do ciclo (duração de cada acorde aprendida em meios compassos). ~½ pulso depois, o áudio confirma; se outro acorde for ouvido com alta confiança, toca o ouvido e destrava (e tenta re-travar no ciclo conhecido assim que dois acordes seguidos baterem com ele); continuar ouvindo o acorde anterior por até 1 pulso é tratado como atraso do músico; discordância fraca conta como "falha", e duas seguidas destravam.
- **FALLBACK:** pad na tônica + quinta do tom principal (ou silêncio sem tom).
- **ENCERRANDO:** termina o compasso da última palhetada, faz fade, e mantém memória por até ~2 compassos de silêncio.

Paralelo ao estado, cada quadro tem um **nível de confiança** (CONFIANTE / DÚVIDA / PERDIDO) que define a saída (ver `01-requisitos.md`, RF4).

## Parâmetros

Todos em `acompanhante/params.py`, com unidade e motivo. Ao portar para C++, esse arquivo é a especificação.

## Saídas do pipeline

- `eventos.json` e `.mid` (canal 1 baixo, canal 2 pad, CC11 para expressão);
- `*_render.wav`: violão original + baixo + pad;
- log por quadro (tempo, onset, BPM estimado, acorde ouvido, confiança, acorde tocado, estado, nível), usado por métricas e relatórios.

## Modo ao vivo

```mermaid
flowchart LR
    MIC[Entrada<br/>mic / interface] -->|bloco 512 @ 22 050 Hz| CB[callback de áudio]
    CB -->|× ganho| PR[Processor.process]
    PR -->|baixo + pad × volume| OUT[Saída<br/>alto-falante / fone]
    PR -. estado, acorde, BPM .-> UI[Janela tkinter<br/>atualiza a cada 50 ms]
    UI -->|ganho, volume, monitor| CB
```

- O fluxo duplex é aberto a 22 050 Hz com bloco de 512 no host API padrão (MME no Windows reamostra sozinho). Pior caso medido: 11 ms de processamento por bloco de 23 ms.
- **Compensação de latência (`lookahead`):** a latência informada pelo driver (entrada + saída, ~115 ms no MME) é passada ao `Processor`. O sintetizador renderiza esse tanto à frente do áudio de entrada, e o motor emite as trocas previstas, a parada e os ataques do baixo nesse mesmo adiantamento, para soarem no tempo. Trocas reativas não têm como ser adiantadas: começam na hora em que são decididas.
- Sem log por quadro no modo ao vivo (memória limitada).
