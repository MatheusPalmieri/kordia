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
37. *(Revista na decisão 51: o port foi feito já, a pedido do Matheus.)* **Recomendação de linguagem: manter Python agora como laboratório; portar o motor ao vivo para C++/JUCE quando o algoritmo estabilizar.** Python não é o gargalo de CPU (2–5 % do orçamento). O ganho real do C++/JUCE é **latência** (ASIO/WASAPI ~10 ms contra ~115 ms do MME) e robustez do thread de áudio (sem GIL/GC), além de VST e do conhecimento prévio do Matheus. Portar agora congelaria um algoritmo que ainda muda a cada teste. Pré-requisito: Visual Studio Build Tools + CMake (não instalados nesta máquina). *Alternativas: Rust (ecossistema de GUI/plugin menos maduro), C# (GC no thread de áudio).*
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

## 2026-10-29 — Port para C++/JUCE

51. **Portar agora, com o algoritmo em C++ como biblioteca e o Python como laboratório que roda essa biblioteca** (pybind11), em vez de duas implementações soltas. O argumento para esperar ("o algoritmo ainda muda") é neutralizado por `tests/test_cpp.py`: o Python continua sendo onde se experimenta, e a paridade é verificada a cada mudança. Medido: decisões idênticas nos 12 cenários, diferenças numéricas ~1e-15, núcleo ~6× mais rápido.
52. **Núcleo sem JUCE** (`cpp/core`, só STL + pocketfft). Mantém o módulo Python leve e o núcleo portável (VST, outro host, testes).
53. **Taxa de análise = taxa da placa ÷ inteiro** (44,1/48 kHz → ÷ 2), com passa-baixa FIR de 63 coeficientes (Blackman) antes da decimação. A síntese fica na taxa da placa. *Alternativa: reamostrar 48 → 22,05 kHz (fração 147/320), mais caro e sem ganho musical.*
54. **JUCE 8.0.15** (a mais recente; a 8.0.4 tinha o mesmo comportamento no WASAPI). Interface no WebView2 (estático), com o SDK NuGet baixado pelo CMake. **ASIO desligado** (exige o SDK da Steinberg, com licença própria); fica para quando houver interface de áudio com ASIO.
55. **WASAPI exclusivo como padrão, buffer de 480 amostras.** Medido nesta máquina: o modo compartilhado entrega a entrada corrompida (amostras float lidas como int32: valores em ≈ +0,416 / −0,584; confirmado reinterpretando os bits; o PortAudio no mesmo modo lê certo, então é a combinação JUCE + este Windows Insider 26300). Exclusivo: entrada correta, latência informada 40 ms, 0 falhas com 480 amostras (256: 14 falhas em 2 s; 960: 80 ms). DirectSound: 35–70 ms e falhas. *Contra:* o modo exclusivo silencia outros programas na mesma placa.
56. **Detector de entrada corrompida** no callback: > 97 % das amostras com |x| entre 0,3 e 0,75 por ~0,5 s → aviso na interface. Música real cruza o zero o tempo todo, então não dispara com violão.
57. **`AudioDeviceManager::initialise(1, 2, …)` ao abrir o dispositivo.** Sem isso o JUCE considera zero canais de entrada necessários. (Não era a causa da corrupção, mas é o uso correto.)
58. **Falhas de áudio contadas só depois de 0,5 s** de fluxo: o driver sempre acusa 1–3 na partida, e isso não é falha durante a música.
59. **Mesma interface nos dois apps**, com transporte detectado pela origem da página (`juce.backend` → GET + consulta periódica; senão POST + SSE). Uma interface só para manter.
60. **Relatório da sessão no app C++ = laboratório Python reproduzindo a sessão** (processo filho). O C++ não precisa de matplotlib nem de código de métricas.
61. **Memória de progressão limitada a 256 entradas nos dois lados** (só o fim é usado). Sessões longas não crescem sem limite, e o Python continua idêntico ao C++.

## 2026-10-29 — Segundo teste ao vivo (app C++)

Relato: aprendeu bem o padrão no começo, depois "entendeu que estava em silêncio", parou e não voltou; quer o metrônomo já antes de tocar; dois lugares de BPM confusos; quer ver a entrada e a saída (baixo/teclado).

