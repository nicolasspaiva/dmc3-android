# Gera tests/insn_tests.cpp: para cada instrução acrescentada ao XenonRecomp, pega
# um trecho REAL do código gerado (recomp/ppc), roda com valores aleatórios e compara
# com uma referência escrita na semântica big-endian do PowerPC.
# Uso: python -I gen_insn_tests.py <pasta recomp/ppc> <saida.cpp>
import glob, re, sys

# referência por instrução. Placeholders: {D},{A},{B} = registradores depois;
# {pD},{pA},{pB} = antes; {I} = imediato; {BI0},{BI1},{BI2} = bits de CR (antes/depois via cr())
# Todo código abaixo roda dentro de check(...) com "pre" = contexto antes, "ctx" = depois,
# "M" = memória antes, "mem" = memória depois, "taken" = desvio tomado.
REF = {
    # ---- desvios ----
    'bdzf':   ('branch', 'uint32_t c = uint32_t(pre.ctr.u64 - 1); EXPECT(ctx.ctr.u32 == c); EXPECT(taken == (c == 0 && !crbit(pre, {BI0})));'),
    'bdnzt':  ('branch', 'uint32_t c = uint32_t(pre.ctr.u64 - 1); EXPECT(ctx.ctr.u32 == c); EXPECT(taken == (c != 0 && crbit(pre, {BI0})));'),
    'cror':   ('cr', 'EXPECT(crbit(ctx, {BI0}) == (crbit(pre, {BI1}) | crbit(pre, {BI2})));'),
    'crorc':  ('cr', 'EXPECT(crbit(ctx, {BI0}) == (crbit(pre, {BI1}) | !crbit(pre, {BI2})));'),
    # ---- inteiras ----
    'addc':   ('int', 'EXPECT({D}.u64 == {pA}.u64 + {pB}.u64); EXPECT(ctx.xer.ca == ((uint64_t({pA}.u32) + {pB}.u32) >> 32));'),
    'addme':  ('int', 'EXPECT({D}.u64 == {pA}.u64 + pre.xer.ca - 1); EXPECT(ctx.xer.ca == ((uint64_t({pA}.u32) + pre.xer.ca + 0xFFFFFFFFull) >> 32));'),
    'subfze': ('int', 'EXPECT({D}.u64 == ~{pA}.u64 + pre.xer.ca); EXPECT(ctx.xer.ca == ((uint64_t(~{pA}.u32) + pre.xer.ca) >> 32));'),
    'subfze.':('int', 'EXPECT({D}.u64 == ~{pA}.u64 + pre.xer.ca); EXPECT(ctx.xer.ca == ((uint64_t(~{pA}.u32) + pre.xer.ca) >> 32)); EXPECT(ctx.cr0.eq == ({D}.s32 == 0)); EXPECT(ctx.cr0.lt == ({D}.s32 < 0)); EXPECT(ctx.cr0.gt == ({D}.s32 > 0));'),
    'eqv':    ('int', 'EXPECT({D}.u64 == ~({pA}.u64 ^ {pB}.u64));'),
    'mulhd':  ('int', 'EXPECT({D}.s64 == int64_t(((__int128){pA}.s64 * {pB}.s64) >> 64));'),
    'mulhdu': ('int', 'EXPECT({D}.u64 == uint64_t(((unsigned __int128){pA}.u64 * {pB}.u64) >> 64));'),
    # ---- memória (D-form: {I}({A}), X-form: {A}+{B}) ----
    'lbzux':  ('memx', 'EXPECT({D}.u64 == M[EA]); EXPECT({A}.u32 == EA);'),
    'lhzu':   ('memd', 'EXPECT({D}.u64 == be_rd(M, EA, 2)); EXPECT({A}.u32 == EA);'),
    'lhzux':  ('memx', 'EXPECT({D}.u64 == be_rd(M, EA, 2)); EXPECT({A}.u32 == EA);'),
    'lhau':   ('memd', 'EXPECT({D}.s64 == int16_t(be_rd(M, EA, 2))); EXPECT({A}.u32 == EA);'),
    'lwzux':  ('memx', 'EXPECT({D}.u64 == be_rd(M, EA, 4)); EXPECT({A}.u32 == EA);'),
    'ldux':   ('memx', 'EXPECT({D}.u64 == be_rd(M, EA, 8)); EXPECT({A}.u32 == EA);'),
    'lhbrx':  ('memx0', 'EXPECT({D}.u64 == (M[EA] | (M[EA + 1] << 8)));'),
    'lfsu':   ('memd', 'uint32_t w = uint32_t(be_rd(M, EA, 4)); float fv; memcpy(&fv, &w, 4); EXPECT(same_f64({D}.f64, double(fv))); EXPECT({A}.u32 == EA);'),
    'lfsux':  ('memx', 'uint32_t w = uint32_t(be_rd(M, EA, 4)); float fv; memcpy(&fv, &w, 4); EXPECT(same_f64({D}.f64, double(fv))); EXPECT({A}.u32 == EA);'),
    'lfdu':   ('memd', 'EXPECT({D}.u64 == be_rd(M, EA, 8)); EXPECT({A}.u32 == EA);'),
    'stbux':  ('memx', 'EXPECT(mem[EA] == {pD}.u8); EXPECT({A}.u32 == EA);'),
    'sthu':   ('memd', 'EXPECT(be_rd(mem, EA, 2) == {pD}.u16); EXPECT({A}.u32 == EA);'),
    'sthux':  ('memx', 'EXPECT(be_rd(mem, EA, 2) == {pD}.u16); EXPECT({A}.u32 == EA);'),
    'stdux':  ('memx', 'EXPECT(be_rd(mem, EA, 8) == {pD}.u64); EXPECT({A}.u32 == EA);'),
    'stfsu':  ('memd', 'float fv = float({pD}.f64); uint32_t w; memcpy(&w, &fv, 4); EXPECT(be_rd(mem, EA, 4) == w); EXPECT({A}.u32 == EA);'),
    'stfsux': ('memx', 'float fv = float({pD}.f64); uint32_t w; memcpy(&w, &fv, 4); EXPECT(be_rd(mem, EA, 4) == w); EXPECT({A}.u32 == EA);'),
    'stfdu':  ('memd', 'EXPECT(be_rd(mem, EA, 8) == {pD}.u64); EXPECT({A}.u32 == EA);'),
    # ---- vetor (elementos em ordem big-endian: e16(v,i) = elemento i do PowerPC) ----
    'vslh':   ('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == uint16_t(e16({pA}, i) << (e16({pB}, i) & 15)));'),
    'vsrh':   ('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == uint16_t(e16({pA}, i) >> (e16({pB}, i) & 15)));'),
    'vsrah':  ('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == uint16_t(int16_t(e16({pA}, i)) >> (e16({pB}, i) & 15)));'),
    'vsrab':  ('vec', 'for (int i = 0; i < 16; i++) EXPECT(e8({D}, i) == uint8_t(int8_t(e8({pA}, i)) >> (e8({pB}, i) & 7)));'),
    'vrlh':   ('vec', 'for (int i = 0; i < 8; i++) { unsigned n = e16({pB}, i) & 15, x = e16({pA}, i); EXPECT(e16({D}, i) == uint16_t((x << n) | (x >> ((16 - n) & 15)) * (n != 0))); }'),
    'vsubshs':('vec', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == sat<int16_t>(int(int16_t(e16({pA}, i))) - int16_t(e16({pB}, i))));'),
    'vaddsbs':('vec', 'for (int i = 0; i < 16; i++) EXPECT(int8_t(e8({D}, i)) == sat<int8_t>(int(int8_t(e8({pA}, i))) + int8_t(e8({pB}, i))));'),
    'vaddsws':('vec', 'for (int i = 0; i < 4; i++) EXPECT(int32_t(e32({D}, i)) == sat<int32_t>(int64_t(int32_t(e32({pA}, i))) + int32_t(e32({pB}, i))));'),
    'vmaxsh': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == std::max(int16_t(e16({pA}, i)), int16_t(e16({pB}, i))));'),
    'vminsh': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == std::min(int16_t(e16({pA}, i)), int16_t(e16({pB}, i))));'),
    'vavguh': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == (unsigned(e16({pA}, i)) + e16({pB}, i) + 1) >> 1);'),
    'vsububm':('vec', 'for (int i = 0; i < 16; i++) EXPECT(e8({D}, i) == uint8_t(e8({pA}, i) - e8({pB}, i)));'),
    'vcmpgtsh':('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == (int16_t(e16({pA}, i)) > int16_t(e16({pB}, i)) ? 0xFFFF : 0));'),
    'vcmpgtsh.':('vec', 'bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = int16_t(e16({pA}, i)) > int16_t(e16({pB}, i)); all &= t; none &= !t; EXPECT(e16({D}, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);'),
    'vcmpgtsw.':('vec', 'bool all = true, none = true; for (int i = 0; i < 4; i++) { bool t = int32_t(e32({pA}, i)) > int32_t(e32({pB}, i)); all &= t; none &= !t; EXPECT(e32({D}, i) == (t ? 0xFFFFFFFFu : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);'),
    'vcmpequh':('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == (e16({pA}, i) == e16({pB}, i) ? 0xFFFF : 0));'),
    'vcmpequh.':('vec', 'bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = e16({pA}, i) == e16({pB}, i); all &= t; none &= !t; EXPECT(e16({D}, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);'),
    'vcmpgtuh.':('vec', 'bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = e16({pA}, i) > e16({pB}, i); all &= t; none &= !t; EXPECT(e16({D}, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);'),
    'vspltish':('vimm', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == {I});'),
    'vpkswss': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == sat<int16_t>(int32_t(i < 4 ? e32({pA}, i) : e32({pB}, i - 4))));'),
    'vpkswss128': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(int16_t(e16({D}, i)) == sat<int16_t>(int32_t(i < 4 ? e32({pA}, i) : e32({pB}, i - 4))));'),
    'vpkshss': ('vec', 'for (int i = 0; i < 16; i++) EXPECT(int8_t(e8({D}, i)) == sat<int8_t>(int16_t(i < 8 ? e16({pA}, i) : e16({pB}, i - 8))));'),
    'vpkswus128': ('vec', 'for (int i = 0; i < 8; i++) EXPECT(e16({D}, i) == sat<uint16_t>(int32_t(i < 4 ? e32({pA}, i) : e32({pB}, i - 4))));'),
    'vpkuhus128': ('vec', 'for (int i = 0; i < 16; i++) EXPECT(e8({D}, i) == std::min<unsigned>(i < 8 ? e16({pA}, i) : e16({pB}, i - 8), 255));'),
    'vupkd3d128': ('vupk', 'for (int i = 0; i < 4; i++) EXPECT(same_f64(f32e({D}, i), ref_half(e16({pA}, 4 + i))));'),
    'vcfpuxws128': ('vfimm', 'for (int i = 0; i < 4; i++) { float x = f32e({pA}, i) * float(1u << {I}); uint32_t want = !(x > 0) ? 0 : x >= 4294967296.0f ? 0xFFFFFFFFu : uint32_t(x); EXPECT(e32({D}, i) == want); }'),
}

