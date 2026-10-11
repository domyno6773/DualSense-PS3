# O que já existia

Levantamento feito antes de escrever qualquer linha, para não recomeçar do
zero. Resumo: existe **um** projeto sério nessa área, ele está parado desde
2015, e a técnica central dele é sólida e foi reaproveitada aqui.

---

## PS3xPAD (OsirisX, 2015)

Repositório: <https://github.com/aldostools/PS3xPAD> · fórum:
<https://www.psx-place.com/resources/ps3xpad.22/>

O único plugin de controle do PS3 com código-fonte aberto. Última versão 0.8,
de dezembro de 2015. Suporta Xbox 360/One, DS3 e DS4. **Não suporta DualSense**
— o controle nem existia.

### O que ele acertou, e que este projeto herda

Três descobertas que resolvem exatamente as dores citadas:

1. **`syscall 574`** registra um pad LDD com uma *máscara de capacidade
   arbitrária*. É isso que permite anunciar
   `CELL_PAD_CAPABILITY_PS3_CONFORMITY` e fazer jogos que exigem controle
   oficial aceitarem o pad virtual.

2. **`syscall 573`** liga o *data insert mode*, sem o qual os dados injetados
   só chegam ao XMB e nunca aos jogos.

3. **`button[0] |= CELL_PAD_CTRL_LDD_PS`** — o botão PS não é um botão normal.
   Ele vive numa palavra extra do relatório que só existe para pads LDD. É
   literalmente a razão de o botão PS não funcionar num DS4 comum.

O fluxo geral (`cellUsbdRegisterExtraLdd` para tomar o device do driver do
sistema → ler relatórios HID → `cellPadLddDataInsert`) também veio daí.

### O que ficou para trás

| Limitação do PS3xPAD | Aqui |
|---|---|
| Sem DualSense | Suportado, incluindo Edge |
| Ring buffer de 10 amostras drenado 1 por vez → input lag | Buffer duplo, sempre a amostra mais nova |
| Sixaxis "4x sensível demais", corrigido no chute | Conversão física (8192 LSB/g → 113 LSB/g) |
| Sem touchpad, LEDs de jogador, bateria | Suportados |
| Wireless exigia dongle USB + pilha Bluetooth inteira | Passthrough pelo Bluetooth interno, sem hardware extra |
| `capability = 0xFFFF` (bits indefinidos) | Máscara explícita, configurável |

---

## Outras alternativas avaliadas

| Opção | Veredito |
|---|---|
| **Adaptadores Brook / CronusMax / Titan** | Funcionam bem, inclusive botão PS. Solução de hardware: emulam um DS3 real por USB (o PS3 não tem autenticação criptográfica de controle, só checa VID/PID + formato do relatório). Custam dinheiro e não é o que foi pedido. |
| **Driver genérico do próprio PS3** | É o que já acontece hoje. Aceita DS4 por USB e por Bluetooth (pareando com PS+Share), mas descarta PS, movimento, touchpad e vibração. |
| **Patch no kernel LV2** | Resolveria vibração sem plugin de jogo, mas exige payload Cobra/Mamba customizado — risco alto, ganho pequeno. Descartado. |
| **PSL1GHT / homebrew comum** | Não serve: um `.self` homebrew não roda junto com jogos. Precisa ser SPRX carregado pelo loader de plugins. |

---

## Referências técnicas usadas

**Formato dos relatórios HID** — driver `hid-playstation` do Linux
(<https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c>).
É a melhor documentação pública de DS4 e DualSense: offsets, IDs de relatório,
semente `0xA2` do CRC32 dos relatórios Bluetooth, escalas de sensor
(`DS_ACC_RES_PER_G = 8192`, `DS_GYRO_RES_PER_DEG_S = 1024`) e os VID/PIDs.
Também confirmou que os paddles do DualSense Edge **não** são decodificados
pelo kernel — por isso aqueles bits estão marcados como "verificar" no código.

**API `cellPad`** — implementação do RPCS3
(`rpcs3/Emu/Io/pad_types.h`, `rpcs3/Emu/Cell/Modules/cellPad.cpp`). De onde
saíram os valores exatos de `CELL_PAD_CTRL_LDD_PS = 0x0001`,
`CELL_PAD_CAPABILITY_*`, os offsets de `button[]`, e a confirmação de que
`cellPadSetActDirect` mapeia `motor[1]` para o motor grande e `motor[0]` para o
pequeno — e de que o pad **precisa** anunciar `CAPABILITY_ACTUATOR` ou a
chamada de vibração é rejeitada com `CELL_PAD_ERROR_UNSUPPORTED_GAMEPAD`.

**NIDs** — no PS3 são `SHA-1(nome + sufixo de 16 bytes da Sony)`, primeiros 4
bytes lidos em little-endian. O algoritmo foi validado contra três valores já
publicados antes de ser usado:

