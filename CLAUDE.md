# CLAUDE.md — instruções permanentes

Projeto: **Kordia**, que **ouve o violão** e toca baixo + pad seguindo os acordes. Leia `docs/00-visao.md` e `docs/02-arquitetura.md` antes de mexer no código.

**Tudo é C++, uma implementação só:** `cpp/core/` (o motor), `cpp/app/` (o app ao vivo, `Kordia.exe`) e `cpp/lab/` (o laboratório, `kordia_lab.exe`: cenários, métricas, prova, relatórios). Não há Python no projeto.

## Princípios (não negociáveis)

1. **Errar tocando pouco > errar tocando nota errada.** Na dúvida: fundamental sem terça, pad na tônica, ou silêncio. Falso disparo é o erro mais caro.
2. **Causalidade/streaming.** Todo processamento do motor é bloco a bloco e só usa o passado, inclusive no laboratório. Proibido: funções que olham o áudio inteiro (ex.: normalizar pelo máximo global, filtro de fase zero).
3. **"Qual acorde" × "quando trocar" são camadas separadas** (`ChordTracker` em `analysis.cpp` × `tempo.cpp`), testadas separadamente. O áudio confirma o acorde; o tempo diz quando.
4. **O áudio manda.** Tom principal e progressão decorada são só prior/palpite; acorde claro fora do tom tem que ser aceito.
5. **Parar quando o músico para**: termina o compasso, fade, esquece após silêncio longo.

## Convenções

- Documentação em português do Brasil; código e identificadores em inglês.
- Todos os parâmetros numéricos em `cpp/core/include/kordia/core.h` (`namespace P`), com unidade e motivo.
- Algoritmos simples e explícitos; nada de excesso de engenharia. Dependências: JUCE (só o app), pocketfft, WebView2; interface em HTML/CSS/JS puro (sem framework).
- Commits pequenos, mensagens claras.

## Comandos

```bat
tasks build [--app]                   :: núcleo + laboratório + testes (+ app JUCE); precisa do VS 2022 ou 2026 (Build Tools bastam)
tasks test [filtro]                   :: kordia_tests (unitários + regressão + referências + prova resumida), ~30 s
tasks avaliar [versao]                :: roda os 14 cenários, salva métricas e relatórios
tasks prova [versao]                  :: músicas novas nos 12 tons + controle com as 6 sessões reais (~5 min; usa sessoes/)
tasks gerar                           :: gera os cenários em dados/
tasks atalho                          :: cria Kordia.lnk (logo como ícone; chama assets\abrir.bat)
Kordia.lnk                            :: app ao vivo (cpp\build\app\KordiaApp_artefacts\Release\Kordia.exe)
```

O laboratório direto (`cpp\build\Release\kordia_lab.exe`, sempre a partir da raiz do repositório):

```bat
kordia_lab rodar --sessao sessoes\<pasta>     :: reproduz uma sessão ao vivo exatamente
kordia_lab rodar --entrada arquivo.wav --modo livre|metronomo|decorado [--bpm 120] [--tom C] [--progressao "C G Am F"]
kordia_lab prova [--versao v] [--progressao ...] [--tom ...] [--variante ...]
kordia_lab gerar --cenario acelerando --saida dados
kordia_lab avaliar --todos [--versao v1]
cpp\build\app\KordiaLiveTest_artefacts\Release\KordiaLiveTest.exe [driver] [taxa] [entrada] [saída]   :: fumaça ao vivo (volume 0): ouvir → Iniciar → parar; KORDIA_FIXED_BPM=90 testa a contagem; cria uma pasta em sessoes/
```

## Ao mudar algo relevante

- Rode `tasks avaliar <nova_versao>` e **atualize `docs/resultados.md`** com a tabela e uma análise honesta (inclusive o que piorou).
- Registre decisões novas ou revistas em **`docs/decisoes.md`** (data, decisão, motivo, alternativas).
- Mudou o aprendizado, os acordes ou o tempo? Rode também a **prova** e compare com a última versão em `docs/resultados.md`. As variantes `real_*` pesam mais que as sintéticas; a nota delas é pessimista, então compare entre versões.
- Se a arquitetura ou um requisito mudar, atualize `docs/02-arquitetura.md` / `docs/01-requisitos.md`.
- Ao apertar ou afrouxar um limiar em `cpp/tests/tests.cpp`, diga o porquê no commit.

## Como pensar neste projeto

- Antes de otimizar um número, pergunte: "isso deixa o sistema mais propenso a tocar nota errada?" Se sim, não vale.
- Áudio sintético é otimista. Ganho só no sintético e não explicável musicalmente é suspeito de sobreajuste.
- O equilíbrio "seguir o que ouve × seguir o que aprendeu" é o principal parâmetro; mexa nele com cenários de variação (`acorde_fora_do_tom`, `troca_de_tom`).

## A régua não pode mudar sem querer

O áudio dos cenários e da prova é a régua de todas as versões em `docs/resultados.md`. `cpp/lab/rng.cpp` (o gerador do NumPy, bit a bit), `lfilter`, `resample_poly` e a ordem das operações em `render` reproduzem exatamente o áudio em que tudo foi medido. `scenario_audio_is_unchanged` compara o áudio dos 14 cenários por hash. Se esse teste falhar, os números deixaram de ser comparáveis: só atualize `cpp/tests/scenario_hashes.inc` de propósito, dizendo isso no commit e em `docs/resultados.md`.

