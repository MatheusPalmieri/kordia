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

1. **Áudio:** escolha a entrada (microfone, interface, mesa) e a saída (já vem o alto-falante padrão). Toque e ajuste o **ganho** até a barra passar da marca cinza na batida.
2. **Música (opcional):** tom principal e **progressão conhecida** (ex.: `C G Am F`). Sem nada, ele só ouve e segue. Se o tom configurado não bater com o que você toca, ele segue o áudio mesmo assim e não usa a tônica errada como segurança.
   **Andamento:** escolha *Seguir meu tempo* (ele detecta e acompanha se você acelerar) ou *BPM fixo* (grade fixa; com o **Clique** ligado, o metrônomo começa no Iniciar e você entra contando). O BPM é um só e dá para mudar com − / + com ele tocando. No modo seguir, **Fixar este BPM** congela o andamento detectado.
3. **Iniciar** e toque.

Ao vivo (mudam sem parar):
- **Volume** do acompanhamento (até 200 %), **dinâmica fixa** ou seguindo a sua batida, **ouvir o violão** na saída.
- **Clique:** ouve o metrônomo. Com BPM fixo, o clique começa no Iniciar e você segue ele. No modo livre, o clique segue o andamento que ele detectou.
- **Fixar BPM:** congela o andamento detectado agora; daí em diante você segue o clique.

A janela mostra o acorde tocado (grande), o que está ouvindo e a confiança, o **próximo acorde** previsto, o estado, o cartão **Sinal** (nível do violão na entrada e medidores do **baixo** e do **teclado**, com as notas que estão soando), o BPM com o pulso do compasso, o **padrão aprendido** e um **diário** com cada decisão e o motivo.

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
