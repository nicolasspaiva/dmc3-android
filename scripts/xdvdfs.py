# Leitor de XDVDFS (sistema de arquivos dos discos do Xbox 360).
# Uso:
#   python -I xdvdfs.py <iso> list
#   python -I xdvdfs.py <iso> extract <caminho/no/disco> <destino>
#   python -I xdvdfs.py <iso> extractdir <pasta/no/disco> <destino>
import os, struct, sys

SECTOR = 2048
PARTITIONS = (0x0, 0x2080000, 0xFD90000, 0x18300000)  # XGD1, XGD3, XGD2, ...


def find_partition(f):
    for off in PARTITIONS:
        f.seek(off + 32 * SECTOR)
        if f.read(20) == b'MICROSOFT*XBOX*MEDIA':
            return off
    raise SystemExit('volume XDVDFS não encontrado')


def read_dir(f, base, sector, size, prefix, out):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    # a tabela é uma árvore binária; lê todos os nós alcançáveis a partir do 0
    seen, stack = set(), [0]
    while stack:
        pos = stack.pop()
        if pos in seen or pos + 14 > len(data):
            continue
        seen.add(pos)
        left, right, sec, length, attr, nlen = struct.unpack_from('<HHIIBB', data, pos)
        if left == 0xFFFF:
            continue
        name = data[pos + 14:pos + 14 + nlen].decode('latin-1')
        path = prefix + name
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)
        if attr & 0x10:
            out.append((path + '/', sec, length, True))
            if length:
                read_dir(f, base, sec, length, path + '/', out)
        else:
            out.append((path, sec, length, False))


def walk(f):
    base = find_partition(f)
    f.seek(base + 32 * SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    out = []
    read_dir(f, base, root_sec, root_size, '', out)
    return base, sorted(out)


def copy(f, base, sec, length, dest):
    os.makedirs(os.path.dirname(dest) or '.', exist_ok=True)
    f.seek(base + sec * SECTOR)
    with open(dest, 'wb') as o:
        left = length
        while left:
            chunk = f.read(min(left, 1 << 24))
            o.write(chunk)
            left -= len(chunk)


def main():
    iso, cmd = sys.argv[1], sys.argv[2]
    with open(iso, 'rb') as f:
        base, entries = walk(f)
        if cmd == 'list':
            total = 0
            for path, sec, length, isdir in entries:
                if not isdir:
                    total += length
                    print(f'{length:12d} {sec:9d} /{path}')
            print(f'{sum(not e[3] for e in entries)} arquivos, {total / 1e9:.2f} GB')
        elif cmd == 'extract':
            want = sys.argv[3].strip('/').lower()
            for path, sec, length, isdir in entries:
                if path.lower() == want:
                    copy(f, base, sec, length, sys.argv[4])
                    return
            raise SystemExit('não encontrado: ' + want)
        elif cmd == 'extractdir':
            want = sys.argv[3].strip('/').lower() + '/'
            n = 0
            for path, sec, length, isdir in entries:
                if not isdir and path.lower().startswith(want):
                    copy(f, base, sec, length, os.path.join(sys.argv[4], path[len(want):]))
                    n += 1
            print(f'{n} arquivos extraídos')


if __name__ == '__main__':
    main()
