# Pedindo para alguém compilar

Compilar exige o Cell SDK, que é proprietário. Quem já tem o ambiente montado
faz isso em segundos — e nos fóruns de PS3 tem bastante gente nessa situação.

Abaixo tem um texto pronto para postar. Está em inglês porque PSX-Place e
GBAtemp são internacionais.

**Onde postar:** [PSX-Place](https://www.psx-place.com) (seção PS3
Homebrew/Development) ou [GBAtemp](https://gbatemp.net) (PS3 hacking).

Antes de postar, suba o projeto para algum lugar que dê para baixar — um repo
no GitHub é o ideal, porque aí a pessoa clona e compila direto.

---

## Texto para copiar

> **[Request] Build help: DS45PAD — DualShock 4 / DualSense VSH plugin**
>
> I wrote a PS3 VSH plugin that gives DS4 and DualSense controllers proper
> DualShock 3 behaviour — working PS button, PS3 conformity so games that
> demand an official pad accept them, and a DS3-matched analog stick response
> curve with automatic calibration (which also fixes Hall effect sticks that
> never reach the ends of their range).
>
> I don't have the Cell SDK, so I can't build it. Would anyone with the
> toolchain be willing to compile it? The code is self-contained — 13 files, no
> external dependencies beyond the SDK stubs.
>
> **Source:** `<link do seu repositório>`
>
> **Build:**
> ```
> cd src && make
> ```
>
> **Requirements:**
> - Cell SDK PPU toolchain (`ppu-lv2-gcc`, `-mprx`), `CELL_SDK` set
> - Stubs: `usbd_stub`, `io_stub`, `fs_stub`, `sysmodule_stub`
> - `scetool` with retail keys
>
> **Important — signing:** the Makefile already uses key revision `0A` (3.55),
> which PS3HEN requires. Please don't change it to `04`; that's what forced
> PS3xPAD to be re-released as "3.55 signed for HEN".
>
> ```
> scetool -0 SELF -1 TRUE -s FALSE -2 0A -3 1070000052000001 \
>         -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000 \
>         -e ds45pad.prx ds45pad_vsh.sprx
> ```
>
> `make` alone produces the VSH build, which is the only one needed. There's
> also `make game` for an optional in-game rumble module, but rumble is off by
> default so it can be skipped.
>
> The code has never been compiled, so warnings or build errors are entirely
> possible — if anything fails to build, please paste the output and I'll fix
> it. Thanks!

---

## Se pedirem alterações

Se a pessoa relatar erro de compilação, me traga a saída exata do `make`. Erros
de primeira compilação são esperados — o código nunca passou por um compilador,
e os pontos mais prováveis (nomes de constantes do SDK que variam de versão, o
header das macros de syscall) estão mapeados na tabela de erros comuns do
[BUILD.md](BUILD.md#se-der-erro-de-compilação).
