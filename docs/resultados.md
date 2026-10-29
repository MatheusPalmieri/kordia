# Resultados

Como reproduzir: `python tasks.py avaliar <versao>` → `resultados/<versao>/metricas.csv|json` + relatórios PNG + áudio renderizado.
Definição de cada métrica em [`04-plano-de-testes.md`](04-plano-de-testes.md).

---

## v2 — 2026-10-29 (depois do primeiro teste ao vivo)

Mudanças: travamento mais rápido e mais teimoso (decisões 38–44), som novo e mais alto (45–46), cenário de realimentação recalibrado (47).

| métrica (média dos 12) | baseline | v2 |
|---|---|---|
| acerto do acorde (`acc_total`) | 0,88 | **0,89** |
| latência média das trocas | 160 ms | **128 ms** |
| latência travada | 58 ms | **47 ms** |
| falso disparo | 0 s | 0 s |
| errado com confiança (episódios) | 0,42 | **1,25** ⚠ |
| recuperação após surpresa | 1,57 pulso | 2,42 pulsos ⚠ |
| `corr_dinamica` | 0,96 | 0,75 ⚠ |
| volume do acompanhamento | −26,0 dBFS | **−11,9 dBFS** |

Por cenário, o que mudou de verdade:
- **`ruido` 0,84 → 0,91** e passou a travar cedo (blips não quebram mais o ciclo).
- `acelerando` 0,94 → 0,96, `batida_abafada` 0,95 → 0,96, `modo_metronomo` latência média 120 → 82 ms: travam antes.
- `estavel_120` igual (0,93 → 0,94), trava no 6º acorde em vez do 8º.
- **Piorou:** erros confiantes subiram (0,42 → 1,25 episódio por cenário). Em boa parte é o custo deliberado da decisão 39: durante o ½ pulso de persistência o sistema ainda toca o acorde previsto enquanto o violão já mudou, e a métrica conta isso. Em `desacelerando_final` (4) e `realimentacao` (4), os dois problemas conhecidos pioram com o pad longo.
- `realimentacao` 0,66 → 0,62 com o retorno no mesmo nível relativo: a cauda longa do pad e o reverb voltam na entrada e seguram o acorde antigo.

Leitura honesta: no sintético, a v2 é melhor na média e em ruído, e pior em surpresas e em realimentação. A mudança foi guiada pelo que o Matheus ouviu ao vivo (padrão que não se mantinha, troca para nota errada), que o sintético não reproduzia. As sessões gravadas vão dizer se o equilíbrio está certo.

---

## baseline — 2026-10-29 (passo zero)

Áudio **sintético** (Karplus-Strong), 12 cenários, tudo causal e em streaming. Processa ~40 s de áudio em ~1 s.

| cenário | acc | acc conf. | lat. média (ms) | lat. reativa (ms) | lat. travada (ms) | p95 (ms) | perdidas | erro BPM | falso disparo (s) | errado conf. | parada (s) | recup. (pulsos) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| estavel_120 | 0,93 | 0,96 | 139 | 263 | 28 | 377 | 0 | 0,5 | 0 | 0 | 0,00 | – |
| acelerando | 0,94 | 0,97 | 117 | 240 | 42 | 258 | 0 | 1,3 | 0 | 0 | 0,03 | – |
| desacelerando_final | **0,81** | 0,90 | 87 | 280 | −84 | 371 | 0 | **13,1** | 0 | 2 | **−3,04** | – |
| dinamica | 0,95 | 0,98 | 116 | 225 | 20 | 250 | 0 | 0,3 | 0 | 0 | 0,02 | – |
| pausa_e_fim | 0,92 | 0,97 | 174 | 261 | 36 | 429 | 0 | 0,3 | 0 | 0 | 0,02 | – |
| nota_de_passagem | 0,84 | 0,93 | 218 | 227 | 177 | 288 | 1 | 0,3 | 0 | 1 | 0,02 | – |
| troca_de_tom | 0,93 | 0,97 | 164 | 247 | 38 | 421 | 0 | 0,3 | 0 | 0 | 0,02 | 0,69 |
| acorde_fora_do_tom (decorado) | 0,93 | 0,98 | 96 | 311 | 37 | 373 | 1 | 0,3 | 0 | 0 | 0,03 | 2,44 |
| batida_abafada | 0,95 | 0,97 | 109 | 210 | 18 | 239 | 0 | 0,3 | 0 | 0 | 0,02 | – |
| ruido | 0,84 | 0,94 | 239 | 342 | 93 | 826 | 0 | 0,2 | 0 | 0 | 0,00 | – |
| realimentacao | **0,66** | 0,92 | **344** | 358 | **332** | 929 | **4** | 0,2 | 0 | 1 | 0,02 | – |
| modo_metronomo | 0,92 | 0,94 | 120 | 303 | −42 | 416 | 0 | 0,0 | 0 | 1 | −0,03 | – |
| **média** | **0,88** | **0,95** | **160** | **272** | **58** | 431 | 0,5 | 1,4 | **0** | 0,4 | −0,24 | 1,57 |

Outros: tempo até travar o andamento ≈ 1,5 s em todos os cenários livres; `corr_dinamica` = 0,96 em `dinamica`; `esqueceu` = sim em `pausa_e_fim` e `troca_de_tom`; cobertura ≈ 99 %.

### Onde o sistema acerta