CRFIELDS = {'lt': 0, 'gt': 1, 'eq': 2, 'so': 3}


def crbit(tok):
    m = re.fullmatch(r'(?:4\*cr(\d)\+)?(lt|gt|eq|so)', tok)
    return (int(m.group(1) or 0)) * 4 + CRFIELDS[m.group(2)] if m else None


def reg(tok):
    m = re.fullmatch(r'([rfv])(\d+)', tok)
    return (m.group(1), int(m.group(2))) if m else None


def name(kind_num, prefix):
    k, n = kind_num
    return f'{prefix}.{k}{n}'


def snippets(ppc_dir):
    found = {}
    for path in sorted(glob.glob(ppc_dir + '/ppc_recomp.*.cpp')):
        lines = open(path).read().split('\n')
        for i, line in enumerate(lines):
            m = re.match(r'\t// (\S+) (.*)$', line)
            if not m or m.group(1) not in REF or m.group(1) in found:
                continue
            mn, ops = m.group(1), m.group(2)
            body = []
            for l in lines[i + 1:]:
                if l.startswith('\t//') or l.startswith('loc_') or l.startswith('}'):
                    break
                body.append(l)
            parsed = parse(mn, ops)
            if parsed is None:
                continue
            found[mn] = (ops, body, parsed)
    return found


