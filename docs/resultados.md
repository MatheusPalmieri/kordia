# Resultados

Como reproduzir: `python tasks.py avaliar <versao>` → `resultados/<versao>/metricas.csv|json` + relatórios PNG + áudio renderizado.
Definição de cada métrica em [`04-plano-de-testes.md`](04-plano-de-testes.md).

---

## v8 — 2026-10-30 (aprender na grade, ouvir antes de tocar, contagem) — medição parcial

**Pendente:** a prova completa e `tasks.py avaliar v8` não terminaram; o sistema interrompeu o processo por falta de memória. Falta medir, portanto, as variantes sintéticas (`clique`, `tom_da_forma`), a tabela dos cenários e a prova inteira em C++. `livre` e `real_livre` não mudaram de caminho. Os números abaixo são os que existem. Para completar: `python -m acompanhante.cli prova --versao v8` e `python tasks.py avaliar v8`.

**Controle: as 6 sessões reais com clique, inteiras** (padrões da interface, tom automático; verdade = posição no ciclo; motor Python antes do port):

| sessão | soou em | antes | agora | errado antes | errado agora | trava agora |
|---|---|---|---|---|---|---|
| 22:50 (29/10) | Si | 0,37 | **0,74** | 24,8 s | 7,4 s | 9,0 compassos |
| 22:51 (29/10) | Dó# | 0,56 | **0,96** | 5,0 s | 0 | 8,5 |
| 22:52 (29/10) | Fá# | 0,97 | 0,97 | 0,6 s | 0 | 7,0 |
| 10:57 (30/10) | Fá# | 0,97 | 0,95 | 0 | 1,4 s | 7,5 |
| 15:52 (30/10) | Fá# | 0,65 | **0,83** | 4,0 s | 3,4 s | 9,0 |
| 15:53 (30/10) | Fá# | 0,47 | **0,97** | 10,8 s | 0 | 9,5 |
| **média** | | **0,66** | **0,90** | **7,5 s** | **2,0 s** | |

A coluna "antes" reproduz cada sessão com a configuração gravada nela. Nas três de 29/10 isso difere da interface atual, mas a nota bate com a da v7 (0,38, 0,56 e 0,97), feita com os padrões da interface.

**Amostra `real_clique`** (60 músicas: os 6 tons pares; mesmos casos nas duas colunas; motor Python, sem a contagem na configuração da prova):

| | v7 | agora |
|---|---|---|
| músicas certas | 18/60 | **30/60** |
| acerto depois de travar | 0,65 | **0,85** |
| harmonia errada por música | 9,0 s | **4,2 s** |

Equivalência: depois do port, os 81 testes passam, inclusive Python × C++ com clique, contagem e manter o padrão ligados, e o controle rodando no núcleo C++ (média ≥ 0,85).

Leitura honesta:

- **Causa do relato:** a confiança por quadro quase nunca passa de 0,70 no primeiro tempo de um acorde real. Na 15:53, o B ficou de fora do ciclo e o início do C# foi datado 2 tempos depois da barra; com *manter o padrão*, os dois erros nunca se corrigiam. O clique estava certo; a grade da memória é que estava errada.
- **Custo: trava mais tarde.** Eram ~5–7 compassos, agora ~7–9,5 num ciclo de 4 compassos. Até lá, só a tônica. Um ciclo de 8 compassos leva ~16. Escolha deliberada: 1,5 volta dava menos acerto (controle 0,79 × 0,90; amostra 24 × 30 músicas).
- **Sessão 10:57 piorou um pouco** (0,97 → 0,95): a 2ª metade de um compasso lida como outro acorde entra no padrão até as voltas seguintes corrigirem.
- **A 22:50 ainda erra:** a harmonia oscila entre Ab e Abm. A soma das voltas decide a terça por posição, e as duas metades de um compasso podem discordar.
- **Menor lida como o maior relativo continua:** Bbm de pestana vira C#. É o que derruba os tons em Ré e Dó da amostra. Pesar a nota grave por meio compasso **não foi adotado**: a 22:51 sobe de 0,75 para 1,00, mas a 15:52 cai de 0,82 para 0,55; no total, 0,86 → 0,81 (peso 0,1, abaixo de 200 Hz).
- **`falsa_volta` segue em 0/6 na amostra:** agora o padrão aprende a volta longa depois de 2 voltas, mas o período até lá toca o ciclo curto.
- **Só vale com clique.** Sem clique, nada mudou, nem a qualidade do reconhecimento.
- **Não testado com o violão ao vivo.** Ouvir antes do Iniciar e a contagem foram verificados no driver real pelo `KordiaLiveTest`, com o microfone em silêncio. A troca de acordes foi medida reproduzindo sessões gravadas.

