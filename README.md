# Acompanhante automático para violão (baixo + pad)

Um sistema que **ouve o violão** e toca junto um baixo e um pad que seguem os acordes **que estão soando**, não uma cifra pré-programada. Pensado para banda de igreja, onde não há clique, o tom muda na hora e o andamento oscila.

**Status:** app ao vivo em **C++/JUCE** (WASAPI exclusivo, ~40 ms de latência de driver) com o núcleo portado do protótipo Python, que continua como laboratório (cenários, métricas, reprodução de sessões). Os dois dão decisões idênticas nos 12 cenários. Baseline: 88 % do tempo com o acorde certo, zero falso disparo, trocas a ~30 ms do tempo quando o ciclo está travado (~260 ms reagindo). Detalhes e falhas conhecidas em [`docs/resultados.md`](docs/resultados.md).

## Instalação

```bash
python -m venv .venv
.venv/Scripts/pip install -e .        # laboratório Python (Windows, Git Bash)
python tasks.py build --app           # núcleo C++ + módulo Python + app JUCE (Kordia.exe)
```

O build C++ precisa do **Visual Studio 2022 Build Tools** com a carga "Desenvolvimento para desktop com C++" (o CMake vem junto). JUCE 8, pocketfft, pybind11 e o SDK do WebView2 são baixados pelo CMake na primeira configuração. `python tasks.py build` (sem `--app`) compila só o núcleo e o módulo `kordia_core` usado pelo laboratório.

Requer Python 3.11+ (testado com 3.15), numpy, scipy, soundfile, matplotlib, pytest, sounddevice (áudio ao vivo) e o Microsoft Edge (já vem no Windows) para a janela do app.

## Uso ao vivo (app)

Dê dois cliques em **`Acompanhante.bat`**. Ele abre o app C++ (`cpp/build/app/KordiaApp_artefacts/Release/Kordia.exe`) se estiver compilado; senão, o app Python (`python -m acompanhante.app`, mesma interface, latência maior).

No app C++ aparecem também **Driver** (padrão: *Windows Audio*, ~22 ms) e **Latência do driver** (padrão: ~10 ms). Use o padrão quando entrada e saída forem aparelhos diferentes (ex.: microfone USB + headset): o Windows compensa a diferença de relógio entre eles. *Windows Audio (Exclusive Mode)* só serve com uma interface de áudio única (entrada e saída no mesmo aparelho); com dois aparelhos a captura morre depois de ~15–20 s. Se algo der errado, a janela avisa no rodapé (sem sinal na entrada, rajada de falhas de áudio, entrada corrompida).

Na tela principal ficam só o que você usa tocando: o **Tom** no topo (*Automático* = ele descobre pelos acordes e mostra qual achou), **Iniciar**, o acorde, o cartão **Sinal** (violão entrando; baixo e teclado saindo, com as notas), o **Andamento**, o **Padrão** aprendido e o **Diário** de decisões. O resto fica em **Ajustes (⚙)**:

- **Áudio:** driver (padrão *Windows Audio*), latência do driver, entrada, saída, ganho (com medidor), ouvir o violão na saída.
- **Som:** volume (até 200 %), dinâmica fixa e **Instrumentos**: pad worship, órgão, piano elétrico, pad de guitarra, cordas e baixo, cada um com liga/desliga e volume, mudando com ele tocando.
- **Música:** *Aprender segurando a tônica* (até reconhecer o padrão, só um pad suave na tônica, sem baixo e sem ficar trocando de acorde), *Manter o padrão aprendido* (erros e atrasos não destravam; só uma mudança de 1½ compasso) e progressão conhecida.

**Andamento:** *Seguir meu tempo* ou *BPM fixo*. O botão **Clique** já inicia tudo se estiver parado: o metrônomo começa na hora, antes de você tocar (no modo seguir ele começa no BPM escolhido e depois acompanha o seu tempo). − / + mudam o BPM com ele tocando; *Fixar este BPM* congela o detectado.

> **Importante:** microfone + caixa de som no mesmo ambiente fazem o sistema ouvir o próprio baixo/pad (pior cenário medido). Prefira **fone** ou o violão **ligado direto**.

### Logs de cada sessão

Cada Iniciar cria `sessoes/<data-hora>/` (fora do git) com a gravação da entrada, a configuração, uma linha por bloco de 23 ms (nível, acorde ouvido, confiança, estado, BPM, custo de processamento, falhas de áudio), todas as decisões com o motivo, um resumo e um **relatório visual** gerado ao parar. "Abrir pasta" no rodapé leva até ela.

No app C++, o relatório é gerado chamando o laboratório Python ao parar (precisa da `.venv`). O processamento é determinístico, então uma sessão pode ser reproduzida exatamente, offline:

```bash
python -m acompanhante.cli rodar --sessao sessoes/2026-10-29_20-40-00
```

Para eu analisar um problema que você ouviu ao vivo, basta mandar a pasta da sessão (ou dizer o horário em que aconteceu).

## Uso em arquivos e avaliação

```bash
# testes (unitários + regressão dos cenários)
python tasks.py test

# gerar um cenário sintético (WAV + verdade-base JSON)
python -m acompanhante.cli gerar --cenario acelerando --saida dados/

# rodar o acompanhante num WAV qualquer (inclusive gravação de celular)
python -m acompanhante.cli rodar --entrada minha_gravacao.wav --modo livre --tom C
python -m acompanhante.cli rodar --entrada gravacao.wav --modo metronomo --bpm 110 --tom C
python -m acompanhante.cli rodar --entrada gravacao.wav --modo decorado --progressao "C G Am F" --tom C

# avaliar todos os cenários (tabela de métricas + relatórios PNG)
python -m acompanhante.cli avaliar --todos --versao baseline
```

`rodar` gera, ao lado da saída escolhida (`--saida`, padrão `saida/`):
- `<nome>_render.wav`: violão original + baixo + pad;
- `<nome>.mid` e `<nome>_eventos.json`: os eventos do acompanhamento;
- `<nome>.png`: relatório visual (forma de onda, onsets, BPM, acordes, estado).

`avaliar` salva tudo em `resultados/<versao>/`.

## Estrutura

```
cpp/core/       núcleo C++ (o motor do produto; espelha acompanhante/ linha a linha)
cpp/python/     módulo kordia_core: o laboratório roda o núcleo C++
cpp/app/        app JUCE (Kordia.exe) e teste de fumaça do caminho ao vivo (KordiaLiveTest.exe)
acompanhante/   laboratório Python: referência do algoritmo, cenários, métricas, relatórios; ui/ = interface
tests/          pytest
docs/           visão, requisitos, arquitetura, riscos, testes, roadmap, decisões, resultados, glossário
dados/          cenários gerados (ignorado pelo git)
sessoes/        gravações e logs das sessões ao vivo (ignorado pelo git)
resultados/     métricas e relatórios por versão (só o CSV/JSON vai para o git)
```

## Documentação

1. [Visão](docs/00-visao.md) · 2. [Requisitos](docs/01-requisitos.md) · 3. [Arquitetura](docs/02-arquitetura.md) · 4. [Riscos](docs/03-riscos.md) · 5. [Plano de testes](docs/04-plano-de-testes.md) · 6. [Roadmap](docs/05-roadmap.md) · [Decisões](docs/decisoes.md) · [Resultados](docs/resultados.md) · [Glossário](docs/glossario.md)
