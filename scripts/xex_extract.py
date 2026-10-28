# Converte um XEX2 (Xbox 360) no PE original: descriptografa (AES-128) e
# desfaz a compressão "basic". A compressão LZX ("normal") não é suportada.
# Usa o AES do próprio Windows (bcrypt.dll), sem bibliotecas externas.
# Uso: python -I xex_extract.py <entrada.xex> <saida.pe>
import ctypes, struct, sys

RETAIL_KEY = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
DEVKIT_KEY = bytes(16)


def aes(key, data, mode):
    bc = ctypes.WinDLL('bcrypt')
    alg, hkey = ctypes.c_void_p(), ctypes.c_void_p()
    assert bc.BCryptOpenAlgorithmProvider(ctypes.byref(alg), 'AES', None, 0) == 0
    m = ('ChainingModeECB' if mode == 'ecb' else 'ChainingModeCBC') + '\0'
    mb = m.encode('utf-16-le')
    assert bc.BCryptSetProperty(alg, 'ChainingMode', mb, len(mb), 0) == 0
    assert bc.BCryptGenerateSymmetricKey(alg, ctypes.byref(hkey), None, 0, key, len(key), 0) == 0
    n = len(data) & ~15
    buf = ctypes.create_string_buffer(bytes(data[:n]), n)
    iv = ctypes.create_string_buffer(16) if mode == 'cbc' else None
    out = ctypes.c_ulong()
    assert bc.BCryptDecrypt(hkey, buf, n, None, iv, 16 if iv else 0, buf, n, ctypes.byref(out), 0) == 0
    bc.BCryptDestroyKey(hkey)
    bc.BCryptCloseAlgorithmProvider(alg, 0)
    return buf.raw[:n] + bytes(data[n:])


def headers(d):
    n = struct.unpack_from('>I', d, 0x14)[0]
    return {k: v for k, v in (struct.unpack_from('>II', d, 0x18 + i * 8) for i in range(n))}


def main():
    d = open(sys.argv[1], 'rb').read()
    assert d[:4] == b'XEX2'
    pe_off, sec_off = struct.unpack_from('>I', d, 8)[0], struct.unpack_from('>I', d, 0x10)[0]
    hdr = headers(d)
    ff = hdr[0x3FF]
    info_size, enc, comp = struct.unpack_from('>IHH', d, ff)
    body = d[pe_off:]

    if enc == 1:
        enc_key = d[sec_off + 0x150:sec_off + 0x160]
        for name, master in (('retail', RETAIL_KEY), ('devkit', DEVKIT_KEY)):
            session = aes(master, enc_key, 'ecb')
            # testa a chave pelo cabeçalho MZ depois de montar a imagem
            image = rebuild(aes(session, body, 'cbc'), d, ff, info_size, comp)
            if image[:2] == b'MZ':
                print('chave', name)
                break
        else:
            raise SystemExit('nenhuma chave gerou um PE válido')
    else:
        image = rebuild(body, d, ff, info_size, comp)

    open(sys.argv[2], 'wb').write(image)
    print(f'PE: {len(image)} bytes, base {hdr.get(0x10201, 0):#x}, entrada {hdr.get(0x10100, 0):#x}')


def rebuild(data, d, ff, info_size, comp):
    if comp == 0:
        return data
    if comp != 1:
        raise SystemExit('compressão LZX não suportada')
    out, pos = bytearray(), 0
    for i in range((info_size - 8) // 8):
        size, zeros = struct.unpack_from('>II', d, ff + 8 + i * 8)
        out += data[pos:pos + size] + bytes(zeros)
        pos += size
    return bytes(out)


if __name__ == '__main__':
    main()
