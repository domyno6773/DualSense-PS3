# Analógicos: por que erram nos extremos, e o que o plugin faz

## O sintoma

O controle é reconhecido, os botões funcionam, mas nos jogos os analógicos
"não vão até o fim": falta curva no volante, o personagem não corre no talo,
a mira não completa o giro, e as diagonais parecem sempre incompletas. Com
sticks Hall effect fica pior.

A suspeita natural é erro de mapeamento ou de escala. **Não é.** Os bytes do
DS4/DualSense já são 0..255 com 0x80 em repouso — a mesma convenção do
DualShock 3 — e o plugin os copia sem tocar. A diferença é a **curva de
resposta**.

## A causa

O conversor analógico do DualShock 3 **satura antes do fim do curso
mecânico**, por volta de 80% dele. Duas consequências:

- empurrar um eixo até o fim dá `0x00`/`0xFF` **com curso sobrando**;
- uma diagonal cheia põe cada eixo a 0,707 do curso mecânico, o que dá
  0,707 / 0,80 = **0,88** de fundo de escala — cerca de `0x0F`.

O DS4 e o DualSense são lineares até o batente. A mesma diagonal cheia dá
0,707 de fundo de escala — cerca de `0x26`. É muito longe do que um jogo de
PS3 foi ajustado para receber.

```
        Diagonal cima-esquerda, empurrada até o batente

        DualShock 3 ....... 0x0F   ← o que o jogo espera
        DS4 / DualSense ... 0x26   ← o que ele recebe hoje
```

Hall effect soma um segundo problema: esses módulos são lineares **e** costumam
ter faixa reduzida — muitos só vão de `0x14` a `0xEB`. Aí nem o eixo sozinho
chega ao extremo, e o jogo nunca vê o valor máximo em direção nenhuma.

## A correção

Um único parâmetro reproduz a curva inteira. Normaliza-se cada eixo contra o
curso que aquele stick **realmente** alcança e divide-se pela fração de
saturação, prendendo no limite:

```
f = clamp(f_medido / 0,80, -1, +1)
```

- **No eixo:** 1,00 / 0,80 satura. Fundo de escala alcançado cedo, exatamente
  como num DS3 — e um Hall effect de curso curto também chega lá.
- **Na diagonal:** 0,707 / 0,80 = 0,88, o número do DS3.
- **Girando o stick:** o traçado é um círculo cortado pelo clamp, virando um
  quadrado de lados arredondados. É a forma que um DS3 real produz.

`STICK_SATURATION` é esse número, em porcentagem.

## Números verificados

Simulação do pipeline exatamente como o código o executa (aritmética inteira
incluída), com zona morta de 4%:

| Situação | Bruto | `100` (cru) | `80` (padrão) | `71` |
|---|---|---|---|---|
| **DS4 de fábrica** (0–255) | | | | |
| repouso | 128,128 | 128,128 | 128,128 | 128,128 |
| esquerda total | 0,128 | **0**,128 | **0**,128 | **0**,128 |
| diagonal cheia | 37,37 | 37,37 | **14,14** | 0,0 |
| meio curso | 64,128 | 66,128 | 51,128 | 41,128 |
| **Hall effect** (20–235) | | | | |
| esquerda total | 21,128 | 1,128 | **0**,128 | **0**,128 |
| diagonal cheia | 52,52 | 37,37 | **15,15** | 1,1 |
| **Stick gasto** (45–210) | | | | |
| esquerda total | 46,128 | 1,128 | **0**,128 | **0**,128 |
| diagonal cheia | 70,70 | 38,38 | **16,16** | 1,1 |

Referência do DualShock 3: eixo cheio `0x00`, diagonal cheia ≈ `0x0F` (15).

Repare em duas coisas:

1. Com `80`, a diagonal cai em **14–16** nos três casos — o valor do DS3.
2. Depois de calibrado, o Hall effect produz **os mesmos números** que um stick
   de fábrica. A diferença de hardware desaparece.

E repare no que acontece **sem** correção: um Hall effect empurrado até o fim
entrega `1`, nunca `0`. Jogos que testam o extremo exato simplesmente nunca
disparam.

## Calibração automática

