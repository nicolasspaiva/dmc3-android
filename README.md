# Devil May Cry 3 — port nativo para Android

Port nativo do **Devil May Cry 3: Special Edition** (versão da *Devil May Cry HD Collection* do Xbox 360) para celulares
Android. O código do jogo foi recompilado estaticamente de PowerPC para ARM64 com o
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp). Gráficos, áudio, vídeos, saves e controles são
reimplementados nativamente (OpenGL ES 3, SDL3, FFmpeg). Não há emulação.

> **Você precisa ter o jogo.** O APK **não traz nenhum arquivo do jogo** (modelos, fases, sons, vídeos). Na primeira
> execução, você copia os arquivos da sua própria cópia da *Devil May Cry HD Collection* do Xbox 360.

Testado num Samsung Galaxy S25 FE (60 fps). Requer Android 11 ou mais novo, ARM64 e OpenGL ES 3.0.

---

## Downloads: completo ou leve

Na página de [Releases](../../releases) há duas versões. Elas só diferem nas cutscenes em vídeo:

| | **DMC3-completo.apk** | **DMC3-leve.apk** |
|---|---|---|
| Cutscenes em vídeo (abertura, finais, cenas pré-renderizadas) | **tocam** | são **puladas** |
| Tamanho do APK | ~32 MB | ~28 MB |
| Arquivos do jogo para copiar | `DMC3.xex` + `Media/` **com** `Media/wmv/` | `DMC3.xex` + `Media/` **sem** `Media/wmv/` |
| Espaço total no celular | **~2,2 GB** | **~1,1 GB** |

As cutscenes feitas com o motor do jogo (diálogos dentro das fases) aparecem nas duas versões. Só as cenas
pré-renderizadas em vídeo mudam. Dá para trocar de versão a qualquer momento, instalando uma por cima da outra: os
saves e as configurações continuam.

---

## Instalação

1. **Baixe e instale o APK.** Na primeira vez, o Android pede para permitir "instalar apps desconhecidos" no navegador
   ou no gerenciador de arquivos.
