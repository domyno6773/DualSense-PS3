# Compilando o plugin

> **Já está compilado.** O `.sprx` assinado está em
> `release/plugins/ds45pad/`. Este documento é para quando você quiser
> recompilar depois de mexer no código.

## Recompilar

```bash
cd src
./build.sh
```

Sai `ds45pad_vsh.sprx`, já assinado e com o magic conferido. O script assume
o SDK em `C:/ps3sdk/cell` e as ferramentas em `C:/ps3sdk/tools`; se você mudar
de lugar, exporte `CELL_SDK` e `DS45_TOOLS`.

Ele não usa GNU make de propósito: o `make.exe` distribuído com o webMAN MOD é
um binário de subsistema **GUI**, então no Git Bash não devolve saída nem
código de saída aproveitável. Chamar o compilador direto é mais simples e muito
mais fácil de depurar.

## Como o ambiente foi montado

O SDK veio do instalador oficial, extraído sem executá-lo. Os pacotes usados,
de dentro de `PS3 4.75 SDK Offline Installer/InstallFiles/`:

| Pacote | Para quê |
|---|---|
| `[157]/PS3_Toolchain_411-Win_420_001.zip` | **O compilador.** `ppu-lv2-gcc` não vem no pacote base do SDK 4.75 |
| `[155]/PS3_SDK-475_001.zip` | Headers e stubs (`libio_stub.a` e companhia) |
| `[164]/PS3_SDK_Samples-475_001.zip` | `samples/mk/*.mk`, usados pelo Makefile |

Cuidado com o `unzip`: o glob `"cell/target/*"` **não** recursa em subpastas em
algumas versões, e extrai só o primeiro nível silenciosamente. Extraia o zip
inteiro ou use padrões explícitos por subdiretório.

### scetool

O repositório do naehrwert traz só o fonte. O binário pronto do repositório do
webMAN MOD funciona, mas é **Cygwin** e precisa de três DLLs que não vêm junto:

```
cygwin1.dll       pacote cygwin
cygz.dll          pacote zlib0
cyggcc_s-1.dll    pacote libgcc1   <- esquecida com facilidade: e dependencia
                                      da cygz.dll, nao do scetool
```

São do projeto Cygwin, livremente redistribuíveis, e o executável é 32 bits —
tem que ser as versões x86. Sem a terceira, o Windows devolve `0xC0000135`
(DLL não encontrada) sem dizer qual.

As chaves ficam em `data/keys`, `data/ldr_curves` e `data/vsh_curves`, e o
scetool as localiza pela variável de ambiente `PS3`, que deve apontar para a
pasta que **contém** o `keys`.

---

## Referência: por que precisa do Cell SDK

## A resposta curta

**O Cell SDK é o toolchain de plugin de PS3.** Não é um contorno nem um
requisito exótico deste projeto: é com ele que o PS3xPAD, o webMAN MOD, o
Akari, o IRISMAN e todo plugin de HEN que você já instalou foram compilados.

"Compilar igual qualquer outro plugin do HEN" **é** compilar com o Cell SDK.
Não existe um caminho mais fácil que os outros devs usam e que eu esqueci de
mencionar — eles usam este.

A boa notícia: o SDK traz um toolchain nativo de Windows. Você não precisa de
WSL, Linux, nem compilar GCC do zero. Depois de montado, `make` e pronto.

## Por que não tem alternativa (checado, não suposto)

Antes de aceitar isso, procurei em todo lugar:

| Tentativa | Resultado |
|---|---|
| PSL1GHT | Tem a API toda (`ioPadLddDataInsert`, `usbRegisterExtraLdd`, `lv2syscall4`), mas **nenhuma regra que produza `.prx`**. Só `%.a`, `%.o`, `%.elf`, `%.self`, `%.pkg` |
| `sprxlinker` | Apesar do nome, faz o oposto: corrige stubs para um homebrew **chamar** SPRX do sistema. Consome, não gera |
| Forks do PSL1GHT (bucanero, bigboss, kakaroto) | Idênticos nesse ponto |
| PSDK3v2 (Estwald) | É baseado em PSL1GHT. Mesma limitação |
| Conversor ELF→PRX aberto | Não existe para PS3. O `psp-prxgen` é só PSP; o `SPRXPatcher` faz outra coisa |

O motivo de fundo: plugin de VSH é **PRX** — módulo relocável com seção
`.sceModuleInfo` e `module_start`/`module_stop`, injetado num processo que já
está rodando. Emitir esse formato exige o modo `-mprx`, que muda o modelo de
código (relocações e TOC próprios). É extensão da SCE ao GCC dela, e o GCC do
`ps3toolchain` é GCC padrão.

