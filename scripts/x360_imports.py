# Lista as importações do XEX (xboxkrnl.exe / xam.xex): ordinal, nome, tipo e endereço
# do registro na imagem. Gera também, para o runtime:
#   - imports_gen.cpp: um stub que registra a chamada para cada função importada ainda não
#     implementada (as implementadas são as que aparecem como KERNEL_IMPL(Nome) em runtime/);
#   - a tabela das variáveis importadas, cujos registros o runtime preenche com um ponteiro.
# Uso: python -I x360_imports.py <DMC3.xex> <pasta XenonUtils/xbox> <pasta runtime> <saida.txt>
import glob, os, re, struct, sys

XEX_HEADER_IMPORT_LIBRARIES = 0x000103FF


def export_names(inc_path):
    out = {}
    for m in re.finditer(r'XE_EXPORT\(\w+,\s*0x([0-9A-Fa-f]+),\s*(\w+),\s*(\w+)\)', open(inc_path).read()):
        out[int(m.group(1), 16)] = (m.group(2), m.group(3))
    return out


def main():
    xex_path, xbox_dir, runtime_dir, out_txt = sys.argv[1:5]
    x = open(xex_path, 'rb').read()
    n = struct.unpack_from('>I', x, 0x14)[0]
    headers = dict(struct.unpack_from('>II', x, 0x18 + i * 8) for i in range(n))
    off = headers[XEX_HEADER_IMPORT_LIBRARIES]
    _, str_size, lib_count = struct.unpack_from('>III', x, off)
    strings, p = [], off + 12
    for _ in range(lib_count):
        end = x.index(b'\0', p)
        strings.append(x[p:end].decode())
        p += ((end - p + 1) + 3) & ~3
    tables = {'xboxkrnl.exe': export_names(os.path.join(xbox_dir, 'xboxkrnl_table.inc')),
              'xam.xex': export_names(os.path.join(xbox_dir, 'xam_table.inc'))}

    pe = open(xex_path.replace('.xex', '.pe'), 'rb').read()
    word = lambda a: struct.unpack_from('>I', pe, a - 0x82000000)[0]

    lib = off + 12 + str_size
    records = []
    for _ in range(lib_count):
        size, = struct.unpack_from('>I', x, lib)
        name_index, count = struct.unpack_from('>HH', x, lib + 0x24)
        libname = strings[name_index]
        for k in range(count):
            slot = struct.unpack_from('>I', x, lib + 0x28 + 4 * k)[0]
            v = word(slot)
            kind, ordinal = v >> 24, v & 0xFFFF
            name, declared = tables.get(libname, {}).get(ordinal, (f'{libname}_{ordinal:X}', '?'))
            records.append((libname, ordinal, name, 'thunk' if kind else 'variable', declared, slot))
        lib += size

    # registros tipo 1 são os thunks das funções. Os tipo 0 são de dois jeitos: variáveis de
    # verdade (kVariable), que o runtime aloca, e o ponteiro que acompanha cada função, que
    # recebe o endereço do thunk
    functions = sorted({(l, o, nm) for l, o, nm, k, _, _ in records if k == 'thunk'}, key=lambda r: r[2])
    thunk_of = {(l, o): slot for l, o, _, k, _, slot in records if k == 'thunk'}
    variables = sorted([r for r in records if r[3] == 'variable' and r[4] != 'kFunction'], key=lambda r: r[2])
    pointers = sorted([(slot, thunk_of[(l, o)], nm) for l, o, nm, k, decl, slot in records
                       if k == 'variable' and decl == 'kFunction' and (l, o) in thunk_of])

    implemented = set()
    for f in glob.glob(os.path.join(runtime_dir, '*.cpp')):
        implemented |= set(re.findall(r'KERNEL_IMPL\((\w+)\)', open(f, encoding='utf-8', errors='replace').read()))

    with open(out_txt, 'w', encoding='utf-8') as o:
        o.write(f'{len(functions)} funções, {len(variables)} variáveis importadas, {len(pointers)} ponteiros de função\n\n')
        for l, ordn, nm in functions:
            o.write(f'func {l:13} {ordn:4X} {nm}{"  [implementada]" if nm in implemented else ""}\n')
        for l, ordn, nm, _, decl, slot in variables:
            o.write(f'var  {l:13} {ordn:4X} {nm} @ 0x{slot:X} ({decl})\n')

    gen = ['// Gerado por scripts/x360_imports.py. Não editar: implemente com KERNEL_IMPL(Nome).',
           '#include "kernel.h"', '']
    for l, ordn, nm in functions:
        if nm not in implemented:
            gen.append(f'PPC_FUNC(__imp__{nm}) {{ kernel::Unimplemented("{nm}", ctx); }}')
    gen += ['', 'const kernel::ImportedVariable kernel::ImportedVariables[] = {']
    gen += [f'    {{ 0x{slot:X}, "{nm}", 0x{ordn:X}, "{l}" }},' for l, ordn, nm, _, _, slot in variables]
    gen += ['    { 0, nullptr, 0, nullptr },', '};', '']
    gen += ['const kernel::ImportedPointer kernel::ImportedPointers[] = {']
    gen += [f'    {{ 0x{slot:X}, 0x{thunk:X} }}, // {nm}' for slot, thunk, nm in pointers]
    gen += ['    { 0, 0 },', '};', '']
    open(os.path.join(runtime_dir, 'imports_gen.cpp'), 'w', encoding='utf-8').write('\n'.join(gen))
    print(f'{len(functions)} funções ({len(implemented & {f[2] for f in functions})} implementadas), '
          f'{len(variables)} variáveis, {len(pointers)} ponteiros de função')


if __name__ == '__main__':
    main()