def parse(mn, ops):
    kind = REF[mn][0]
    toks = ops.split(',')
    if kind in ('branch',):
        return {'BI0': crbit(toks[0])}
    if kind == 'cr':
        bits = [crbit(t) for t in toks]
        return None if None in bits or len(set(bits)) < 3 else {f'BI{i}': b for i, b in enumerate(bits)}
    if kind == 'memd':
        d = reg(toks[0]); m = re.fullmatch(r'(-?\d+)\((r\d+)\)', toks[1])
        a = reg(m.group(2))
        if a[1] == 0 or (d[0] == 'r' and d[1] == a[1]):
            return None
        return {'D': d, 'A': a, 'I': int(m.group(1))}
    if kind == 'memx0':
        # rA pode ser o literal 0 (endereço = só rB)
        d, b = reg(toks[0]), reg(toks[2])
        a = None if toks[1] == '0' else reg(toks[1])
        regs = {x[1] for x in (d, a, b) if x}
        return None if len(regs) < (3 if a else 2) else {'D': d, 'A': a, 'B': b}
    if kind == 'memx':
        d, a, b = (reg(t) for t in toks)
        regs = {x[1] for x in (d, a, b) if x[0] == 'r'}
        if a[1] == 0 or len(regs) < (3 if d[0] == 'r' else 2):
            return None
        return {'D': d, 'A': a, 'B': b}
    if kind == 'vimm':
        return {'D': reg(toks[0]), 'I': int(toks[1], 0)}
    if kind == 'vupk':
        # só o tipo FLOAT16_4 (20 >> 2 = 5); os outros tipos já vinham do XenonRecomp
        d, a, imm = reg(toks[0]), reg(toks[1]), int(toks[2], 0)
        return None if imm != 20 else {'D': d, 'A': a}
    if kind == 'vfimm':
        d, a = reg(toks[0]), reg(toks[1])
        return None if d == a else {'D': d, 'A': a, 'I': int(toks[2], 0)}
    rs = [reg(t) for t in toks]
    if kind == 'int':
        rs = rs + [None] * (3 - len(rs))
        if len({r for r in rs if r}) < len([r for r in rs if r]):
            return None
        return dict(zip('DAB', rs))
    if kind == 'vec':
        # registradores repetidos são válidos: a referência sempre lê o estado anterior
        return dict(zip('DAB', rs))


