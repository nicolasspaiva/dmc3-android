# Desmonta microcódigo de shader da Xenos (GPU do Xbox 360), para conferir o tradutor runtime/xenos_shader.cpp.
# Formatos conforme o R400/Adreno A2xx: instruções de controle de 48 bits (duas a cada 3 palavras),
# instruções de fetch e ALU de 96 bits (3 palavras), endereçadas em unidades de 3 palavras.
# O cabeçalho do XDK (.hdr gravado pela captura) diz onde o programa começa: palavra em hdr+0x18 = seção do
# programa; seção+0 = deslocamento no microcódigo, seção+4 = tamanho.
# Uso: python -I xenos_disasm.py <ucode.bin> [vs|ps]   (lê <ucode>.hdr ao lado, se existir)
import struct, sys

CF = ['nop', 'exec', 'exec_end', 'cond_exec', 'cond_exec_end', 'cond_pred_exec', 'cond_pred_exec_end',
      'loop_start', 'loop_end', 'cond_call', 'return', 'cond_jmp', 'alloc', 'cond_exec_pred_clean',
      'cond_exec_pred_clean_end', 'mark_vs_fetch_done']
VEC = ['add', 'mul', 'max', 'min', 'seq', 'sgt', 'sge', 'sne', 'frc', 'trunc', 'floor', 'mad', 'cnde', 'cndge',
       'cndgt', 'dp4', 'dp3', 'dp2add', 'cube', 'max4', 'setp_eq_push', 'setp_ne_push', 'setp_gt_push',
       'setp_ge_push', 'kill_eq', 'kill_gt', 'kill_ge', 'kill_ne', 'dst', 'mova']
VEC_ARGS = [2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 3, 3, 2, 2, 3, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1]
SCA = ['adds', 'adds_prev', 'muls', 'muls_prev', 'muls_prev2', 'maxs', 'mins', 'seqs', 'sgts', 'sges', 'snes',
       'frcs', 'truncs', 'floors', 'exp', 'logc', 'log', 'rcpc', 'rcpf', 'rcp', 'rsqc', 'rsqf', 'rsq', 'maxas',
       'maxasf', 'subs', 'subs_prev', 'setp_eq', 'setp_ne', 'setp_gt', 'setp_ge', 'setp_inv', 'setp_pop',
       'setp_clr', 'setp_rstr', 'kills_eq', 'kills_gt', 'kills_ge', 'kills_ne', 'kills_one', 'sqrt', '?41',
       'mulsc0', 'mulsc1', 'addsc0', 'addsc1', 'subsc0', 'subsc1', 'sin', 'cos', 'retain_prev']
XYZW = 'xyzw'


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


def swz(s, n=4):
    # swizzle relativo: componente i lê ((s >> 2i) + i) & 3
    return ''.join(XYZW[(bits(s, 2 * i, 2) + i) & 3] for i in range(n))


def dest_swz(s):
    return ''.join('xyzw01?_'[bits(s, 3 * i, 3)] for i in range(4))


def mask(m):
    return ''.join(c for i, c in enumerate(XYZW) if m >> i & 1) or '_'


def alu(w, kind):
    w0, w1, w2 = w
    vdst, vrel, abs_c = bits(w0, 0, 6), bits(w0, 6, 1), bits(w0, 7, 1)
    sdst, srel, export = bits(w0, 8, 6), bits(w0, 14, 1), bits(w0, 15, 1)
    vmask, smask, vclamp, sclamp, sop = bits(w0, 16, 4), bits(w0, 20, 4), bits(w0, 24, 1), bits(w0, 25, 1), bits(w0, 26, 6)
    sw = [bits(w1, 16, 8), bits(w1, 8, 8), bits(w1, 0, 8)]   # src1, src2, src3
    neg = [bits(w1, 26, 1), bits(w1, 25, 1), bits(w1, 24, 1)]
    pred_cond, predicated = bits(w1, 27, 1), bits(w1, 28, 1)
    reg = [bits(w2, 16, 8), bits(w2, 8, 8), bits(w2, 0, 8)]
    vop = bits(w2, 24, 5)
    sel = [bits(w2, 31, 1), bits(w2, 30, 1), bits(w2, 29, 1)]   # 1 = registro temporário, 0 = constante

    def src(i, n=4):
        if sel[i]:
            r = f'r{reg[i] & 63}' + ('[aL]' if reg[i] & 64 else '')
            if reg[i] & 128:
                r = f'|{r}|'
        else:
            r = f'c{reg[i]}' + ('|abs|' if abs_c else '')
        return ('-' if neg[i] else '') + r + '.' + swz(sw[i], n)

    def dst(d, m, is_scalar):
        if export:
            name = {62: 'oPos', 63: 'oPts'}.get(d, f'o{d}') if kind == 'vs' else ({61: 'oDepth'}.get(d, f'oC{d}'))
        else:
            name = f'r{d}'
        return f'{name}.{mask(m)}'

    pred = f'({"p" if pred_cond else "!p"}) ' if predicated else ''
    out = []
    if vmask or vop >= 20 and vop <= 27:
        args = ', '.join(src(i) for i in range(VEC_ARGS[vop] if vop < len(VEC_ARGS) else 2))
        out.append(f'{pred}{VEC[vop] if vop < len(VEC) else f"v?{vop}"}{"_sat" if vclamp else ""} {dst(vdst, vmask, False)}, {args}')
    if smask or sop not in (50,):
        name = SCA[sop] if sop < len(SCA) else f's?{sop}'
        d = dst(vdst if export else sdst, smask, True)
        out.append(f'{pred}{name}{"_sat" if sclamp else ""} {d}, {src(2)}')
    return ' ; '.join(out) or 'alu nop'


