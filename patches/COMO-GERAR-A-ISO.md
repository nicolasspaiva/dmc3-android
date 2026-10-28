# Como gerar a ISO com os mods

`build_iso.py` cria uma **cópia** da ISO original com os mods embutidos no executável. A ISO original nunca é alterada. Toda vez que você mudar um mod, rode o build de novo: ele sempre parte da ISO original, então nunca se aplica patch em cima de patch.

## Gerar

```
python -I build_iso.py "<ISO original>" "../build/DMC3 SE - Style Switch.iso"
```

Opções:
- `--no-sound`: gera sem o som de confirmação ao trocar de estilo.

Saída (exemplo da build atual):

```
SLUS_213.61: 5352880 -> 5354916 bytes, moved from LBA 276 to 2153680
mod: 261 instructions at 0xff000, hook at 0x1ec468, sound on
new executable CRC (PCSX2): 2221DE71
```

## Como funciona

1. Lê o `SLUS_213.61` da ISO original e confere que é a versão esperada (instruções nos endereços do gancho).
2. Monta a rotina do mod a partir de `style_switch/build_pnach.py` (o mesmo código-fonte do `.pnach`).
3. No executável:
   - reaproveita o segmento de carga vazio (program header 1, 0 bytes) para carregar o mod em `0x000FF000`;
   - troca o `jal frame_Begin` do `main` (`0x001EC468`) por `jal 0x000FF000`.
4. Copia a ISO, grava o executável novo **no fim** da cópia e atualiza a entrada do diretório ISO9660 e o tamanho do volume. O executável original vem colado ao `IOPRP280.IMG`, sem espaço para crescer; os outros arquivos não mudam de lugar.

## Depois de gerar

- **CRC novo:** o executável muda, então o emulador vê outro CRC (`2221DE71` na build atual). Patches e cheats presos ao CRC original (`25FC361B`) não se aplicam à ISO nova.
  - O widescreen 16:9 já foi copiado para `../build/SLUS-21361_2221DE71.pnach` e instalado em `Documentos\PCSX2\patches\`.
  - **Não** ative o cheat "Style Switch (D-pad)" na ISO nova: o mod já está dentro dela.
- **Android (NetherSX2):** copie a ISO nova para o celular. Para ter widescreen, copie também `SLUS-21361_2221DE71.pnach` para a pasta `patches` do NetherSX2.
- **Uso pessoal:** a ISO gerada contém o jogo; não compartilhe.

## Conferir uma build

```
python -I ../scripts/iso9660.py "../build/DMC3 SE - Style Switch.iso" list
python -I ../scripts/iso9660.py "../build/DMC3 SE - Style Switch.iso" extract /SLUS_213.61 ../build/SLUS_213.61.patched
```

Na build atual: só 16 bytes do executável original mudaram (cabeçalho do segmento reaproveitado e o gancho), e as 193 palavras do `.pnach` testado batem com o executável gerado.