- **Nunca tocou quando não devia**: zero segundos de falso disparo em todos os cenários, inclusive com ruído e realimentação. Para no fim do compasso (parada ≈ 0,02 s depois da barra) e esquece a progressão após silêncio longo. O princípio "errar para o lado do silêncio" está funcionando.
- **A previsão funciona**: depois que o ciclo trava, as trocas caem a ~20–40 ms do tempo real, contra ~250 ms reagindo. É a tese central do projeto (risco 1) confirmada em números.
- **Andamento estável e rampas**: erro < 1,5 BPM com colcheias, palhetadas abafadas e aceleração de 100 → 125 BPM; trava o andamento em ~1,5 s.
- **Metrônomo**: trocas adiantadas do violão são seguradas até a grade (latência travada −42 ms, ou seja, no tempo).
- **Acorde fora do tom / mudança de tom**: Em, E e Bb no lugar do esperado são aceitos (o prior do tom não bloqueia); com a progressão decorada, o sistema toca o previsto, ouve a contradição, corrige em < 1 compasso e volta a travar no ciclo.
- **Dinâmica**: correlação 0,96 entre a intensidade das palhetadas e a velocity do acompanhamento.

### Onde falha, e por quê

1. **Realimentação (acc 0,66, 4 trocas perdidas, latência ~340 ms mesmo travado).** Com o próprio baixo/pad voltando na entrada a −6 dB, o chroma fica dominado pelo acorde que o sistema já está tocando: ele "se confirma" e segura o acorde antigo por até um compasso (no relatório: Sol tocando sobre Lá menor por ~4 s). O sistema **não** fica preso tocando depois do fim (para corretamente). Observação honesta: a faixa de onsets acima de 150 Hz (decisão 16) não fez diferença neste cenário; o sistema para igual sem ela.
2. **Ritardando final (erro de BPM 13, parou 3 s antes).** O rastreador (ACF de 8 s + PLL com ganho baixo) fica para trás numa desaceleração de 110 → 65 BPM em 4 compassos e no último acorde salta para ~146 BPM. Com o andamento errado, a regra de parada encerra cedo. O erro é para o lado seguro (silêncio), mas corta o acorde final que soaria. Aumentar o ganho do PLL (testado 0,2 e 0,3) não ajudou e piorou o jitter nos outros cenários.
3. **Latência reativa ~½ pulso e aprendizado lento.** Antes de travar (2 voltas completas ≈ 16 s a 120 BPM) e após toda surpresa, a troca chega ~250 ms atrasada (janela de 186 ms + confirmação). Notas de passagem e ruído geram trocas espúrias curtas que quebram a repetição exata e atrasam o travamento (`nota_de_passagem` só travou aos 32 s).
4. **Lá menor ouvido como Lá maior.** O voicing aberto de Am tem uma única corda com Dó; o 5º harmônico do Lá cai em Dó#. Em `nota_de_passagem` isso gerou ~1 s de Lá maior **com confiança alta**: o tipo de erro mais caro (terça errada).
5. **Ruído + voz (acc 0,84, p95 826 ms).** A confiança cai, várias trocas viram "dúvida" e esperam; não toca nota errada, mas atrasa.

### Ressalva

Áudio sintético é otimista: o Karplus-Strong é mais limpo e mais regular que um violão gravado no celular. Esses números são um teto. O próximo dado que importa é o das gravações reais.

### Histórico de ajustes até o baseline (mesma sessão)

| mudança | efeito medido |
|---|---|
| templates com 6 harmônicos (era 4) | acurácia do acorde ouvido 0,885 → 0,904 (`estavel_120`) |
| confiança = softmax por quadro suavizado (o posterior do HMM saturava em 1,0 até nos erros) | acertos com confiança ~0,85 e erros ~0,55–0,67: a escada de confiança passou a ter sentido |
| atraso do músico ≠ contradição; sem troca antecipada no metrônomo | `modo_metronomo` acc 0,87 → 0,92 |
| troca reativa só logo após palhetada; memória descarta acordes < 1 pulso | `ruido` acc 0,77 → 0,84 e passou a travar |
| período refratário 80 → 100 ms | removeu dupla detecção no espalhamento da batida |

### Os 3 maiores problemas e o próximo passo

1. **Realimentação**: o sistema se ouve e segura o acorde antigo. Próximo passo: cancelar o próprio sinal (temos a saída exata, amostra a amostra; um filtro adaptativo NLMS estima o caminho acústico), e enquanto isso recomendar DI + retorno em fone.
2. **Latência e aprendizado antes de travar**: ~½ pulso de atraso nas 2 primeiras voltas e depois de cada surpresa; blips atrasam o travamento. Próximo passo: travar com 1,5 volta quando a confiança for alta e usar a fase do compasso para agendar até as trocas reativas no próximo tempo, em vez de tocar quando detecta.
3. **Ritardando e terça errada (Am → A)**: o tempo se perde em desacelerações fortes; Lá menor aberto vira Lá maior com confiança alta. Próximo passo: ACF com janela menor/ponderada no recente + PLL que aceita erro maior quando a tendência é consistente; para a terça, usar o chroma da região grave (fundamental) e tratar maior × menor da mesma fundamental como dúvida (tocar sem terça) até haver evidência sustentada.

**Recomendação imediata:** antes de otimizar mais o sintético, rodar as primeiras gravações reais do celular (`rodar`), porque elas vão dizer qual desses três dói mais na prática.
