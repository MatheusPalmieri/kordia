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
16. **Onsets só na faixa 150 Hz–5 kHz.** Os ataques do próprio baixo (≤ 125 Hz) não viram "palhetada" na realimentação; a palhetada de violão é banda larga e continua detectável.
17. **librosa não é usado.** O pipeline é todo próprio e causal; nada de função offline.
18. **Saída com 1 bloco de atraso estrutural**: a decisão tomada com o bloco *k* vale a partir do início do bloco *k+1*, como em tempo real.