---

## v7 — 2026-10-30 (prova: músicas novas nos 12 tons)

Como reproduzir: `python -m acompanhante.cli prova --versao v7` → `resultados/v7/prova.csv` (cerca de 10 min; 852 músicas). Configuração do app ao vivo: aprender na tônica + manter o padrão, tom automático. Definição em [`04-plano-de-testes.md`](04-plano-de-testes.md).

| variante | o que é | músicas certas | acerto depois de travar | harmonia errada por música |
|---|---|---|---|---|
| `clique` | sintético, metrônomo | 123/204 | 0,83 | 3,8 s |
| `tom_da_forma` | sintético, tom configurado meio tom acima do que soa (sessão de 30/10) | 121/204 | 0,83 | 3,9 s |
| `livre` | sintético, sem clique | 97/204 | 0,81 | 6,0 s |
| `real_clique` | compassos do violão real, remontados e transpostos | 24/120 | 0,61 | 10,4 s |
| `real_livre` | idem, sem clique | 19/120 | 0,63 | 10,3 s |
| **sintético** | | **341/612 (56 %)** | **0,82** | |
| **violão real** | | **43/240 (18 %)** | **0,62** | |

"Certa" = travou, acerto ≥ 90 % depois de travar e no máximo 2 s de harmonia errada.

**Controle com as sessões reais (mesma nota):**

| sessão | soou em | original, sem cortar | mesmos compassos e ordem, com cortes |
|---|---|---|---|
| formas de Dó (22:50) | Si | 0,38 | 0,21 |
| formas de Ré (22:51) | Dó# | 0,56 | 0,28 |
| formas de Sol (22:52) | Fá# | 0,97 | 0,97 |
| formas de Sol (30/10, até 100 s) | Fá# | 0,97 | 0,97 |

Leitura honesta:

