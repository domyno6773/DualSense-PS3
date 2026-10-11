# Instalação em CFW

O caminho é o mesmo do HEN, e mais simples — **não há HEN para reativar a cada
boot**. Este documento cobre só as diferenças; o passo a passo detalhado está em
[INSTALAR_HEN.md](INSTALAR_HEN.md).

---

## O que é igual

- Os arquivos vão para `/dev_hdd0/plugins/ds45pad/`
- O webMAN MOD entra no `boot_plugins.txt`
- O `ds45pad_game.sprx` é carregado pelo PS3MAPI com o jogo rodando
- A assinatura do meu `.sprx` (revisão de chave `0A`) serve nos dois

## O que muda

### 1. Nada de reativar

Em CFW o `boot_plugins.txt` é lido no boot e pronto. Sem exploit de navegador,
sem app de ativação.

### 2. Variante do webMAN conforme o CFW

O webMAN MOD tem builds diferentes. Escolha a que combina com o seu:

| Seu CFW | Variante |
|---|---|
| Rebug (Cobra) | `webftp_server_rebug_cobra_ps3mapi.sprx` |
| Evilnat / outro com Cobra | `webftp_server_cobra_ps3mapi.sprx` |
| CFW sem Cobra | `webftp_server_ps3mapi.sprx` |
| PS3HEN | `webftp_server_ps3mapi.sprx` |

O importante é ser uma variante **`ps3mapi`** — é ela que traz a função que
carrega plugin em processo de jogo. Os nomes variam entre releases; confira a
lista de arquivos da release que você baixar.

### 3. MAMBA usa outro arquivo

Se o seu setup roda MAMBA em vez de Cobra, a lista de plugins se chama
`mamba_plugins.txt`, não `boot_plugins.txt`. Mesmo formato, um caminho por
linha.

### 4. A rede de segurança continua valendo

Em CFW um plugin ruim no `boot_plugins.txt` também pode travar o boot. O truque
do pendrive funciona igual: `boot_plugins.txt` **vazio** na raiz de um pendrive
FAT32 tem prioridade sobre o do HD.

Plano B: Menu de Recuperação → Reconstruir Banco de Dados, que apaga o
`/dev_hdd0/boot_plugins.txt`.

CFW dá mais margem de erro que HEN em assinatura de módulo, mas não é imune —
monte o pendrive antes de testar.

---

## Configuração recomendada

`/dev_hdd0/boot_plugins.txt`:

```
/dev_hdd0/plugins/webftp_server_cobra_ps3mapi.sprx
/dev_hdd0/plugins/ds45pad/ds45pad_vsh.sprx
```

A segunda linha é **opcional** — serve só para o botão PS funcionar no XMB. Se
não fizer diferença para você, deixe só o webMAN: menos peças móveis.

E no `ds45pad.txt`, `INSERT_INTO_GAME = 0`. Ligado, ele impede apps de carregar,
e não é mais necessário — a compatibilidade vem do hook em jogo.

---

## Uma vantagem que o CFW tem

Com **kernel DEX** e EBOOT convertido para debug, existe a possibilidade de
carregar o `ds45pad_game.sprx` automaticamente no boot do jogo, em vez de pelo
PS3MAPI a cada vez. É o mecanismo que o PS3xPAD documenta como
`AUTO_GAME_LOAD`.

Eu não implementei isso e não testei — está registrado aqui porque é a
diferença prática mais relevante entre CFW e HEN para este projeto, caso você
queira eliminar o passo manual do PS3MAPI.
