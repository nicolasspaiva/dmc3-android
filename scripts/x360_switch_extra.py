# Completa a tabela de switches do XenonAnalyse para executáveis VC2010 (DMC3 360).
# O XenonAnalyse só acha o tamanho da tabela quando o "cmplwi + bgt" fica logo antes
# do salto. Aqui, para cada "lis, rlwinm, addi, lwzx, mtctr, bctr" que ficou de fora,
# a tabela é lida logo depois do bctr e termina onde o código volta (menor rótulo).
# Uso: python -I x360_switch_extra.py <imagem.pe> <saida_xenonanalyse.toml> <saida_final.toml>
import re, struct, sys

BASE = 0x82000000


def main():
    pe_path, analyse_path, out_path = sys.argv[1:4]
    d = open(pe_path, 'rb').read()
    word = lambda a: struct.unpack_from('>I', d, a - BASE)[0]
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    sections = pe + 24 + opt
    count = struct.unpack_from('<H', d, pe + 6)[0]
    text = None
    for i in range(count):
        if d[sections + i * 40:sections + i * 40 + 8].rstrip(b'\0') == b'.text':
            size, rva = struct.unpack_from('<II', d, sections + i * 40 + 8)
            text = (BASE + rva, BASE + rva + size)

    # funções da .pdata, para limitar a tabela à função que contém o switch
    pdata = []
    for i in range(count):
        if d[sections + i * 40:sections + i * 40 + 8].rstrip(b'\0') == b'.pdata':
            size, rva = struct.unpack_from('<II', d, sections + i * 40 + 8)
            for a in range(BASE + rva, BASE + rva + size, 8):
                begin, info = struct.unpack_from('>II', d, a - BASE)
                if begin:
                    pdata.append((begin, begin + ((info >> 8) & 0x3FFFFF) * 4))
    pdata.sort()

    def function_range(a):
        """função da .pdata que contém a; fora dela, a região entre duas funções"""
        lo, hi = text
        for b, e in pdata:
            if b <= a < e:
                return b, e
            if e <= a:
                lo = max(lo, e)
            elif b > a:
                hi = min(hi, b)
                break
        return lo, hi

    def bound_confirmed(fstart, fend, n):
        """existe um "cmplwi crX, rY, n-1" na função?"""
        return any(word(a) >> 26 == 10 and word(a) & 0xFFFF == n - 1 for a in range(fstart, fend, 4))

    analysed = open(analyse_path).read()
    known = {int(x, 16) for x in re.findall(r'base = 0x([0-9A-F]+)', analysed)}
    unconfirmed = []

    extra, skipped = [], []
    for bctr in range(text[0] + 20, text[1], 4):
        if word(bctr) != 0x4E800420:
            continue
        lis, rlw, addi, lwzx = (word(bctr - k) for k in (20, 16, 12, 8))
        if not (lis >> 26 == 15 and rlw >> 26 == 21 and addi >> 26 == 14
                and lwzx >> 26 == 31 and (lwzx >> 1) & 0x3FF == 23):
            continue
        base = bctr - 20
        if base in known:
            continue
        hi = (lis & 0xFFFF) << 16
        lo = addi & 0xFFFF
        table = (hi + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
        reg = (rlw >> 21) & 31
        if table != bctr + 4:
            skipped.append((base, 'tabela fora do lugar'))
            continue
        labels, a = [], table
        while a < text[1]:
            v = word(a)
            if not (text[0] <= v < text[1]) or v & 3 or (labels and a >= min(labels)):
                break
            labels.append(v)
            a += 4
        if not labels or min(labels) != a:
            # rótulos também antes da tabela: ela termina na primeira palavra que não
            # é endereço dentro da função, ou num endereço que já é rótulo
            fstart, fend = function_range(base)
            labels, a = [], table
            while a < fend:
                v = word(a)
                if not (fstart <= v < fend) or v & 3 or a in labels:
                    break
                labels.append(v)
                a += 4
            if not labels or any(table <= l < a for l in labels):
                skipped.append((base, 'fim da tabela incerto'))
                continue
            if not bound_confirmed(fstart, fend, len(labels)):
                unconfirmed.append(base)
        extra.append((base, reg, labels))

    # tabelas de deslocamento em .rdata (bibliotecas), com um nop no meio que o XenonAnalyse não aceita:
    #   byte:  lis, addi, lbzx r0,r12,rI, rlwinm r0,r0,2, lis, nop, addi, add, mtctr, bctr -> base + (b << 2)
    #   short: lis, rlwinm r0,rI,1, addi, lhzx r0,r12,r0, lis, addi, nop, add, mtctr, bctr -> base + h
    def simm(w):
        v = w & 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    def is_x(w, xo):
        return w >> 26 == 31 and (w >> 1) & 0x3FF == xo

    offset_extra = []
    for bctr in range(text[0] + 40, text[1], 4):
        if word(bctr) != 0x4E800420:
            continue
        w = [word(bctr - 40 + 4 * i) for i in range(10)]   # w[0] = bc, w[9] = mtctr
        if w[1] >> 26 == 15 and w[2] >> 26 == 14 and is_x(w[3], 87) and w[6] == 0x60000000:      # byte
            kind, start = 'byte', bctr - 36
            table = ((w[1] & 0xFFFF) << 16) + simm(w[2])
            index = (w[3] >> 11) & 31
            hi, lo = w[5], w[7]
        elif w[1] >> 26 == 15 and w[2] >> 26 == 21 and is_x(w[4], 279) and w[7] == 0x60000000:  # short
            kind, start = 'short', bctr - 36
            table = ((w[1] & 0xFFFF) << 16) + simm(w[3])
            index = (w[2] >> 21) & 31
            hi, lo = w[5], w[6]
        else:
            continue
        # o limite (cmplwi) pode ter outras instruções entre ele e o bc
        cmp = next((word(a) for a in range(bctr - 44, bctr - 64, -4) if word(a) >> 26 == 10), 0)
        if cmp >> 26 != 10:
            skipped.append((start, f'switch {kind} sem cmplwi antes do desvio'))
            continue
        count = (cmp & 0xFFFF) + 1
        target_base = (((hi & 0xFFFF) << 16) + simm(lo)) & 0xFFFFFFFF
        if kind == 'byte':
            labels = [target_base + (d[table - BASE + i] << 2) for i in range(count)]
        else:
            labels = [target_base + struct.unpack_from('>H', d, table - BASE + 2 * i)[0] for i in range(count)]
        offset_extra.append((start, index, labels))

    out = [analysed.rstrip('\n'), '', '# ---- ABSOLUTE JUMPTABLE (x360_switch_extra.py) ----']
    for base, reg, labels in extra:
        out += ['[[switch]]', f'base = 0x{base:X}', f'r = {reg}', 'labels = [']
        out += [f'    0x{l:X},' for l in labels] + [']', '']
    out += ['# ---- OFFSET JUMPTABLE com nop (x360_switch_extra.py) ----']
    for base, reg, labels in offset_extra:
        out += ['[[switch]]', f'base = 0x{base:X}', f'r = {reg}', 'labels = [']
        out += [f'    0x{l:X},' for l in labels] + [']', '']
    open(out_path, 'w').write('\n'.join(out) + '\n')
    print(f'{len(known)} do XenonAnalyse, {len(extra)} acrescentados, {len(offset_extra)} de deslocamento, {len(skipped)} sem solução')
    for base, why in skipped:
        print(f'  0x{base:X}: {why}')
    if unconfirmed:
        print(f'{len(unconfirmed)} sem "cmplwi" que confirme o tamanho: ' + ', '.join(f'0x{b:X}' for b in unconfirmed))


if __name__ == '__main__':
    main()
