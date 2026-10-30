# 04 — Plano de testes

## Cenários sintéticos

Gerados por `acompanhante/scenarios.py` com violão Karplus-Strong (6 cordas, voicings abertos, batida com espalhamento entre cordas, jitter humano de tempo e intensidade). Todos começam com ~1 s de silêncio e terminam com o último acorde soando um compasso, seguido de silêncio. Cada um tem semente fixa e gera WAV + JSON de verdade-base (`dados/<nome>.wav|.json`).

| Nome | O que testa | Modo de execução |
|---|---|---|
| `estavel_120` | C–G–Am–F em loop, 120 BPM, baixo-cima | livre, tom C |
| `acelerando` | 100 → 125 BPM gradual | livre, tom C |
| `desacelerando_final` | 110 BPM e ritardando até ~70 no final | livre, tom C |
| `dinamica` | crescendo e diminuendo | livre, tom C |
| `pausa_e_fim` | pausa de ½ compasso (só abaixa), silêncio longo (encerra e esquece), depois outra progressão | livre, tom C |
| `nota_de_passagem` | Am com baixo em Sol e notas soltas que **não** devem trocar o acorde | livre, tom C |
| `troca_de_tom` | Dó → (silêncio) → Ré com capotraste → (sem silêncio) → Sol | livre, tom C |
| `acorde_fora_do_tom` | Em, E, Bb no lugar do esperado | **decorado** `C G Am F`, tom C |
| `batida_abafada` | palhetadas percussivas sem nota intercaladas | livre, tom C |
| `ruido` | ruído de fundo + "voz" senoidal | livre, tom C |
| `realimentacao` | a saída do próprio sistema volta na entrada (laço fechado, 30 ms, ~6 dB abaixo do violão) | livre, tom C |
| `erros_com_padrao` | padrão travado; depois acorde errado, troca atrasada e entrada atrasada (verdade = progressão pretendida) | livre, tom C, **manter o padrão** |
| `tom_errado` | B–F#–G#m–E com o tom configurado em Dó (caso real de 2026-10-29) | livre, tom C |
| `modo_metronomo` | violão adianta/atrasa trocas em relação ao clique | **metrônomo** 110 BPM, tom C |

## Prova (`acompanhante/exam.py`)

`python -m acompanhante.cli prova [--progressao ...] [--tom ...] [--variante ...] [--versao v]` → tabela e `resultados/<versao>/prova.csv`. Responde: **tocando uma música nova, em qualquer tom, o app acaba tocando o que foi tocado?** Usa a configuração da interface (aprender na tônica, manter o padrão, tom automático) e o violão meio tom abaixo. Batida (3) e BPM (72–120) giram com o tom.

| Variante | Entrada | Configuração |
|---|---|---|
| `clique` | sintético | metrônomo com clique; entra 2 compassos depois do Iniciar |
| `tom_da_forma` | sintético | idem, com o tom configurado no nome da forma (meio tom acima do que soa) |
| `livre` | sintético | sem clique |
| `real_clique`, `real_livre` | compassos reais de `sessoes/`, remontados e transpostos | como `clique` e `livre` (só com as pastas locais e só com I, V, vi, IV) |

Progressões: 2, 3, 4, 6 e 8 acordes, duas por compasso, dois compassos por acorde, volta na tônica, menor, ii–V–I, empréstimo, dominante secundária, formas de worship (`Cadd9`, `D/F#`, `Em7`, `Dsus4`) com capotraste e a armadilha `falsa_volta`.

Nota de cada música: `lock_compassos` (compassos até travar), `acc` (acorde exato depois de travar), `wrong_s` (segundos de harmonia errada, fora do primeiro tempo de cada troca; a tônica só conta se não pertencer ao tom), `played` (últimos acordes tocados). **Certa:** travou, `acc` ≥ 0,90 e `wrong_s` ≤ 2.

Fora da prova: compasso ternário, seções diferentes (verso/refrão), mudança de tom e cantor por cima.

## Verdade-base (JSON)

`tempo` (curva de BPM), `beats`, `downbeats`, `chords` (segmentos `[início, fim, rótulo]`, com `N` = silêncio e `P` = pausa), `changes` (com flag `surpresa` para trocas inesperadas), `ends` (instante em que o músico terminou), `run` (modo, bpm, tom, progressão para a execução).

## Métricas (`acompanhante/metrics.py`)

