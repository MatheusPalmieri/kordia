# 00 — Visão

## O problema

Em banda de igreja é comum faltar alguém: baixista, tecladista. Já existem apps de pad e de playback/multitrack, mas todos tocam algo **fixo**: um pad parado numa nota, ou uma faixa gravada com andamento e tom definidos.

Ao vivo a realidade é outra:

- **não há clique, metrônomo ou guia**: a banda simplesmente toca;
- o tom, a música e o trecho (verso, refrão) mudam na hora;
- o andamento oscila, e a banda costuma acelerar.

Um playback fixo não acompanha nada disso.

## O objetivo

Um sistema que **ouve o violão** e toca junto um **baixo** e um **pad/teclado** que seguem os acordes que estão sendo tocados.

Exemplo: estou em Dó tocando Fá, Sol, Lá menor, Dó. Se eu trocar o último acorde por Mi menor, o acompanhamento vai para Mi menor **porque ouviu**, não porque alguém digitou a cifra.

## O diferencial

> **O sistema se baseia no som do instrumento, não numa cifra pré-programada.**

A cifra (progressão "decorada") e o tom principal podem ajudar como **palpite**, mas a palavra final é sempre do áudio. Se o áudio for claro, ele vence a previsão.

## Princípios

1. **Errar tocando pouco é muito melhor do que errar tocando nota errada.** Um pad em Dó sobre um Lá menor quase não incomoda; um baixo em Ré, sim. Na dúvida, o sistema toca menos (só a fundamental, só o pad da tônica, ou silêncio).
2. **Separar "qual acorde" de "quando trocar".** O áudio diz qual acorde; o rastreador de tempo diz quando. Uma camada corrige a outra.
3. **Prever, não só reagir.** Detectar um acorde leva tempo; para o baixo não entrar atrasado, o sistema aprende o padrão e o andamento e **agenda** a troca. O áudio confirma ou corrige.
4. **Parar quando o músico para.** Continuar tocando depois que a música acabou é o pior erro possível.
5. **Acompanhamento canônico.** O baixo e o pad seguram a base do acorde; quem enfeita é o músico.

## Público

Músicos de banda de igreja (e bandas pequenas em geral) que tocam violão ou guitarra limpa fazendo acompanhamento com acordes "bolachões", em andamentos lentos a médios (≈ 60–130 BPM), e que precisam cobrir um baixista ou tecladista ausente.

## O que não é

- Não é um "Shazam": não reconhece a música.
- Não acompanha solo, melodia, voz nem bateria.
- Não é (ainda) um VST ou pedal: é um app de Windows, com um laboratório offline para medir as ideias com números.