def fetch(w, kind):
    w0, w1, w2 = w
    op = bits(w0, 0, 5)
    src, dst_ = bits(w0, 5, 6), bits(w0, 12, 6)
    const = bits(w0, 20, 5)
    dsw = bits(w1, 0, 12)
    pred = ''
    if op == 0:
        sel = bits(w0, 25, 2)
        ssw = bits(w0, 30, 2)
        fmt = bits(w1, 16, 6)
        signed, integer = bits(w1, 12, 1), bits(w1, 13, 1)
        exp = bits(w1, 24, 6)
        exp = exp - 64 if exp & 32 else exp
        mini = bits(w1, 30, 1)
        stride = bits(w2, 0, 8)
        off = bits(w2, 8, 23)
        off = off - (1 << 23) if off & (1 << 22) else off
        return (f'vfetch{"_mini" if mini else ""} r{dst_}.{dest_swz(dsw)}, r{src}.{XYZW[ssw]}, vf{const * 3 + sel} '
                f'fmt {fmt} {"signed" if signed else "unsigned"} {"int" if integer else "norm"} exp {exp} '
                f'stride {stride} offset {off}')
    if op == 1:
        ssw = bits(w0, 26, 6)
        denorm = bits(w0, 25, 1)
        dim = bits(w2, 21, 2) if False else bits(w2, 14, 2)
        return (f'tfetch r{dst_}.{dest_swz(dsw)}, r{src}.{swz(ssw | 0xC0, 3)}, tf{const}'
                f'{" denorm" if denorm else ""} filt mag {bits(w1, 12, 2)} min {bits(w1, 14, 2)} mip {bits(w1, 16, 2)}')
    return f'fetch op {op} r{dst_}, r{src}, c{const} ({w0:08X} {w1:08X} {w2:08X})'


def main():
    data = open(sys.argv[1], 'rb').read()
    kind = sys.argv[2] if len(sys.argv) > 2 else 'vs'
    try:
        hdr = open(sys.argv[1][:-4] + '.hdr', 'rb').read()
        sec = struct.unpack_from('>I', hdr, 0x18)[0]
        off, size = struct.unpack_from('>II', hdr, sec)
        print(f'; programa em +0x{off:X}, 0x{size:X} bytes; seção: ' + ' '.join(f'{x:08X}' for x in struct.unpack_from(f'>{(len(hdr) - sec) // 4}I', hdr, sec)))
        data = data[off:off + size]
    except FileNotFoundError:
        pass
    words = struct.unpack(f'>{len(data) // 4}I', data[:len(data) // 4 * 4])
    slot = lambda i: words[3 * i:3 * i + 3]
    # programa de controle: começa no slot 0 e vai até o primeiro endereço de exec
    first_exec = len(words) // 3
    i = 0
    cfs = []
    while i < first_exec:
        w0, w1, w2 = slot(i)
        for cf in (w0 | (w1 & 0xFFFF) << 32, (w1 >> 16) | w2 << 16):
            op = bits(cf, 44, 4)
            cfs.append(cf)
            if op in (1, 2, 3, 4, 5, 6, 13, 14):
                first_exec = min(first_exec, bits(cf, 0, 12))
        i += 1
    for n, cf in enumerate(cfs):
        op = bits(cf, 44, 4)
        name = CF[op]
        if op in (1, 2, 3, 4, 5, 6, 13, 14):
            addr, count, seq = bits(cf, 0, 12), bits(cf, 12, 3), bits(cf, 16, 12)
            extra = ''
            if op in (3, 4):
                extra = f' b{bits(cf, 34, 8)} cond {bits(cf, 42, 1)}'
            elif op in (5, 6, 13, 14):
                extra = f' p cond {bits(cf, 42, 1)}'
            print(f'cf{n}: {name} addr {addr} count {count}{extra}')
            for k in range(count):
                w = slot(addr + k)
                is_fetch = seq >> (2 * k) & 1
                print(f'    {addr + k:3}: ' + (fetch(w, kind) if is_fetch else alu(w, kind)))
            if op in (2, 4, 6, 14):
                break
        elif op == 12:
            print(f'cf{n}: alloc {["?", "position", "interp/color", "memory"][bits(cf, 41, 2)]} size {bits(cf, 0, 3)}')
        else:
            print(f'cf{n}: {name} ({cf:012X})')


if __name__ == '__main__':
    main()
