# Registro de decisões

Formato: data — decisão. **Motivo.** *Alternativas.*

## 2026-10-29 — Passo zero

1. **Python 3.15 em `.venv` com pip; dependências em `pyproject.toml`.** O `uv` não está instalado na máquina; venv + pip é o caminho sem fricção. *Alternativa: uv.*
2. **`tasks.py` como executor de tarefas (`python tasks.py test|avaliar|gerar`), além de um `Makefile`.** Não há `make` no Windows desta máquina. *Alternativa: só Makefile.*
3. **Taxa de amostragem interna 22 050 Hz; bloco/hop de 512 amostras (≈ 23 ms).** Harmonia de violão cabe até ~5 kHz; metade do custo de 44,1 kHz. Áudio de entrada com outra taxa é reamostrado (`resample_poly`) e mixado para mono.
4. **Duas FFTs por bloco: 1024 para onsets, 4096 para harmonia.** Onsets precisam de resolução temporal; o chroma precisa de resolução em frequência nas cordas graves (≈ 5,4 Hz/bin). A janela de 4096 (186 ms) é o principal custo de latência da camada "qual acorde", por isso o modo travado prevê as trocas.
5. **Vocabulário: 24 tríades (maior/menor) + "N" (sem acorde).** 7ª, sus, add9 são reduzidos à tríade base, coerente com "acompanhamento canônico". *Alternativa: templates de 7ª/sus — adiado até medir necessidade.*
6. **Persistência via filtro HMM causal (forward), não por regras de contagem.** Um único mecanismo explica persistência, peso por posição (probabilidade de troca maior logo após onset em tempo forte) e prior por tom (distribuição de destino da transição). Fácil de portar (matriz 25×25).
7. **Rastreador de tempo: autocorrelação da envoltória de fluxo (prior log-normal ~105 BPM) + PLL.** ACF resolve "2 palhetadas por pulso"; PLL segue rampas. *Alternativa: só intervalos entre onsets (falha com colcheias); madmom (proibido).*
8. **Compasso assumido 4/4.** A fase do compasso começa no primeiro onset (músico começa no tempo 1) e é corrigida pela posição das trocas de acorde confirmadas.
9. **Regra de parada:** "parou" = nenhum onset por mais de max(1,5 pulso, 1,25 × maior intervalo recente entre onsets). Ao parar, toca até o fim do compasso da última palhetada (ou para já, se esse fim já passou) e faz fade de 0,4 s. Memória mantida até 2 compassos de silêncio; depois esquece progressão e tempo (OCIOSO). **Motivo:** concilia "termina o compasso", "pausa curta só abaixa" e "silêncio longo esquece" sem tocar compassos extras depois do fim.
10. **Modo decorado: cada acorde da progressão dura 1 compasso; o alinhamento com o ciclo é feito pelo primeiro acorde ouvido com confiança.** Simples; durações diferentes ficam para depois.
11. **Modo metrônomo: grade inicia no primeiro onset; trocas aplicadas no próximo tempo forte (1 ou 3).** Se a troca é ouvida até ½ pulso depois de um tempo forte, aplica na hora (o músico atrasou). Não há gravação do clique no WAV para alinhar a grade.
12. **Ciclo trava após 2 repetições completas** (período 2–8 acordes). Conservador: errar travando tarde custa só latência; travar errado custa nota errada.
13. **Prior por tom: diatônicos peso 1,0; demais 0,3.** "Menos provável", nunca bloqueado.
14. **Sintetizador e escritor MIDI próprios** (aditivo simples, streaming, precisão de amostra; MIDI tipo 0 escrito à mão). Evita dependências e já é o formato que o C++ vai precisar. *Alternativa: mido, fluidsynth.*
15. **Realimentação em laço fechado de verdade**: a saída do sintetizador volta na entrada do próximo bloco (atraso 30 ms, −6 dB). Testa o risco real (o sistema se seguir e não parar), não uma aproximação em duas passadas.
16. **Onsets só na faixa 150 Hz–5 kHz.** Os ataques do próprio baixo (≤ 125 Hz) não viram "palhetada" na realimentação; a palhetada de violão é banda larga e continua detectável. *Medido depois: no cenário `realimentacao` atual não fez diferença (mesmo resultado com 30 Hz). Mantido como precaução barata; reavaliar com gravação real.*
17. **librosa não é usado.** O pipeline é todo próprio e causal; nada de função offline.
18. **Saída com 1 bloco de atraso estrutural**: a decisão tomada com o bloco *k* vale a partir do início do bloco *k+1*, como em tempo real.
19. **Confiança ≠ posterior do HMM.** O HMM decide *qual* acorde; a confiança é um softmax por quadro (β = 25 sobre o cosseno) suavizado por EMA (0,3), lido no acorde escolhido pelo HMM. **Motivo:** o posterior do HMM saturava em ~1,0 até nos erros, o que tornava a escada de confiança inútil. Com a nova medida, acertos têm mediana ~0,85 e erros ~0,55–0,67.
20. **Templates com 6 harmônicos** (peso 0,6^(h−1)). O 5º harmônico (terça maior) e o 6º (quinta) aparecem no espectro do violão; com 4, Lá menor era confundido com Lá maior com mais frequência (acurácia ouvida 0,885 → 0,904).
21. **Troca reativa só até 0,35 s depois de uma palhetada.** Uma troca de acorde nasce de uma batida; derivas entre batidas (decaimento, ruído, voz) não trocam o acompanhamento. `ruido`: 0,77 → 0,84.
22. **Memória descarta acordes que duraram menos de 1 pulso** antes de procurar ciclos. Senão um "blip" quebra a repetição exata e o sistema nunca trava.
23. **Ouvir o acorde anterior até 1 pulso depois de uma troca prevista = músico atrasado, não contradição.** E no modo metrônomo não há troca antecipada (a grade manda). Sem isso o modo metrônomo destravava a cada troca atrasada (acc 0,87 → 0,92).
24. **Período refratário de onsets 80 → 100 ms.** A batida espalhada em 6 cordas (~70 ms) gerava dupla detecção; 100 ms ainda permite semicolcheias a 130 BPM (115 ms).
25. **PLL_BETA mantido em 0,10.** Testado 0,2 e 0,3 para seguir o ritardando: não melhorou `desacelerando_final` e aumentou o jitter de BPM nos outros cenários.
26. **Latência separada em reativa × travada nas métricas.** A média misturada escondia o efeito da previsão, que é a tese central.
27. **Teste de causalidade automático** (`test_pipeline_is_causal`): trocar o futuro do áudio não pode mudar nenhum evento nem amostra de saída antes do ponto de troca.

