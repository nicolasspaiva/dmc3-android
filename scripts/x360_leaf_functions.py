# Calcula os limites das funções folha (sem entrada na .pdata) que o XenonRecomp
# corta errado e grava na seção "functions" do TOML do recompilador.
#
# O Function::Analyze do XenonRecomp trata o bctr de um switch como fim de função
# e às vezes parte uma função folha em várias. Aqui, numa região sem .pdata:
#   - entradas = início da região, alvos de bl e ponteiros nos dados (vtables etc.)
#     que caem nela;
#   - cada função vai de uma entrada até a próxima (sem o preenchimento de zeros);
#   - se um desvio condicional, um "b" para endereço que não é entrada ou um rótulo
#     de switch sai da função, ela é unida à vizinha.
# Regiões tratadas: as que têm switch e as que o build acusou (rótulos não declarados).
#
# Uso: python -I x360_leaf_functions.py <imagem.pe> <switch_tables.toml> <recomp.toml> [build_log.txt]
import bisect, re, struct, sys

BASE = 0x82000000
BEGIN = '# BEGIN functions (scripts/x360_leaf_functions.py)'
END = '# END functions'


def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    count = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    out = {}
    for i in range(count):
        off = pe + 24 + opt + i * 40
        name = d[off:off + 8].rstrip(b'\0').decode()
        size, rva = struct.unpack_from('<II', d, off + 8)
        out[name] = (BASE + rva, BASE + rva + size)
    return out


