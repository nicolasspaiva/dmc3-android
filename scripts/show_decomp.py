# Mostra o pseudocódigo do Ghidra de funções de um arquivo exportado por X360DecompileRange.java,
# sem as declarações de variáveis locais.
# Uso: python -I show_decomp.py <arquivo.c> <linhas máximas> <hex> [<hex> ...]
import re, sys

DECL = re.compile(r'^\s+(undefined|int|uint|ulonglong|longlong|bool|byte|char|float|double|short|ushort|code)\w*\s+\**\w+(\s*\[\d+\])?;$')


def main():
    path, limit, addrs = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
    src = open(path, encoding='utf-8').read()
    for a in addrs:
        m = re.search(r'// ==== (\S+) @ %s\n(.*?)(?=\n// ==== |\Z)' % a.lower(), src, re.S)
        print(f'===== {a} ({m.group(1) if m else "?"})')
        if not m:
            continue
        lines = [l for l in m.group(2).split('\n') if l.strip() and not DECL.match(l) and l.strip() not in ('{', '}')]
        print('\n'.join(lines[:limit]))


if __name__ == '__main__':
    main()
