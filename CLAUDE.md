# CLAUDE.md — instruções permanentes

Projeto: **Kordia** (o nome do produto), que **ouve o violão** e toca baixo + pad seguindo os acordes. `acompanhante` é só o nome interno do pacote Python. Leia `docs/00-visao.md` e `docs/02-arquitetura.md` antes de mexer no código.

**Duas implementações do mesmo algoritmo:** `acompanhante/` (Python: referência e laboratório) e `cpp/core/` (C++: o motor do app ao vivo). Elas têm que dar **exatamente** as mesmas decisões (`tests/test_cpp.py`).

## Princípios (não negociáveis)

1. **Errar tocando pouco > errar tocando nota errada.** Na dúvida: fundamental sem terça, pad na tônica, ou silêncio. Falso disparo é o erro mais caro.
2. **Causalidade/streaming.** Todo processamento é bloco a bloco e só usa o passado, inclusive no harness offline. Proibido: funções que olham o áudio inteiro (ex.: `librosa.beat.beat_track`, normalizar pelo máximo global, filtfilt).
3. **"Qual acorde" × "quando trocar" são camadas separadas** (`chords.py` × `tempo.py`), testadas separadamente. O áudio confirma o acorde; o tempo diz quando.
4. **O áudio manda.** Tom principal e progressão decorada são só prior/palpite; acorde claro fora do tom tem que ser aceito.
5. **Parar quando o músico para**: termina o compasso, fade, esquece após silêncio longo.

## Convenções

- Documentação em português do Brasil; código e identificadores em inglês.
- Todos os parâmetros numéricos em `acompanhante/params.py`, com unidade e motivo (é a especificação para o port em C++/JUCE).
- Algoritmos simples e explícitos; nada de excesso de engenharia. Dependências: numpy, scipy, soundfile, matplotlib, pytest; interface em HTML/CSS/JS puro (sem framework).
- Commits pequenos, mensagens claras.

## Comandos

```bash
python -m venv .venv && .venv/Scripts/pip install -e .   # ou .venv/bin/pip no Linux/macOS
Kordia.lnk                            # app ao vivo (assets/abrir.bat abre cpp/build/app/KordiaApp_artefacts/Release/Kordia.exe)
python tasks.py atalho                # cria Kordia.lnk (logo como ícone; chama assets/abrir.bat)
python assets/gerar_icones.py         # regenera svg, png e ico da logo; depois: python tasks.py build --app
python -m acompanhante.cli rodar --sessao sessoes/<pasta>   # reproduz uma sessão ao vivo exatamente
python tasks.py build [--app]         # núcleo C++ + kordia_core (+ app JUCE); precisa do VS 2022 Build Tools
python tasks.py test                  # pytest (unitários + regressão + equivalência C++)
python -m acompanhante.cli avaliar --todos --backend cpp   # métricas rodando o núcleo C++
cpp/build/app/KordiaLiveTest_artefacts/Release/KordiaLiveTest.exe [driver] [taxa] [entrada] [saída]   # fumaça ao vivo (volume 0)
python tasks.py avaliar [versao]      # roda os 12 cenários, salva métricas e relatórios
python -m acompanhante.cli prova [--versao v] [--progressao ...] [--tom ...] [--variante ...]   # músicas novas nos 12 tons (~10 min; usa sessoes/)
python -m acompanhante.cli gerar --cenario acelerando --saida dados/
python -m acompanhante.cli rodar --entrada arquivo.wav --modo livre|metronomo|decorado [--bpm 120] [--tom C] [--progressao "C G Am F"]
python -m acompanhante.cli avaliar --todos [--versao v1]
```

## Ao mudar algo relevante

- Rode `python tasks.py avaliar <nova_versao>` e **atualize `docs/resultados.md`** com a tabela e uma análise honesta (inclusive o que piorou).
- Registre decisões novas ou revistas em **`docs/decisoes.md`** (data, decisão, motivo, alternativas).
- Mudou o aprendizado, os acordes ou o tempo? Rode também a **prova** e compare com a última versão em `docs/resultados.md`. As variantes `real_*` pesam mais que as sintéticas; a nota delas é pessimista, então compare entre versões.
- Se a arquitetura ou um requisito mudar, atualize `docs/02-arquitetura.md` / `docs/01-requisitos.md`.
- Ao apertar ou afrouxar um limiar em `tests/test_cenarios.py`, diga o porquê no commit.

## Como pensar neste projeto

- Antes de otimizar um número, pergunte: "isso deixa o sistema mais propenso a tocar nota errada?" Se sim, não vale.
- Áudio sintético é otimista. Ganho só no sintético e não explicável musicalmente é suspeito de sobreajuste.
- O equilíbrio "seguir o que ouve × seguir o que aprendeu" é o principal parâmetro; mexa nele com cenários de variação (`acorde_fora_do_tom`, `troca_de_tom`).

