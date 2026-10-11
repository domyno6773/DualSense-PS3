# Instalação passo a passo — PS3HEN

Guia específico para HEN. Para CFW, veja [INSTALAR_CFW.md](INSTALAR_CFW.md) — é
o mesmo caminho, sem o passo de reativar a cada boot.

> **Arquitetura final:** o que resolve compatibilidade e analógicos é o
> **`ds45pad_game.sprx`**, carregado no processo do jogo pelo PS3MAPI do webMAN.
> O `ds45pad_vsh.sprx` é opcional e serve só para o botão PS no XMB.
> Veja [ARQUITETURA.md](ARQUITETURA.md).

## Resumo do que instalar

```
/dev_hdd0/plugins/webftp_server_ps3mapi.sprx     <- webMAN MOD (obrigatorio)
/dev_hdd0/plugins/ds45pad/ds45pad_game.sprx      <- o que faz o trabalho
/dev_hdd0/plugins/ds45pad/ds45pad.txt            <- configuracao
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx       <- opcional (botao PS no XMB)
```

`/dev_hdd0/boot_plugins.txt`:

```
/dev_hdd0/plugins/webftp_server_ps3mapi.sprx
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
```

Com o jogo rodando: navegador no IP do PS3 → aba **PS3MAPI** → **Game Plugins**
→ selecione o processo do jogo → **Set** → num slot livre digite
`/dev_hdd0/plugins/ds45pad/ds45pad_game.sprx` → **Load**.

Confirme em `/dev_hdd0/tmp/ds45pad_boot.txt`:

```
game_mode
hook_cellPadGetData_ok
hook_cellPadGetDataExtra_ok
hook_cellPadGetInfo2_ok
```

Para **sair do jogo** sem botão PS: mesma aba PS3MAPI, botão **Exit**.

---

O restante deste documento é o detalhamento, incluindo a rede de segurança.

---

## ⚠️ Leia isto primeiro

**No HEN, um plugin com assinatura errada faz o console travar no boot.** Não é
brick permanente — tem conserto e o conserto está no Passo 1 — mas você não vai
querer descobrir isso sem a rede de segurança montada.

A regra é simples: o `.sprx` **tem** que ser assinado com revisão de chave
`0A` (3.55). O Makefile deste projeto já faz isso. Se você pegar um `.sprx`
pronto de outra pessoa, confirme que foi assinado assim.

---

## Passo 0 — O que você precisa ter

| Item | Observação |
|---|---|
| PS3 com HEN ativo | Qualquer versão recente (4.8x / 4.9x) |
| `ds45pad_vsh.sprx` compilado e assinado | Veja o Passo 2 — este é o passo mais trabalhoso |
| Um pendrive FAT32 | Para a rede de segurança e para transferir arquivos |
| Um gerenciador de arquivos no PS3 | multiMAN, IRISMAN ou webMAN MOD |

Ter **webMAN MOD** instalado facilita muito, porque ele dá FTP e você transfere
tudo pelo PC sem ficar indo e voltando com pendrive. Não é obrigatório.

---

## Passo 1 — Monte a rede de segurança (faça isto ANTES de tudo)

Este passo leva um minuto e é o que te salva se algo der errado.

1. Pegue um pendrive FAT32.
2. Na **raiz** dele (não dentro de pasta nenhuma), crie um arquivo de texto
   **vazio** chamado exatamente `boot_plugins.txt`.
3. Guarde esse pendrive.

Por que funciona: o `boot_plugins.txt` do pendrive tem **prioridade** sobre o do
HD interno. Se o console travar ao ativar o HEN, é só plugar esse pendrive antes
de ativar — nenhum plugin carrega, e você entra normalmente para consertar.

**Plano B**, se você não tiver o pendrive na hora: entre no **Menu de Recuperação**
(desligue no botão, segure Power até o segundo bipe, solte, segure de novo) e use
**Reconstruir Banco de Dados**. Isso apaga o `/dev_hdd0/boot_plugins.txt`.

---

## Passo 2 — Obtenha o `.sprx` compilado

**Sendo direto: eu não consigo compilar isto para você**, e este é o degrau real
do projeto.