| Símbolo | Calculado | Confere com |
|---|---|---|
| `allocator/malloc` | `0x759E0635` | valor fixo no PS3xPAD ✅ |
| `allocator/free` | `0x77A602DD` | valor fixo no PS3xPAD ✅ |
| `sys_io/cellPadInit` | `0x1CF98800` | NID público conhecido ✅ |
| `sys_io/cellPadSetActDirect` | **`0xF65544EE`** | usado no hook de vibração |

**Build de SPRX** — Makefiles do PS3xPAD e do webMAN MOD. Ambos usam o Cell SDK
oficial (`ppu-lv2-gcc -mprx`) e assinam com `scetool` usando exatamente os
mesmos parâmetros, que é o que faz um plugin carregar tanto em CFW quanto em
HEN.

**Comportamento do DS4 no Bluetooth do PS3** — relatos consistentes da
comunidade (Push Square, GamesRadar e outros) confirmam que o pareamento nativo
funciona desde uma atualização de sistema, mas sem touchpad, sixaxis, botão PS
nem vibração. É exatamente o cenário que o modo passthrough atende.

---

# O que aprendemos medindo no console

Esta seção é o resultado da depuração em hardware, e é a parte que não estava em
documentação nenhuma. Vale mais que o levantamento acima.

## Sobre pads virtuais

**Um pad LDD sempre reporta `device_type = 5`.** Nunca `STANDARD` (0). Pedindo
capacidade `0x1F` o sistema concede `0x07` — conformidade, pressão e sixaxis — e
nega os dois bits de hardware. O tipo é imutável. Consequência: **nenhum plugin
baseado em pad virtual pode satisfazer um jogo que checa o tipo de dispositivo.**
Vale para este projeto, para o PS3xPAD e para qualquer outro.

**A syscall 573 (`INSERT_DATA_INTO_GAME_MODE_ON`) impede apps de carregar.**
Com ela ligada, qualquer app iniciado depois — multiMAN incluído — nunca termina
de subir. O pad virtual em si é inofensivo; é a exposição aos jogos que trava.
Isolado por bissecção: sem pad → abre; pad sem 573 → abre; pad com 573 → trava.
O PS3xPAD bate na mesma parede, e é por isso que o README dele manda apertar
`START+SELECT+R3` **depois** que o jogo sobe.

**`CELL_PAD_CTRL_LDD_PS` funciona, mas só no XMB.** O botão PS em jogo é tratado
pela VSH, não pelo jogo — nenhum hook no processo do jogo alcança isso.

## Sobre USB

**O driver genérico do firmware não solta o DualSense.**
`cellUsbdRegisterExtraLdd` não toma o dispositivo dele: o callback de attach
nunca dispara (diagnóstico `probe_NEVER_CALLED`). O controle fica na porta 0 como
pad genérico, `device_type = 1`, capacidade `0x00`. O PS3xPAD sofre do mesmo mal
— "0 USB controllers detected" com o controle plugado.

**Um processo de jogo não carrega `libusbd`.** A lista de módulos de um título em
execução tem `libio` e `libfs`, mas não `libusbd`. Um único import dela deixa o
carregador incapaz de resolver o módulo, e o PS3MAPI fica preso em "Load" para
sempre.

## Sobre assinatura

**Revisão de chave `0A` (3.55) é obrigatória no HEN.** Mais antiga trava o boot.

**O auth-id depende de onde o módulo vai rodar:**

| Destino | auth-id |
|---|---|
| Processo da VSH | `1070000052000001` (sys/internal + vsh/module) |
| Processo de jogo | `1010000001000003` (retail game/update) |

O `xpad_game.sprx` do PS3xPAD usa o segundo, pelo mesmo motivo.

## Sobre desvios de função

**`*(uint32_t *)0x1008C + 0x984` só resolve no processo da VSH.** É o percurso
de tabela que os plugins publicados usam; num processo de jogo o 0x10000 é o ELF
do EBOOT e não há tabela ali.

**O caminho que funciona em qualquer processo:** pegar o ponteiro da função
importada, que em PowerPC64 ELFv1 já é o endereço do descritor
`{ponto de entrada, TOC}`, e sobrescrever as duas palavras. Medido: o descritor
de `cellPadGetData` ficou ~1,6 MB fora do módulo injetado, ou seja é o
compartilhado dentro do `sys_io` — e patcheá-lo redireciona o processo inteiro.

## Sobre a curva dos analógicos

A hipótese central se sustentou: o conversor do DualShock 3 satura a ~80% do
curso mecânico, e reproduzir isso com um único parâmetro faz DS4, DualSense e
Hall effect entregarem os mesmos números que um DS3. Verificado em simulação com
a aritmética inteira do C, e confirmado em jogo. Detalhes em
[ANALOGICOS.md](ANALOGICOS.md).