def CodeBuiltPointers(word, t0, t1, in_table):
    """endereços de funções montados no código (lis rX,hi + addi rY,rX,lo): ponteiros de callback que não
    aparecem como bl nem nos dados. Só vale se a palavra anterior encerra uma função (blr, b, bctr ou
    preenchimento), para não confundir com o endereço de uma tabela de switch."""
    out = set()
    for a in range(t0, t1, 4):
        w = word(a)
        if w >> 26 != 15 or (w >> 16) & 31 != 0:
            continue
        reg, hi = (w >> 21) & 31, (w & 0xFFFF) << 16
        for b in range(a + 4, min(a + 32, t1), 4):
            x = word(b)
            if x >> 26 == 14 and (x >> 16) & 31 == reg:
                lo = x & 0xFFFF
                target = (hi + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
                if t0 < target < t1 and not target & 3 and not in_table(target):
                    prev = word(target - 4)
                    if prev in (0x4E800020, 0x4E800420, 0) or (prev >> 26 == 18 and not prev & 1):
                        out.add(target)
                break
    return out


def main():
    pe_path, switch_path, toml_path = sys.argv[1:4]
    build_log = sys.argv[4] if len(sys.argv) > 4 else None
    d = open(pe_path, 'rb').read()
    word = lambda a: struct.unpack_from('>I', d, a - BASE)[0]
    sec = sections(d)
    t0, t1 = sec['.text']

    # .pdata: funções com tamanho exato
    p0, p1 = sec['.pdata']
    pdata = []
    for a in range(p0, p1, 8):
        begin, data = struct.unpack_from('>II', d, a - BASE)
        if begin:
            pdata.append((begin, begin + ((data >> 8) & 0x3FFFFF) * 4))
    pdata.sort()

    # regiões sem .pdata dentro de .text
    gaps, cur = [], t0
    for b, e in pdata:
        if b > cur:
            gaps.append((cur, b))
        cur = max(cur, e)
    if cur < t1:
        gaps.append((cur, t1))
    gap_starts = [g[0] for g in gaps]

    def gap_of(a):
        k = bisect.bisect_right(gap_starts, a) - 1
        return gaps[k] if k >= 0 and a < gaps[k][1] else None

    # switches: base (lis) -> rótulos, e as tabelas embutidas no código (não são instruções)
    switches, tables = {}, []
    for block in open(switch_path).read().split('[[switch]]')[1:]:
        base = int(re.search(r'base = 0x([0-9A-F]+)', block).group(1), 16)
        labels = [int(x, 16) for x in re.findall(r'^\s+0x([0-9A-F]+),', block, re.M)]
        switches[base] = labels
        # tabela absoluta logo depois do bctr; as de deslocamento ficam em .rdata
        if word(base + 20) == 0x4E800420:
            tables.append((base + 24, base + 24 + 4 * len(labels)))
    tables.sort()
    table_starts = [t[0] for t in tables]

    def in_table(a):
        k = bisect.bisect_right(table_starts, a) - 1
        return k >= 0 and a < tables[k][1]

    # entradas: alvos de bl e ponteiros para .text guardados nos dados
    entries = set()
    for a in range(t0, t1, 4):
        w = word(a)
        if w >> 26 == 18 and w & 1 and not in_table(a):
            off = w & 0x3FFFFFC
            entries.add(a + (off - (1 << 26) if off & 0x2000000 else off))
    for name in ('.rdata', '.data'):
        s0, s1 = sec[name]
        for a in range(s0, min(s1, BASE + len(d)) - 3, 4):
            v = word(a)
            if t0 <= v < t1 and not v & 3:
                entries.add(v)
    entries |= CodeBuiltPointers(word, t0, t1, in_table)

    # regiões a tratar: as com switch, as que já estão no TOML (vindas de builds anteriores)
    # e as que o build atual acusou
    todo = {gap_of(b) for b in switches if gap_of(b)}
    current = open(toml_path, encoding='utf-8').read()
    if BEGIN in current:
        block = current.split(BEGIN, 1)[1].split(END, 1)[0]
        todo |= {gap_of(int(a, 16)) for a in re.findall(r'address = 0x([0-9A-F]+)', block)} - {None}
    if build_log:
        for f, line in re.findall(r'(ppc_recomp\.\d+\.cpp)\((\d+),\d+\): error: use of undeclared label', open(build_log, errors='replace').read()):
            lines = open(f'{pe_path.rsplit("x360", 1)[0]}recomp/ppc/{f}').read().split('\n')
            i = int(line) - 1
            while not lines[i].startswith('PPC_FUNC_IMPL'):
                i -= 1
            g = gap_of(int(re.search(r'sub_([0-9A-F]+)', lines[i]).group(1), 16))
            if g:
                todo.add(g)

    def targets(start, end):
        """desvios que precisam ficar dentro da função: (endereço, é_b_incondicional)"""
        out = []
        for a in range(start, end, 4):
            if in_table(a):
                continue
            w = word(a)
            op = w >> 26
            if op == 18 and not w & 1:
                off = w & 0x3FFFFFC
                out.append((a + (off - (1 << 26) if off & 0x2000000 else off), True))
            elif op == 16 and not w & 1:
                off = w & 0xFFFC
                out.append((a + (off - 0x10000 if off & 0x8000 else off), False))
            if a in switches:
                out += [(l, False) for l in switches[a]]
        return out

    functions = []
    for g0, g1 in sorted(todo):
        start = g0
        while start < g1 and word(start) == 0:
            start += 4
        cuts = sorted({start} | {e for e in entries if start < e < g1} | {g1})
        # funções candidatas: de um corte ao próximo; une enquanto algum desvio sair
        funcs = [[cuts[i], cuts[i + 1]] for i in range(len(cuts) - 1)]
        changed = True
        while changed:
            changed = False
            for i, (s, e) in enumerate(funcs):
                for t, uncond in targets(s, e):
                    if s <= t < e:
                        continue
                    if uncond and (t in entries or not (g0 <= t < g1)):
                        continue  # chamada de cauda
                    j = next((k for k, f in enumerate(funcs) if f[0] <= t < f[1]), None)
                    if j is None:
                        raise SystemExit(f'desvio em sub_{s:X} para 0x{t:X} fora da região 0x{g0:X}-0x{g1:X}')
                    lo, hi = min(i, j), max(i, j)
                    funcs[lo:hi + 1] = [[funcs[lo][0], funcs[hi][1]]]
                    changed = True
                    break
                if changed:
                    break
        for s, e in funcs:
            while e > s and word(e - 4) == 0:
                e -= 4
            if e > s:
                functions.append((s, e - s))

    text = open(toml_path, encoding='utf-8').read()
    block = [BEGIN, f'# {len(functions)} funções folha em {len(todo)} regiões sem .pdata', 'functions = [']
    block += [f'    {{ address = 0x{a:X}, size = 0x{n:X} }},' for a, n in functions]
    block += [']', END]
    if BEGIN in text:
        text = re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END), '\n'.join(block), text, flags=re.S)
    else:
        text = text.rstrip('\n') + '\n\n' + '\n'.join(block) + '\n'
    open(toml_path, 'w', encoding='utf-8').write(text)
    print(f'{len(todo)} regiões, {len(functions)} funções gravadas em {toml_path}')


if __name__ == '__main__':
    main()
