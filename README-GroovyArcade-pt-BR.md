# MiSTerFiles para GroovyArcade (x86_64, CRT 15 kHz)

Gerenciador de arquivos com controle por joystick/controles de arcade, igual ao
MiSTerFiles do MiSTer (mesma interface, mesmo ajuste "Tela / Overscan"), agora
para o GroovyArcade (Linux x86_64 baseado em Arch).

Baseado no 351Files de Tardigrade-nx (https://github.com/Tardigrade-nx/351Files),
licenca GPLv3 (ver `licenses/`).

## Funciona 100% offline

O gabinete nao precisa de internet nem de `pacman`:

- SDL2, SDL2_image, SDL2_ttf, FreeType, libstdc++ e libgcc estao **dentro do binario**
  (link estatico). As fontes (Noto Sans) e os icones vao na pasta `res/`.
- O binario so usa a glibc do sistema, e pede uma versao antiga: **glibc 2.28 ou mais
  nova** (qualquer GroovyArcade/Arch desde 2018-2019 tem).
- Video: o SDL usa o que ja existe no GroovyArcade. Com X11 (frontend rodando no X)
  usa a propria libX11 do sistema, sem OpenGL. Sem X, usa KMS/DRM (libdrm/libgbm/EGL
  do Mesa, que o GroovyArcade ja tem para o frontend e o GroovyMAME).
- Teclado/encoder de arcade e joysticks: via SDL (udev/evdev do sistema).

## Conteudo do pacote

```
MiSTerFiles-groovyarcade.tar.gz
└── MiSTerFiles/
    ├── MiSTerFiles              programa (x86_64)
    ├── MiSTerFiles.sh           launcher (use este para abrir)
    ├── MiSTerFiles.ini.example  configuracao de exemplo (vira MiSTerFiles.ini)
    ├── instalar-attract.sh      adiciona o app ao Attract-Mode (opcional)
    ├── attract/                 arquivos de emulador/romlist do Attract-Mode
    ├── res/                     icones, fontes, gamecontrollerdb.txt
    ├── licenses/                licencas (SDL2, FreeType, Noto, 351Files)
    ├── MiSTerFiles.sha256       checksum do binario
    └── LEIA-ME.md               este arquivo
```

## Instalacao pelo pendrive

O pendrive pode ser FAT32 (o mais compativel) ou exFAT. Use o **.tar.gz**: ele guarda
as permissoes de execucao, que o FAT32/exFAT perdem.

### 1. No PC

Copie `MiSTerFiles-groovyarcade.tar.gz` para a raiz do pendrive. Ejete o pendrive com
seguranca.

### 2. No gabinete: abrir um terminal

Ligue o pendrive no gabinete e abra um terminal de texto, de um destes jeitos:

- saia do frontend para o menu do GroovyArcade (gasetup) e escolha a opcao de shell/terminal; ou
- aperte `Ctrl+Alt+F2` (ou F3...) e faca login (usuario `arcade` ou `root`).
  Para voltar a tela do frontend depois: `Ctrl+Alt+F7` (ou F1).

### 3. Montar o pendrive

```bash
lsblk -f                         # ache o pendrive, ex.: sdb1 (vfat ou exfat)
sudo mkdir -p /mnt/usb
sudo mount /dev/sdb1 /mnt/usb    # troque sdb1 pelo nome que apareceu
ls /mnt/usb                      # deve aparecer MiSTerFiles-groovyarcade.tar.gz
```

(Se estiver logado como `root`, rode os comandos sem `sudo`.)

### 4. Copiar / extrair para a home

```bash
tar -xzf /mnt/usb/MiSTerFiles-groovyarcade.tar.gz -C /home/arcade/
sudo chown -R arcade: /home/arcade/MiSTerFiles       # garante que o usuario arcade e o dono
chmod +x /home/arcade/MiSTerFiles/MiSTerFiles /home/arcade/MiSTerFiles/*.sh
```

Se em vez do .tar.gz voce copiou a pasta `MiSTerFiles` ja extraida para o pendrive:

```bash
cp -r /mnt/usb/MiSTerFiles /home/arcade/
sudo chown -R arcade: /home/arcade/MiSTerFiles
chmod +x /home/arcade/MiSTerFiles/MiSTerFiles /home/arcade/MiSTerFiles/*.sh   # obrigatorio vindo de FAT32/exFAT
```

Conferir se o binario chegou inteiro (opcional):

```bash
cd /home/arcade/MiSTerFiles && sha256sum -c MiSTerFiles.sha256     # deve dizer: OK
```

### 5. Desmontar o pendrive

```bash
cd /
sync
sudo umount /mnt/usb
```

### 6. Rodar

Teste rapido pelo terminal (como usuario `arcade`):

```bash
# frontend rodando no X (tela do gabinete), comando dado de outro terminal (Ctrl+Alt+F2):
DISPLAY=:0 /home/arcade/MiSTerFiles/MiSTerFiles.sh
# volte para a tela do frontend (Ctrl+Alt+F7) para ver o app

# sem X (frontend fechado, GroovyArcade em modo KMS), direto no console:
/home/arcade/MiSTerFiles/MiSTerFiles.sh
```

Para sair do app: Y -> `Quit` (ou configure `KEY_QUIT`, ver abaixo).
O log fica em `/tmp/MiSTerFiles.log` (driver de video, modo de tela, controles).

### 7. Colocar no Attract-Mode / Attract-Mode Plus (opcional)

Como usuario `arcade`:

```bash
bash /home/arcade/MiSTerFiles/instalar-attract.sh
```

Isso cria `~/.attract/emulators/MiSTerFiles.cfg` e `~/.attract/romlists/MiSTerFiles.txt`
(um "jogo" chamado MiSTerFiles). Depois, no Attract-Mode: `Tab` (configuracao) ->
**Displays** -> **Add** -> nome `Utilitarios` -> em **Collection/Romlist** escolha
`MiSTerFiles` -> volte. Ao escolher o item, o Attract-Mode abre o `MiSTerFiles.sh`;
ao sair do app, volta para o frontend.

Manualmente (mesmo efeito): copie `attract/MiSTerFiles.cfg` para
`~/.attract/emulators/` e `attract/MiSTerFiles.txt` para `~/.attract/romlists/`
(ajuste o caminho dentro do .cfg se nao instalou em `/home/arcade/MiSTerFiles`).

### Atualizar

Repita os passos 3 a 5. O seu `MiSTerFiles.ini` (overscan, teclas) nao vem no pacote,
entao e mantido.

### Remover

```bash
rm -rf /home/arcade/MiSTerFiles ~/.attract/emulators/MiSTerFiles.cfg ~/.attract/romlists/MiSTerFiles.txt
```

## Controles

| Acao                         | Teclado / encoder (padrao)            | Gamepad      |
|------------------------------|----------------------------------------|--------------|
| Mover                        | setas (ou keypad 8/2/4/6)              | D-pad / analogico |
| Abrir / confirmar (A)        | Enter, **Ctrl esquerdo** (botao 1)     | A            |
| Voltar / cancelar (B)        | Esc, Backspace, **Alt esquerdo** (botao 2) | B        |
| Selecionar arquivo (X)       | Espaco (botao 3), Insert               | X / Back     |
| Menu de contexto (Y)         | Tab, **Shift esquerdo** (botao 4), F1  | Y / Start    |
| Pagina acima / abaixo (L/R)  | PgUp/Home, PgDn/End                    | LB / RB (e gatilhos) |

Os botoes em negrito sao o padrao do MAME para o jogador 1 (encoders tipo I-PAC /
Xin-Mo em modo teclado). Para mudar, edite o `MiSTerFiles.ini` (fica em
`/home/arcade/MiSTerFiles/`):

```ini
KEY_A="Return,Left Ctrl"
KEY_B="Escape,Left Alt"
KEY_Y="Tab,Left Shift,1"      # ex.: Start do P1 tambem abre o menu
KEY_QUIT="5"                  # ex.: Coin do P1 sai do app (vazio = desligado)
```

Nomes de tecla: os do SDL (`Up`, `Return`, `Escape`, `Space`, `Tab`, `Left Ctrl`,
`Left Alt`, `Left Shift`, `Right Ctrl`, `PageUp`, `PageDown`, `1`, `5`, `Z`, `F10`...),
varios separados por virgula. Para descobrir o nome de uma tecla do seu painel:
`MF_DEBUG_INPUT=1 ./MiSTerFiles.sh` e veja `/tmp/MiSTerFiles.log` (linhas `Groovy: key ...`).

Joysticks/encoders em **modo joystick** que o SDL nao reconhece como gamepad usam os
numeros de botao `JOY_A`, `JOY_B`, `JOY_X`, `JOY_Y`, `JOY_L`, `JOY_R`, `JOY_QUIT`
(0 = primeiro botao). `JOYSTICK=0` desliga joysticks.

## Tela / Overscan (bordas cortadas na TV)

Igual ao MiSTer: aperte **Y** -> **Tela / Overscan**. Esquerda/Direita ajustam a margem
(0 a 15 % da altura da tela, a mesma nos 4 lados), A salva, B cancela. As bordas
amarelas devem aparecer inteiras na TV. O valor e gravado em `MiSTerFiles.ini` como
`OVERSCAN=<porcentagem>`; tudo fora da area segura fica preto.

A margem lateral e corrigida pela proporcao da TV, para ter o mesmo tamanho visual que
a de cima/baixo em qualquer resolucao (640x480, 640x240, 320x240...). Padrao 4:3;
numa TV widescreen use `ASPECT=16:9`.

## Video e resolucao

- `VIDEO_MODE=desktop` (padrao): usa o modo que ja esta na tela (o que o switchres/
  frontend configurou, ex. 640x480 entrelacado). E o mais seguro no CRT.
- `VIDEO_MODE=640x480` / `320x240`...: pede esse modo (precisa existir na lista de
  modos do GroovyArcade).
- `RENDER_SIZE=640x240`: desenha a interface em 640x240 e estica para a tela (ex. letras
  maiores numa tela 640x480i).
- Abaixo de 400 linhas o app usa automaticamente o layout compacto (`LOWRES=auto`).
- `VIDEO_DRIVER=auto` escolhe X11 se houver `$DISPLAY`, senao KMS/DRM. Pode forcar `x11`
  ou `kmsdrm`.

## Se nao abrir

Veja `/tmp/MiSTerFiles.log`. Ele mostra a glibc, o usuario/grupos, `DISPLAY`, o driver
de video, o modo de tela e as teclas configuradas.

- `x11 not available` / `Couldn't open display`: o X nao esta acessivel; rode com
  `DISPLAY=:0` (o GroovyArcade faz `xhost +` no `.xinitrc`).
- `kmsdrm not available`: sem X e sem acesso ao `/dev/dri/card0` (o frontend ainda
  esta segurando a tela, ou o usuario nao esta no grupo `video`). Feche o frontend ou
  rode pelo Attract-Mode.
- Teclas nao respondem no modo KMS: o usuario precisa ler `/dev/input/event*` (grupo
  `input`), como o GroovyMAME.
- `GLIBC_2.xx not found`: o sistema e mais antigo que 2018 (nao deve acontecer).
