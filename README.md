# Acompanhante automático para violão (baixo + pad)

Um sistema que **ouve o violão** e toca junto um baixo e um pad que seguem os acordes **que estão soando**, não uma cifra pré-programada. Pensado para banda de igreja, onde não há clique, o tom muda na hora e o andamento oscila.

**Status:** passo zero, um protótipo offline em Python, causal (streaming), testado em áudio sintético com verdade-base. Baseline: 88 % do tempo com o acorde certo, zero falso disparo, trocas a ~30 ms do tempo quando o ciclo está travado (~260 ms reagindo). Detalhes e falhas conhecidas em [`docs/resultados.md`](docs/resultados.md).

## Instalação

```bash
python -m venv .venv
.venv/Scripts/pip install -e .        # Windows (Git Bash)
# .venv/bin/pip install -e .          # Linux/macOS
```

Requer Python 3.11+ (testado com 3.15), numpy, scipy, soundfile, matplotlib, pytest, sounddevice (áudio ao vivo) e o Microsoft Edge (já vem no Windows) para a janela do app.

## Uso ao vivo (app)

Dê dois cliques em **`Acompanhante.bat`** (ou `python -m acompanhante.app`). Abre uma janela própria (Edge em modo app, sem barras de navegador), com tema claro/escuro automático.

1. **Áudio:** escolha a entrada (microfone, interface, mesa) e a saída (já vem o alto-falante padrão). Toque e ajuste o **ganho** até a barra passar da marca cinza na batida.
2. **Música (opcional):** tom principal, **BPM fixo** e **progressão conhecida** (ex.: `C G Am F`). Sem nada, ele só ouve e segue.
3. **Iniciar** e toque.

Ao vivo (mudam sem parar):
- **Volume** do acompanhamento (até 200 %), **dinâmica fixa** ou seguindo a sua batida, **ouvir o violão** na saída.
- **Clique:** ouve o metrônomo. Com BPM fixo, o clique começa no Iniciar e você segue ele. No modo livre, o clique segue o andamento que ele detectou.
- **Fixar BPM:** congela o andamento detectado agora; daí em diante você segue o clique.

A janela mostra o acorde tocado (grande), o que está ouvindo e a confiança, o **próximo acorde** previsto, o estado, o BPM com o pulso do compasso, o **padrão aprendido** e um **diário** com cada decisão e o motivo.

> **Importante:** microfone + caixa de som no mesmo ambiente fazem o sistema ouvir o próprio baixo/pad (pior cenário medido). Prefira **fone** ou o violão **ligado direto**.

### Logs de cada sessão

Cada Iniciar cria `sessoes/<data-hora>/` (fora do git) com a gravação da entrada, a configuração, uma linha por bloco de 23 ms (nível, acorde ouvido, confiança, estado, BPM, custo de processamento, falhas de áudio), todas as decisões com o motivo, um resumo e um **relatório visual** gerado ao parar. "Abrir pasta" no rodapé leva até ela.

O processamento é determinístico, então uma sessão pode ser reproduzida exatamente, offline:

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
acompanhante/   código (um módulo por camada; parâmetros em params.py; ui/ = interface do app)
tests/          pytest
docs/           visão, requisitos, arquitetura, riscos, testes, roadmap, decisões, resultados, glossário
dados/          cenários gerados (ignorado pelo git)
sessoes/        gravações e logs das sessões ao vivo (ignorado pelo git)
resultados/     métricas e relatórios por versão (só o CSV/JSON vai para o git)
```

## Documentação

1. [Visão](docs/00-visao.md) · 2. [Requisitos](docs/01-requisitos.md) · 3. [Arquitetura](docs/02-arquitetura.md) · 4. [Riscos](docs/03-riscos.md) · 5. [Plano de testes](docs/04-plano-de-testes.md) · 6. [Roadmap](docs/05-roadmap.md) · [Decisões](docs/decisoes.md) · [Resultados](docs/resultados.md) · [Glossário](docs/glossario.md)
