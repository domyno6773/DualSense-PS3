# Modo diagnóstico

Existem duas coisas que nenhuma documentação responde e só o console responde:
onde o botão PS aparece (se aparecer) no caminho Bluetooth, e como os eixos do
sixaxis do DS4/DualSense correspondem aos do DualShock 3.

O modo diagnóstico responde as duas.

## Ligando

Em `ds45pad.txt`:

```
DIAG = 1
```

Reinicie o plugin. Tudo vai para `/dev_hdd0/tmp/ds45pad_diag.txt` — puxe pelo
FTP do webMAN. Toda vez que um código do relatório muda, uma linha é gravada.

Ele amostra `cellPadGetData()` **e** `cellPadGetRawData()`, porque a variante
crua às vezes carrega códigos que a normal descarta — e é exatamente ali que o
botão PS pode estar escondido.

Desligue depois: o arquivo cresce rápido.

---

## Achando o botão PS no Bluetooth

1. Pareie o controle e mande para a porta 7 (`Reatribuir controles`).
2. `DIAG = 1`, reinicie o plugin.
3. Aperte **só o botão PS**, uma vez, e espere dois segundos.
4. Abra o arquivo.

A lista de portas sai primeiro e diz qual é qual:

```
--- portas ---
max=7 conectados=2
porta 0  status=0x0001  setting=0x0006  cap=0x001F  type=5  [CONFORME]
porta 6  status=0x0001  setting=0x0006  cap=0x0000  type=0  [GENERICO]
```

`[GENERICO]` é o seu DS4/DualSense. `[CONFORME]` é o controle virtual criado
pelo plugin — ignore.

Agora procure o que mudou quando você apertou PS:

```
data porta 6:  [2]=0x0100
```

Isso é um achado: código **2**, bit **0x0100**. Coloque no `ds45pad.txt`:

```
BT_PS_CODE = 2
BT_PS_MASK = 0x0100
BT_PS_COMBO = NONE
```

Botão PS real no Bluetooth, sem combinação.

**Se nada aparecer** ao apertar PS, o firmware realmente descarta o botão e não
há o que fazer pelo lado do software — fique com `BT_PS_COMBO`. Esse é o
resultado mais provável, mas vale os trinta segundos de teste.

O mesmo procedimento funciona para o clique do touchpad e para as paletas do
DualSense Edge.

---

## Calibrando o sixaxis

Só faz sentido por USB (no Bluetooth não há movimento nenhum).

O plugin já converte a magnitude corretamente — o DS4/DualSense reportam
8192 unidades por g, o DualShock 3 usa 113 em torno do centro 512. O que pode
estar errado é a **direção** de cada eixo, que não está documentada.

Teste rápido, sem ler arquivo nenhum: pegue um jogo com sixaxis evidente
(qualquer um com minigame de inclinação; o menu do *flOw* ou *Super Stardust HD*
servem bem) e observe:

| Sintoma | Correção em `ds45pad.txt` |
|---|---|
| Inclinar para a esquerda move para a direita | `INVERT_X = 1` |
| Inclinar para frente move para trás | `INVERT_Y = 1` |
| Girar o controle responde ao contrário | `INVERT_G = 1` |
| Inclinar de lado responde como se fosse para frente | `SWAP_XY = 1` |
| Reage de menos | Suba `ACCEL_SCALE` (ex.: `150`) |
| Reage demais / treme | Desça `ACCEL_SCALE` (ex.: `70`) |

`GYRO_SCALE` é o mais provável de precisar de ajuste, porque o campo `SENSOR_G`
do DS3 não tem equivalência física limpa com o giroscópio dos controles novos —
o valor atual é uma estimativa deliberada.

---

## Descobrindo o VID/PID de um controle não oficial

Ligue `DIAG = 1`, plugue o controle e procure no arquivo. Se o plugin não o
reconheceu, ele não aparece — nesse caso use um PC (`lsusb` no Linux, o
Gerenciador de Dispositivos no Windows) e adicione a linha em
`ds45pad_devices.txt`:

```
0x054c, 0x0ce6, DUALSENSE
```

Escolha `DS4` ou `DUALSENSE` conforme o formato de relatório que o clone imita —
na dúvida, tente os dois; o errado simplesmente não produz entrada nenhuma.

---

## Nada funciona

Em ordem:

1. **Nenhum aviso no XMB ao iniciar** → o plugin não carregou. Confira o
   caminho no `boot_plugins.txt` e se o SPRX foi assinado.
2. **Aviso aparece, controle não faz nada por USB** → outro plugin de controle
   pode ter pegado o device antes. Descarregue o PS3xPAD se estiver instalado.
3. **Funciona no XMB, não no jogo** → as syscalls 573/574 não estão disponíveis;
   confirme que o Cobra/Mamba está ativo.
4. **Jogo específico recusa o controle** → tente `CAPABILITY_MASK = 0xFFFF`.
5. **Dois controles respondendo ao mesmo tempo (Bluetooth)** → falta reatribuir
   o controle físico para a porta 7.