`STICK_CALIBRATION = AUTO` aprende o curso real de cada eixo enquanto você
joga, e é o que faz a linha "Hall effect" da tabela funcionar.

Como funciona, e por que é seguro:

- Cada eixo mantém um **histograma** dos valores vistos numa janela de 512
  amostras. No fim da janela, `min`/`max` andam parte do caminho até o bucket
  mais extremo que foi atingido pelo menos 8 vezes. Um valor visto uma vez não
  move nada, por mais extremo que seja.
- **Alargar é rápido, estreitar é 8× mais lento.** Alargar é como o curso real
  é descoberto; estreitar devagar evita que algumas janelas de movimento
  parcial retunem a resposta de forma perceptível, mas garante que um extremo
  errado decaia sozinho.
- Com o controle parado, todas as amostras caem no bucket central, as duas
  estimativas ficam dentro de `CAL_MIN_SPAN` e **nada se move** — deixar o
  controle na mesa não colapsa a faixa.
- O centro só é aceito depois de o valor ficar parado por cerca de um segundo,
  **e** só se estiver a menos de 25 unidades de 0x80. Sem essa segunda trava,
  segurar o stick para um lado durante uma partida arrastaria o centro junto.
- Começa assumindo uma faixa conservadora (`0x14`–`0xEB`), típica de Hall
  effect. Errar para o lado estreito é inofensivo — o clamp absorve. Errar para
  o lado largo é que impediria alcançar o máximo.

O resultado vai para `/dev_hdd0/plugins/ds45pad/ds45pad_cal.txt`, então o primeiro menu depois de um
reboot já está certo. Girar os dois analógicos uma vez ao ligar acelera tudo.

### Por que não é mais uma catraca

A primeira versão alargava um passo por amostra e **nunca estreitava**. Isso é
uma catraca de mão única: qualquer extremo espúrio virava permanente, e
recalibrar rodava o mesmo código de mão única, então não havia como desfazer.

Um build antigo tinha justamente uma fonte de extremos espúrios — um relatório
não preenchido era lido como raw 0 — o que arrastou o `min` de todos os eixos
até o trilho, enquanto os `max` continuaram honestos.

O detalhe que fez isso demorar para ser encontrado é **onde o estrago aparece**:

- **Nos eixos puros, é invisível.** O clamp da saturação entrega 0x00/0xFF
  sólido de qualquer jeito, com faixa certa ou faixa inflada.
- **Nas diagonais, aparece.** Ali nada clampa (0.707/0.80 = 0.88, meio de
  escala), então a faixa inflada se traduz direto em menos alcance.
- E como cada diagonal usa um par diferente de meios-spans, uma fuga só do
  lado `min` custa **16% de alcance em cima-à-esquerda, 8% nos dois lados, e
  zero em baixo-à-direita**.

Ou seja: um cantinho específico de um analógico que não fecha, com todo o resto
perfeito, e imune a recalibração. Foi exatamente esse o relato que levou à
troca da catraca pelo histograma.

## Ajuste fino

| Sintoma | O que mexer |
|---|---|
| Ainda falta alcance nos cantos | `STICK_SATURATION = 75` ou `71` |
| Personagem "gruda" nas diagonais | `STICK_SATURATION = 85` |
| Quero o valor cru, sem correção | `STICK_SATURATION = 100` |
| Analógico anda sozinho | Suba `STICK_DEADZONE` |
| Hall effect, quero precisão máxima no centro | `STICK_DEADZONE = 0` |
| Calibração ficou estranha | Apague `/dev_hdd0/plugins/ds45pad/ds45pad_cal.txt` e gire os sticks |

`71` merece uma nota: é o valor em que 0,707 / 0,71 ≈ 1, ou seja, a diagonal
chega ao canto exato (`0x00`/`0xFF`). Um DS3 real **não** faz isso — vai até
`0x0F`. Use `71` se um jogo específico só reage no extremo absoluto, mas `80` é
o que reproduz o controle original.

## Bluetooth

A correção também vale no modo Bluetooth: o driver genérico do firmware entrega
valores de eixo, e a curva é aplicada sobre eles do mesmo jeito. A calibração
aprende a faixa que chega, seja ela qual for.
