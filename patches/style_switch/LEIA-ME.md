# Troca de estilo em tempo real — DMC3 SE (PS2, SLUS-21361)

Patch para **PCSX2** (PC) e **NetherSX2/AetherSX2** (Android) que deixa trocar o estilo do Dante durante a missão, sem voltar à estátua, como o *Free Style* da versão de Switch.

| Direcional (durante a missão) | Estilo |
|---|---|
| ↑ | Trickster |
| → | Swordmaster |
| ← | Gunslinger |
| ↓ | Royal Guard |

- Cada estilo mantém o próprio nível e experiência.
- As animações do estilo novo são carregadas e o ícone da HUD muda junto.
- Durante a missão com o Dante, os atalhos originais do direcional (telas de status) ficam desativados. Na pausa, nos menus e com o Vergil, o direcional funciona normalmente.

## Instalação

O arquivo `SLUS-21361_25FC361B.pnach` vai na pasta de cheats do emulador:

- **PCSX2 2.x (Windows):** `Documentos\PCSX2\cheats\`. Em *Propriedades do jogo › Cheats*, ative os cheats e marque **Style Switch (D-pad)** e, se quiser o som, **Style Switch - sound**.
- **NetherSX2 (Android):** `<pasta do NetherSX2>/cheats/`. Ative os cheats nas propriedades do jogo.

O número `25FC361B` é o CRC do executável. Se o emulador mostrar outro CRC para o seu jogo, renomeie o arquivo para `SLUS-21361_<CRC>.pnach`.

## Como funciona

A chamada `jal frame_Begin` do loop principal (`0x001EC468`) passa a chamar uma rotina de 190 instruções em `0x000FF000`. Ela roda logo depois da leitura do controle e antes da lógica do quadro:

1. Confere se há missão em andamento (`CSceneGameMain` estado 1) e se o jogador é o Dante.
2. Atualiza a tabela de animações de estilo do Dante (`jogador + 0x243C + 4×pacote`, pacotes 13–26) com `FUN_001AEFC0`, como o jogo faz ao criar o Dante. Pacote carregado → ponteiro válido; pacote ausente → 0. Sem isso o Dante fica em T-pose depois da troca.
3. Apaga o direcional do controle 1, para o jogo não abrir as telas de status.
4. Se um direcional foi apertado e nenhuma ação de estilo está em andamento, troca o estilo usando as funções do próprio jogo:

| Etapa | Função do jogo | Origem |
|---|---|---|
| Guarda nível/EXP do estilo antigo e carrega os do novo | — | dados de save |
| Restaura as flags de golpes ativas a partir dos golpes comprados | cópia `+0x29F8` → `+0x29D8` | setup do Dante (`FUN_0025b420`) |
| Libera os golpes do estilo conforme o nível | `FUN_0025b8c0` (vtable do Dante `+0x148`) | setup do Dante |
| Carrega o pacote de animações do estilo novo e descarrega o antigo | `FUN_00203f20(0, save+0x104)` | troca de estilo na Divinity Statue |
| Troca o ícone da HUD | `FUN_0013d660` + `CDraw::vf9` + `CDraw::vf13` | criação da HUD (`FUN_0030e9c0`) |
| Toca o som de confirmação de estilo (opcional) | `FUN_0033e630` | menu de estilo |

### Endereços usados

| Endereço / campo | Significado |
|---|---|
| `*(0x01D0D70C)` = G | `CSceneGameMain + 0x140`; `G+0x0C` jogador, `G+0x24` cena |
| cena `+0x18` / `+0x18E90` | estado da cena (1 = missão) / HUD |
| HUD `+0x38` / `+0x554A` / `+0x30C0` | pacote da HUD / índice do ícone / modelo do ícone |
| `0x005F5BB8` | tabela estilo → ícone |
| jogador `+0x3C6C` / `+0x3C6E` / `+0x3C74` / `+0x3C7C` | estilo / ação de estilo / nível / EXP |
| `*(0x01D0D710)` | dados de save: `+0xC5[6]` níveis, `+0xCC[6]` EXP, `+0x104` registro de setup, `+0x116`/`+0x117`/`+0x138` estilo/nível/EXP atuais |
| `0x006C3740/44/46` | controle 1: segurados / apertados / soltos no quadro |

## Estado de teste

- **Verificação da montagem:** as instruções comuns foram conferidas com o desmontador Capstone. As 8 instruções de 128 bits do PS2 (`lq`/`sq`) foram conferidas contra o próprio jogo, que usa exatamente essas palavras centenas de vezes.
- **PCSX2 2.2 (Windows), 2026-10-21:** versão 5 testada; troca de estilo, animações, ícone da HUD e execução dos golpes do Swordmaster funcionando.
- **NetherSX2 (Android):** ainda não testado.

## Limitações conhecidas

- **Quicksilver e Doppelganger** não estão no direcional; dá para sair deles para outro estilo.
- As animações vêm do disco: logo após a troca, por uma fração de segundo, um golpe de estilo pode sair sem animação.
- Sons específicos de golpes de estilo podem faltar, porque o jogo os carrega por outro caminho.
- Se o jogo regravar o save inteiro a partir da cópia do início da missão, a EXP ganha nos estilos usados antes da última troca pode não ser salva.

## Histórico

- **v1:** gancho em `frame_End`, só trocava o número do estilo; o direcional também abria as telas de status.
- **v2:** gancho movido para `frame_Begin` e direcional escondido do jogo; faltavam animações e ícone.
- **v3:** carrega as animações, libera os golpes e atualiza o ícone da HUD com as funções do próprio jogo; Dante ainda em T-pose nos golpes do estilo novo.
- **v4:** atualiza a cada quadro a tabela de ponteiros de animação de estilo do Dante, que o jogo só preenche ao criar o personagem.
- **v5:** antes de liberar os golpes do nível, restaura as flags de golpes ativas (`+0x29D8`, 8 palavras) a partir da cópia dos golpes comprados (`+0x29F8`); antes, os golpes de nível de todos os estilos usados se acumulavam (bugs na execução do Swordmaster).

## Recompilar

```
python -I build_pnach.py ../../ps2/SLUS_213.61 .
```
O script recusa gerar o patch se as instruções nos endereços do gancho não forem as esperadas (versão diferente do jogo).
