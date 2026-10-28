# Relatório de pesquisa — DMC3 Special Edition (PC / Steam)

Data: 2026-10-20 · Alvo: `dmc3.exe` do *Devil May Cry HD Collection* (Steam), versão 1.0.2.0
SHA-256: `425d05bf1ea6531aca7633469b0d6c45af86b7fb05260b8b7eaa9329cf0ac805`
Ferramentas: REA 6.7.0 + Ghidra 12.1.2 e scripts próprios em `scripts/`

Escopo: análise só das partes **não criptografadas** do executável e dos dados. O código das funções (`.text`) está protegido pela Steam e não foi decompilado.

## 1. Situação do executável

| Seção  | Tamanho | Entropia | Observação |
|--------|---------|----------|------------|
| .text  | 3,4 MB  | 8,00     | Código **criptografado** pela proteção da Steam |
| .rdata | 2,0 MB  | 4,07     | Legível: classes, vtables, imports, strings, shaders |
| .data  | 8,1 MB (virtual) | 3,31 | Legível |
| .pdata | 248 KB  | 6,04     | Tabela de funções, legível |
| .bind  | 229 KB  | 7,96     | Loader da proteção Steam (ponto de entrada) |

- **21.138 funções** com início e tamanho conhecidos (tabela `.pdata`), somando 3,24 MB de código. A REA/Ghidra registrou 16.329.
- Decompilar qualquer função retorna `halt_baddata()`.
- O executável referencia `dmc3.pdb` (GUID `1abe1344-1afa-43dd-b044-655a51545121`), que não é distribuído.
- Caminho de build original: `d:\workspace\dmc-hd\dmc3\code\dmc3\source\dmc3nb\`.
- O patch Crimson incluído na pasta do jogo **não é compatível** com este executável: ele espera um `dmc3.exe` de 6.567.320 bytes (este tem 6.790.632).

## 2. Arquitetura recuperada

403 classes com vtable recuperada (endereço e tamanho de cada método virtual). Dados completos em `pc/rea/pe_structure.json`.

### Hierarquias principais
```
CWork
├── CActor (+ IActor, ICollisionHandle)
│   ├── CPlayer (+ IPlayer)
│   │   ├── CPlDante        29 métodos virtuais
│   │   ├── CPlVergil
│   │   └── CPlNewVergil
│   └── CNonPlayer (+ INonPlayer)
│       ├── CNonPlayerDeath → CEm000 … (inimigos simples)
│       └── CEm010 … CEm037  ~123–148 métodos virtuais (inimigos/chefes)
│           └── CEm029Cart, CEm021 (derivam de outro inimigo)
└── CStage → CStageSet* (~40 objetos de cenário)