- **O acerto é bimodal:** 0,98 quando o padrão trava certo, 0,3–0,7 quando trava errado. Duas das quatro sessões reais já estavam erradas e isso não aparecia: a v6 registrava só "travou em 25,5 s", não *no quê*.
- **O aprendizado depende de sorte na janela de 1,5 volta:** sorteados, os compassos da sessão de 30/10 caem de 0,97 para 0,57. Por sessão de origem, em `real_clique`: 22:52 dá 0,82; 22:50 dá 0,59; 22:51 dá 0,47.
- **`real_*` é pessimista; compare entre versões, não leia 18 % como taxa no palco.** A verdade vem da posição no ciclo, e alguns compassos podem ser variação do músico (em 30/10, dois compassos de B foram ouvidos como F#). Ao sortear, eles entram em qualquer lugar. Os cortes só se mostraram neutros nas duas sessões boas; nas fracas, remontar na mesma ordem baixou a nota.
- **`falsa_volta` (C G C G Am F): 0/60.** O ciclo de 2 acordes trava antes de chegar o Am. Limite do desenho do aprendizado, ainda não atacado.
- **O sintético não julga tipo de forma:** nele as formas abertas foram piores que as pestanas, o contrário do medido no violão (decisão 78). E ele continua mais fácil que o real em confiança (0,73 × 0,56–0,69).

**Corrigido nesta versão:** as durações do ciclo vinham da volta mais antiga (decisão 91). `emprestado` com clique e tom da forma: 0/24 → 20/24. Nos 14 cenários, a média segue em 0,90.

**Hipóteses medidas e não adotadas** (96 músicas `real_clique`; base: 23 certas, acerto 0,63, 9,7 s errados, trava em 5,4 compassos):

| mudança | certas | acerto | errado | trava em |
|---|---|---|---|---|
| desligar *manter o padrão* | 10 | 0,52 | 6,4 s | 5,4 compassos |
| exigir 2 voltas inteiras para travar | 18 | 0,59 | 7,7 s | 7,1 compassos (4 músicas sem travar) |

---

## v6 — 2026-10-29 (aprender na tônica, tom automático, camadas)

**Sessões reais** (violão meio tom abaixo; reproduzidas sem tom configurado, com aprender na tônica + manter o padrão):

| sessão | soou em | antes de travar | travou em |
|---|---|---|---|
| formas de Dó (22:50) | Si (B F# G#m E) | só pad em Si | 25,5 s |
| formas de Ré (22:51) | Dó# (C# G# A#m F#) | só pad em Dó# | 22,6 s |
| formas de Sol (22:52) | Fá# (F# C# D#m B) | só pad em Fá# | 22,2 s |

Acordes ouvidos com confiança: Sol → 98 % dentro da progressão; Dó → 89 % (G#m às vezes vira G#); Ré → o A#m da pestana quase não aparece (vira C#).

**Cenários** (14, laboratório com os padrões antigos): média 0,90, latência travada 44 ms; nada mudou em relação à v5. Com aprender na tônica: antes de travar só há a tônica (C nos cenários em Dó, B no `tom_errado`), testado em 3 cenários.

---

## v5 — 2026-10-29 (manter o padrão)

Cenários (14; núcleo C++): média **0,90**, latência média 122 ms, travada 44 ms, falso disparo médio 0,02 s (só `erros_com_padrao`, que roda com o modo manter e encerra 0,6 s depois do fim).

`erros_com_padrao` (erros do músico com o padrão travado):

| | acerto | latência travada | errado confiante |
|---|---|---|---|
| áudio manda, antes das decisões 72–73 | 0,876 | 148 ms | 1 |
| áudio manda, v5 | 0,921 | 72 ms | 0 |
| **manter o padrão, v5** | **0,960** | **37 ms** | **0** |

Modo manter nos outros cenários (custo medido):

| cenário | áudio manda | manter |
|---|---|---|
| estavel_120 | 0,94 | 0,94 |
| ruido | 0,91 | 0,91 |
| modo_metronomo | 0,93 | 0,93 |
| pausa_e_fim | 0,93 | 0,93 |
| acorde_fora_do_tom | 0,92 | 0,89 |
| troca_de_tom (sem pausa) | 0,93 | 0,90 |

Em todos com o modo manter: parada ~0,6 s depois do fim do último compasso (5 tempos sem palhetada) e 0,2–0,6 s de "falso disparo" por isso.

---

## v4 — 2026-10-29 (depois do segundo teste ao vivo)

**Sessão real** (`sessoes/2026-10-29_21-39-39`, violão em Si, 37 s antes da pane do driver), reproduzida com o algoritmo novo:

| | v3 | v4 |
|---|---|---|
| palhetadas detectadas (tocadas: ~57) | 30 | **55** |
| paradas falsas | 3 | 1 (+ 1 pausa real de 1,8 s, onde ele parou e voltou) |
| pad tocando Dó por cima de Si | sim | **não** |

**Ao vivo** (microfone USB + headset): modo compartilhado corrigido, 22 ms de latência informada, 60 s sem a captura morrer (no exclusivo com dois aparelhos ela morria em ~15–20 s).

**Cenários** (13, agora com `tom_errado`; núcleo C++): média de acerto **0,90** (v2: 0,89), latência média 125 ms, latência travada 45 ms, zero falso disparo. `tom_errado` = 0,96. O resto igual à v2 (o piso novo do detector não mudou os cenários).

---

## v3 — 2026-10-29 (port para C++/JUCE)

As métricas de algoritmo são **as mesmas da v2**: o núcleo C++ dá decisões idênticas (12/12 cenários, `tests/test_cpp.py`). O que mudou foi o motor ao vivo:

| ao vivo, nesta máquina | app Python (v2) | app C++/JUCE (v3) |
|---|---|---|
| driver | MME | WASAPI exclusivo |
| latência informada pelo driver (entrada + saída) | ~116 ms | **~40 ms** |
| buffer | 512 a 22,05 kHz (23 ms) | 480 a 44,1 kHz (10,9 ms) |
| custo médio por bloco | ~1 ms (de 23 ms) | **~0,07 ms** (de 10,9 ms) |
| falhas de áudio em teste de 2–15 s | 0 | 0 (após a partida) |
| processamento offline de 38 s de áudio | 2,6 s | 0,4 s |

O que ainda não foi medido: latência real ponta a ponta (violão → caixa) com loopback físico; sessões longas com violão. A latência das **trocas reativas** continua ~250 ms de algoritmo + driver; o ganho do C++ é nos ~75 ms de driver.

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