## 2026-10-29 — Interface ao vivo

28. ~~**Interface em tkinter + áudio em sounddevice (PortAudio).**~~ *Substituída pela decisão 36.* tkinter vem com o Python; sounddevice é a forma mais direta de abrir entrada e saída em tempo real. *Alternativas: PyQt (dependência pesada), app web (latência e acesso a áudio piores), JUCE (fica para a versão C++).*
29. **Um único `Processor` para arquivo e ao vivo.** O `run()` foi refatorado para usá-lo; as métricas ficaram idênticas ao baseline. Garante que o que é medido nos cenários é o que toca ao vivo.
30. **Fluxo aberto direto a 22 050 Hz no host API padrão (MME).** O Windows reamostra; evita reamostrar em Python dentro do callback. *Limite: MME tem latência alta (~115 ms medidos); WASAPI/ASIO de baixa latência ficam para o C++/JUCE.*
31. **Compensação da latência de saída (`lookahead`).** Sem ela, ~115 ms de atraso de driver anulariam o ganho da previsão (~30 ms). Trocas previstas, fim do compasso e ataques do baixo são emitidos adiantados; trocas reativas, não (não dá para prever).
32. **Ataque do baixo nos tempos 1 e 3 agora é agendado no tempo previsto** (antes era no bloco em que o tempo passava, até 23 ms atrasado). Necessário para o `lookahead` funcionar no baixo.
33. **Tom, BPM e progressão só mudam ao reiniciar; ganho, volume e monitor mudam ao vivo.** Trocar o modo no meio exigiria recriar o rastreador e a memória; Parar/Iniciar faz isso de forma explícita.
34. **Monitor do violão desligado por padrão e aviso de realimentação na janela.** Microfone + alto-falante é o pior cenário medido (`realimentacao`).

## 2026-10-29 — Primeiro teste ao vivo do Matheus (v2)

Feedback: interface precária, volume baixo, pad parecendo órgão, demora para pegar o padrão e não mantê-lo, trocas para nota errada mesmo travado, falta de clique, "falha de áudio", pedido de logs.