## Ao vivo × arquivo

`Processor` é o sistema inteiro bloco a bloco, em Python (`pipeline.py`) e em C++ (`cpp/core`). `run()` (arquivos, métricas) e o `LiveEngine` (callback de áudio) usam esse objeto; nunca duplique lógica fora dele. O callback de áudio não pode bloquear: nada de I/O, prints ou alocação grande lá dentro.

## Regras de trabalho combinadas com o Matheus

- **A cada interação: commit e `git push origin main`** (repositório privado github.com/MatheusPalmieri/kordia). Sem branches nem PRs, salvo pedido.
- **Documentação sempre completa e atual.** Toda mudança relevante atualiza README, docs/ e este arquivo na mesma rodada.
- **Toda regra nova que ele disser vai para esta seção.**
- **O app ao vivo é só C++** (`Kordia.exe`). Python é laboratório: não recrie um app ou caminho ao vivo em Python.
- **Melhorar o harness junto com o código:** bug achado ao vivo vira cenário ou teste; sessões gravadas (`sessoes/`) são a fonte de verdade sobre o violão real.
- **Linguagem não é dogma:** ele aceita C++, Rust ou C# se for melhor para latência e qualidade de som. Decisões de stack ficam registradas em `docs/decisoes.md` com números.
- **Qualidade de produto:** interface moderna e apresentável (referência: apps da Apple), pad de worship (não órgão), volume alto o bastante.
- **Logs para iterar juntos:** cada decisão do motor tem um motivo legível (`engine.reason`), mostrado no diário da interface e gravado na sessão.

## App ao vivo

- `cpp/app`: `LiveEngine` (áudio e sessão) + WebView2 com `ui/index.html` (HTML/CSS/JS sem dependências), embutida no exe; mudou a interface, rode `python tasks.py build --app`.
- A sessão é gravada numa thread, o callback só enfileira; `session.py` lê a pasta no laboratório. O relatório pós-sessão vem do laboratório (`rodar --sessao`) e precisa da `.venv`.
- `?snapshot` na URL pula a tela de entrada (para capturas headless do layout).
- **Logo:** K de neon branco, com vão entre a haste e o "<", em quadrado preto de cantos arredondados. A geometria mora em `assets/gerar_icones.py` e é repetida no `<g id="logo">` e no favicon de `ui/index.html` (que precisa ser um arquivo só: o JUCE embute só ele).

## Regra do port (Python × C++)

- **Toda mudança de algoritmo ou parâmetro é feita nos dois lados na mesma rodada:** `acompanhante/*.py` e `cpp/core/`. `tests/test_cpp.py` compara parâmetros, trocas, estados, acordes ouvidos, áudio e métricas nos 12 cenários; tem que continuar verde. Depois de mexer no C++: `python tasks.py build`.
- Python é onde se experimenta (rápido de mudar, tem métricas e relatórios); quando funciona, porta-se para o C++ com os mesmos nomes e a mesma ordem de operações. Semântica de Python a respeitar no C++: `round()` é meio-para-par (`pyround`), `%` nunca é negativo (`pymod`), `None` vira `optional`/`-1`/`0`.
- **Thread de áudio (C++):** nada de I/O, locks bloqueantes ou alocação no callback. Estado para a interface sai por `SpinLock` com *try-lock*; sessão sai por `AbstractFifo`. Atalhos conhecidos estão marcados com `shortcut:` (ex.: o pocketfft aloca por chamada).
- **WASAPI:** o JUCE 8.0.15 tem um bug no modo compartilhado (lê float como int32 quando o Windows devolve `WAVE_FORMAT_IEEE_FLOAT` simples); `cpp/app/CMakeLists.txt` aplica uma correção de uma linha na configuração e falha com mensagem clara se o JUCE mudar. Padrão: modo compartilhado (compensa a diferença de relógio entre microfone e saída em aparelhos diferentes). O modo exclusivo com dois aparelhos derrapa e a captura morre em ~15–20 s.
- **O violão do Matheus está meio tom abaixo**: "C G Am F" soa B F# G#m E. Ao analisar sessões, compare com a progressão um semitom abaixo.
- **Padrões da interface ≠ padrões do laboratório:** na interface, *aprender na tônica* e *manter o padrão* vêm ligados e o tom em *Automático*; no `RunConfig` (cenários) vêm desligados, para os cenários antigos continuarem comparáveis. Cada modo novo ganha cenário/teste próprio.
- **Sessões reais mandam:** ao investigar um relato ao vivo, comece por `sessoes/<pasta>` (config, `decisoes.jsonl`, `quadros.csv`, `entrada.wav`); reproduza com `rodar --sessao`; meça antes de mexer. Os parâmetros calibrados só no sintético já falharam no violão real (piso de onset).