ICom / IComAction → CCom → CComEm000/006/008   (IA de inimigos, ~270 métodos)
IDraw / IDrawOperate → CDrawOperate → CDraw → CDrawBreak, CDrawShadow …   (~104 métodos)
CCamera → CCameraPlayer, CCameraBoss, CCameraRail …
```

- Inimigos seguem a numeração `emNNN`, igual aos arquivos `emNNN.pac` dos dados.
- O maior método do Dante tem 8.536 bytes (provavelmente o update/estado de combate).

## 3. Superfície de plataforma (o que um port precisaria substituir)

O executável importa **só ~150 funções** de sistema. É uma superfície pequena:

| Sistema | Funções importadas | Substituto no Android |
|---|---|---|
| Gráficos | `D3D11CreateDeviceAndSwapChain`, `CreateDXGIFactory` (resto via interfaces COM) | Vulkan / OpenGL ES; shaders HLSL → SPIR-V |
| Áudio | 32 funções FMOD Core + 3 FMOD Studio; XAudio2 | FMOD (tem SDK Android) |
| Entrada | `XInputGetState/SetState`, `DirectInput8Create` | Android Input / SDL2 |
| Vídeo | Media Foundation (`MFCreateSourceReaderFromURL` …) | MediaCodec / libvpx |
| Janela | ~33 funções USER32 (janela, mensagens, foco) | `ANativeActivity` / SDL2 |
| Arquivos/threads | ~47 funções KERNEL32 | POSIX / NDK |
| Steam | 6 funções (init, conquistas, nuvem) | remover |

O código-fonte HLSL dos shaders está embutido no executável (ex.: `fx_bloom_glare.hlsl`, `fx_dof.hlsl`).

## 4. Dados do jogo (`data/dmc3`)

| Componente | Conteúdo | Tamanho |
|---|---|---|
| `dmc3-0.nbz` | ZIP comum (deflate) com a pasta `GData.afs/`: 4.334 arquivos | 0,96 GB compactado / 2,26 GB extraído |
| `Video/*.webm` | 30 cutscenes/vídeos | 479 MB |
| `afs/sound/*.ogg` | 154 músicas | 227 MB |
| `GData.afs/` (solta) | 167 arquivos que substituem os do `.nbz` (mod instalado) | 105 MB |

Conteúdo do `.nbz` por tipo de `.pac` (cabeçalho `PAC` em 3.513 arquivos, `PNST` em 212):

| Prefixo | Arquivos | Tamanho | Provável conteúdo |
|---|---|---|---|
| `id` | 2.693 | 1.057 MB | salas/áreas |
| `st` | 554 | 673 MB | fases |
| `m` | 69 | 397 MB | missões/cutscenes |
| `em` | 24 | 44 MB | inimigos |
| `pl`, `plwp_*` | 97 + 20 | ~30 MB | personagens e armas |
| `snd_*` | ~250 | ~50 MB | efeitos sonoros |
| `msg_*`, `memoca_*` | 16 | <1 MB | textos em 8 idiomas |

**Estimativa de tamanho para Android:** os dados atuais, como estão, somam cerca de 1,7 GB (o `.nbz` já vem compactado). Com o executável nativo, um pacote ficaria em torno de **1,8 GB**, bem abaixo da meta de 3–5 GB.

## 5. Versão PS2 (ISO SLUS-21361, Special Edition USA)

Sem criptografia: o executável foi **decompilado por completo**.

| Item | Valor |
|---|---|
| Executável | `SLUS_213.61`, ELF MIPS R5900, 5,3 MB, sem símbolos |
| Compilador | GCC 2.96 (SCE 2.8.1) |
| Ferramenta | Ghidra 12.1.2 + extensão *ghidra-emotionengine-reloaded* (a REA no Windows não aceita ELF MIPS) |
| Funções | **12.046**, todas decompiladas para C sem falha (`ps2/export/decompiled.c`, 17,8 MB) |
| Strings | 4.502 |

### Classes
- 365 registros de classe recuperados das estruturas de RTTI do executável; **338 têm o mesmo nome no PC**.
- A hierarquia bate com a do PC em **321 de 338** casos. As 17 diferenças são só as interfaces `I*`, que existem apenas no PC.
- 361 vtables encontradas. **2.103 ponteiros de método** foram nomeados no formato `Classe::vfN`; 1.163 deles caem no início de uma função detectada pelo Ghidra (`ps2/export/functions_named.tsv`).
- `CDraw` (59 métodos) e `CStage` (7) têm vtables do mesmo tamanho no PS2 e no PC, o que confirma o mapeamento.

### Nomes de funções (`ps2/export_named/`)
O C decompilado foi regerado com nomes aplicados no projeto Ghidra:

| Origem do nome | Funções |
|---|---|
| Slots de vtable (`Classe__vfN`) | 1.163 (+726 funções novas criadas a partir desses slots) |
| Mensagens de erro da própria biblioteca (`ADXF_Open`, `sceGsPutDrawEnv`…) | 287 |
| Carregadores, log e wrappers | 22 |
| Manuais, por leitura do código | 17 |
| **Total com nome** | **2.281 de 12.772** |

Fluxo principal identificado:
```
entry → main
  crt_RunStaticConstructors
  sys_InitIopAndModules      reinicia o IOP com cdrom0:\IOPRP280.IMG
  game_Init                  60 fps, cria o objeto do jogo (0x1d0cab0)
  loop:
    frame_WaitVSync → frame_Begin → game_UpdateStateMachine (estados 0/1/2)
    → gfx_SwapDisplayList → frame_End → gfx_QueuePacket
```

### REA e PS2
A REA recusa o executável tanto no Windows quanto no Linux (WSL, com Ghidra 12.1.2 e a extensão do PS2): *"Machine-specific MIPS variants … need a separately supported processor profile."* A análise do PS2 foi feita só com o Ghidra headless e scripts próprios.

### Bibliotecas e plataforma
| Sistema | PS2 | Observação para Android |
|---|---|---|
| Vídeo | CRI Sofdec (`.sfd`, MPEG) | converter para H.264/VP9 |
| Música | CRI ADX (`.adx`) via `ADXT_*`/`ADXF_*` | converter para Ogg/Opus |
| Gráficos | GS/VU do PS2 (sem API de alto nível) | reescrever todo o render |
| E/S | `sceCd*`, `sceSif*`, módulos IOP (`MODULE.BIN`, `IOPRP280.IMG`) | substituir por arquivos comuns |
| Memory card | `libmc` | arquivos de save |

### Dados da ISO (4,39 GB)
| Arquivo | Conteúdo | Tamanho |
|---|---|---|
| `GDATA.AFS` | 3.863 arquivos (`PAC` 3.183, `PNST` 212, `EVT` 22…) | 1.240 MB |
| `MDATA1.AFS` + `MDATA2.AFS` | 32 vídeos `.sfd` | 2.263 MB |
| `SDATA.AFS` | 154 músicas `.adx` | 875 MB |

Os `.pac` usam os mesmos formatos da versão de PC. Com vídeo e áudio recodificados, os dados caberiam em ~1,5 GB.

## 6. Dante, estilos e dano (PS2)

### CPlDante
- Herança: `CPlDante ← CPlayer ← CActor ← CWork`. Tem 4 vtables (herança múltipla); a 4ª (22 slots, `0x602788`) concentra o comportamento próprio do Dante.
- Os tamanhos batem com o PC: slots 8/9/10 do PS2 (796 / 10.736 / 900 bytes) ↔ slots 15/16/17 do PC (752 / 8.536 / 896).
- `CPlDante__vf0` é o **destrutor**; pela ordem de destruição, o objeto contém: `CMotion` (+0x2214), `CDamage` (+0x2a20), `CDamageCalc` (+0x2a64), `CPermitAttack` (+0x2b00), `CKeyBuffer` (+0x3d08, buffer de comandos) e arrays próprios do Dante a partir de +0x4a80.
- `FUN_0025b420` (slot 8) carrega a configuração da missão no jogador: armas, estilo, nível e EXP.

| Campo do jogador | Conteúdo |
|---|---|
| `+0x3c6c` | estilo atual |
| `+0x3c6e` | estado da ação de estilo (0 = livre) |
| `+0x3c74` | nível do estilo |
| `+0x3c7c` | EXP do estilo (float) |

Acesso global: `*(*(0x1d0d70c) + 0xc)` = jogador.

### Estilos
| Índice interno | Estilo (pela ordem do menu) |
|---|---|
| 0 | Swordmaster |
| 1 | Gunslinger |
| 2 | Trickster |
| 3 | Royal Guard |
| 4 | Quicksilver |
| 5 | Doppelganger |

- O menu de estilo (`CUIDStyleSelect`) mostra na ordem `{2,0,1,3,4,5}`, o que fixa a correspondência acima.
- Tabela de EXP em `0x594850`: todos os estilos pedem **30.000** para o nível 2 e **99.999** para o nível 3.
- Save (`*(0x1d0d710)`): níveis por estilo em `+0xc5`, EXP por estilo em `+0xcc`, estilo/nível/EXP atuais em `+0x116`/`+0x117`/`+0x138`.

### Cálculo de dano (`CDamageCalc__vf3`, `0x2f17a0`)
Cada personagem tem seu próprio `CDamageCalc`, que processa o dano **recebido**:

1. **Crítico:** sorteio 0–100 com um Mersenne Twister (MT19937, tabela em `0x1e560b0`); se menor que a chance do golpe (`ataque+0x08`), todos os valores do golpe ×1,1.
2. **Base:** `dano = ataque.HP × A[ataque.idxA]/100 × B[ataque.idxB]/100 × defesa_do_alvo()`; o mesmo vale para os dois medidores secundários (atordoamento e derrubada).
3. **Estado especial do alvo** (função virtual `+0x5c`): jogador recebe ×2; inimigo recebe ×0,3 (medidores ×0,2).
4. **Dificuldade** (global `0x1d0d73c`, gravada pela tela de seleção de missão):

| Dificuldade | Dano no jogador | Dano no inimigo |
|---|---|---|
| 0 Easy | ×0,5 | ×2,0 |
| 1 Normal | ×1 | ×1 |
| 2 Hard | ×1,25 | ×0,9 |
| 3 Very Hard | ×2,0 | ×0,7 |
| 4 Dante Must Die | ×4,0 | ×0,5 |

5. Casos especiais: um par específico de personagens (id 44) usa `HP/15`; um tipo de ataque (`+0x3d == 10`) contra o tipo 3 leva ×0,2.
6. **Aplicação:** subtrai HP e medidores. Ataques "não letais" (`+0x30`) deixam o HP em 1. HP ≤ 0 → reação de morte (vfunc `+0x24`); medidor de atordoamento zerado → atordoado (`+0x28`); derrubada (`+0x2c`); senão, reação normal (`+0x30`).

Os nomes "atordoamento", "derrubada" e "estado especial" são interpretações pelo uso no código.

### Troca de estilo em tempo real (patch)
Implementada como patch `.pnach` para PCSX2/NetherSX2 em `patches/style_switch/` (detalhes no `LEIA-ME.md` de lá):
- Direcional: ↑ Trickster, → Swordmaster, ← Gunslinger, ↓ Royal Guard.
- Gancho em `0x1ec468` (o `jal frame_Begin` do loop principal) → rotina de 190 instruções em `0x000FF000`, que roda depois da leitura do controle e antes da lógica do quadro.
- Só atua em missão (`CSceneGameMain +0x18 == 1`) com o Dante; apaga o direcional para os atalhos de tela de status do jogo não dispararem.
- Na troca, repete o que o jogo faz na Divinity Statue: guarda/carrega nível e EXP no save, restaura as flags de golpes ativas (`+0x29d8`) a partir dos golpes comprados (`+0x29f8`), libera os golpes do nível (`FUN_0025b8c0`), carrega o pacote de animações do estilo (`FUN_00203f20`) e refaz o ícone da HUD (`CDraw::vf9`/`vf13`).
- A cada quadro atualiza a tabela de ponteiros de animação de estilo do Dante (`+0x243c + 4×pacote`, pacotes 13–26), que o jogo só preenche ao criar o personagem; sem isso o Dante fica em T-pose.
- Descobertas usadas: o jogo mantém na memória só os pacotes de animação do estilo atual (pool de 3,3 MB em `0x6c3810+0x2e60`) e, ao sair do menu de status, **recria o Dante** para trocar de estilo.
- Montagem conferida com o Capstone e, para as instruções `lq`/`sq` do PS2, contra o próprio código do jogo. **Versão 5 testada no PCSX2 2.2 em 2026-10-21: troca, animações, HUD e golpes do Swordmaster OK.**
- Também pode ser embutido numa cópia da ISO com `patches/build_iso.py` (executável com CRC `2221DE71`); instruções em `patches/COMO-GERAR-A-ISO.md`. Detalhes e histórico em `patches/style_switch/LEIA-ME.md`.

## 7. Limites desta análise

- PC: sem o código decompilado (proteção Steam), só se sabe onde cada método está, quanto ocupa e a qual classe pertence.
- PS2: o código está todo em C decompilado, mas sem nomes para ~90% das funções e com o render escrito diretamente para o hardware do PS2 (GS/VU), que não tem equivalente no Android.
- Um port nativo exigiria reescrever o render, o áudio, o vídeo e a E/S, e nomear/entender milhares de funções. É um projeto de anos, não uma conversão.
- Para jogar no Android hoje: a ISO roda direto no emulador **NetherSX2/AetherSX2**; em CHD fica um pouco menor que os 4,4 GB atuais (os vídeos MPEG quase não comprimem; tamanho exato não medido).

## 8. Como reproduzir

```
python -I scripts/rdata_scan.py      pc/dmc3.exe pc/rea/rdata_scan.json
python -I scripts/pe_structure.py    pc/dmc3.exe pc/rea/pe_structure.json
python -I scripts/nbz_list.py        "<jogo>/data/dmc3/dmc3-0.nbz"
python -I scripts/nbz_pac_survey.py  "<jogo>/data/dmc3/dmc3-0.nbz"
rea analyze pc/dmc3.exe --provider ghidra --snapshot pc/rea/dmc3.snapshot

# PS2
python -I scripts/iso9660.py  <iso> extract /SLUS_213.61 ps2/SLUS_213.61
python -I scripts/afs_list.py <iso> GDATA:1535159 MDATA1:3096 MDATA2:456062 SDATA:1108052
analyzeHeadless ps2/ghidra_project DMC3_PS2 -import ps2/SLUS_213.61 -processor r5900:LE:32:default \
    -scriptPath scripts/ghidra -postScript ExportAll.java ps2/export
python -I scripts/ps2_classes.py ps2/SLUS_213.61 pc/rea/pe_structure.json ps2/export/functions.tsv ps2/export/classes.json
python -I scripts/ps2_autoname.py ps2/export ps2/export/names.tsv
analyzeHeadless ps2/ghidra_project DMC3_PS2 -process SLUS_213.61 -noanalysis -scriptPath scripts/ghidra \
    -postScript ApplyNames.java ps2/export/vtable_names.tsv ps2/export/names.tsv ps2/export/manual_names.tsv \
    -postScript ExportAll.java ps2/export_named
```
(`GHIDRA_INSTALL_DIR` e `JAVA_HOME` precisam estar definidos no terminal.)

## 9. Versão Xbox 360 (DMC HD Collection) — nova base do port

Data: 2026-10-21 · ISO: `Devil May Cry - HD Collection (World) (En,Ja,Fr,De,Es,It).iso` (8,74 GB, disco XGD3, partição em `0x2080000`)

### Arquivos do DMC3 no disco
Cada jogo tem seu executável. Só estes arquivos são do DMC3 (extraídos em `x360/`):

| Arquivo | Conteúdo | Tamanho |
|---|---|---|
| `DMC3.xex` | executável (XEX2, AES "normal", compressão "basic") | 9,2 MB |
| `Media/DMC3-0.zip` | `GData.afs/`: 3.885 arquivos (3.400 `.pac`), mesmo formato do PC | 889 MB |
| `Media/DMC3-1.zip` | `afs/sound/`: 155 músicas `.ogg` | 238 MB |
| `Media/wmv/*.wmv` | 29 vídeos (cutscenes e Game Over) | ~1,2 GB |

`Default.xex` é o menu da coletânea; `Audio/`, `Video/`, `Bundles/` e `Scripts/` são do DMC1/DMC2 e do menu.

### Executável
- Descriptografado com a chave retail por `scripts/xex_extract.py` (AES do próprio Windows): `x360/DMC3.pe`, PE PowerPC big-endian (máquina `0x1F2`), base `0x82000000`, entrada `0x8246A120`.
- **Código legível**: `.text` com 6,2 MB e entropia 6,65 (no PC está criptografado pela Steam, 8,00).
- **11.350 funções** na `.pdata`, 5,8 MB de código. 7.410 instruções VMX128 (SIMD do Xenon).
- PDB de origem: `D:\DMC3\Projects\DMC\DMC3\X360DMC3\build\vc2010\Release\DMC3.pdb` (não distribuído).
- Bibliotecas: D3D9 do Xbox (com compilador de shaders), FMOD (mesmo motor de áudio do PC), decodificador WMV, zlib, libpng, libjpeg. Importa de `xboxkrnl.exe` e `xam.xex`.
- **389 classes com RTTI. As 363 classes `C*` do PC estão todas no 360**, e as interfaces `I*` também existem aqui (no PS2 não existiam). O mapeamento de vtables PC ↔ 360 deve ser quase 1:1.

### REA e Xbox 360
A REA recusa o PE com qualquer provedor: *"unsupported PE architecture (COFF machine 0x01f2)"*. O Ghidra 12.1.2 instalado também não carrega esse PE (o carregador PE dele só aceita PowerPC little-endian) e não decodifica VMX128 sem a extensão XEXLoaderWV.

### Recompilação
O caminho é o **XenonRecomp** (PPC → C++) com o **XenosRecomp** para os shaders. Rascunho de configuração em `x360/DMC3_recomp.toml`, com as 8 rotinas auxiliares do compilador localizadas e conferidas:

| Rotina | Endereço |
|---|---|
| `__savegprlr_14` / `__restgprlr_14` | `0x8258FC00` / `0x8258FC50` |
| `__savefpr_14` / `__restfpr_14` | `0x82590C40` / `0x82590C8C` |
| `__savevmx_14` / `__restvmx_14` | `0x82591590` / `0x82591828` |
| `__savevmx_64` / `__restvmx_64` | `0x82591624` / `0x825918BC` |

A imagem descriptografada está no **layout de memória** (endereço = `0x82000000` + offset no arquivo); os campos de offset de arquivo do cabeçalho PE não valem para ela. Uma primeira versão desta tabela usou esses campos e saiu deslocada em `0xBA00`.

### Primeira recompilação (2026-10-21)
Ferramentas: LLVM 23.1.4, CMake 4.4.4, Ninja 1.13.2, VS 2022 Build Tools (MSVC + Windows SDK 22621). XenonRecomp `ddd128b` em `tools/XenonRecomp`, compilado com `tools/build_xenonrecomp.bat`.

**Tabelas de `switch`:** 1.217 `bctr` no código; 625 são `switch` com tabela absoluta, o resto é chamada indireta (vtable).
- O VC2010 do DMC3 gera `lis → rlwinm → addi → lwzx → mtctr → bctr`; o XenonAnalyse só conhecia a ordem do Sonic (`lis → addi → rlwinm`) e achava 0. Acrescentei esse padrão em `XenonAnalyse/main.cpp` (`SWITCH_ABSOLUTE_RLWINM_FIRST`): 515 achados.
- `scripts/x360_switch_extra.py` acha mais 88 cuja checagem de limite fica em outro bloco (a tabela fica logo depois do `bctr` e termina no menor rótulo). **Total: 603 de 625.** As 22 restantes ficam acima de `0x825A0000` (provavelmente bibliotecas).
- 71 tabelas ficam em funções folha, que não têm entrada na `.pdata`; isso é normal.

**XenonRecomp:** roda em ~3 s e gera 104 arquivos C++ (107 MB, ~25 mil funções emitidas) em `recomp/ppc/`. Log em `recomp/recomp_log.txt`. Um arquivo de teste (`ppc_recomp.0.cpp`) compila com clang-cl **sem erros**. O código usa SIMDe para os vetores, que também compila para ARM/NEON (Android).

**Instruções acrescentadas ao XenonRecomp (2026-10-21).** A primeira passada deixou ~14 mil instruções sem tradução, de 53 tipos que o Sonic não usava. Todas foram implementadas em `XenonRecomp/recompiler.cpp` (mais `ppc_vctuxs` em `XenonUtils/ppc_context.h`):

| Grupo | Instruções |
|---|---|
| Desvios e CR | `bdzf` (idioma do VC2010 para `switch` pequeno: `mtctr` + um `bdzf` por caso), `bdnzt`, `cror`, `crorc` |
| Inteiras | `addc`, `addme`, `subfze(.)`, `eqv`, `mulhd`, `mulhdu` |
| Loads/stores com atualização | `lbzux`, `lhzu`, `lhzux`, `lhau`, `lwzux`, `ldux`, `lfsu`, `lfsux`, `lfdu`, `stbux`, `sthu`, `sthux`, `stdux`, `stfsu`, `stfsux`, `stfdu`, mais `lhbrx` e `lvehx` |
| Vetor (VMX/VMX128) | `vslh`, `vsrh`, `vsrah`, `vsrab`, `vrlh`, `vsubshs`, `vaddsbs`, `vaddsws`, `vsububm`, `vmaxsh`, `vminsh`, `vavguh`, `vspltish`, `vcmpgtsh(.)`, `vcmpgtsw(.)`, `vcmpequh(.)`, `vpkshss(128)`, `vpkswss(128)`, `vpkswus128`, `vpkuhus128`, `vcfpuxws128`; e o bit de registro do `vcmpgtuh.`, que era ignorado |

Resultado: **0 instruções não reconhecidas** no log.

**Testes de semântica** (`tests/`, rodar `tests\run_tests.bat`): `gen_insn_tests.py` pega do código gerado um trecho real de cada uma das 53 instruções e o executa com 20 mil entradas aleatórias (com valores de borda: saturação, carry, NaN, infinito), comparando com uma referência escrita na ordem big-endian do PowerPC. **53 de 53 passam.** Uma mutação proposital (operandos do pack trocados) é detectada, então o teste pega esse tipo de erro.

**Limites de função (2026-10-22).** Depois das instruções, sobraram 2.694 avisos "Switch case … outside function" e 16 erros de compilação `use of undeclared label` em 5 arquivos. Todos os casos (72 `switch` e 6 funções) estavam em **funções folha**, que não têm `.pdata`: o `Function::Analyze` do XenonRecomp trata o `bctr` de um `switch` como fim de função e às vezes parte uma função em duas.
- `scripts/x360_leaf_functions.py` recalcula essas funções. Os pontos de entrada são o início da região, os alvos de `bl` e os ponteiros para código guardados em `.rdata`/`.data`. Cada função vai até a próxima entrada, e duas vizinhas são unidas se um desvio condicional, um `b` para endereço que não é entrada ou um rótulo de `switch` passar de uma para a outra.
- Resultado: **124 funções em 47 regiões**, gravadas na seção `functions` de `x360/DMC3_recomp.toml` (entre os marcadores `BEGIN/END functions`). Nenhum ponto de entrada ficou no meio de uma função declarada.

**Situação atual:** o log do XenonRecomp não tem nenhum aviso (0 instruções não reconhecidas, 0 `switch` fora da função), e **os 96 arquivos gerados compilam sem erro** (`recomp\build.bat`, 39 MB de objetos). Os testes de instrução continuam 53/53.

**Últimos `switch` e `setjmp`/`longjmp` (2026-10-22).**
- Os 22 `switch` de biblioteca que faltavam têm rótulos **antes** da tabela também, e a regra "a tabela termina no menor rótulo" não servia. `x360_switch_extra.py` agora tem uma segunda regra: a tabela termina na primeira palavra que não é endereço dentro da função (`.pdata`, ou a região entre duas funções) ou num endereço que já é rótulo. Os 22 têm 12 casos cada, e para todos existe na função um `cmplwi` com limite 11, que confirma o tamanho. **Total: 625 de 625**, e o código gerado tem 625 blocos `switch`.
- `longjmp` = `0x82593880`: uma das duas funções que chamam `RtlUnwind`, restaura r1, r13–r31, f14–f31 e os vetores a partir do `jmp_buf` (r3) e desvia com `mtlr r5 ; blr`. `setjmp` = `0x82593BA0`, logo depois: grava exatamente o mesmo conjunto e retorna 0. O XenonRecomp troca as duas pelas versões nativas (usadas em 3 arquivos). O `jmp_buf` nativo fica dentro do buffer do jogo: o do 360 tem pelo menos 316 bytes (o `setjmp` grava até o offset 312), e o do Windows x64 ocupa 256, então cabe. No Android isso precisa ser conferido de novo.

**Situação do recompilador:** log sem nenhum aviso, 96 arquivos compilam sem erro, testes de instrução 53/53. O que falta agora é o **runtime**. Nada foi executado ainda, e compilar não prova que o código está certo.

Depois do recompilador fechar, o maior trabalho é o **runtime**: implementar as funções do `xboxkrnl.exe`/`xam.xex` que o jogo importa, a camada gráfica (D3D9 do Xenos → Vulkan, shaders pelo XenosRecomp), áudio (FMOD tem SDK para Android), entrada e arquivos.

### Requisito: troca de estilo nativa
O port precisa ter a troca de estilo em tempo real do patch do PS2 (`patches/style_switch/`) como recurso nativo. No 360 a ideia é a mesma, mas feita em C++ no código recompilado em vez de um patch de assembly: localizar no 360 as mesmas funções (`CPlDante`, `CUIDStyleSelect`, carga de pacote de animação de estilo, atualização da HUD) pelas vtables e chamar a sequência da Divinity Statue a partir do direcional.

### Reproduzir
```
python -I scripts/xdvdfs.py <iso> list
python -I scripts/xdvdfs.py <iso> extract DMC3.xex x360/DMC3.xex
python -I scripts/xdvdfs.py <iso> extractdir Media x360/Media
python -I scripts/xex_extract.py x360/DMC3.xex x360/DMC3.pe
tools\build_xenonrecomp.bat
tools/XenonRecomp/build/XenonAnalyse/XenonAnalyse.exe x360/DMC3.xex x360/DMC3_switch_tables.analyse.toml
python -I scripts/x360_switch_extra.py x360/DMC3.pe x360/DMC3_switch_tables.analyse.toml x360/DMC3_switch_tables.toml
python -I scripts/x360_leaf_functions.py x360/DMC3.pe x360/DMC3_switch_tables.toml x360/DMC3_recomp.toml [recomp/build_log.txt]
tools/XenonRecomp/build/XenonRecomp/XenonRecomp.exe x360/DMC3_recomp.toml tools/XenonRecomp/XenonUtils/ppc_context.h
recomp\build.bat
tests\run_tests.bat
```

## 10. Runtime (2026-10-22)

O executável `recomp/bin/DMC3.exe` junta o código recompilado (`recomp/ppc`) com o runtime em `runtime/`. Para regerar os stubs, compilar e rodar, use `runtime\run.bat`; ele precisa do CMake e do Ninja no PATH.

| Arquivo | Conteúdo |
|---|---|
| `main.cpp` | reserva os 4 GB de endereço do console; carrega `x360/DMC3.pe` em `0x82000000`; preenche a tabela de funções (24.399 entradas); preenche as importações; monta o bloco do processador (`r13`), a pilha e a thread principal (pilha do host de 256 MB); chama o ponto de entrada `0x8246A120`. Em caso de crash, mostra o endereço do console acessado e a função recompilada mais próxima; uma chamada indireta para endereço sem função mostra o endereço procurado |
| `memory.cpp` | memória virtual em `0x40000000+` e física em `0xA0000000+` (alocação sequencial, confirmada sob demanda) |
| `kernel_memory.cpp` | `NtAllocateVirtualMemory`, `NtFreeVirtualMemory`, `MmAllocatePhysicalMemoryEx`, `MmGetPhysicalAddress`… |
| `kernel_file.cpp` | sistema de arquivos: `game:\`, `d:\` e `\Device\Cdrom0\` apontam para `x360\` (ou para a variável `DMC3_DISC`); abrir, criar, ler, escrever, consultar, listar pastas, fechar |
| `kernel_misc.cpp` | TLS, seções críticas (thread única), tempo, strings, `DbgPrint`, configuração do console, região e idioma |
| `imports_gen.cpp` | gerado por `scripts/x360_imports.py`: um stub que registra a chamada para cada função ainda não implementada, as 13 variáveis importadas (`XboxHardwareInfo`, `KeTimeStampBundle`…) e os 179 ponteiros de função |

As importações são **179 funções** (132 do `xboxkrnl.exe`, 47 do `xam.xex`) e **13 variáveis**; a lista completa está em `x360/imports.txt`. **38 funções estão implementadas.**

**Até onde o jogo chega** (primeiras execuções, 2026-10-22):
1. O CRT inicializa: memória, TLS, configuração do console.
2. O jogo **encontra e abre seus dados**: lista `game:\Media\` e abre `DMC3-0.zip`, `DMC3-1.zip` e `DMC3-2.zip` (para quando `DMC3-3.zip` não existe).
3. XAM: versão do sistema, rede, notificações, estado de login.
4. **Inicialização gráfica**: `VdInitializeEngines`, buffer de comandos da GPU, ring buffer, callback de interrupção. O D3D do jogo lê os registradores da GPU Xenos em `0x7FC80000`; essa página hoje é só memória comum.
5. Para em `ExCreateThread`: o jogo cria uma thread e espera que ela rode.

Correções no recompilador que essa execução revelou:
- 10 `switch` de biblioteca com tabela de **deslocamentos** em `.rdata` (`lbzx`/`lhzx` + `add`, com um `nop` no meio) viravam chamada indireta. Um deles é o formatador do `printf` (56 casos). `x360_switch_extra.py` agora reconhece esse formato, e o total é **635 tabelas**.
- `x360_leaf_functions.py` mantém as regiões que já estão no TOML, para não perder entre execuções as regiões vindas do log de build.

### Threads e sincronização (2026-10-22)
`runtime/kernel_thread.cpp`:
- **Threads reais.** Cada thread do jogo é uma thread do host (pilha de 64 MB reservada), com contexto, pilha, bloco do processador (`r13`), KTHREAD e TLS próprios. `ExCreateThread` usa o `XapiThreadStartup` quando o jogo passa um; também estão implementados suspender, retomar, terminar e `KeDelayExecutionThread`.
- **Objetos de espera** (eventos, semáforos, threads) com estado único: um mutex e uma variável de condição, indexados pelo endereço do cabeçalho de despacho no console, que é mantido atualizado. As funções `Ke*` (por ponteiro) e `Nt*` (por handle) enxergam o mesmo objeto. Espera simples, múltipla (todos ou qualquer um) e com tempo limite relativo ou absoluto.
- **Seções críticas** viram mutexes recursivos do host, criados no primeiro uso. **Spinlocks** são atômicos sobre a palavra na memória do console. O TLS tem valores por thread.
- As tabelas do runtime (alocador, arquivos, registro de chamadas) têm mutex.

**64 funções implementadas** (de 179).

**Ferramenta de diagnóstico:** `DMC3_WATCHDOG=N` faz o runtime, depois de N segundos, pausar cada thread do jogo e imprimir onde ela está (offset no executável), a última chamada ao kernel e o endereço de retorno no jogo. `scripts/host_where.py recomp/bin/DMC3.map <rva>` traduz o offset na função, usando o map do linker.

**Onde o boot para agora:** o jogo cria duas threads de trabalho da GPU (`0x8248F778`, que ficam esperando, como deveriam), consulta o modo de vídeo, treina a EDRAM e então fica em `sub_824799C0`. É o laço do D3D do jogo que espera a GPU avançar um contador (`device+11024`) e checa se ela travou (`0x82484ED8`). Não há GPU processando o ring buffer, então o laço não termina.

**Próximo passo (decidido: renderizador nativo, alvo mobile):** mapear a camada de D3D do jogo, ou seja, localizar e nomear as funções do D3D do XDK dentro do `DMC3.xex` (criação do device, estados, texturas, shaders, desenho, `Present`), e substituí-las por um renderizador nativo, como faz o Unleashed Recompiled. Assim o ring buffer da Xenos nunca é usado, e o laço acima some junto com o resto do D3D original.

## 11. Mapa do D3D (2026-10-22)

Detalhes em [`x360/d3d/D3D_MAP.md`](x360/d3d/D3D_MAP.md).

- **Ferramentas novas:** Ghidra carregando a imagem crua em `0x82000000` com `PowerPC:BE:64:64-32addr`, e as 23.959 funções criadas a partir da lista do recompilador (`scripts/ghidra/X360Setup.java`, cerca de 1 minuto). Grafo de chamadas (`scripts/x360_callgraph.py`), vtables pelo RTTI (`scripts/x360_vtables.py`: 909 vtables, 13.838 métodos) e strings por função (`scripts/x360_code_strings.py`).
- **D3D do XDK** em `0x82470000`–`0x82490000` (881 funções), localizado pelas funções que chamam as importações `Vd*`. **104 funções** são chamadas de fora. O device fica num ponteiro global em `0x82A2ABA0`.
- **43 funções nomeadas** (30 de confiança alta), incluindo `D3DDevice_Swap`, inicialização da GPU, os render states de profundidade, alfa, blend e cull (identificados pelo layout dos registradores da Xenos), texturas e o laço onde o boot para (`D3D_WaitForGpuProgress`). Elas cobrem 45% das chamadas que entram no D3D.
- **Descoberta principal:** o port HD manteve o modelo de render do PS2. A função `0x82122770` recebe o valor do registrador ALPHA do GS e o traduz em estados de blend do D3D; o backend tem classes `PS3Texture` e `PS3Clut` (paleta CLUT do PS2). A tradução PS2 → D3D é lógica do jogo e já está recompilada; **para o renderizador nativo, basta substituir as funções do D3D do XDK**, sem mexer na lógica de desenho do jogo.
- **Nomeação completa da API (2026-10-22):** as 104 funções do D3D chamadas de fora estão nomeadas (118 nomes no total, 83 de confiança alta). Desenho identificado pelo pacote PM4 `DRAW_INDX` (`DrawVertices` `0x82479038`, `DrawIndexedVertices` `0x82479450`). Vertex e index buffers pelo tipo no cabeçalho do recurso. Vertex e pixel shader separados pela ligação com a declaração de vértice. Samplers pelos campos da fetch constant. O backend do jogo cria cerca de 65 shaders (37 de pixel, 28 de vértice), que são o que o renderizador nativo vai precisar converter.

## 12. Camada nativa no lugar do D3D — primeiro marco (2026-10-22)

**Divisão adotada** (`runtime/gpu.h`):
- Setters de estado, criação, Lock e Unlock de recursos do D3D original **continuam rodando**. Eles só gravam na memória do console (registradores-sombra do device, cabeçalhos de recursos). O renderizador nativo vai **ler o estado direto do device** (`*(0x82A2ABA0)`) no momento do desenho.
- O que dependia da GPU Xenos foi substituído: `D3D_WaitForGpuProgress` (`0x824799C0`) marca toda fence emitida como concluída (`*(device+0x2B10) = device+0x2B1C`) e devolve 0, que é como os laços de espera do D3D (`BlockOnFence` e a espera do swap) terminam. `VdSwap` vira o fim do quadro (`gpu::Present`). `Draw*` e `Clear` por enquanto só contam e seguem para o original. Os comandos que o D3D ainda grava para a Xenos não são executados por ninguém.
- **Mecanismo de substituição:** cada `sub_X` gerado é um alias fraco de `__imp__sub_X`, então `GUEST_OVERRIDE(endereço)` em `runtime/` troca a função em todas as chamadas, inclusive pela tabela de funções, e `GUEST_ORIGINAL(endereço)` chama a original.
- Funções de vídeo do kernel (`runtime/kernel_video.cpp`): modo 1280×720 widescreen a 60 Hz, inicialização "pronta", callback de interrupção registrado.

**Resultado:** o jogo **passa da inicialização do D3D e roda o laço de quadros** (~1.800 quadros/s, sem vsync). Cada quadro tem 121 `DrawVerticesUP` e 2 `Clear`, o perfil de uma tela 2D. No caminho, inicializa entrada (`XamInputGetCapabilitiesEx`), áudio (XAudio, 3 threads) e login (`XamShowSigninUI`), e carrega arquivos de `DMC3-0.zip` (texturas `.tm2`, sons `snd_sys.*`, `lei20_4c.bin`, `memoca_eng.pac`). São 7 threads, todas em estado saudável. 82 de 179 funções do kernel implementadas.

**Correções feitas no caminho:**
- `longjmp` indireto: a libpng 1.5 do jogo recebe o `longjmp` por ponteiro (`png_set_longjmp_fn(png, 0x82593880, 0x540)`). O XenonRecomp só redireciona as chamadas diretas, então a indireta caía no `longjmp` recompilado, que lê o `jmp_buf` no formato do PowerPC, mas quem o gravou foi o `setjmp` nativo (r1 = 0, crash). `0x82593880` agora é substituído pelo `longjmp` nativo (`runtime/debug_hooks.cpp`).
- `NtReadFile`/`NtWriteFile`: `ByteOffset` especial (-1 = fim, -2 = posição atual) e evento de conclusão sinalizado pelo sistema de objetos.
- Página dos registradores do decodificador de áudio XMA (`0x7FEA0000`) mapeada como memória comum.

**Diagnóstico novo:** relatório de crash com registradores e pilha do jogo; `kernel::CallGuest` (chamar funções do jogo a partir do runtime); `DMC3_ZLIB_TEST`, `DMC3_DUMP_PNG`, `DMC3_LOG_READS` e `DMC3_LOG_ALLOC`.

**Teste diferencial do zlib:** o zlib 1.2.5 recompilado (`inflateInit2_` `0x8258CF68`, `inflate` `0x8258D1C8`) foi testado contra o zlib do Python em 40 casos (texto, repetição, dados aleatórios, mistura, linhas tipo PNG; níveis 0/1/6/9; de uma vez e em pedaços). **40 de 40 corretos** (`tests/make_zlib_vectors.py`).

**Defeito resolvido: `memset` cortado pelo XenonRecomp (2026-10-23).** Sintoma: um PNG embutido em `memoca_eng.pac` chegava à libpng com o fim sobrescrito pela tabela de mensagens do próprio jogo. Investigação:
- Watchpoint de escrita por hardware (`DMC3_WATCH`, `runtime/watch.cpp`) no endereço corrompido: a thread de carga (`ReadRequest@AsyncIOThread`) descomprimia o `.pac` enquanto a thread principal alocava e zerava memória no mesmo lugar.
- Log do alocador de blocos (`0x824368D0`, bitmap de 1 byte por bloco, sem trava; `DMC3_LOG_POOL`): a alocação de 0x3000 bytes (6 blocos de 0x800) marcava só 5, e a próxima alocação recebia o 6º bloco.
- O cálculo da quantidade de blocos estava certo. O erro era o `memset` do CRT (`0x825901E0`): o teste diferencial `DMC3_CRT_TEST` (todos os tamanhos de 0 a 300, 8 alinhamentos) deu 1.194 falhas em 4.816 casos, sempre faltando o último byte quando o tamanho ≡ 2 ou 3 (mod 4).
- **Causa raiz:** o `memset` é uma função folha (sem `.pdata`), e o `Function::Analyze` do XenonRecomp tratava o `bdzlr` (retorno condicional: decrementa o CTR e retorna se zerou) como `blr` incondicional. A função terminava antes da cauda. O teste usava só o bit 0x10 do campo BO ("ignora a condição"); o retorno só é incondicional com 0x10 **e** 0x04 ("ignora o contador").
- **Correção:** `tools/XenonRecomp/XenonAnalyse/function.cpp`, `conditional = (BO & 0x14) != 0x14`. Só o `memset` mudou de tamanho (144 → 160 bytes); dois fragmentos dele deixaram de ser funções separadas.
- **Verificação:** `memset`/`memcpy` 0 falhas em 4.816 casos; o bitmap marca os 6 blocos; a tabela vai para depois do `.pac`; a libpng não registra mais erro e o PNG chega idêntico byte a byte ao original; zlib 40/40; instruções 53/53; o laço de quadros continua.
- Lição: a análise de limites de função do XenonRecomp é a fonte de bugs mais silenciosa. Testes diferenciais de funções básicas do CRT (`tests/` e os modos `DMC3_*_TEST`) devem ser repetidos a cada mudança no recompilador.

**Próximo passo:** janela e primeiro desenho nativo: ler do device o estado, os vértices de `DrawVerticesUP` e as texturas, e desenhar a tela 2D. A biblioteca de janela e de gráficos ainda precisa ser escolhida e baixada.

## 13. Janela e primeiro desenho nativo (2026-10-23)

![Primeira tela desenhada pelo renderizador nativo](x360/shot_300.png)

**Biblioteca:** SDL3 3.4.18 (`tools/SDL3`, pacote oficial `SDL3-devel-3.4.18-VC.zip`) para janela, eventos e contexto; OpenGL 3.3 core para desenho (no Android, OpenGL ES 3 com o mesmo código, mudando só o cabeçalho do shader e a criação do contexto). As funções do OpenGL são carregadas pelo próprio SDL, sem biblioteca extra.

**Arquivos:**
- `runtime/renderer.h`, `runtime/renderer_gl.cpp`: janela 1280×720 com vsync, shader que reproduz a modulação do PS2 (cor do vértice ×2, 0x80 = intensidade cheia), alpha test no shader, blend e operação de blend da Xenos convertidos para OpenGL, cache de texturas (por endereço/tamanho/formato, com amostra do conteúdo para detectar mudança), captura de tela (`DMC3_SCREENSHOT=<quadro>`, `DMC3_SCREENSHOT_FILE`; `scripts/rgba_to_png.py` converte).
- `runtime/gpu.cpp`: `DrawVerticesUP` decodifica os vértices pela declaração do device (`+0x2FD0`: objeto com contagem em `+0x18` e elementos de 12 bytes em `+0x34`), lê a textura do estágio 0 pela fetch constant do device (`(stage+0x30)*0x18`) e o blend e alpha test pelos campos do device, e manda para o renderizador. `Clear` (flags em `r4`: bits 0–3 render targets, 0x10 Z, 0x20 stencil; cor em `r6`) e `VdSwap` (apresentação) também. Diagnóstico: `DMC3_CAPTURE=<quadro>` (com `DMC3_CAPTURE_BRIEF=1` para uma linha por desenho) e `DMC3_DEBUG_FRAME`.

**O que a tela inicial usa** (capturado do quadro 300):
- 121 `DrawVerticesUP` por quadro, primitiva 13 (`QUADLIST`), stride 24: posição `FLOAT3` já em clip space (com y para baixo: o shader do jogo inverte), cor `UBYTE4N` na convenção do PS2 (0x80 = 1,0) e texcoord `FLOAT2` **em texels × 16** (o UV de ponto fixo 12.4 do GS do PS2).
- Texturas `8_8_8_8` lineares (não tiled), em palavras big-endian `0xAARRGGBB`: painel 256×128, ícone 128×128 e 117 letras 32×32. Blend `SRC_ALPHA` / `1-SRC_ALPHA`.
- Os dois últimos desenhos são triângulos de tela cheia que leem o render target 1280×720 (tiled) "resolvido": o jogo desenha a interface num render target e copia para a tela. Como ainda não há render targets, tudo vai direto para a janela e essas duas passadas são puladas.

**Resultado:** a tela de aviso de gravação ("Please do not turn off the Xbox 360 console…" com "Ⓐ OK") aparece correta, com texto, painel, cores e transparência. O jogo fica parado nela esperando o botão A, porque a entrada ainda não está implementada.

**Próximos passos:**
1. Entrada: `XamInputGetState`/`XamInputGetCapabilities` a partir do controle e do teclado pelo SDL3.
2. Render targets e `Resolve`, no lugar de pular as passadas de tela cheia.
3. Texturas em tile e comprimidas (DXT), `DrawVertices`/`DrawIndexedVertices` com vertex/index buffers, e os shaders do jogo (cerca de 65) para as telas 3D.

## 14. Entrada, login e seleção de dispositivo (2026-10-23)

**Entrada** (`runtime/input.cpp`, `runtime/kernel_input.cpp`): a cada quadro, na thread da janela, o teclado e o controle são lidos pelo SDL3 e guardados num retrato no formato do XInput, que `XamInputGetState`, `XamInputGetCapabilities(Ex)` e `XamInputSetState` leem (o jogador 1 está sempre conectado; o teclado funciona como controle). Controles XInput do Windows são reconhecidos (`XInput Controller #1`).

| Tecla | Botão | Tecla | Botão |
|---|---|---|---|
| Enter / Z | A | Backspace / X | B |
| C | X | V | Y |
| setas | direcional | W A S D | analógico esquerdo |
| I J K L | analógico direito | Q / E | LB / RB |
| 1 / 3 | LT / RT | Espaço / Tab | Start / Back |

`DMC3_INPUT_SCRIPT="quadro:botões:duração,..."` simula apertos para testes automáticos (ex.: `400:A:10`). `DMC3_SCREENSHOT` aceita vários quadros (`300,700,...`), gravados em `DMC3_SCREENSHOT_DIR`.

**Por que o A não fazia efeito no começo:** a entrada chegava ao jogo, que lê o jogador 1 a cada quadro (`0x826613FC`), mas o jogo tinha chamado `XamShowSigninUI` (tela de login do sistema) e esperava a notificação de que ela fechou. Sem notificações, ele ignorava a entrada. `runtime/kernel_xam.cpp` agora implementa notificações (`XamNotifyCreateListener`, `XNotifyGetNext`), com as de início do console; o jogador 1 aparece logado (`XamUserGetSigninState`); e as telas do sistema "abrem e fecham" na hora com `XN_SYS_UI` 1/0. `XamShowDeviceSelectorUI` escolhe o disco rígido e conclui pelo `XOVERLAPPED` (com o evento sinalizado).

**Duas instruções que faltavam no XenonRecomp,** reveladas pelo novo caminho de boot (código perto do FMOD): `vupkd3d128` tipo 5 (`FLOAT16_4`: 4 halfs → 4 floats, cerca de 60 usos) e `vpkd3d128` tipo 3 (`FLOAT16_2`: 2 floats → 2 halfs, 1 uso). Antes viravam `__builtin_debugtrap()` (o jogo parava num breakpoint). Agora não há nenhum `debugtrap` no código gerado. O teste de semântica ganhou o `vupkd3d128` (referência por outro caminho de conversão): **54 de 54**.

**Resultado:** o jogo passa da tela de aviso com o A, vai para a seleção de dispositivo e procura os saves. Ele para numa caixa de mensagem do sistema: "Insufficient free space on storage device. At least 128 KB of free space is required to save this game data.", porque as funções de conteúdo (saves) ainda não existem.

**Próximo passo: sistema de saves** (`XamContentGetDeviceData` com espaço livre, `XamContentCreateEnumerator`/`XamEnumerate`, `XamContentCreateEx`/`XamContentClose` mapeando a raiz do save para uma pasta do host, `XamShowMessageBoxUI`).

## 15. Sistema de saves (2026-10-23)

`runtime/kernel_content.cpp` e `runtime/kernel_xam.cpp`:
- **Dispositivo:** um disco rígido (ID 1) de 4 GB com 3 GB livres (`XamContentGetDeviceData`, `XamContentGetDeviceState`). Era a falta de espaço livre que gerava a mensagem "Insufficient free space…".
- **Conteúdos = pastas do host:** cada save vira `saves/<nome do arquivo do conteúdo>` (ou a pasta em `DMC3_SAVE_DIR`), com o nome de exibição em `.display`. `XamContentCreateEx` cria ou abre conforme o modo (CREATE_NEW, CREATE_ALWAYS, OPEN_EXISTING, OPEN_ALWAYS, TRUNCATE_EXISTING) e **monta a raiz** pedida pelo jogo (`savedrive:`) nessa pasta; o sistema de arquivos consulta as montagens antes do disco do jogo. `XamContentClose`, `XamContentDelete` (só dentro da pasta de saves) e `XamContentGetCreator`.
- **Enumeração:** `XamContentCreateEnumerator` lista as pastas de `saves/`; `XamEnumerate` devolve `XCONTENT_DATA` (0x134 bytes cada).
- **Caixas de mensagem** (`XamShowMessageBoxUI`/`UIEx`): o texto vai para o log e a resposta é o botão padrão (ou `DMC3_MSGBOX_BUTTON`).
- Operações assíncronas concluem pelo `XOVERLAPPED` (resultado, itens e evento); o 9º argumento e seguintes vêm da pilha (`r1+0x54`, de 8 em 8).

**Resultado:** na primeira execução o jogo não acha save, cria `savedrive:` → `saves\SaveGame` e grava `save.bin` (18.992 bytes); nas seguintes, acha 1 conteúdo, abre (OPEN_EXISTING) e lê o save. Depois disso começa a tocar um vídeo de tela cheia.

**Correção nos limites de função:** um ponteiro de função montado no código (`lis`+`addi`) para `0x824A4490` caía no meio de uma função folha declarada (chamada indireta para endereço sem função). `x360_leaf_functions.py` agora também usa como pontos de entrada os endereços montados no código cuja palavra anterior encerra uma função (`blr`, `b`, `bctr` ou preenchimento), excluindo as tabelas de switch: 127 funções declaradas. Conferência global: dos 837 ponteiros montados no código, só sobram 11 que não são início de função — 10 são as bases das tabelas de deslocamento (esperado) e 1 é um funclet de tratamento de exceção do CRT (`0x825966E8`, dentro de `0x825965F0`), que só roda em exceções.

**Onde para agora:** no vídeo. O jogo usa o decodificador WMV do XDK (`WMFDecodeX86`, classes `XMVVideo@DMC3` e `X360FullMotionVideoManager@DMC3`) com 4 threads novas, e a thread principal fica esperando um evento delas (`0x824AB3FC`) enquanto elas também esperam.

**Próximo passo:** vídeos. Primeiro, fazer o jogo pular os vídeos (o gerenciador de vídeo responde "terminou") para chegar ao título e aos menus; depois, tocar os `.wmv` de verdade com um decodificador nativo que também sirva no Android.

## 16. Vídeos pulados e chegada à tela de título (2026-10-23)

**Interface de vídeo** (Ghidra, `x360/d3d/fmv.c`): `X360FullMotionVideoManager@DMC3::vf1` (`0x824493D0`) cria um `XMVVideo@DMC3` e chama o `Open` dele (vf3, `0x8244FAF8`), que monta o player XMV do XDK (`0x8249B870`) e cria a superfície e a textura do quadro. O jogo consulta o estado por vf10 (`0x8244FE70`): 4 = terminado, 3 = tocando, 2 = pausado, 0 = parado, 5 = sem player. O gerenciador conta como ativos os vídeos em estado diferente de 0, 2, 4 e 5. vf4 (`0x8244F740`) desenha o quadro e não faz nada quando o byte `+0x25A` (terminado) está ligado.

**`runtime/video.cpp`:**
- `Open` roda só a parte da classe base (`0x82120BA0`) e deixa o objeto sem player, com `+0x25A` = 1. O nome do vídeo vai para o log (`[video] pulando ...`).
- O estado (vf10) responde "terminado" quando não há player.
- `Stop` (vf9) e `Seek` (vf11, posição em `f1`) usavam o player sem checar; agora não fazem nada sem ele.

Os vídeos `GuroE.wmv` (logo da Capcom), `m99_s00.wmv` e `Title.wmv` são pulados, e o jogo carrega `DMC3-1.zip` e entra na tela de título.

**Memória física espelhada:** a D3D converte endereços da GPU de volta para a CPU com `| 0xC0000000`, então um índice alocado em `0xA3EF4038` era lido em `0xC3EF4038` e o jogo caía. `runtime/memory.cpp` agora monta a memória física (512 MB) como uma seção mapeada duas vezes, em `0xA0000000` e `0xC0000000`, como no console. A faixa `0xE0000000` (páginas de 4 KB) ainda não é espelhada.

**Ritmo de 60 Hz:** com vsync num monitor de 165 Hz o jogo andava a 165 quadros/s, e a lógica dele conta quadros de 60 Hz. `renderer_gl.cpp` agora segura a apresentação em 60 quadros/s (`SDL_DelayPrecise`).

**Resultado:**
- A tela de título roda (quadros ~600 a 1650, com 6 a 8 `DrawIndexedVertices` por quadro).
- Com START, o jogo segue: pula `m99_s00.wmv` de novo e passa a um estado com 2 desenhos por quadro.
- Sem crash nem travamento, a 60 quadros/s.
- **A imagem fica preta.** O título e os menus usam `DrawIndexedVertices` com os shaders do jogo e cópias de render targets em tile, e nada disso existe ainda na camada nativa.

**Próximo passo:** fazer o título e os menus aparecerem. Isso exige:
- render targets como FBOs e resolve;
- `DrawIndexedVertices` e `DrawVertices` com buffers de vértice e índice;
- conversão dos shaders do jogo;
- texturas em tile e DXT.

## 17. Renderizador nativo: shaders traduzidos, render targets e a tela de título (2026-10-23)

**Como o jogo desenha um quadro** (captura do título, `DMC3_CAPTURE`):
1. Tudo vai para um render target de 1280×720 (RT0, superfície `0x401C4480`).
2. `D3DDevice_Resolve` copia o RT0 para texturas.
3. Duas passadas de tela cheia leem essas texturas.
4. Um último resolve vai para a textura que o `D3DDevice_Swap` mostra.

O `Resolve` público é `0x82478AD0` (antes chamado de `DrawWithFlags`), e `0x82471F88` é uma rotina interna que recebe um retângulo.

**Tradutor de shaders** (`runtime/xenos_shader.cpp`): converte o microcódigo da Xenos em GLSL 3.30, que também serve para GLES 3.0 com outro cabeçalho.
- **Controle:** `exec`, `exec_end`, `cond_exec` (constantes booleanas) e `cond_pred_exec` (predicado).
- **ALU:** as 30 operações vetoriais (exceto `cube`) e as 51 escalares, incluindo predicados, `kill`, `a0`, `ps` e as operações constante×registro (`mulsc`, `addsc`, `subsc`).
- **Fetch:** `vfetch` (vira atributo) e `tfetch` 2D.
- **Saídas:** o vertex shader escreve `oPos` e os interpoladores 0–7; o pixel shader escreve `oC0` e a profundidade.
- **Teste de alfa:** fica no fim do pixel shader.
- **Swizzles:** os de ALU são relativos (`(s >> 2i) + i`); os de fetch são absolutos.
- **Desmontador:** `scripts/xenos_disasm.py` é a referência.
- **Ainda não suportados:** laços, chamadas e saltos, texturas 3D/cubo, endereçamento relativo de registradores (`aL`) e `cube`. O log avisa quando um shader os usa.

**O que o D3D fazia e agora é feito na camada nativa** (`runtime/gpu.cpp`):
- **Programa do shader:** a seção indicada em `cabeçalho+0x18` dá o deslocamento e o tamanho do programa dentro do microcódigo.
- **Constantes literais:** o que vem antes do programa são os `def` do shader, ou seja, os vec4 de `c252–c255`. Exemplos: `c255.x = 1/16` converte as coordenadas "texels×16" do PS2, e `c254.y = 1/128` normaliza a cor 0x80. São aplicadas em cada desenho, por cima das constantes do device (VS em `device+0x780`, PS em `+0x1780`, booleanas em `+0x2780`).
- **Ligação dos `vfetch`:**
  - Shaders genéricos (como o dos sprites do título) vêm com os `vfetch` sem formato. O D3D completava essas instruções ao desenhar e as mandava direto para a GPU.
  - O cabeçalho lista cada `vfetch` com sua semântica (uso/índice). A declaração de vértices dá stream, offset e tipo.
  - No tipo `D3DDECLTYPE`, os bits 0–5 são o formato da Xenos, o bit 8 indica sinal, o bit 9 indica inteiro e os bits 10–21 são o swizzle de destino.
  - O fetch constant do stream `s` é o 95−s; o stride (em palavras) fica em `device+0x3268+s`.
- **Vértices:** são lidos na CPU para cada desenho e convertidos para vec4.
  - Formatos: 8_8_8_8, 2_10_10_10, 16_16(_16_16), half floats, inteiros de 32 bits e floats.
  - Cada palavra é lida com a troca de bytes do fetch constant.
  - Nos formatos de 16 bits, o primeiro componente fica na metade alta da palavra. Isso foi descoberto porque a tela de título saiu transposta.
- **Índices:** 16/32 bits big-endian, com base vertex e reinício de tira.
- **Estado lido dos registradores-sombra:**
  - `RB_DEPTHCONTROL` (`+0x2934`), `RB_COLORCONTROL` (`+0x293C`, teste de alfa), `PA_SU_SC_MODE_CNTL` (`+0x2948`, cull), `RB_COLOR_MASK` (`+0x28DC`);
  - viewport `PA_CL_VPORT_*` (`+0x2908`) com `PA_CL_VTE_CNTL` (`+0x294C`);
  - blend como antes.
- **Render targets:**
  - Cada superfície vira uma textura num FBO. O tamanho vem de `+0x18` (pitch) e `+0x2C` (bytes).
  - A profundidade é um renderbuffer depth24/stencil8.
  - O `Resolve` copia o FBO para uma textura do GL associada ao endereço da textura de destino.
  - Quando um desenho lê esse endereço, usa a textura do GL em vez da memória.
  - O `Swap` diz qual textura apresentar.
  - As imagens ficam com a linha 0 em cima, como no console: o vertex shader inverte o y, e só a apresentação desinverte.
- **Texturas:** 8_8_8_8 linear e em tile (fórmula de endereçamento da Xenos). O cache usa um hash do conteúdo inteiro, porque o jogo reescreve no lugar a textura do texto dos menus. A amostragem anterior, 1 byte a cada 61, deixava texto velho na tela.

**Diagnóstico:**
- `DMC3_LOG_SHADERS=1` grava o GLSL gerado no log.
- `DMC3_DEBUG_FRAME=n` resume cada desenho e resolve do quadro n.
- `DMC3_SHOW_RT=<superfície>` mostra um render target direto na janela.
- `DMC3_FLAT_PS=1` pinta todos os pixels de vermelho; `DMC3_NO_DEPTH=1` desliga o teste de profundidade.
- `DMC3_CAPTURE` agora também registra resolve, swap, estados e constantes, e grava cabeçalhos e microcódigo em `DMC3_CAPTURE_DIR`.

**Resultado:**
- A tela de aviso, a tela de título (Vergil, Dante, logo, NEW GAME / LOAD GAME / OPTIONS) e o menu OPTIONS, com o texto de ajuda, aparecem certos, a 60 quadros/s.
- A navegação funciona: RIGHT, A, B e DOWN.
- Capturas: `x360/shots6/shot_1000.png` e `x360/shots7/shot_1600.png`.
- 7 shaders traduzidos no caminho até o título.

**Ainda falta:**
- formatos de textura DXT e outros;
- resolve de profundidade e resolve com retângulo;
- desenhos com viewport desligado;
- primitiva rectlist;
- o resto do jogo (NEW GAME e as fases), que vai exigir mais shaders e formatos.

## 18. NEW GAME e a primeira cena de gameplay (2026-10-23)

**Trava depois do NEW GAME (vídeo de abertura):**
- A cena de evento (`CSceneDemo::vf3`, `0x823392E0`) fica no estado 200 até `sub_8242BC60` responder que o primeiro quadro do vídeo está pronto (byte `+0x240` do `XMVVideo`). Só depois disso ela passa a acompanhar o estado do vídeo.
- O pulo de vídeo deixava esse byte em 0. Agora `Open` marca o quadro como pronto, e o jogo segue: pula `m01_s00.wmv`, carrega e entra na Missão 1.
- `DMC3_LOG_VIDEO=1` registra as chamadas aos métodos de vídeo com a pilha do jogo.

**Skinning (personagens em pose T):**
- As matrizes de osso são lidas com `c[64 + a0]`.
- O bit `address_absolute` da instrução ALU quer dizer "relativo ao `a0`"; sem ele, a referência é o contador de laço `aL`. O tradutor tinha isso invertido.

**Texturas** (`runtime/xenos_texture.cpp`): decodificação genérica na CPU.
- Cada texel é lido como a GPU leria (troca de bytes do fetch constant) e vira os componentes x, y, z, w; o swizzle do fetch constant (palavra 3, bits 1–12) os leva a RGBA.
- Formatos: 8, 1_5_5_5, 5_6_5, 4_4_4_4, 8_8_8_8 e DXT1/DXT3/DXT5, lineares ou em tile.

**Resolve de profundidade** (flag 4):
- A profundidade do render target é copiada para uma textura depth24/stencil8 ligada ao endereço de destino.
- O jogo usa essa textura num shader que restaura o depth buffer (`oDepth = tfetch.x`) e nas partículas suaves.
- Antes, uma textura branca ocupava esse lugar: a restauração gravava "longe" no buffer inteiro.

**Para testes:**
- O cursor do título fica guardado no save.
- `DMC3_SAVE_DIR` apontando para uma pasta vazia deixa a sequência de botões previsível.
- Com save novo, aparece a tela de explicação dos estilos antes da missão.

**Resultado:**
- A Missão 1 roda no escritório do Dante: Dante animado com a Rebellion, os Hell Pride, o HUD (vida, orbes vermelhas, rank de estilo "Blast!" / "Alright!"), faíscas e rastros de golpe.
- 60 quadros/s com 100–170 desenhos indexados por quadro e 28 shaders traduzidos sem erro.
- Capturas: `x360/shots9/shot_4800.png`, `x360/shots9/shot_5400.png` e a tela de estilos em `x360/shots9/shot_3600.png`.

**Observações:**
- A iluminação da cena sai bem avermelhada. Ainda não comparei com o console, então não sei se é a iluminação do jogo ou um erro.
- Sem áudio: XAudio e XMA são stubs.
- Vídeos continuam pulados.

## 19. Troca de estilo em tempo real nativa (2026-10-24)

O mod "Style Switch" do PS2 (`patches/style_switch/`) agora é um recurso do port, em `runtime/style_switch.cpp`.

**Controles:** direcional durante a missão com o Dante: ↑ Trickster, → Swordmaster, ← Gunslinger, ↓ Royal Guard.
- O direcional é tirado do estado do controle entregue ao jogo (`XamInputGetState` → `style::FilterButtons`), então os atalhos originais não disparam.
- Fora da missão (menus, pausa) o direcional funciona normalmente.

**Equivalências PS2 → 360** (achadas comparando a configuração da missão `FUN_0025B420` ↔ `CPlDante::vf15`, a criação do Dante e a HUD):

| O quê | PS2 | 360 |
|---|---|---|
| estilo / nível / EXP do Dante | `+0x3C6C` / `+0x3C74` / `+0x3C7C` (bytes, float) | `+0x40DC` / `+0x40FC` / `+0x4108` (int, int, float) |
| ação de estilo em andamento | `+0x3C6E` | `+0x40E4` (observado em execução) |
| golpes ativos / comprados | `+0x29D8` / `+0x29F8` | `+0x2E28` / `+0x2E48` |
| liberar golpes do nível | vtable `+0x148` | vtable `+0x24` (`CPlDante::vf9`, `0x822F6618`) |
| dados do jogador | save `+0xC5` níveis, `+0xCC` EXP | `CSceneGame+0xD8`: `+0x58` estilo, `+0x5C` níveis[6], `+0x74` EXP[6] |
| registro da missão | save `+0x104` | `CSceneGame+0x184` (`+0x38` estilo, `+0x3C` nível, `+0x60` EXP) |
| carregar pacotes do estilo | `FUN_00203F20(0, registro)` | `sub_822D2728(0, registro)` |
| pool de movimentos | `0x6C3810` | `0x827BC5E8`; carregar `sub_822ABD48(pool, 0, arq, 0)`, procurar `sub_822AAFC0(pool, 1, id×34 + arq)` |
| tabela de animações do Dante | `+0x243C + 4×arquivo` | `+0x27EC + 4×arquivo` (estilo: arquivos 13–26) |
| ícone do estilo na HUD | tabela `0x5F5BB8`, `FUN_0013D660` + `CDraw::vf9/vf13` | tabela `0x82010D30`; HUD de `sub_82382148`: libera o handle `modelo+0x1C` (`sub_824369D8`), liga os arquivos `(ícone+9)×2` e `ícone×2+0x13` (vtable `+0x28`), vtable `+0x30`, `sub_8217ACC8(HUD+0x3D0)` |
| atualização por quadro | gancho em `frame_Begin` | `CPlDante::vf21` (`0x822F7A18`, chamada a cada quadro na missão) |

**Como a troca funciona:**
1. O direcional pede a troca. Os pacotes de animação do estilo novo começam a carregar, e os do estilo atual continuam no pool.
2. A troca de fato só acontece quando os pacotes novos estão carregados e não há ação de estilo em andamento.
3. Na troca, o nível e a EXP do estilo antigo vão para os dados do jogador, e os do novo voltam de lá. Os golpes ativos são restaurados a partir dos comprados, o jogo libera os golpes do nível, os pacotes ficam só os do estilo novo, a tabela de animações do Dante é atualizada e o ícone da HUD é refeito.
4. Proteção extra: se um movimento de estilo pedir um pacote fora da tabela, `sub_822E69D8` não o toca. No PS2 esse caso só deixava o golpe sem animação, mas no 360 derrubava o jogo.

**Problemas encontrados no caminho:**
- `CPlDante::vf16` não é o update por quadro: é a criação do Dante. O update é `vf21`.
- Trocar sem esperar o carregamento derrubava o jogo no interpretador de movimentos (`sub_821336C8`, ponteiro nulo).

**Testes** (ferramentas novas):
- `DMC3_LOG_STYLE=1` registra trocas, campos do Dante e a tabela de animações.
- `DMC3_STYLE_SHOTS=1` grava uma captura 40 quadros depois de cada troca.
- O `DMC3_INPUT_SCRIPT` aceita entradas relativas a eventos: `m01_s00+N` (vídeo pulado) e `missao+N` (primeiro quadro do Dante na missão).
- Os testes usam uma cópia fixa do save principal, para não aparecer o tutorial nem o cursor do título mudar.

**Resultado:**
- Num roteiro com 15 trocas entre os 4 estilos, com ações de estilo (B) logo em seguida, não houve queda.
- O Dante muda de arma e pose e o ícone da HUD acompanha (`x360/shots12/icones_estilo.png`).
- O estilo atual ganha EXP normalmente.

**Ainda não conferido:**
- se a EXP de cada estilo é gravada no save no fim da missão;
- o comportamento com Quicksilver e Doppelganger (que não estão no direcional);
- uma tela verde que apareceu numa captura logo depois de mudar para Trickster. Parece um efeito de tela cheia ainda não tratado pelo renderizador, não a troca em si.

## 20. Tela verde, tela avermelhada e o Clear (2026-10-24)

**Sintoma:** logo depois de trocar para Trickster, uma captura saía com o fundo verde chapado, com só um inimigo e a HUD por cima. Em todo o gameplay, a cena saía com uma cor vermelha forte.

**Diagnóstico:**
- A captura pedida na troca agora também detalha o quadro (`renderer::RequestScreenshot` liga o mesmo registro do `DMC3_DEBUG_FRAME`).
- `DMC3_DUMP_RESOLVES=1` grava cada resolve desse quadro (`resolve_<n>_<endereço>.rgba`).
- O verde aparecia entre dois resolves sem nenhum desenho no meio. O registro de `Clear` mostrou a cor `0x400FF760`.

**Causa 1 — `D3DDevice_Clear` (`0x82471D90`) é a forma `ClearF` do XDK.**
- Argumentos: `r4` flags, `r5` retângulo, `r6` ponteiro para 4 floats (r, g, b, a), `f1` Z, `r7` stencil.
- O valor de `r6` era tratado como cor `0xAARRGGBB`. O endereço de pilha `0x400FF760` virava a cor `(0x0F, 0xF7, 0x60)`, exatamente o verde da tela.
- Agora a cor é lida pelo ponteiro; ponteiro nulo vira preto transparente. As cores observadas são `0 0 0 0`.

**Causa 2 — vértices dos desenhos UP com a troca de bytes errada** (regressão da seção 18).
- Os dados do `DrawVerticesUP` vêm da memória da CPU (big-endian, troca 8in32), mas eu usava a troca do fetch constant 95, que o D3D configura para o seu buffer interno.
- As posições saíam lixo, e as passadas de tela cheia (composição da cena, correção de cor, tela de aviso) não desenhavam nada.
- Daí a tela verde (sobrava a cor do `Clear`), a cena avermelhada em todo o gameplay e a tela de aviso preta.

**Resultado:**
- O verde sumiu. A cena tem a iluminação certa: o escritório do Dante em tons quentes de marrom.
- A tela de aviso e o título continuam certos.
- Num teste com 10 trocas de estilo, nenhuma captura com verde.
- Capturas: `x360/shots12/estilo_02_3.png` e `estilo_04_2.png`.

## 21. EXP por estilo e o save — verificação parcial (2026-10-24)

**Piloto automático para testes** (roteiro `autoplay.ps1` na pasta de rascunho da sessão):
- Usa o save modelo e começa a Missão 1 com mira travada (RB) e ataque (Y) contínuos.
- Fica em Swordmaster no começo e troca para Gunslinger no meio.
- O Dante vence a missão sozinho: "Mission Clear" com rank A em cerca de 3 minutos de jogo, com a cena de encerramento.

**Verificado:**
- A EXP de cada estilo fica separada nos dados do jogador (`CSceneGame+0xD8`). Na troca para Gunslinger, a EXP do Swordmaster (378) foi guardada em `EXP[0]`, e o Gunslinger continuou acumulando no campo do Dante.
- O estilo atual atravessa o fim da missão: no menu Customize da Missão 2, o estilo selecionado é Gunslinger.

**Não verificado:**
- A gravação no arquivo. No resultado da missão, as opções são **Save / Mission Select / Next**, e o roteiro ainda não acertou o momento de escolher "Save".
- O usuário pediu para parar o piloto automático antes de concluir isso.
- Ainda falta confirmar se o jogo, ao gravar, leva para o save as EXP de todos os estilos ou só a do estilo atual.

**Achado de passagem:** depois da missão, o vertex shader `401DABC0` (342 palavras) não traduziu: usa salto condicional (`cond_jmp`, instrução de controle 11), que o tradutor ainda não suporta.

## 22. Por que o save "corrompia" (2026-10-24)

**Sintoma:** ao salvar, o jogo mostrava "Load Failed — the save data may be corrupted or damaged. Would you like to overwrite the data with new save data?", e o arquivo não mudava.

**O que acontece de fato** (módulo de save do port HD, `0x8244D000`–`0x8244E100`; uma thread executa os comandos 2 = carregar e 3 = gravar):
- **Primeiro boot:** não há save, então o jogo cria `save.bin` gravando os dados de um perfil vazio: `sub_82124DB0` preenche tudo com `0xFF`, e `sub_8244D638` copia para o buffer gravado por `sub_8244DB18`. Os 18.992 bytes de `0xFF` são intencionais; não é erro de escrita do port.
- **Carga** (`sub_8244DD10`): lê o arquivo inteiro e só considera que há dados se algum byte for diferente de `0xFF`. Arquivo todo `0xFF` = estado 2.
- **Antes de gravar**, o jogo carrega o save. Com o estado 2, pergunta se pode sobrescrever: botão 0 "Okay", botão 1 "Continue Without Saving" (padrão).
- **O erro do port:** as caixas de mensagem do sistema eram respondidas sozinhas com o botão padrão. O jogo então nunca gravava. Na partida de teste do usuário a pergunta apareceu 3 vezes e as 3 foram respondidas "Continue Without Saving".

**Correção** (`runtime/kernel_xam.cpp`):
- As caixas de mensagem do sistema agora aparecem para o jogador como diálogo nativo (`SDL_ShowMessageBox`, que também existe no Android), com os botões do jogo e o padrão no Enter.
- A resposta automática ficou só para os testes: com `DMC3_INPUT_SCRIPT` vale o botão padrão, ou o de `DMC3_MSGBOX_BUTTON`.
- O texto UTF-16 do console agora é convertido para UTF-8 de verdade (antes, caracteres fora do ASCII viravam `?`).
- O log registra a resposta escolhida.
- `DMC3_LOG_READS` agora também registra as escritas (`NtWriteFile`: tamanho, quantos bytes diferem de `0xFF`, quem chamou).

**Falta verificar:** um save real escolhendo "Okay", e depois carregar esse save (LOAD GAME) para conferir o nível e a EXP de cada estilo.

## 23. Tela preta com inimigo perto da câmera (2026-10-24)

**Sintoma (relatado pelo usuário):** às vezes, quando um inimigo fica entre a câmera e o Dante, o cenário some (preto puro) e só aparecem os objetos colados na câmera, a HUD e a mira.

**Captura automática:**
- Com `DMC3_DETECT_BLACK=1`, quando o Dante está na missão e mais da metade do quadro sai preto exato, o quadro seguinte é detalhado e capturado.
- Com `DMC3_DUMP_RESOLVES=1`, cada imagem intermediária também é gravada.
- O usuário jogou e o defeito foi capturado 6 vezes (`x360/shots16/preto_*`).

**Diagnóstico:**
- Nessas situações o jogo faz uma composição extra: limpa o RT0, resolve para `A17AE000` e redesenha a cena com duas passadas de tela cheia.
  1. `PS 401CE1F0` com blend `SRC_ALPHA + ONE`;
  2. `PS 401DF380` com blend `CONSTANT_COLOR / ONE_MINUS_CONSTANT_COLOR` e máscara RGB.
- A segunda usa a **cor constante de blend** (`D3DRS_BLENDFACTOR`), que o renderizador nunca aplicava (`glBlendColor` ficava em zero). A passada não desenhava nada e a cena se perdia.
- O cenário estava intacto até o resolve anterior: o resultado seguinte saía 96% preto.

**Correção:**
- A cor constante é lida dos registradores `RB_BLEND_RED..ALPHA` (`device+0x28E0`..`0x28EC`, floats; conferido no `D3DDevice_SetRenderState_BlendFactor`, `0x824727C0`) e aplicada com `glBlendColor`.
- O resumo de cada desenho no quadro detalhado mostra essa cor.

**Também verificado:** o save gravado pelo usuário carrega. O jogo abre direto no Mission Select com a missão 2 liberada.

**Falta:** confirmar jogando que a tela preta não aparece mais. O jogo foi aberto de novo com a detecção ligada (`x360/shots17`).

**Segunda rodada (mesmo dia):** a tela preta continuou. O registro detalhado, agora com o estado de hardware, mostrou a causa principal:
- Na primeira passada de composição (`PS 401CE1F0`), o registrador `RB_BLENDCONTROL` (`device+0x2938`) vale `0x00010001` (ONE/ZERO, sem mistura).
- O renderizador lia o blend de `device+0x2FF8`/`+0x2FFC`, um estado intermediário do D3D que estava desatualizado (`0x00010706`, mistura por alfa). Com alfa zero, a cena entrava com peso zero.

**Correção:** o blend agora vem do registrador `RB_BLENDCONTROL`, com cor e alfa sempre separados; ONE/ADD/ZERO nos dois campos = sem mistura. A cor constante da primeira rodada continua necessária (a segunda passada usa `CONSTANT_COLOR` com `0,97`).

Telas conferidas depois da mudança: aviso, título, LOAD (mostra o save do usuário: Gunslinger Lv1, Missão 01 concluída) e a abertura da Missão 2.

**Confirmado (2026-10-24):** o usuário jogou depois da correção e a tela preta não voltou. A detecção disparou 3 vezes, mas as três capturas (`x360/shots18`) são normais: duas transições 100% pretas e uma área escura com o cenário visível e a barra de vida de um chefe.

## 24. Texturas dos modelos 3D trocadas (2026-10-25)

- **Sintoma:** objetos com pedaços de outra textura do atlas. Exemplo na loja da Missão 1: o tapete vermelho aparecia com as caixas de som.
- **Amostragem:** o renderizador usava clamp e nenhum mipmap para tudo. Agora usa repetição e filtros do fetch constant (dw0 bits 10-15, dw3 bits 19-24) por sampler object, e gera mipmaps para as texturas convertidas.
- **Ordem dos pares de 16 bits** (formatos 16_16, 16_16_16_16 e half):
  - A GPU tira x da metade baixa da palavra já trocada, a regra do Xenia, para qualquer troca de bytes.
  - Os vfetch completados pela declaração do D3D (modelos com esqueleto e tela de título) trocam x↔y e z↔w no swizzle do tipo.
  - Sem essa troca, o shader do cenário (VS 401DB7F0, que lê u do 2º componente) e os shaders com esqueleto (VS 401DA590/401DC9E0, que leem u do 1º) não ficavam certos ao mesmo tempo.
  - Conferido em três casos: tela de título, loja da Missão 1 (comparada com uma captura do jogo original) e personagens.
- **Diagnósticos:**
  - `DMC3_UNLIT` troca a cor iluminada por cinza, para ver só as texturas.
  - `DMC3_DEBUG_VERTICES` lista os vértices nos quadros detalhados.
  - `DMC3_LOG_DECL` registra a declaração e o formato 16 bits por shader.
  - `DMC3_SHOW_SHADERS=ps:END,vs:END` imprime shader e literais no quadro 300.
  - `DMC3_DUMP_TEXTURES` faz as capturas do código gravarem as texturas.
- **Pendente:**
  - O cenário segue mais escuro que no original. A sala só tem luz ambiente (VC21 = 220, 210, 200, em escala 0-255), e o VS multiplica a cor do vértice por 1/255 (literal c255.z). Falta conferir a leitura da cor do vértice (UBYTE4N, 8in32).
  - Faixas de luz de bordas duras nas cutscenes.

## 25. Cenário escuro: sombras por stencil (2026-10-25)

O cenário saía muito mais escuro que no original, com paredes quase pretas. As sombras do jogo usam volumes de sombra em quatro passos:
1. **Cópia da profundidade:** PS 401D45F0 copia a profundidade para a superfície 640x368, desenhando sem render target de cor e gravando gl_FragDepth.
2. **Volumes de sombra:** cerca de 20 DrawVerticesUP com PS = 0 e RB_DEPTHCONTROL 1070C7E3.
   - Stencil de frente e verso separados.
   - Frente soma 1 e verso subtrai 1 quando o teste de z passa (z GEQUAL).
3. **Máscara:** uma passada de tela cheia (00700573) escurece onde o stencil é diferente da referência 0x80 (RB_STENCILREFMASK, device+0x2900; verso em +0x28FC).
4. **Aplicação:** a máscara é aplicada à cena.

O port tinha três falhas nesse caminho:
- **Desenhos sem pixel shader:** eram descartados. Agora usam um fragment shader vazio, sem gravar cor.
- **Superfície só de profundidade:** não existia. O tamanho agora vem do objeto de profundidade, e o renderbuffer é indexado pelo tamanho dele.
- **Teste de stencil:** não era aplicado. Agora usa glStencilFuncSeparate, glStencilOpSeparate e glStencilMaskSeparate, com a frente compensada pela inversão do y.

A causa final do escuro era o **ClearF ler o stencil em r7**. O Z em f1 ocupa a vaga de r7 no ABI do PowerPC, então o stencil vem em r8 (0x80). Com o stencil limpo em 0, todo pixel contava como sombra. Conferido na loja da Missão 1, comparada com a captura do jogo original enviada pelo usuário.

## 26. Faixas de luz e áudio (2026-10-25)

**Faixas de luz das cutscenes:** sumiram com a correção das coordenadas de 16 bits (seção 24). Os polígonos de luz usam o shader do cenário, e com u e v trocados liam só o miolo claro da textura de brilho. Conferido na abertura da Missão 3 (Arkham e Vergil na torre), carregando uma cópia do save e escolhendo a missão no MISSION SELECT.

**Como o áudio funciona no jogo:**
- O motor é o **FMOD Ex**, com codecs Ogg Vorbis e XMA e a saída própria de Xbox 360.
- A música são 154 OGG em `DMC3-1.zip/afs`. Os efeitos são bancos do Capcom Sound Engine em `GData.afs/snd_*.pac` (mais `snd_sys.phd/.bd/.tsb`). Não há uso de XMA.

**O que o port implementa** (`runtime/kernel_audio.cpp`):
- `XAudioRegisterRenderDriverClient` cria uma thread do runtime (`kernel::CreateHostedThread`, nova: código do host com contexto do jogo). Ela chama o callback do FMOD sempre que a fila do SDL3 tem menos de 6 quadros (~32 ms).
- O callback recebe um **ponteiro** para a palavra com o argumento, não o argumento. Passado direto, o FMOD chamava KeSetEvent num objeto inválido e travava.
- `XAudioSubmitRenderDriverFrame` recebe 6 canais × 256 floats big-endian, um canal depois do outro. A organização foi conferida medindo a suavidade do sinal.
  - O ponteiro é **virtual** (buffer em 0x82A77630, na imagem). Traduzi-lo como físico lia memória vazia.
  - A conversão para estéreo põe o centro e os traseiros a -3 dB e descarta o LFE.
- `XAudioGetSpeakerConfig` responde 0x00010001. `XAudioGetVoiceCategoryVolume` responde 1,0.

**Diagnóstico:** o log mostra quadros e pico a cada ~10 s. `DMC3_AUDIO_DUMP=<arquivo>` grava os 6 canais crus.

## 27. Saltos nos shaders e formato dos vídeos (2026-10-25)

**cond_jmp (instrução de controle 11):** o VS 401DABC0 (342 palavras) não era traduzido. Ele desenha duas passadas aditivas sobre os personagens nas cutscenes (blend 000E0106, ~270 e ~170 vértices na altura da cabeça), provavelmente o brilho de cabelo e pele.
- Formato, conferido no `ucode.h` do Xenia: bits 0-12 destino (índice da instrução de controle), 13 incondicional, 14 pelo predicado, 34-41 constante booleana, 42 condição.
- Programas com salto viram uma máquina de estados no GLSL: `while` + `switch (pc)`, um `case` por instrução de controle, e o salto faz `pc = destino; continue`. O GLSL exige um `break` depois do último `case`.
- Programas sem salto continuam em sequência, como antes.

**Vídeos (`Media/wmv`, 29 arquivos, 1,1 GB):** são ASF com vídeo **VC-1 Advanced Profile (WVC1)** e áudio **WMA 10 Professional** (128 kbps, 44 kHz, estéreo). O jogo toca esses arquivos pelo player XMV próprio. O port hoje pula todos (`runtime/video.cpp`).

**Vídeos tocando (player nativo, 2026-10-25):**
- **Decodificação:** `runtime/video_player.cpp` usa o FFmpeg 8.1 (build LGPL compartilhado do BtbN, em `tools/ffmpeg`; as DLLs são copiadas para `recomp/bin` no build).
- **Estrutura:** uma thread decodifica VC-1 para RGBA (swscale) e WMA Pro para estéreo float a 48 kHz (swresample, para um SDL_AudioStream próprio).
- **Fila e sincronia:** no máximo 6 quadros e meio segundo de áudio. O áudio fica pausado até o primeiro quadro aparecer, e daí os quadros seguem o relógio desse início.
- **Tela:** a apresentação (`renderer_gl.cpp`) desenha o quadro por cima do jogo, com faixas pretas.
- **Proporção:** os arquivos declaram pixel 1:1, mas 512 de largura é vídeo do PS2 (512x448 em 4:3), então o pixel é 7:6. O logo sai em 4:3 e a abertura (512x320) em ~1,87:1.
- **Ligação com o jogo** (`runtime/video.cpp`): Open inicia o player. vf10 responde 3 ("tocando") enquanto há vídeo. vf9 (Stop) encerra o player, e é por ele que o jogo pula o vídeo quando o botão é apertado (laço em 0x82338F.., leitura de botões 0x8242C8C0, parada 0x8242BAA0).
- **Testes:** com `DMC3_INPUT_SCRIPT` os vídeos continuam sendo pulados. `DMC3_PLAY_VIDEOS` força tocar, e `DMC3_SKIP_VIDEOS` pula sempre.