## Ao vivo × arquivo

`Processor` (`cpp/core`) é o sistema inteiro bloco a bloco. `lab::run()` (arquivos, métricas) e o `LiveEngine` (callback de áudio) usam esse objeto; nunca duplique lógica fora dele. O callback de áudio não pode bloquear: nada de I/O, prints ou alocação grande lá dentro.

## Regras de trabalho combinadas com o Matheus

- **A cada interação: commit e `git push origin main`** (repositório privado github.com/MatheusPalmieri/kordia). Sem branches nem PRs, salvo pedido.
- **Documentação sempre completa e atual.** Toda mudança relevante atualiza README, docs/ e este arquivo na mesma rodada.
- **Toda regra nova que ele disser vai para esta seção.**
- **Sem Python no projeto, nem como ferramenta:** o app, o laboratório, os testes e as tarefas são C++ (e `tasks.bat`). Não recrie nada em Python.
- **Melhorar o harness junto com o código:** bug achado ao vivo vira cenário ou teste; sessões gravadas (`sessoes/`) são a fonte de verdade sobre o violão real.
- **Linguagem não é dogma:** ele aceita C++, Rust ou C# se for melhor para latência e qualidade de som. Decisões de stack ficam registradas em `docs/decisoes.md` com números.
- **Qualidade de produto:** interface moderna e apresentável (referência: apps da Apple), pad de worship (não órgão), volume alto o bastante.
- **Logs para iterar juntos:** cada decisão do motor tem um motivo legível (`engine.reason`), mostrado no diário da interface e gravado na sessão.

## App ao vivo

- `cpp/app`: `LiveEngine` (áudio e sessão) + WebView2 com `cpp/app/ui/index.html` (HTML/CSS/JS sem dependências), embutida no exe; mudou a interface, rode `tasks build --app`.
- A sessão é gravada numa thread, o callback só enfileira; `load_session` (`cpp/lab/files.cpp`) lê a pasta no laboratório. O relatório pós-sessão (`replay_<pasta>.svg`) vem do `kordia_lab rodar --sessao`, que o app chama ao parar.
- `?snapshot` na URL pula a tela de entrada (para capturas headless do layout).
- **Logo:** K de neon branco, com vão entre a haste e o "<", em quadrado preto de cantos arredondados. `assets/kordia.svg` é a fonte; a geometria é repetida no `<g id="logo">` e no favicon de `ui/index.html` (que precisa ser um arquivo só: o JUCE embute só ele). `kordia-1024.png`, `kordia-32.png` e `kordia.ico` são versionados e, se a logo mudar, têm que ser exportados à mão a partir do SVG.

## C++ do núcleo e do app

- **Thread de áudio:** nada de I/O, locks bloqueantes ou alocação no callback. Estado para a interface sai por `SpinLock` com *try-lock*; sessão sai por `AbstractFifo`. Atalhos conhecidos estão marcados com `shortcut:` (ex.: o pocketfft aloca por chamada).
- **WASAPI:** o JUCE 8.0.15 tem um bug no modo compartilhado (lê float como int32 quando o Windows devolve `WAVE_FORMAT_IEEE_FLOAT` simples); `cpp/app/CMakeLists.txt` aplica uma correção de uma linha na configuração e falha com mensagem clara se o JUCE mudar. Padrão: modo compartilhado (compensa a diferença de relógio entre microfone e saída em aparelhos diferentes). O modo exclusivo com dois aparelhos derrapa e a captura morre em ~15–20 s.
- **O violão do Matheus está meio tom abaixo**: "C G Am F" soa B F# G#m E. Ao analisar sessões, compare com a progressão um semitom abaixo.
- **Padrões da interface ≠ padrões do laboratório:** na interface, *aprender na tônica* e *manter o padrão* vêm ligados e o tom em *Automático*; no `RunConfig` (cenários) vêm desligados, para os cenários antigos continuarem comparáveis. Cada modo novo ganha cenário/teste próprio.
- **Com clique, a harmonia é lida por meio compasso e o padrão é aprendido na grade** (`Processor::close_slot`, `ProgressionMemory::push_slot`, `Engine::grid_out`). Sem clique, ou com progressão conhecida, segue o caminho quadro a quadro. Mudança nesse caminho se mede com o controle das sessões reais (`CONTROLS` em `cpp/lab/exam.cpp`, impresso pela prova).
- **O app ouve desde que abre** (`LiveEngine::start(q, preview=true)`, `/api/listen`): sem som, sem sessão, sem apagar o diário. **Iniciar conta 4 tempos** (`RunConfig.contagem`) quando há BPM; antes disso o motor não recebe nada.
- **Sessões reais mandam:** ao investigar um relato ao vivo, comece por `sessoes/<pasta>` (config, `decisoes.jsonl`, `quadros.csv`, `entrada.wav`); reproduza com `kordia_lab rodar --sessao`; meça antes de mexer. Os parâmetros calibrados só no sintético já falharam no violão real (piso de onset).
