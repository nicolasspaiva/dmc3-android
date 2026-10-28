# Traduz offsets (rva) do DMC3.exe para a função que os contém, usando o map do linker.
# Uso: python -I host_where.py <DMC3.map> <rva em hex> [<rva> ...]
import bisect, re, sys


def main():
    map_path, rvas = sys.argv[1], [int(x, 16) for x in sys.argv[2:]]
    text = open(map_path, errors='replace').read()
    base = int(re.search(r'Preferred load address is ([0-9a-fA-F]+)', text).group(1), 16)
    symbols = []
    # linhas: " 0001:00012340       nome      00000001400123a0 f   obj"
    for m in re.finditer(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s', text, re.M):
        symbols.append((int(m.group(2), 16) - base, m.group(1)))
    symbols.sort()
    starts = [s[0] for s in symbols]
    for rva in rvas:
        k = bisect.bisect_right(starts, rva) - 1
        name = symbols[k][1] if k >= 0 else '?'
        print(f'{rva:X}: {name} +0x{rva - symbols[k][0]:X}')


if __name__ == '__main__':
    main()