Um plugin da VSH é um **PRX** — módulo relocável com seção `.sceModuleInfo`,
injetado num processo que já está rodando — e não um `.self` homebrew comum.
Produzir PRX exige o modo `-mprx`, que só existe no GCC da SCE. O toolchain
open source (`ps3toolchain`/PSL1GHT) tem a API toda de que o plugin precisa, mas
**não consegue emitir esse formato**. O porquê, em detalhe, está em
[BUILD.md](BUILD.md#por-que-o-psl1ght-não-serve-e-essa-pergunta-merece-resposta-precisa).

Ou seja: Cell SDK — que é o mesmo toolchain com que **todo** plugin de HEN é
compilado, não um requisito especial deste projeto. Ele traz um toolchain
nativo de Windows, então não precisa de WSL nem compilar nada do zero.

**O passo a passo de montagem está em [COMPILAR.md](COMPILAR.md)**, com o que
procurar, como conferir que o pacote está completo, as variáveis de ambiente e
os erros comuns. Depois é só:

```bash
cd src
make
```

Sai `src/ds45pad_vsh.sprx`.

Se preferir não montar ambiente nenhum, [PEDIR_BUILD.md](PEDIR_BUILD.md) tem um
pedido pronto para postar em fórum.

De qualquer forma, **confirme a linha de assinatura**: tem que ter `-2 0A`.

---

## Passo 3 — Copie os arquivos para o console

Crie a pasta `/dev_hdd0/plugins/ds45pad/` e ponha três arquivos nela:

```
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx        <- src/ds45pad_vsh.sprx
/dev_hdd0/plugins/ds45pad/ds45pad.txt             <- data/ds45pad.txt
/dev_hdd0/plugins/ds45pad/ds45pad_devices.txt     <- data/ds45pad_devices.txt
```

**Por FTP (webMAN MOD instalado):** conecte no IP do PS3 pela porta 21, usuário
e senha em branco, e arraste os arquivos.

**Por pendrive:** copie a pasta `ds45pad` para o pendrive, plugue no PS3, e use
o multiMAN/IRISMAN para copiar de `/dev_usb000/ds45pad` para
`/dev_hdd0/plugins/ds45pad`.

Os dois `.txt` são opcionais — sem eles o plugin usa os padrões, que já são os
que você quer. Mas leve o `ds45pad.txt` de qualquer forma: é ali que fica o
ajuste dos analógicos.

---

## Passo 4 — Registre o plugin no `boot_plugins.txt`

1. Abra (ou crie) o arquivo `/dev_hdd0/boot_plugins.txt`.
2. Adicione **uma linha**, com o caminho completo:

```
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
```

3. Salve.

Detalhes que costumam dar problema:

- Um caminho por linha, sem aspas, sem espaços sobrando.
- O arquivo tem que estar na **raiz** do `dev_hdd0`, não dentro de `plugins/`.
- Se você já usa webMAN MOD, a linha dele já está aí. Só acrescente a sua
  embaixo, não substitua.
- Se o seu setup usa MAMBA em vez de Cobra, o arquivo se chama
  `mamba_plugins.txt`.

---

## Passo 5 — Reinicie e ative o HEN

1. Reinicie o PS3.
2. Ative o HEN do jeito que você já faz (o app HEN Installer ou o exploit pelo
   navegador).
3. Alguns segundos depois deve aparecer o aviso **`DS45PAD 1.0.0 carregado`**.

O aviso demora ~8 segundos de propósito, para não aparecer enquanto o XMB ainda
está montando. O controle já funciona antes disso.

**Se o console travar aqui:** desligue no botão de trás, plugue o pendrive do
Passo 1, ligue e ative o HEN. Nenhum plugin vai carregar. Aí é quase certo que o
`.sprx` foi assinado errado — volte ao Passo 2 e confira o `-2 0A`.

---

## Passo 6 — Conecte o controle

### Por cabo USB (recomendado)

Plugue e pronto. A barra de luz acende na cor da porta e o botão PS abre o XMB.

### Por Bluetooth

1. Conecte o controle **por cabo** primeiro.
2. **Ajustes → Ajustes de acessórios → Gerenciar dispositivos Bluetooth →
   Registrar novo dispositivo → Iniciar busca**.
3. Enquanto procura, segure **PS + Share** (DS4) ou **PS + Create** (DualSense)
   até a luz piscar rápido.
4. Aceite o pareamento e tire o cabo.
5. Ainda em **Ajustes de acessórios → Reatribuir controles**, mande o
   DS4/DualSense para a **porta 7**.

O passo 5 não é opcional: ele libera a porta 1 para o controle virtual. Sem
isso os jogos veem os dois controles e o genérico (o ruim) ganha.

Por Bluetooth o botão PS vem por **SELECT + START**, porque o firmware não
entrega o botão PS real por esse caminho.

---

## Passo 7 — Calibre os analógicos (30 segundos, vale a pena)

Com o controle conectado, **gire os dois analógicos algumas vezes até o
batente**, fazendo círculos completos.

Isso ensina ao plugin o curso real de cada eixo. É o que faz um stick Hall
effect — que muitas vezes só vai de `0x14` a `0xEB` — alcançar o extremo. O
resultado fica salvo em `/dev_hdd0/plugins/ds45pad/ds45pad_cal.txt` e não precisa ser refeito.

Depois teste num jogo. Se ainda faltar alcance nos cantos, abra
`ds45pad.txt` e baixe:

```
STICK_SATURATION = 75
```

Ou `71`, que leva a diagonal ao canto absoluto. O detalhamento está em
[ANALOGICOS.md](ANALOGICOS.md).

---

## Se o console travar ao ligar o HEN

Plugue o pendrive do Passo 1 e ligue o HEN — nenhum plugin carrega e você entra
normal. Então leia **`/dev_hdd0/tmp/ds45pad_boot.txt`**.

O plugin grava um marcador nesse arquivo **antes** de cada etapa que mexe com o
sistema, fechando o arquivo a cada escrita. O último marcador presente nomeia a
etapa que travou:

| Último marcador | Onde travou |
|---|---|
| *(arquivo não existe)* | O módulo nem chegou a rodar — assinatura ou formato do SPRX |
| `start` | Leitura do `ds45pad.txt` |
| `config` | Leitura do `ds45pad_cal.txt` |
| `sticks` | Registro do driver USB (`cellUsbdRegisterExtraLdd`) |
| `usb_init` | Ainda no registro USB |
| `usb_ok` | Instalação do desvio de vibração |
| `rumble` | Entrada no laço principal |
| `loop` | Rodou. Se travou depois, é a injeção do pad ou o Bluetooth |
| `notify` | A busca na tabela de imports da VSH — ponha `NOTIFY = 0` |

Com o marcador em mãos, dá para isolar rápido. Duas mitigações que quase sempre
adiantam:

- `MODE = BT` no `ds45pad.txt` pula todo o registro USB;
- `NOTIFY = 0` (já é o padrão) evita a leitura da tabela de imports, que depende
  do layout do firmware.

## Se algo não funcionar

| Sintoma | Causa provável |
|---|---|
| Console trava ao ativar o HEN | Assinatura errada. Pendrive do Passo 1, e confira `-2 0A` |
| Nenhum aviso no XMB | Caminho errado no `boot_plugins.txt`, ou o arquivo não está na raiz do `dev_hdd0` |
| Aviso aparece, controle não responde no USB | Outro plugin de controle pegou o aparelho antes. Tire o PS3xPAD do `boot_plugins.txt` se estiver lá |
| Funciona no XMB mas não no jogo | O HEN não expôs as syscalls de pad. O plugin ainda funciona parcialmente; o botão PS continua bom |
| Um jogo específico recusa o controle | `CAPABILITY_MASK = 0xFFFF` no `ds45pad.txt` |
| Dois controles respondendo juntos (Bluetooth) | Falta reatribuir o físico para a porta 7 (Passo 6) |
| Analógicos ainda curtos nos cantos | Refaça o Passo 7, depois `STICK_SATURATION = 75` |

Para investigar qualquer um desses, ligue `DIAG = 1` no `ds45pad.txt` e leia
`/dev_hdd0/tmp/ds45pad_diag.txt` — o procedimento está em
[DIAGNOSTICO.md](DIAGNOSTICO.md).

---

## Desinstalar

Apague a linha do `/dev_hdd0/boot_plugins.txt` e reinicie. A pasta
`plugins/ds45pad/` pode ficar onde está sem efeito nenhum.
