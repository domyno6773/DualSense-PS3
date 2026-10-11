# Teste de referência: PS3xPAD 0.8 no HEN

Objetivo deste teste: descobrir se o problema é **a minha implementação** ou **o
seu ambiente**. O PS3xPAD é código de terceiro, comprovadamente rodando em HEN,
e usa o mesmo mecanismo de botão PS que eu uso. Se ele funcionar, o bug é meu e
eu conserto. Se não funcionar nem ele, o mecanismo não faz no seu firmware o que
a documentação diz — e aí é outro problema, que nenhum código meu resolve.

---

## É quase idêntico ao que já fizemos

Mesmo pendrive de segurança, mesmo `boot_plugins.txt`, mesmo FTP, mesma
reativação do HEN. **Só cinco coisas mudam:**

### 1. Tire o meu plugin do ar primeiro

No `/dev_hdd0/boot_plugins.txt`, **apague ou comente a linha do ds45pad**. Dois
plugins de controle disputando o mesmo dispositivo dá resultado sem sentido, e
invalida o teste.

Não precisa apagar os arquivos, só a linha.

### 2. Pasta e arquivos diferentes

```
/dev_hdd0/plugins/ps3xpad/xpad_vsh.sprx
/dev_hdd0/plugins/ps3xpad/xpad_game.sprx
/dev_hdd0/plugins/ps3xpad/xpad_settings.txt
/dev_hdd0/plugins/ps3xpad/xpad_devices.txt
/dev_hdd0/plugins/ps3xpad/xpad_remap.txt
/dev_hdd0/plugins/ps3xpad/font.png
/dev_hdd0/plugins/ps3xpad/background.png
```

Os dois `.png` e o `xpad_game.sprx` não são usados neste teste, mas o plugin
espera encontrá-los. Copie tudo.

### 3. Linha diferente no boot_plugins.txt

```
/dev_hdd0/plugins/ps3xpad/xpad_vsh.sprx
```

### 4. Modo nativo — é o que funciona com DualSense

O PS3xPAD não tem driver de DualSense (o controle é bem mais novo que ele). Mas
o **modo nativo** não precisa: ele lê o pad genérico que o firmware já criou e
reinjeta como pad LDD. Exatamente o mesmo desenho do meu `MODE = BT`.

No `xpad_settings.txt`, já vem configurado assim:

```
XPAD_MODE = 1
```

E então, **obrigatório**: em **Ajustes → Ajustes de acessórios → Reatribuir
controles**, mande o DualSense para a **porta 7**.

Sem esse passo o modo nativo não acha o controle. É o mesmo motivo do meu modo
Bluetooth: libera a porta baixa para o pad virtual.

### 5. Combinações de botão diferentes

O PS3xPAD tem menu próprio no XMB:

| Combinação | Função |
|---|---|
| START + SELECT + QUADRADO | Abre o menu |
| START + SELECT + L3 | Descarrega o plugin |
| START + SELECT + R3 | Move o plugin para o processo do jogo |

O menu é útil: ele mostra o que o plugin está detectando.

---

## O que observar

Duas perguntas, e as duas respostas importam:

**1. O multiMAN abre normalmente com o PS3xPAD carregado?**

- **Abre** → o esqueleto dele é estável no seu console, e o problema de
  estabilidade é do meu código. O caminho é enxertar meus módulos no esqueleto
  dele.
- **Não abre** → o seu ambiente tem problema com plugins de pad em geral, não
  com o meu especificamente. Nada que eu escreva resolve isso, e vale investigar
  o setup do HEN.

**2. O botão PS do DualSense abre o XMB?**

- **Abre** → o mecanismo funciona no seu firmware e o bug é meu. Conserto.
- **Não abre** → `CELL_PAD_CTRL_LDD_PS` não faz no seu firmware o que a
  documentação diz. O botão PS precisa de outra abordagem, e eu paro de insistir
  nessa.

Um aviso honesto: o PS3xPAD é de 2015 e nunca foi testado em firmware 4.9x nem
com DualSense. É possível que ele simplesmente não carregue. **Isso também é
resultado útil** — significa que a incompatibilidade é de época, não do meu
código.

---

## Depois do teste

Para voltar ao meu plugin: troque a linha do `boot_plugins.txt` de volta e
reinicie. Os dois nunca devem estar ativos ao mesmo tempo.
