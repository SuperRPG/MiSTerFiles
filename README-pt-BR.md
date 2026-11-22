# MiSTerFiles

Gerenciador de arquivos que roda **no próprio MiSTer**, controlado pelo controle (joystick)
ou teclado. É um fork do [351Files](https://github.com/Tardigrade-nx/351Files) de Tardigrade-nx (veja Créditos).
Copiar, recortar, colar, apagar, renomear, criar pasta, ver imagens e textos, editar texto.

## Instalação

1. Copie o conteúdo da pasta `Scripts` deste pacote para `/media/fat/Scripts` do MiSTer
   (pelo FTP/SMB ou direto no cartão SD). Deve ficar assim:

   ```
   /media/fat/Scripts/MiSTerFiles.sh
   /media/fat/Scripts/.MiSTerFiles/MiSTerFiles
   /media/fat/Scripts/.MiSTerFiles/res/...
   ```

   A pasta `.MiSTerFiles` começa com ponto, então não aparece no menu Scripts (é de propósito).
2. No MiSTer: **F12 → Scripts → MiSTerFiles**.
3. Para sair: botão **Y** (ou **Tab**) → **Quit**. O MiSTer mostra "Press any key to continue";
   aperte qualquer tecla/botão para voltar ao menu.

### TV de tubo (CRT) / vídeo componente / 15 kHz

O app desenha no *framebuffer* do Linux do MiSTer, e o MiSTer só liga esse framebuffer para
scripts quando `fb_terminal` é diferente de 0 no `MiSTer.ini`.

- **Se o seu `MiSTer.ini` tem `fb_terminal=0`**, não precisa editar nada: na primeira vez o
  script avisa na janela do OSD, ativa `fb_terminal` *temporariamente* (monta uma cópia do
  ini em `/tmp`; o arquivo no cartão não é alterado) e recarrega o menu. Abra
  **Scripts → MiSTerFiles** de novo e ele abre. Ao sair, o menu recarrega com o seu ini original.
- Para não passar por esse recarregamento toda vez, coloque na seção `[MiSTer]` do `MiSTer.ini`:
  - TV/CRT pela saída analógica sem scaler (`vga_scaler=0`, `direct_video=0`): `fb_terminal=2`
    (framebuffer 640x240 a 15 kHz, aparece na TV)
  - HDMI: `fb_terminal=1` (o padrão)

  Com `fb_terminal=1` e `vga_scaler=0`, a imagem do framebuffer **não** sai na saída analógica
  (só no HDMI) — por isso, em CRT, use `2`.
- Em CRT 15 kHz o app usa automaticamente o layout pequeno (240 linhas). Pode forçar com
  `LAYOUT=small` ou `LAYOUT=large` no `MiSTerFiles.ini`.

O programa é estático (SDL2, SDL2_image, SDL2_ttf e FreeType já vão dentro do binário).
O log de cada execução fica em `/tmp/MiSTerFiles.log` (o anterior em `/tmp/MiSTerFiles.log.old`).

## Controles

O MiSTer já converte o seu controle mapeado em teclas quando um script roda na tela,
então os botões seguem o mapeamento que você fez no menu do MiSTer:

| Ação | Controle | Teclado |
|---|---|---|
| Mover | D-pad | Setas |
| Abrir pasta / arquivo | A | Enter |
| Voltar / pasta acima | B | Esc ou Backspace |
| Marcar arquivo (seleção) | X | Espaço ou Insert |
| Menu (copiar, colar, apagar, renomear, Tela / Overscan, sair...) | Y | Tab, X ou F1 |
| Página acima / abaixo | L / R | PgUp / PgDn (Home / End) |

## Tela / Overscan (bordas cortadas na TV)

TV de tubo costuma cortar as bordas da imagem. Para ajustar:

1. Aperte **Y** (ou **Tab**) e escolha **Tela / Overscan** (fica logo acima de *Quit*).
2. Aparece uma moldura amarela marcando a área segura. Use **Esquerda/Direita**
   (ou Cima/Baixo) para aumentar ou diminuir a margem até a moldura aparecer inteira na TV.
3. **A / Enter** salva, **B / Esc** cancela.

Todo o programa (lista, menus, diálogos, teclado na tela) passa a ser desenhado dentro da
área segura; fora dela fica preto. O valor é gravado em
`/media/fat/Scripts/.MiSTerFiles/MiSTerFiles.ini` como `OVERSCAN=<n>` (o arquivo é criado a
partir do `.example` se não existir; os outros ajustes e comentários são mantidos).

`OVERSCAN` é uma porcentagem da altura da tela (0 a 15), aplicada igual nos 4 lados. Como no
modo 640x240 da TV cada pixel é duas vezes mais largo do que alto, a margem lateral em pixels é
calculada para ficar com o mesmo tamanho visual da margem de cima/baixo
(ex.: 10% em 640x240 = 24 linhas em cima/baixo e 48 pixels nas laterais). Por ser porcentagem,
o mesmo valor funciona em 640x240 e 640x480. Padrão: 0.

## Ajustes (opcional)

Copie `.MiSTerFiles/MiSTerFiles.ini.example` para `.MiSTerFiles/MiSTerFiles.ini` e edite:

- `OVERSCAN`: margem de segurança em % (veja acima; o app grava sozinho).
- `FB_WIDTH` / `FB_HEIGHT`: vazio = automático (CRT: 640x240 do MiSTer; HDMI: 640x480).
- `LAYOUT`: `auto`, `small` (240 linhas) ou `large` (480 linhas).
- `AUTO_FB=0`: não ativa o framebuffer temporariamente; só mostra no OSD o que mudar no ini.
- `START_DIR`: pasta inicial (padrão `/media/fat`).
- `MF_JOYSTICK=1`: lê o controle direto pelo SDL (use só se o controle não responder;
  pode causar movimento em dobro junto com o mapeamento do MiSTer).
- `MF_SWAP_RB=1`: se as cores aparecerem trocadas (barra de seleção laranja em vez de azul).

Se algo der errado, o log fica em `/tmp/MiSTerFiles.log`.

## Cuidado

Ele mexe em qualquer arquivo do MiSTer (inclusive `/media/fat/linux` e o `MiSTer.ini`).
Apagar é definitivo.

## Compilar (para desenvolvedores)

Num Linux Debian/Ubuntu x86_64:

```
sudo apt install g++-arm-linux-gnueabihf cmake make wget zip
./build_MiSTer.sh
```

Gera `dist/` e `MiSTerFiles.zip`. As dependências estáticas ficam em `deps/prefix`.
O código específico do MiSTer está em `src/mister.cpp` / `src/mister.h`
(saída de vídeo em `/dev/fb0` e entrada via evdev / SDL GameController) e no bloco
`DEVICE_MISTER` de `src/def.h`.

## Créditos e licenças

MiSTerFiles é um fork do **351Files** de Tardigrade-nx (https://github.com/Tardigrade-nx/351Files),
que por sua vez é baseado no DinguxCommander. Distribuído sob a mesma licença do projeto original (GPLv3).
O README original fica em `README-upstream.md` e em `.MiSTerFiles/licenses/CREDITS-upstream-README.md`.

Demais componentes: SDL2 / SDL2_image / SDL2_ttf (zlib), FreeType (FTL), fontes Noto (OFL).


## GroovyArcade (PC x86_64)

Existe tambem uma versao para o GroovyArcade (gabinete com PC x86_64 e CRT 15 kHz), com a
mesma interface e o mesmo "Tela / Overscan". Ela roda totalmente offline (tudo embutido no
binario) e e instalada por pendrive: veja `README-GroovyArcade-pt-BR.md`
(pacote `MiSTerFiles-groovyarcade.tar.gz`, build com `build_GroovyArcade.sh`).