35. **Diagnóstico da "falha de áudio" antes de trocar de linguagem.** Medido no fluxo real: processamento médio 0,5 ms (pico 24,6 ms) por bloco de 23 ms, sem falhas em 15 s sem interface. Conclusão: CPU não é o gargalo; as falhas vêm de picos ocasionais com o buffer pequeno do MME e/ou da interface antiga disputando o processador. Agora cada falha fica registrada com instante e custo do bloco (`quadros.csv`) para confirmar com dados reais.
36. **Interface web local (HTML/CSS/JS) numa janela do Edge em modo app, servida pelo próprio processo Python (stdlib `http.server` + SSE).** Dá uma interface moderna com pouco código e sem dependências; o Edge já vem no Windows. A mesma interface pode ir para o C++ (JUCE 8 tem WebView). *Alternativas: tkinter (precária, pedido explícito de algo melhor), PyQt/PySide (dependência pesada), pywebview (depende de pythonnet, sem suporte ao Python 3.15).*
37. **Recomendação de linguagem: manter Python agora como laboratório; portar o motor ao vivo para C++/JUCE quando o algoritmo estabilizar.** Python não é o gargalo de CPU (2–5 % do orçamento). O ganho real do C++/JUCE é **latência** (ASIO/WASAPI ~10 ms contra ~115 ms do MME) e robustez do thread de áudio (sem GIL/GC), além de VST e do conhecimento prévio do Matheus. Portar agora congelaria um algoritmo que ainda muda a cada teste. Pré-requisito: Visual Studio Build Tools + CMake (não instalados nesta máquina). *Alternativas: Rust (ecossistema de GUI/plugin menos maduro), C# (GC no thread de áudio).*
38. **Travar o ciclo ao ver a volta recomeçar** (os primeiros k = max(2, ⌈p/2⌉) acordes da segunda volta), não após 2 voltas completas. Num ciclo de 4 acordes trava no 6º, não no 8º.
39. **Contradição só destrava se persistir ½ pulso (`LOCK_CONTRA_BEATS`).** Um quadro confiante errado (ex.: Am ouvido como A por um instante) não derruba mais o padrão. Custo medido: recuperação de surpresa real ~½ pulso mais lenta (`acorde_fora_do_tom` 2,44 → 2,96 pulsos). Escolhido porque o erro relatado ao vivo (trocar para nota errada com o padrão travado) é mais frequente e mais caro.
40. **Mesma fundamental, terça diferente, enquanto travado → toca sem terça e mantém o padrão.** Aplica a escada de confiança (RF4) ao caso Am × A.
41. **Discordâncias fracas só contam se forem outro acorde real** (não "N", não PERDIDO), e são precisas 3 seguidas (era 2). Com violão real a confiança oscila muito mais que no sintético.
42. **Troca antecipada só vale até 1 pulso antes do tempo previsto.** Evita que um acorde mal ouvido, por acaso igual ao próximo, dispare a troca cedo.
43. **O início do acorde na memória é o pulso cheio anterior** (`floor(fase + 0,3)`), não o arredondado. A detecção atrasada e a palhetada de contratempo empurravam o registro 1 pulso para frente, e toda previsão virava reação.
44. **Blip medido em tempo real** (acorde que durou < 1 pulso) e correção que repete o acorde anterior é fundida. Antes um blip de 0,4 s entrava no ciclo aprendido (G Am F **Am** C).
45. **Som novo:** pad de serras desafinadas com voicing aberto, passa-baixa de 1,8 kHz, ataque 0,9 s, liberação 2,2 s, reverb estéreo Freeverb (combs e allpasses processados em blocos ≤ atraso, sem laço por amostra), baixo com senoide saturada, limitador `tanh`. Volume do acompanhamento −26,0 → **−11,9 dBFS** RMS (+14 dB, ~5×) sem clipar. Custo ~1,3 ms por bloco.
46. **Piso de velocity 35 → 60.** Tocar fraco não pode sumir. Custo: `corr_dinamica` 0,96 → 0,75 (a faixa dinâmica ficou mais estreita de propósito).
47. **Cenário `realimentacao` redefinido para o retorno chegar ~6 dB abaixo do violão** (ganho 0,1 sobre a saída nova, mais alta). O ganho antigo, 0,5, com a saída 14 dB mais alta, colocava o retorno acima do violão e mudava o significado do teste. Na vida real, mais volume = mais realimentação: fone ou DI ficam ainda mais importantes.
48. **Logs de sessão em disco e reprodução determinística** (`session.py`, `rodar --sessao`). É o elo entre o teste ao vivo do Matheus e a análise offline.
49. **Motivo legível para cada decisão** (`engine.reason`), mostrado no diário da interface e gravado em `decisoes.jsonl`.
50. **Exceções de limiar com nome e motivo, não afrouxamento geral:** `errado_confiante_n ≤ 4` só para `desacelerando_final` (problema nº 3) e `realimentacao` (problema nº 1).
