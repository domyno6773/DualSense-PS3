# Arquitetura

## A decisão central

O projeto começou com a arquitetura errada e levou muitas iterações para chegar
na certa. Vale registrar as duas, porque entender por que a primeira não podia
funcionar é o que justifica a segunda.

### O que não funciona: pad virtual (LDD)

A ideia natural é criar um controle virtual conformante e esperar que o jogo
fale com ele. É o que o PS3xPAD faz, e o que este projeto fez por muito tempo.

**Não pode funcionar para jogos que exigem controle oficial.** Um pad LDD sempre
reporta `device_type = CELL_PAD_DEV_TYPE_LDD` (5). Nunca `STANDARD` (0). Um jogo
que checa o tipo reprova qualquer pad virtual, de qualquer plugin, sempre. Medido
no console: pedindo capacidade `0x1F` o sistema concede `0x07` (conformidade,
pressão e sixaxis) e o tipo permanece `5`, imutável.

Adaptadores de hardware passam porque **são** um DualShock 3 aos olhos do
console — VID `054C:0268`, descritor HID e formato de relatório do DS3, adotados
pelo driver do próprio firmware. Nenhum plugin pode fazer isso.

### O que funciona: responder a pergunta de dentro do jogo

Se o jogo pergunta "esse controle é oficial?" e nós estamos dentro do processo
dele, podemos responder. O `ds45pad_game.sprx` intercepta `cellPadGetInfo2` e
reporta `device_type = 0` com conformidade total.

Isso compra o mesmo resultado que o adaptador, sem hardware. É o que o
"compatibility mode" do PS3xPAD tentava obter remendando o jogo — interceptar a
API é muito menos invasivo.

E, no mesmo hook, `cellPadGetData` recebe a correção da curva dos analógicos.
Funciona sobre qualquer controle que chegue ao jogo, porque nunca toca em
identidade de dispositivo.

---

## Como o desvio funciona

Em PowerPC64 ELFv1 um ponteiro de função **é** o endereço de um descritor de
duas palavras `{ponto de entrada, TOC}`. Todo módulo que importa uma função
aponta para o **mesmo** descritor na biblioteca exportadora. Sobrescrever essas
duas palavras redireciona o processo inteiro.

```c
uint32_t *opd = (uint32_t *)cellPadGetData;   /* o descritor */
saved[0] = opd[0];  saved[1] = opd[1];
opd[0] = hook[0];   opd[1] = hook[1];        /* e pronto */
```

Sem percorrer tabelas, sem NID, sem depender de onde o processo está carregado.
Chamar o original é chamar através de `saved`, que é um descritor válido.

**Confirmado por medição:** o descritor de `cellPadGetData` ficou em `0x00AA0728`
e o meu módulo (9 KB) em `0x00C400C4` — ~1,6 MB de distância, ou seja é o
descritor compartilhado dentro do `sys_io`, não um thunk local.

A primeira tentativa percorria a tabela de imports a partir de
`*(uint32_t *)0x1008C + 0x984`, deslocamento herdado dos plugins de VSH
publicados. Isso **só** resolve no processo da VSH; num processo de jogo o
0x10000 é o ELF do EBOOT. As guardas de faixa que eu havia posto salvaram o
console: retornaram vazio em vez de dereferenciar lixo.

---

## Arquivos

| Arquivo | Papel |
|---|---|
| `gamehook.c` | **O núcleo.** Desvios de `cellPadGetInfo2` e `cellPadGetData` dentro do jogo |
| `sticks.c` | Calibração e a curva de resposta do DS3 — veja [ANALOGICOS.md](ANALOGICOS.md) |
| `hook.c` | Mecanismo de desvio de descritor |
| `config.c` | Leitura do arquivo de configuração |
| `main.c` | Entrada do PRX; caminho de jogo e caminho de VSH separados por `#ifdef` |
| `util.c` | Boot trace, CRC32, leitura de arquivo, remapeamento |
| `libc.c` | Runtime mínimo (`-nodefaultlibs`) |
| `padldd.c`, `usb.c`, `passthru.c`, `ds4.c`, `dualsense.c`, `diag.c` | Caminho antigo, só no build de VSH |

O build de jogo compila **7 arquivos** e importa **2 bibliotecas** (`sys_fs` e
threads). Isso não é cosmético: um processo de jogo carrega `libio` e `libfs`
mas **não** `libusbd`, e um único import dela deixa o carregador incapaz de
resolver o módulo — o PS3MAPI fica preso em "Load" para sempre.

Auth-id também difere por build: `1070000052000001` (vsh/module) para o da VSH,
`1010000001000003` (retail game) para o de jogo. O `xpad_game.sprx` do PS3xPAD
usa o mesmo, pelo mesmo motivo.

---

## Limites conhecidos

**Botão PS em jogo: impossível por este caminho.** É a VSH que trata esse botão
e desenha o menu rápido; o jogo nunca o vê. Um hook dentro do processo do jogo
está na camada errada. O módulo da VSH resolve no XMB, via
`CELL_PAD_CTRL_LDD_PS` na palavra extra do pad LDD, e só ali.

**O desvio alcança o processo, mas depende do jogo chamar `sys_io`.** Um título
que leia o pad por outro caminho não seria capturado. O God of War II HD lista
`libio` entre seus módulos e funciona.

**Timing.** O plugin entra depois de o jogo já ter subido. Se o jogo faz a
checagem de controle uma única vez na inicialização, pode ser preciso forçar
reavaliação — desplugar e replugar o controle, ou entrar no menu de controles.

**Vibração, sixaxis e touchpad** não são implementados por este caminho. O código
existe no build de VSH, herdado da arquitetura anterior.
