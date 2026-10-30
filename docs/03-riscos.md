# 03 — Riscos

Cada risco tem mitigação e um cenário de teste que o exercita (ver `04-plano-de-testes.md`).

| # | Risco | Mitigação | Como é testado |
|---|---|---|---|
| 1 | **Latência da detecção de acorde.** Identificar um acorde exige janela (~100–190 ms); o baixo entraria atrasado. | Rastreador de tempo **prevê** a troca no modo travado/decorado; o áudio só confirma. Pad tem ataque lento, o atraso pesa menos. | Métrica de latência das trocas (ms e fração de pulso), comparando OUVINDO × TRAVADO em todos os cenários. |
| 2 | **Palhetada ≠ tempo.** Em baixo-cima há 2 palhetadas por pulso. | Autocorrelação da envoltória com prior de andamento + PLL que só corrige com onsets perto do tempo previsto. | `estavel_120` (colcheias), `batida_abafada`, erro de BPM. |
| 3 | **Acelerar sem clique.** | PLL corrige período a cada tempo; ACF reavaliada a cada 0,25 s; trocas previstas usam o período atual. | `acelerando` (100→125), `desacelerando_final`. |
| 4 | **O sistema se ouvir (realimentação).** Microfone pega o próprio baixo/pad; o detector pode seguir o próprio acompanhamento e nunca parar. | Recomendação de uso: entrada direta (DI) e retorno em fone. No código: onsets só acima de 150 Hz (ignora ataques do baixo). Futuro: cancelamento adaptativo (NLMS) do sinal que o próprio sistema emite. | `realimentacao`: laço fechado real, a saída do sintetizador volta na entrada com atraso e atenuação. |
| 5 | **Ruído e interferências** (voz, outros instrumentos, ruído de dedos, palhetadas abafadas). | Limiar adaptativo de onset; estado "N" no HMM; persistência; batida abafada tem chroma plano e não vence o acorde atual. | `ruido`, `batida_abafada`. |
| 6 | **Acordes ambíguos** (C6 × Am7, power chord). | Vocabulário reduzido a tríades; empate resolvido por persistência (fica no anterior) e prior do tom; dúvida maior/menor → fundamental + quinta sem terça. | `nota_de_passagem` (Am/G ≈ C + A). Power chords: futuro. |
| 7 | **Excesso de confiança no padrão.** Quanto mais confia, mais insiste quando o músico improvisa. | Contradição forte destrava imediatamente; fraca, só depois de 2 seguidas. Esse equilíbrio é o principal parâmetro a calibrar. | `acorde_fora_do_tom` (modo decorado), `troca_de_tom`. Métrica de recuperação. |
| 8 | **Trocas no meio do compasso.** O modo previsão assume troca no tempo 1. | Memória guarda a duração de cada acorde em tempos (arredondada a meio compasso). | Ainda sem cenário dedicado (pendência). |
| 9 | **Capotraste e voicings variados.** | Detecta-se o som real; templates não dependem da forma. | `troca_de_tom` usa formas de Dó com capotraste na 2ª casa para tocar em Ré. |
| 11 | **Driver entregando entrada corrompida** (WASAPI compartilhado nesta máquina: float lido como int32). | Modo exclusivo como padrão; detector de corrupção avisa na interface. | `KordiaLiveTest.exe "Windows Audio" 0` mostra o aviso; padrão sem aviso. |
| 12 | **Modo exclusivo toma a placa** (outros programas mudos). | Documentado; opção de driver na interface. | Manual. |
| 10 | **Falsos disparos** (tocar quando não devia): o erro mais caro. | Parada por ausência de onsets, fim de compasso + fade, esquecimento após silêncio longo, escada de confiança. | Métrica de falsos disparos e de parada em todos os cenários (todos terminam com silêncio). |

## Riscos de método (do próprio protótipo)

- **Áudio sintético é otimista.** Karplus-Strong é mais "limpo" que um violão real gravado no celular. Os números do baseline são um teto, não uma promessa. Mitigação: `rodar` aceita WAV real desde já.
- **Métricas que se ajustam ao gerador.** Evitar calibrar parâmetros só para os cenários; preferir regras com motivo musical e registrar em `decisoes.md`.
