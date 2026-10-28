# Converte a captura de tela do runtime ("RGBA" + largura + altura + linhas de baixo para cima) em PNG.
# Uso: python -I rgba_to_png.py <entrada.rgba> <saida.png>
import struct, sys, zlib


def main():
    data = open(sys.argv[1], 'rb').read()
    assert data[:4] == b'RGBA'
    w, h = struct.unpack('<II', data[4:12])
    # a tela não usa o alfa do framebuffer: grava opaco para o PNG não sair transparente
    pixels = bytearray(data[12:])
    pixels[3::4] = b'\xff' * (w * h)
    pixels = bytes(pixels)
    rows = [pixels[(h - 1 - y) * w * 4:(h - y) * w * 4] for y in range(h)]   # OpenGL lê de baixo para cima
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(sys.argv[2], 'wb').write(png)
    print(f'{sys.argv[2]}: {w}x{h}')


if __name__ == '__main__':
    main()