def main():
    ppc_dir, out_path = sys.argv[1:3]
    found = snippets(ppc_dir)
    out = [open(__file__.replace('gen_insn_tests.py', 'insn_tests_prelude.h')).read()]
    tests = []
    for mn, (ops, body, p) in sorted(found.items()):
        fn = 't_' + re.sub(r'\W', '_', mn)
        code = '\n'.join(re.sub(r'goto loc_[0-9A-F]+;', 'taken = true;', l) for l in body)
        out.append(f'// {mn} {ops}\nstatic void {fn}(PPCContext& __restrict ctx, uint8_t* base, bool& taken)\n{{\n'
                   f'\tPPCRegister temp{{}}; PPCVRegister vTemp{{}}; uint32_t ea{{}}; (void)temp; (void)vTemp; (void)ea; (void)taken;\n{code}\n}}\n')
        kind, check = REF[mn]
        subs = {}
        for key in 'DAB':
            if p.get(key):
                subs[key] = name(p[key], 'ctx')
                subs['p' + key] = name(p[key], 'pre')
        for key in ('I', 'BI0', 'BI1', 'BI2'):
            if key in p:
                subs[key] = str(p[key])
        ea = ''
        if kind == 'memd':
            ea = f'uint32_t EA = uint32_t(pre.r{p["A"][1]}.u32 + ({p["I"]}));'
        elif kind == 'memx':
            ea = f'uint32_t EA = pre.r{p["A"][1]}.u32 + pre.r{p["B"][1]}.u32;'
        elif kind == 'memx0':
            ea = f'uint32_t EA = {f"pre.r{p[chr(65)][1]}.u32 + " if p["A"] else ""}pre.r{p["B"][1]}.u32;'
        # só os marcadores conhecidos: o resto das chaves é C++
        check = re.sub(r'\{(p?[DAB]|I|BI[012])\}', lambda m: subs[m.group(1)], check)
        mode = {'memd': 1, 'memx': 1, 'memx0': 1, 'vfimm': 2}.get(kind, 0)
        out.append(f'static void c_{fn[2:]}(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)\n'
                   f'{{\n\t(void)M; (void)mem; (void)taken;\n\t{ea}\n\t{check}\n}}\n')
        tests.append(f'\t{{ "{mn} {ops}", {fn}, c_{fn[2:]}, {mode} }},')
    rand = ['static void randomize_regs(PPCContext& c, int mode)\n{']
    rand += [f'\tc.r{n}.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();' for n in range(32)]
    rand += [f'\tc.f{n}.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;' for n in range(32)]
    rand += [f'\tfor (int i = 0; i < 2; i++) c.v{n}.u64[i] = rnd64();' for n in range(128)]
    rand += [f'\tif (mode == 2) for (int i = 0; i < 4; i++) c.v{n}.f32[i] = rndf();' for n in range(128)]
    rand += [f'\tc.cr{n}.lt = rng() & 1; c.cr{n}.gt = rng() & 1; c.cr{n}.eq = rng() & 1; c.cr{n}.so = rng() & 1;' for n in range(8)]
    rand += ['\tc.xer.ca = rng() & 1;', '\tc.ctr.u64 = rng() % 2 ? rng() % 4 : rnd64();', '}\n']
    out.insert(1, '\n'.join(rand))
    missing = sorted(set(REF) - set(found))
    out.append('static const Test TESTS[] = {\n' + '\n'.join(tests) + '\n};\n')
    out.append('int main() { return run_all(TESTS, sizeof(TESTS) / sizeof(TESTS[0])); }\n')
    open(out_path, 'w').write('\n'.join(out))
    print(f'{len(found)} instruções com teste; sem trecho utilizável: {missing}')


if __name__ == '__main__':
    main()
