# 01 — Requisitos

## Escopo

**Dentro (agora):** violão (ou guitarra limpa) tocando acordes completos com batida; saída de baixo + pad seguindo os acordes; andamentos de ≈ 60 a 130 BPM, mas sem quebrar fora disso.

**Fora (por enquanto):** solos, melodias e licks (risco de o sistema tratar notas de solo como acordes; solução futura: footswitch "congelar acorde"); reconhecer a música; acompanhar voz ou bateria; VST, mobile.

## Requisitos funcionais

### RF1 — Duas camadas independentes
- **RF1.1 Qual acorde:** identificar o acorde tocado (maior, menor; variações simples como 7ª/sus são reduzidas à tríade base). Produzir uma **distribuição de probabilidade**, não só um rótulo.
- **RF1.2 Quando trocar:** seguir o pulso e a posição no compasso e **prever** o instante da próxima troca.
- As duas camadas têm testes separados.

### RF2 — Modos de operação
- **RF2.1 Livre (sem clique):** estima o andamento a partir das palhetadas, segue acelerações e desacelerações graduais; as trocas previstas aceleram junto. Se perder o tempo, entra em modo de segurança (reativo, sem previsão, notas sustentadas).
- **RF2.2 Metrônomo:** BPM fixo dado pelo usuário. A grade é fixa; o áudio só diz **qual** acorde. Uma troca antecipada pelo músico é aplicada só no tempo certo da grade. Com clique, a harmonia é lida por meio compasso e o padrão é aprendido na grade (RF16).
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

### RF9 — Uso ao vivo por interface gráfica
- Escolher dispositivo de **entrada** (microfone, interface, mesa) e de **saída** (padrão: alto-falante do sistema).
- Configurar, todos opcionais: tom principal, BPM fixo (liga/desliga o modo metrônomo), progressão conhecida (liga/desliga o modo decorado). BPM e progressão podem ser usados juntos.
- Ajustar ganho de entrada (com medidor de nível), volume do acompanhamento (até 200 %), dinâmica fixa ou seguindo a batida, e monitorar o violão na saída.
- **Clique audível** (liga/desliga e volume ao vivo). Com BPM fixo, a grade e o clique começam no Iniciar. No modo livre, o clique segue o andamento detectado.
- **Fixar BPM detectado** ao vivo (o músico passa a seguir o clique).
- Botão **Iniciar/Parar**. Ao iniciar, o sistema ouve a entrada e toca baixo + pad em tempo real.
- Mostrar ao vivo: acorde tocado, acorde ouvido e confiança, próximo acorde previsto, estado (em linguagem simples), BPM e pulso no compasso, padrão aprendido, diário de decisões com motivo, latência, custo de processamento e falhas de áudio.
- Interface moderna e apresentável (referência: apps da Apple), tema claro/escuro.
- Compensar a latência de saída do driver nas trocas previstas e no pulso do baixo.

### RF10 — Logs e reprodução de sessões
- Cada sessão ao vivo grava entrada, configuração, estado por bloco, decisões com motivo e resumo, sem I/O no callback de áudio.
- Ao parar, gera o relatório visual da sessão.
- Qualquer sessão pode ser reproduzida offline com resultado idêntico (`rodar --sessao`).

### RF11 — Som
- Pad de worship: serras desafinadas, voicing aberto (fundamental, quinta, oitava, décima, shimmer), passa-baixa, ataque ~0,9 s, liberação ~2,2 s (acordes se fundem), reverb estéreo grande. Ao parar, fade curto (0,6 s).
- Baixo audível também em alto-falante pequeno (senoide saturada).
- Volume alto o bastante sem distorcer (limitador suave na saída).

### RF12 — Manter o padrão aprendido (configurável; padrão: ligado)
- Depois que o ciclo trava, erros e atrasos do músico (acorde errado, troca atrasada, entrada atrasada) não destravam: o acompanhamento segue a progressão na grade, com volume constante.
- Dúvida não leva à tônica: com o padrão travado, o padrão é a melhor aposta.
- Mudança deliberada (outra parte, outro tom): aceita quando o áudio discorda com confiança por 1½ compasso seguido.
- Parada: depois de 5 tempos sem palhetada (uma entrada atrasada não é o fim); esquece após 2 compassos.
- Desligado: comportamento anterior (o áudio manda; contradição sustentada por ½ pulso destrava).

### RF13 — Aprender segurando a tônica (configurável; padrão: ligado)
- Até o padrão travar: só um pad suave (60 %) na tônica do tom, sem baixo e sem seguir acorde a acorde (era "uma sambança de troca de nota").
- O tom é o configurado enquanto bater com os acordes ouvidos; senão, o detectado por eles; sem nenhum dos dois, silêncio.
- Com o padrão travado: acompanhamento completo. Se destravar (mudança deliberada), volta a aprender na tônica.

### RF14 — Tom automático
- Opção *Automático* no seletor de tom: deduz a tonalidade maior pelos acordes ouvidos (I, IV e V pesam mais; precisa de 3 acordes diferentes). A tela mostra o tom detectado.

### RF15 — Camadas de instrumento
- Pad worship, órgão, piano elétrico, pad de guitarra, cordas e baixo; cada um liga/desliga e tem volume próprio, ao vivo.

### RF16 — Ouvir antes de tocar, contagem e aprender na grade
- **Ouvir ao abrir:** com o app aberto e parado, a tela mostra o acorde (ou a nota) que está reconhecendo e o nível do violão. Não toca nada e não grava sessão.
- **Contagem:** no Iniciar com BPM conhecido, 4 cliques, 4-3-2-1 na tela. Nada é tocado nem aprendido antes do primeiro tempo do compasso seguinte. O clique da contagem soa mesmo com o botão Clique desligado.
- **Aprender na grade (com clique, sem progressão conhecida):** um acorde por meio compasso, lido pela soma do meio compasso. O ciclo é a menor volta de compassos inteiros que se repete, depois de 2 voltas. Cada posição do ciclo soma as últimas 3 voltas, então o padrão se corrige sozinho. Três meios compassos seguidos contra o padrão (com *manter o padrão*) destravam e ele reaprende.

### RF9 (revisão) — Interface
- Tela principal enxuta; Áudio, Som/Instrumentos e Música avançada num painel de Ajustes. Tom no topo.
- O botão Clique inicia tudo quando parado; no modo seguir, o clique começa no BPM escolhido e depois segue o músico.
