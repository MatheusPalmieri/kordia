# 05 — Roadmap

## Fase 0 — Passo zero (atual)
Protótipo offline, causal, em Python. Áudio sintético com verdade-base, 12 cenários, métricas, baseline. Entregável: números honestos sobre onde a ideia funciona e onde falha.

## Fase 0.5 — App ao vivo (feito)
App com interface web moderna, entrada/saída de áudio, tom, BPM fixo, progressão, clique, fixar BPM, volume, dinâmica fixa; Iniciar e tocar. Mesmo processador dos testes, com compensação da latência do driver. Cada sessão é gravada e pode ser reproduzida offline. Serve para testar com o violão de verdade antes do port.

## Fase 0.6 — Iterar com sessões reais (agora)
- Matheus toca e manda as pastas de `sessoes/`; cada problema ouvido vira cenário ou teste de regressão.
- Prioridades: travamento e manutenção do padrão com violão real, latência reativa, confusão de terça.

## Fase 1 — Refinamento
- Atacar os 3 maiores problemas do baseline (ver `resultados.md`).
- Calibrar o equilíbrio "seguir o que ouve × seguir o que aprendeu" (risco 7).
- Cenário de trocas a cada 2 tempos (risco 8) e power chords (risco 6).
- Cancelamento do próprio sinal (NLMS) para o cenário de realimentação.
- Estimar compasso 3/4 e 6/8, não só 4/4.

## Fase 2 — Gravações reais
- Gravações no celular: C–G–Am–F no tempo, acelerando, com metrônomo; com e sem capotraste; violão de aço e nylon; guitarra limpa.
- Anotar verdade-base de algumas à mão para medir.
- Ajustar parâmetros de onset/chroma para áudio real (ruído de sala, compressão do celular).

## Fase 3 — Tempo real (C++/JUCE) — em andamento
Feito (2026-10-29): núcleo C++ idêntico ao Python (`tests/test_cpp.py`), módulo `kordia_core` para o laboratório, app JUCE com WASAPI exclusivo (~40 ms), interface web reaproveitada no WebView2, sessões gravadas no mesmo formato e relatório automático.

Pendente:
- ASIO (SDK da Steinberg) para interfaces de áudio profissionais.
- FFT pré-alocada (`juce::dsp::FFT` ou plano próprio) no lugar do pocketfft, que aloca a cada chamada.
- Motivos de decisão sem alocação no thread de áudio (hoje `std::string`).
- Instalador / exe assinado; ícone; guardar configurações no app.
- VST3/AU do mesmo núcleo.
- Portar `params.py` e os módulos um a um, com os mesmos testes (vetores de entrada/saída gerados pelo Python).
- Baixa latência: bloco de 128–256 amostras, análise harmônica em thread separada.
- Entrada de interface de áudio, saída MIDI e/ou sintetizador interno.

## Fase 4 — Extras (ideias registradas)
- **Passagens inteligentes** do baixo entre acordes (walking simples, aproximação cromática).
- **Footswitch / "congelar acorde"** para permitir solos sem o sistema seguir as notas.
- **Nível fixo vs. seguir dinâmica** (configurável; padrão segue dinâmica).
- **Guitarra elétrica** (entrada limpa, antes dos efeitos).
- **VST/AU** para usar dentro de uma DAW ou de um host ao vivo.
- **ML leve** (ex.: classificador de acordes pequeno) só se medir ganho nos cenários e nas gravações.
- **UI:** salvar configurações e setlist, modo palco (tela grande), atalhos de pedal.
- Salvar progressões por música ("setlist").