2. **Abra o app e permita o "acesso a todos os arquivos".** O app abre a tela do sistema. Ative a opção e volte ao
   jogo. Essa permissão serve **só** para o app usar a pasta `DMC3` da memória interna (veja [Saves](#saves)).
3. **Copie os arquivos do jogo** para `Memória interna/DMC3/`, pelo PC (cabo USB) ou pelo gerenciador de arquivos do
   celular:

   ```
   DMC3/
   ├── DMC3.xex            ← executável do DMC3 (do disco da HD Collection)
   └── Media/
       ├── DMC3-0.zip
       ├── DMC3-1.zip
       ├── DMC3-2.zip
       └── wmv/            ← só na versão completa: os vídeos das cutscenes
   ```

   Enquanto faltar algum arquivo, o app mostra **o que falta** e um botão **Verificar de novo**.
4. **Pronto.** Na primeira execução completa, o app converte o `DMC3.xex` em `DMC3.pe` (alguns segundos, uma vez só) e
   confere se é a versão certa do jogo. Daí em diante, o jogo abre direto.

---

## Cutscenes

O DMC3 tem dois tipos de cena:

- **Cenas com o motor do jogo**: personagens e cenários 3D em tempo real. Funcionam sempre, nas duas versões.
- **Cenas pré-renderizadas em vídeo**: a abertura ("Capcom presents"), algumas cenas entre missões e os extras. São
  arquivos `.wmv` (vídeo VC-1 e áudio WMA Pro) na pasta `Media/wmv/` do disco, 29 arquivos com cerca de 1,1 GB no
  total.

**Como os vídeos são tocados (versão completa):**

- Quando o jogo pede um vídeo, o port abre o arquivo correspondente em `DMC3/Media/wmv/` com um player nativo (FFmpeg,
  só com os decodificadores VC-1 e WMA Pro, licença LGPL).
- O vídeo é desenhado por cima do jogo, com a proporção original (faixas pretas quando precisar). O som sai junto com
  a imagem.
- **Pular** é decidido pelo próprio jogo, como no original: quando ele manda parar, o player para na hora.
- Se a pasta `wmv/` não estiver lá, ou faltar algum arquivo, aquele vídeo é pulado e o jogo continua normalmente.

**Na versão leve**, o player de vídeo não está no APK. Todo vídeo é dado como terminado na hora, e a pasta `wmv/` não é
necessária. A história não muda: o jogo segue para a cena ou fase seguinte.

**Para adicionar as cutscenes depois:** instale a versão completa por cima da leve e copie a pasta `wmv/` para
`DMC3/Media/`.

---

## Saves

O jogo usa o sistema de saves do Xbox 360. O port o reimplementa gravando em arquivos comuns:

```
DMC3/
└── saves/
    └── SaveGame/
        ├── save.bin      ← o save do jogo (progresso, orbes, estilos, itens...)
        └── .display      ← nome mostrado na tela de carregar
```

- **Onde fica:** `Memória interna/DMC3/saves/`, numa pasta comum da memória, **fora** da pasta interna do app. Por
  isso o app pede o "acesso a todos os arquivos".
- **Desinstalar não apaga.** O Android apaga a pasta interna de um app quando ele é desinstalado, mas a pasta `DMC3`
  continua com os saves e os arquivos do jogo. Reinstalando, tudo volta como estava.
- **Atualizar não apaga.** Instalar uma versão nova por cima (ou trocar entre completo e leve) mantém saves e
  configurações.
- **Backup:** copie a pasta `DMC3/saves/` para o PC ou para a nuvem. Para restaurar, copie de volta com o jogo fechado.
- **Trocar de celular:** copie a pasta `DMC3` inteira (jogo + saves) para o celular novo e instale o APK.
- **Quando grava:** o jogo salva nos pontos de sempre (tela de save entre missões e menus). A mensagem *"Please do not
  turn off the Xbox 360 console..."* é a do jogo original. Não feche o app enquanto o ícone de gravação aparece.
- **Compatibilidade:** o `save.bin` é o mesmo formato do jogo. Um save criado pelo port no PC funciona no celular e
  vice-versa (basta copiar a pasta `SaveGame`).

As **configurações do port** (modo de tela, layout dos botões, controles visíveis, padrão do controle) ficam dentro do
app. Elas voltam ao padrão se o app for desinstalado. Os saves não são afetados.

**Vindo de uma versão de teste antiga?** Elas guardavam tudo em `Android/data/org.dmc3port/files`. Na primeira
abertura desta versão, o app move sozinho os arquivos e os saves de lá para a pasta `DMC3`.

---

## Controles

- **Na tela:** analógico, A/B/X/Y, LB/RB/LT/RT, direcional (troca de estilo) e START/BACK. O botão redondo no alto
  esconde ou mostra os controles.
- **Controle Bluetooth** (Xbox, PlayStation, Switch Pro e outros): os controles da tela somem sozinhos quando ele é
  usado. Os botões seguem o **padrão Xbox**: o de baixo pula. No menu, dá para trocar para as **letras da Nintendo**.
- **Troca de estilo em tempo real** pelo direcional: ↑ Trickster, → Swordmaster, ← Gunslinger, ↓ Royalguard. O nome do
  estilo aparece acima da barra de vida.

### Menu do port (engrenagem embaixo, no meio)

- **Tela:** preencher (estica), 16:9 (faixas pretas), zoom ou **widescreen** (campo de visão aberto até a largura do
  celular, sem esticar os personagens).
- **Controles na tela:** visíveis ou ocultos.
- **Botões do controle:** posição Xbox ou letras.
- **Pular quadros:** automático (padrão) ou desligado. Num celular que não aguenta 60 quadros/s numa cena pesada, o
  app deixa de desenhar alguns quadros, no máximo um a cada dois, para o jogo não ficar mais lento que o relógio. Assim
  as falas e a música das cutscenes em tempo real continuam sincronizadas com a imagem.
- **Editar layout dos botões:** arraste cada botão. Toque num botão e use − / + para mudar o tamanho, depois **Pronto**.
- **Restaurar layout padrão.**

---

## Problemas comuns

| Sintoma | O que fazer |
|---|---|
| "Arquivos do jogo não encontrados" | Confira se a pasta é `Memória interna/DMC3` (não `Download/DMC3`) e se os nomes estão iguais aos da lista. |
| "O DMC3.xex é de outra versão do jogo" | O port só funciona com o `DMC3.xex` da *HD Collection* do Xbox 360. Versões de PS2, PC, PS3/PS4 ou Switch não servem. |
| As cutscenes não aparecem | Na versão leve isso é esperado. Na completa, confira a pasta `DMC3/Media/wmv/`. |
| Áudio fora de sincronia nas cutscenes em tempo real | Deixe **Pular quadros** em automático no menu da engrenagem. |
| O app fecha ao abrir | Envie o arquivo `DMC3/dmc3_log.txt` ao abrir uma issue. |

---

## Compilando

Requer Windows, Python 3, CMake, Ninja, Visual Studio Build Tools + LLVM (clang-cl), Android SDK + NDK 28.2 e JDK 21.
O código recompilado é gerado a partir do **seu** `DMC3.xex` e não fica no repositório.

1. `scripts/xex_extract.py x360/DMC3.xex x360/DMC3.pe`: gera o PE.
2. `tools/build_xenonrecomp.bat` e o XenonRecomp com `x360/DMC3_recomp.toml`: geram `recomp/ppc/`.
3. `tools/build_ffmpeg_android.sh`: compila o FFmpeg mínimo para o Android (só para a versão completa).
4. Em `android/`, rode `gradlew assembleCompletoRelease -Pabis=arm64-v8a` (ou `assembleLeveRelease`).

Detalhes técnicos da recompilação estão em [RELATORIO_REA_DMC3.md](RELATORIO_REA_DMC3.md).

---

## Aviso legal

Projeto de fãs, sem fins lucrativos e sem ligação com a Capcom. *Devil May Cry* é marca da Capcom. Nenhum arquivo do
jogo é distribuído aqui: para jogar, é necessário possuir a *Devil May Cry HD Collection* do Xbox 360.
