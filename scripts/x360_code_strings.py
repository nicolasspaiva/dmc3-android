# Strings referenciadas pelo código numa faixa de endereços: procura pares "lis rX, hi" seguidos
# (até 8 instruções depois) de "addi rY, rX, lo" ou de um load/store com base rX, cujo endereço
# cai numa string ASCII de .rdata/.data. Mostra a função (da lista de funções) que faz a referência.
# Uso: python -I x360_code_strings.py <imagem.pe> <callgraph.json> <hex início> <hex fim>
import bisect, json, re, struct, sys

BASE = 0x82000000


def main():
    pe_path, graph_path, lo, hi = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
    d = open(pe_path, 'rb').read()
    word = lambda a: struct.unpack_from('>I', d, a - BASE)[0]
    funcs = sorted(int(k, 16) for k in json.load(open(graph_path)))

    def string_at(a):
        if not (BASE <= a < BASE + len(d)):
            return None
        m = re.match(rb'[\x20-\x7e\t\n]{4,200}\0', d[a - BASE:a - BASE + 201])
        if not m or (a > BASE and 0x20 <= d[a - BASE - 1] <= 0x7e):
            return None
        return m.group(0)[:-1].decode()

    found = {}
    for a in range(lo, hi, 4):
        w = word(a)
        if w >> 26 != 15 or (w >> 16) & 31 != 0:
            continue
        reg, high = (w >> 21) & 31, (w & 0xFFFF) << 16
        for b in range(a + 4, min(a + 36, hi), 4):
            x = word(b)
            op, ra = x >> 26, (x >> 16) & 31
            if ra != reg or op not in (14, 32, 34, 36, 38, 40, 44, 48, 52):
                continue
            low = x & 0xFFFF
            target = (high + (low - 0x10000 if low & 0x8000 else low)) & 0xFFFFFFFF
            s = string_at(target)
            if s:
                f = funcs[bisect.bisect_right(funcs, a) - 1]
                found.setdefault(f, set()).add(s)
            break
    for f in sorted(found):
        print(f'{f:08X}: ' + ' | '.join(sorted(found[f]))[:300])


if __name__ == '__main__':
    main()