---

## Montando o ambiente no Windows

### 1. Obter o SDK

Procure por **"PS3 SDK 4.75"** ou **"CELL SDK 3.41"**. São as duas versões que a
cena usa; qualquer uma serve para este projeto. É software da Sony de 2010,
abandonado há mais de uma década, e circula em arquivos da cena — não é
distribuído pela Sony e eu não vou linkar.

Você tem o pacote certo se, depois de extrair, existirem estes caminhos:

```
<raiz>\host-win32\ppu\bin\ppu-lv2-gcc.exe
<raiz>\target\ppu\lib\libio_stub.a
<raiz>\target\ppu\lib\libusbd_stub.a
<raiz>\target\ppu\lib\libfs_stub.a
<raiz>\target\ppu\lib\libsysmodule_stub.a
<raiz>\samples\mk\sdk.makedef.mk
```

Se algum desses faltar, o pacote está incompleto — procure outro. Os quatro
`.a` são exatamente os que este plugin usa.

### 2. Obter o scetool com as chaves

O `scetool` é **open source**: <https://github.com/naehrwert/scetool>. O que
não vem junto é o arquivo `keys`, com as chaves retail 3.55 — derivadas pela
cena em 2011 e distribuídas do mesmo jeito que o SDK.

Ponha `scetool.exe` e o `keys` na mesma pasta, e essa pasta no PATH.

### 3. Variáveis de ambiente

No PowerShell, para testar na sessão atual:

```powershell
$env:CELL_SDK = "C:\ps3sdk"
$env:PATH = "$env:PATH;C:\ps3sdk\host-win32\ppu\bin;C:\ps3sdk\host-win32\bin;C:\scetool"
```

Trocando os caminhos pelos seus. Para deixar permanente, use
**Configurações → Sistema → Sobre → Configurações avançadas do sistema →
Variáveis de Ambiente**.

### 4. Conferir que funcionou

```powershell
ppu-lv2-gcc --version
```

```powershell
scetool --help
```

Se os dois responderem, está pronto.

### 5. Compilar

```powershell
cd C:\Users\victo\Desktop\Geral\GOW\TestePlugin\src
make
```

Sai `ds45pad_vsh.sprx`.

### 6. Conferir o resultado

O arquivo tem que ter alguns dezenas de KB e começar com a assinatura `SCE\0`.
Para checar:

```powershell
Format-Hex -Path ds45pad_vsh.sprx -Count 4
```

Deve mostrar `53 43 45 00`. Se mostrar `7F 45 4C 46` (`ELF`), o `scetool` não
rodou e o arquivo **não vai carregar** — e, no HEN, um SPRX inválido em
`boot_plugins.txt` trava o boot. Monte a rede de segurança do
[INSTALAR_HEN.md](INSTALAR_HEN.md#passo-1--monte-a-rede-de-segurança-faça-isto-antes-de-tudo)
antes de testar.

---

## Erros comuns

| Erro | Causa |
|---|---|
| `make: *** No rule to make target ... sdk.makedef.mk` | `CELL_SDK` errado ou não definido |
| `ppu-lv2-gcc: command not found` | Falta `host-win32\ppu\bin` no PATH |
| `cannot find -lusbd_stub` | SDK incompleto — falta `target\ppu\lib` |
| `system_call_4` não declarado | Adicione `#include <sys/syscall.h>` no topo de `padldd.c` e `main.c` |
| `cellPadGetRawData` não declarado | SDK antigo. É usado só pelo diagnóstico; apague as 4 linhas que o chamam em `diag.c` |
| `scetool: command not found` | Não está no PATH, ou falta o arquivo `keys` ao lado dele |
| Warnings de `unused variable` | Normais, pode ignorar |

O código nunca passou por um compilador. Se aparecer erro que não está nesta
tabela, me traga a saída do `make` que eu corrijo.

---

## Enquanto isso

O **PS3xPAD** tem uma build já assinada para HEN, pronta para baixar e instalar
hoje ([GBAtemp](https://gbatemp.net/download/ps3xpad-3-55-signed-for-hen.37155/)).
Ela te dá botão PS e conformidade com jogos num DualShock 4 — sem DualSense, e
**sem a correção da curva dos analógicos**, que é justamente a sua queixa
principal. Serve como paliativo enquanto você monta o ambiente, não como
substituto.
