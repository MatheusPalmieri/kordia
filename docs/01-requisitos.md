# 01 — Requisitos

## Escopo

**Dentro (agora):** violão (ou guitarra limpa) tocando acordes completos com batida; saída de baixo + pad seguindo os acordes; andamentos de ≈ 60 a 130 BPM, mas sem quebrar fora disso.

**Fora (por enquanto):** solos, melodias e licks (risco de o sistema tratar notas de solo como acordes; solução futura: footswitch "congelar acorde"); reconhecer a música; acompanhar voz ou bateria; interface gráfica, VST, mobile.

## Requisitos funcionais

### RF1 — Duas camadas independentes
- **RF1.1 Qual acorde:** identificar o acorde tocado (maior, menor; variações simples como 7ª/sus são reduzidas à tríade base). Produzir uma **distribuição de probabilidade**, não só um rótulo.
- **RF1.2 Quando trocar:** seguir o pulso e a posição no compasso e **prever** o instante da próxima troca.
- As duas camadas têm testes separados.

### RF2 — Modos de operação
- **RF2.1 Livre (sem clique):** estima o andamento a partir das palhetadas, segue acelerações e desacelerações graduais; as trocas previstas aceleram junto. Se perder o tempo, entra em modo de segurança (reativo, sem previsão, notas sustentadas).
- **RF2.2 Metrônomo:** BPM fixo dado pelo usuário. A grade é fixa; o áudio só diz **qual** acorde. Uma troca antecipada pelo músico é aplicada só no tempo certo da grade.
- **RF2.3 Decorado:** o usuário informa a progressão (ex.: `C G Am F` em loop). O sistema prevê o próximo acorde e agenda a troca; se o áudio contradiz com clareza, corrige.

### RF3 — Tom principal como âncora
- Opcional (ex.: Dó). Acordes diatônicos têm probabilidade a priori maior.
- Acordes fora do tom **não são bloqueados**: são só "menos prováveis de antemão".
- Se o sistema se perder: pad na tônica do tom principal. Sem tom configurado: silêncio.

### RF4 — Escada de confiança (a mais importante)
| Nível | Situação | Saída |
|---|---|---|
| CONFIANTE | acorde claro | baixo na fundamental + pad com o acorde completo |
| DÚVIDA | entre dois acordes | mantém o acorde anterior; se a dúvida for maior/menor da mesma fundamental, toca fundamental + quinta (sem terça) |
| PERDIDO | nada confiável | pad tônica + quinta do tom principal; sem tom, silêncio |

### RF5 — Dinâmica
- A energia das palhetadas controla o volume do baixo/pad, com curva **suavizada** (não oscila a cada batida).
- Padrão: seguir a dinâmica. (Futuro: opção de nível fixo.)

### RF6 — Parar quando o músico para
- Ao detectar que o músico parou, o acompanhamento **termina o compasso** em que estava a última palhetada (usando o andamento rastreado) e faz um fade curto.
- Silêncio curto (< ~1 compasso) é **pausa**: o acompanhamento abaixa e segura; a memória da progressão é mantida.
- Silêncio longo (≥ ~2 compassos) é **fim**: o sistema esquece a progressão e o andamento e volta ao estado inicial.

### RF7 — Notas de passagem e ornamentos
- Persistência: uma mudança só vira troca se durar o suficiente ou cair em tempo forte.
- Peso por posição: tempo forte pesa mais.
- O acompanhamento toca o acorde **base** (ex.: Lá menor), não a ornamentação (ex.: Lá menor com baixo em Sol).

### RF8 — Aprender o padrão
- Primeira volta: só reage, com cautela.
- Ao detectar repetição, **trava** o ciclo e passa a prever.
- Discordância única com baixa confiança: mantém a previsão. Discordância com alta confiança ou repetida: abandona o padrão e reaprende.
- Métodos clássicos primeiro (chromagram, templates, HMM, detecção de ciclos). ML só se houver ganho medido.

## Requisitos não funcionais

- **RNF1 Causalidade/streaming:** todo processamento é bloco a bloco, sem olhar o futuro, inclusive no harness offline. Nada de funções que precisem do áudio inteiro.
- **RNF2 Latência:** o atraso estrutural do pipeline é de 1 bloco (512 amostras a 22 050 Hz ≈ 23 ms) mais o tempo de decisão. Trocas previstas devem cair a menos de ±¼ de pulso do tempo real; trocas reativas, a menos de ~½ pulso.
- **RNF3 Portabilidade:** algoritmos simples e explícitos, portáveis para C++/JUCE. Todos os parâmetros (janelas, limiares) ficam documentados em `acompanhante/params.py`.
- **RNF4 Mensurabilidade:** toda mudança relevante é avaliada nos cenários e registrada em `docs/resultados.md`.
- **RNF5 Segurança musical:** falso disparo (tocar quando não devia) é o erro mais caro e tem métrica própria.