| Métrica | Definição |
|---|---|
| `acc_total` | % do tempo com acorde na verdade-base em que o pad toca exatamente o acorde certo |
| `acc_confiante` | mesma coisa, restrita aos quadros em que o sistema estava no nível CONFIANTE |
| `cobertura` | % do tempo com acorde em que o sistema toca alguma coisa |
| `acc_nao_confiante`, `tempo_nao_confiante` | acurácia e % do tempo com acorde fora do nível CONFIANTE (dúvida/perdido/fallback) |
| `lat_media_ms`, `lat_p95_ms`, `lat_media_pulso` | atraso entre a troca real e a troca do acompanhamento para o acorde certo (negativo = adiantado); procura de −1 a +2 pulsos |
| `lat_reativo_ms`, `lat_travado_ms` | a mesma latência separada pelo estado no momento da troca (OUVINDO × TRAVADO): mede o ganho da previsão |
| `trocas_perdidas` | trocas reais sem correspondência nessa janela |
| `bpm_erro_medio` | média de \|BPM estimado − real\| enquanto o músico toca (só modo livre) |
| `t_trava_tempo_s` | tempo do primeiro onset até o erro ficar < 4 % por 2 s seguidos |
| `falso_disparo_s`, `falso_disparo_n` | segundos e número de notas tocando onde não devia (silêncio, fim, depois do fim; tolerância de fim de compasso + fade) |
| `errado_confiante_n` | episódios ≥ ½ pulso em que tocou acorde errado no nível CONFIANTE |
| `parada_s` | para cada fim: tempo entre o fim do último compasso e o silêncio total do acompanhamento |
| `recuperacao_pulsos` | maior latência entre as trocas marcadas como `surpresa` |
| `parou_no_compasso`, `esqueceu` | parou entre −0,25 s e +0,35 s da barra final; passou por OCIOSO entre um fim e a música seguinte |
| `corr_dinamica` | correlação entre intensidade real e velocity do acompanhamento |

Os resultados vão para `resultados/<versao>/metricas.csv` e `.json`, com relatórios PNG em `resultados/<versao>/relatorios/`.

## Critérios de aceitação (limiares de regressão)

Começam frouxos e são apertados a cada versão (ver `tests/test_cenarios.py`):

- `estavel_120`: `acc_total` ≥ 80 %, `falso_disparo_s` ≤ 1 s, erro de BPM ≤ 5.
- Todo cenário: `falso_disparo_s` ≤ 2 s e `parada_s` ≤ 1 compasso + 1 s.
- `modo_metronomo`: latência média dentro de ±½ pulso.
- `estavel_120`: latência travada < metade da reativa (a previsão tem que ajudar).
- `dinamica`: correlação ≥ 0,7. `pausa_e_fim` e `troca_de_tom`: esqueceu a progressão.

## Testes unitários

`pytest` cobre cada módulo isolado: gerador (afinação do Karplus-Strong, verdade-base coerente), onsets (contagem em batida conhecida), classificador (cada tríade sintética é reconhecida), tempo (BPM de pulsos sintéticos, rampa), memória (detecção de ciclo e previsão), máquina de estados (parada, esquecimento), MIDI (arquivo válido), clique (segue o andamento; com BPM fixo começa no Iniciar), gravação/reprodução de sessão, compensação de latência, e **causalidade** (mudar o áudio depois de um instante não altera nenhuma saída antes dele).

## Gravações reais

Sem verdade-base: `rodar` gera só o relatório visual e o WAV renderizado. A avaliação é de ouvido e pela figura. Gravações previstas: C–G–Am–F no tempo, acelerando e com metrônomo.

## Sessões reais

Cada sessão ao vivo (`sessoes/<data-hora>/`) é reproduzível offline com resultado idêntico (`rodar --sessao`). Fluxo: o Matheus relata um problema e indica a sessão e o horário; reproduzimos, achamos a causa no `decisoes.jsonl` e no `quadros.csv`, corrigimos e, quando possível, transformamos o trecho num cenário ou teste de regressão.

## Equivalência C++ × Python

`tests/test_cpp.py` (pulado se o módulo não estiver compilado): parâmetros iguais; em cada um dos 12 cenários, as mesmas trocas, estados e acordes ouvidos por bloco, áudio sintetizado igual (< 1e-9) e métricas iguais; clique e `lookahead` iguais; e o núcleo a 24 kHz dá o mesmo resultado musical que a 22,05 kHz.

## Fumaça do caminho ao vivo (C++)

`KordiaLiveTest.exe [driver] [taxa] [entrada] [saída]` (variáveis: `KORDIA_SECONDS`, `KORDIA_BUFFER`, `KORDIA_FIXED_BPM`, `KORDIA_BPM_LIVE`) abre os dispositivos reais com volume 0 e imprime o nível na entrada do processador, o nível bruto do driver, a latência, as falhas de áudio e o erro mostrado à interface. Também grava uma sessão. Foi assim que a entrada corrompida do WASAPI compartilhado foi achada.