62. **Causa da parada, medida na sessão:** modo exclusivo com dois aparelhos (microfone USB 48 kHz + headset): relógios independentes (o clique vazado mediu 988 Hz em vez de 1000, −1,2 %), falhas de áudio contínuas a partir de ~18 s e captura morta (zeros) a partir de 37 s. **Correção:** voltar ao modo compartilhado, que compensa a deriva, corrigindo o bug do JUCE (decisão 63). Medido: entrada correta, 22 ms, 60 s sem a captura morrer.
63. **Patch no JUCE 8.0.15** (`juce_WASAPI_windows.cpp`): reconhecer `WAVE_FORMAT_IEEE_FLOAT` simples como float. Causa raiz da "entrada corrompida" da decisão 55. Aplicado pelo CMake na configuração, idempotente, com erro explícito se o código mudar. Vale reportar ao JUCE.
64. **Piso do detector de palhetadas 0,6 → 0,25** (+ mascaramento: por 0,3 s depois de uma palhetada o limiar é ≥ 25 % do fluxo dela). Na sessão real, o piso 0,6 pegava 30 de ~57 palhetadas (picos reais 0,6–4, contra 3–9 no sintético) e gerava paradas falsas. Com 0,25 + máscara: 55 palhetadas; cenários sintéticos sem mudança. *Alternativa considerada: ganho automático (AGC); descartada porque o nível tocado já era parecido (−29 dB) e o problema era a forma do fluxo, não o volume.*
65. **A tônica do modo de segurança só toca se o tom configurado bater com o que ele vem ouvindo** (≥ metade dos últimos 8 acordes diatônicos); senão, silêncio. Na sessão real o violão estava em Si (I–V–vi–IV: B F# G#m E) com o tom configurado em Dó, e a segurança tocava Dó por cima.
66. **Cenário `tom_errado`** (B–F#–G#m–E com tom Dó) e teste de que a segurança fica muda quando o tom não bate.
67. **BPM em um lugar só** (cartão Andamento: *Seguir meu tempo* / *BPM fixo*, − / + ao vivo, Clique, Fixar este BPM). `Processor.set_bpm` muda o andamento mantendo a fase (grade fixa ou BPM fixado), nos dois lados.
68. **Cartão Sinal**: nível do violão, medidores de baixo e teclado (RMS dos barramentos no sintetizador) com as notas soando.
69. **"Sem sinal na entrada" e "rajada de falhas" são avisos no rodapé, não erros**: microfones USB zeram o sinal em sala silenciosa, então zeros por 2 s podem ser normais.
70. **Hipótese de afinação descartada com medida:** o violão estava a −3 cents de A=440; os acordes meio tom abaixo eram o que foi tocado (Si). Sem estimador de afinação por enquanto.

## 2026-10-29 — Terceiro teste ao vivo: manter o padrão

Relato: com o padrão reconhecido, um atraso ou erro do músico fazia o sistema largar o padrão e tentar aprender outro; o pedido é manter o padrão (ou a tônica) mesmo com erros, ainda mais com BPM fixo; configurável no futuro.

71. **Modo "manter o padrão aprendido"** (`RunConfig.manter_padrao`, `Engine.hold`), configurável ao vivo, **ligado por padrão na interface** e desligado no laboratório (os cenários antigos continuam medindo o "áudio manda"). Com o padrão travado: erros, atrasos e dúvidas não destravam; só uma discordância confiante sustentada por **1,5 compasso** reaprende (2,0 testado: troca de tom sem pausa 0,88 contra 0,90; erros iguais); dúvida não vai para a tônica; volume não abaixa entre palhetadas; parada após **5 tempos** sem palhetada (1,5 tempo antes), esquecimento nos 2 compassos de sempre (um esquecimento de 4 compassos tinha piorado `pausa_e_fim` de 0,93 para 0,88).
72. **Início do acorde quando ele aparece primeiro sem terça:** a troca conta de quando a fundamental apareceu, não de quando a terça ficou clara. Antes, o ciclo ficava registrado 1 tempo atrasado e toda previsão virava "você antecipou".
73. **Autocorreção da grade do padrão:** quando o músico troca antes do previsto (até 1 pulso), o padrão passa a usar o início observado. Um deslocamento sistemático se corrige na volta seguinte.
74. **Cenário `erros_com_padrao`**: padrão travado; depois meio compasso de acorde errado (2×), troca atrasada 1¼ tempo e entrada atrasada de 2 tempos. Verdade-base = a progressão pretendida. Manter: **0,96**; áudio manda: 0,92 (era 0,876 antes das decisões 72–73).
76. **Ganho automático de entrada antes da análise (AGC)**, Python e C++ (2026-10-29, 4º teste). O teste chegou a −40/−47 dB e o detector achou 33 palhetadas em 68 s: 3 paradas falsas e o padrão esquecido. Com AGC (alvo −28 dB; sobe a 0,05 e desce a 0,01 por bloco, só acima de −55 dB; ganho entre −6 e +24 dB): 126 palhetadas, 0 paradas, padrão travado. A decisão 64 tinha descartado o AGC olhando só uma sessão de nível normal; o nível varia entre testes. Silêncio e dinâmica continuam medidos no nível bruto. Custo: `ruido` 0,91 → 0,86 (um Em espúrio entrou no ciclo aprendido); média dos cenários igual (0,896).
75. **Custo conhecido do modo manter** (medido; detalhes em `resultados.md` v5): demora 1,5 compasso para aceitar uma mudança deliberada sem pausa (`troca_de_tom` 0,93 → 0,90, `acorde_fora_do_tom` 0,92 → 0,89) e encerra ~0,6 s depois do fim do último compasso.

## 2026-10-29 — Quinto teste ao vivo (Dó, Ré e Sol)

Relato: Sol saiu muito bem; Dó e Ré se perderam (suspeita: pestana em Fá e Si menor); quer o clique antes de tocar; camadas de teclado; Áudio/Som/Música em ajustes à parte com tom num menu rápido; enquanto não acha o acorde, ele troca rápido demais: melhor segurar a tônica até aprender.

77. **O violão está meio tom abaixo** (as três sessões soaram em Si, Dó# e Fá#; desvio de afinação de −3 cents em relação à grade). A detecção estava certa; o tom configurado (Si nas três) atrapalhou duas delas. Por isso o tom passa a poder ser **automático**.
78. **Pestanas medidas:** em Ré (soando Dó#), o A#m da pestana de Si menor chega sem a quinta e com o G# anterior soando; vira C# (2 notas em comum). Testado peso extra para a nota grave (fundamental no baixo): A#m 0 → 6 %, mas Mi piora; **não adotado**. Fica como limitação conhecida, a atacar com gravações rotuladas.
79. **Aprender segurando a tônica** (`aprender_na_tonica`, padrão ligado na interface, desligado no laboratório): até travar, saída = pad na tônica a 60 %, sem baixo; a memória continua aprendendo pelo que ouve. Nas 3 sessões reais (sem tom configurado): uma única saída antes de travar, a tônica certa (Si, Dó#, Fá#); travou em 22–25 s.
80. **Tom = configurado se bate com o que ouviu, senão detectado** (`theory.detect_key`: soma de pesos I 1,6, IV/V 1,2, vi 1,0, ii/iii 0,7; ≥ 3 acordes distintos). Vale para o aprendizado e para a segurança (fallback). A lista usada é a dos acordes **ouvidos** (antes: tocados), que é o que interessa para saber o tom.
81. **Clique antes de tocar no modo seguir** (`bpm_inicial`): o rastreador começa semeado no BPM escolhido, ancorado no Iniciar; PLL e autocorrelação assumem quando chegam palhetadas (no cenário que acelera de 100 a 125 BPM, o erro de andamento fica em 1,3 BPM). O botão Clique, com o app parado, inicia tudo.
82. **Camadas de instrumento no sintetizador** (worship, órgão, piano elétrico, pad de guitarra, cordas + ganho do baixo). Barramento "quente" (worship, guitarra, cordas) passa pelo passa-baixa; "brilhante" (órgão, piano) não; os dois vão para o reverb. Níveis equilibrados para ~−31 dBFS por nota. Configuração padrão numericamente idêntica ao som anterior; Python e C++ idênticos (< 1e-15) com qualquer combinação.
83. **Interface: Ajustes num painel lateral, tom no topo** (pedido explícito). Mesmos ids da interface anterior, a lógica de estado não mudou.
84. **Teste de paridade com `lookahead`** comparava instantes com igualdade exata (diferença 1e-15); passou a arredondar como os demais. Não havia divergência real.
