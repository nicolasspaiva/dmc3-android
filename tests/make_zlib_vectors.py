# Gera casos de teste de deflate cru (windowBits = -15, o formato que o jogo usa dentro dos zips).
# Formato do arquivo: repetido { u32 tamanho original, u32 tamanho comprimido, original, comprimido } (little-endian).
# Uso: python -I make_zlib_vectors.py <saida.bin>
import random, struct, sys, zlib


def cases():
    rnd = random.Random(1234)
    text = b''.join(b'Devil May Cry 3 - Dante %d style %d\n' % (i, i % 6) for i in range(3000))
    yield 'texto', text
    yield 'repetição', b'ABCD' * 50000
    yield 'aleatório', bytes(rnd.getrandbits(8) for _ in range(70000))
    mixed = bytearray()
    for i in range(400):
        mixed += bytes(rnd.getrandbits(8) for _ in range(rnd.randrange(1, 200)))
        mixed += bytes([rnd.getrandbits(8)]) * rnd.randrange(1, 300)
        mixed += text[rnd.randrange(0, 1000):][:rnd.randrange(10, 500)]
    yield 'misto', bytes(mixed)
    # parecido com linhas de PNG: byte de filtro + pixels com gradiente
    rows = bytearray()
    for y in range(256):
        rows.append(y % 5)
        rows += bytes(((x * 3 + y) & 0xFF) for x in range(1024))
    yield 'png', bytes(rows)


def main():
    out = bytearray()
    n = 0
    for name, data in cases():
        for level in (0, 1, 6, 9):
            c = zlib.compressobj(level, zlib.DEFLATED, -15)
            comp = c.compress(data) + c.flush()
            # cada caso duas vezes: o runtime testa os pares de uma vez e os ímpares em pedaços
            out += (struct.pack('<II', len(data), len(comp)) + data + comp) * 2
            n += 2
    open(sys.argv[1], 'wb').write(out)
    print(f'{n} casos, {len(out)} bytes')


if __name__ == '__main__':
    main()
