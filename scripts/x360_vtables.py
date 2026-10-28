# Recupera as vtables do executável do 360 pelo RTTI do MSVC (ponteiros de 32 bits, big-endian):
#   TypeDescriptor { pVFTable, spare, char name[] = ".?AVNome@@" }
#   CompleteObjectLocator { signature = 0, offset, cdOffset, pTypeDescriptor, pClassHierarchyDescriptor }
#   vtable[-1] = &CompleteObjectLocator; vtable[0..] = métodos
# Saída: TSV "endereço da função  Classe::vfN  offset do subobjeto".
# Uso: python -I x360_vtables.py <imagem.pe> <saida.tsv>
import re, struct, sys

BASE = 0x82000000


def main():
    pe_path, out_path = sys.argv[1:3]
    d = open(pe_path, 'rb').read()
    word = lambda a: struct.unpack_from('>I', d, a - BASE)[0]
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    count = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    sec = {}
    for i in range(count):
        off = pe + 24 + opt + i * 40
        size, rva = struct.unpack_from('<II', d, off + 8)
        sec[d[off:off + 8].rstrip(b'\0').decode()] = (BASE + rva, BASE + rva + size)
    t0, t1 = sec['.text']
    r0, r1 = sec['.rdata']
    data_ranges = [sec['.rdata'], sec['.data']]
    in_data = lambda a: any(lo <= a < hi for lo, hi in data_ranges)

    # TypeDescriptors: nome ".?AV...@@" precedido de 8 bytes
    types = {}
    for m in re.finditer(rb'\.\?A[VU]([\w@$?]+?)@@\0', d):
        types[BASE + m.start() - 8] = m.group(1).decode()

    # COLs em .rdata
    cols = {}
    for a in range(r0, r1 - 20, 4):
        if word(a) == 0 and word(a + 12) in types and in_data(word(a + 16)):
            cols[a] = (types[word(a + 12)], word(a + 4))

    rows, tables = [], 0
    for a in range(r0, r1 - 4, 4):
        col = word(a)
        if col not in cols:
            continue
        name, offset = cols[col]
        vt = a + 4
        n = 0
        while vt + 4 * n < r1:
            f = word(vt + 4 * n)
            if not (t0 <= f < t1) or (n > 0 and word(vt + 4 * n) in cols):
                break
            rows.append((f, f'{name}::vf{n}', offset, vt))
            n += 1
        if n:
            tables += 1
    with open(out_path, 'w') as o:
        o.write('function\tname\tsubobject_offset\tvtable\n')
        for f, nm, off, vt in sorted(rows):
            o.write(f'{f:08X}\t{nm}\t{off}\t{vt:08X}\n')
    print(f'{len(types)} tipos, {len(cols)} COLs, {tables} vtables, {len(rows)} slots')


if __name__ == '__main__':
    main()
