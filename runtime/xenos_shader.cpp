// Tradução do microcódigo da Xenos para GLSL. Formato das instruções (R400 / Adreno A2xx):
//   - controle (CF): 48 bits, duas a cada 3 palavras, opcode nos bits 44-47; o programa de controle vai da
//     palavra 0 até o primeiro endereço de exec;
//   - fetch e ALU: 96 bits (3 palavras), endereçadas em unidades de 3 palavras. A sequência de um exec diz,
//     2 bits por instrução, se é fetch (bit 0) ou ALU.
// Swizzles de ALU são relativos (componente i lê ((s >> 2i) + i) & 3); os de fetch são absolutos.
#include "xenos_shader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace xenos
{
    namespace
    {
        uint32_t Bits(uint64_t v, int lo, int n) { return uint32_t((v >> lo) & ((1ull << n) - 1)); }

        const char XYZW[] = "xyzw";

        struct Translator
        {
            const uint32_t* words;
            uint32_t count;
            bool pixel;
            Shader out;
            std::string code;
            std::set<uint32_t> outputs;   // interpoladores escritos (vértice)
            uint32_t maxRegister = 0;
            uint32_t lastFullFetch = ~0u;  // índice em out.fetches do último vfetch completo (para os "mini")

            void Fail(const std::string& why)
            {
                if (out.error.empty())
                    out.error = why;
            }

            void Line(const std::string& s) { code += "    " + s + "\n"; }

            std::string Reg(uint32_t index)
            {
                maxRegister = std::max(maxRegister, index);
                return "r[" + std::to_string(index) + "]";
            }

            // ---- ALU ----
            struct Alu
            {
                uint32_t w0, w1, w2;
                uint32_t VectorDest() const { return Bits(w0, 0, 6); }
                bool AbsConstants() const { return Bits(w0, 7, 1); }
                uint32_t ScalarDest() const { return Bits(w0, 8, 6); }
                bool Export() const { return Bits(w0, 15, 1); }
                uint32_t VectorMask() const { return Bits(w0, 16, 4); }
                uint32_t ScalarMask() const { return Bits(w0, 20, 4); }
                bool VectorClamp() const { return Bits(w0, 24, 1); }
                bool ScalarClamp() const { return Bits(w0, 25, 1); }
                uint32_t ScalarOp() const { return Bits(w0, 26, 6); }
                uint32_t Swizzle(int i) const { return Bits(w1, 16 - 8 * i, 8); }   // i = 0, 1, 2 -> src1, src2, src3
                bool Negate(int i) const { return Bits(w1, 26 - i, 1); }
                bool PredCondition() const { return Bits(w1, 27, 1); }
                bool Predicated() const { return Bits(w1, 28, 1); }
                bool AddressAbsolute() const { return Bits(w1, 29, 1); }
                bool ConstRelative(int slot) const { return Bits(w1, 31 - slot, 1); }   // slot 0 = bit 31, 1 = bit 30
                uint32_t Register(int i) const { return Bits(w2, 16 - 8 * i, 8); }
                uint32_t VectorOp() const { return Bits(w2, 24, 5); }
                bool IsTemp(int i) const { return Bits(w2, 31 - i, 1); }
            };

            // constante: qual dos dois "slots" de constante a fonte usa (o primeiro operando constante usa o 0)
            std::string SourceBase(const Alu& a, int i)
            {
                uint32_t reg = a.Register(i);
                std::string base;
                if (a.IsTemp(i))
                {
                    if (reg & 0x40)
                        Fail("registro com endereçamento relativo (aL)");
                    base = Reg(reg & 0x3F);
                    if (reg & 0x80)
                        base = "abs(" + base + ")";
                }
                else
                {
                    int slot = 0;
                    for (int k = 0; k < i; k++)
                        if (!a.IsTemp(k))
                            slot = 1;
                    std::string index = std::to_string(reg);
                    // relativo: ao registrador de endereço a0 (address_absolute) ou ao contador de laço aL
                    if (a.ConstRelative(slot))
                    {
                        if (a.AddressAbsolute())
                            index = "clamp(" + index + " + addr, 0, 255)";
                        else
                            Fail("constante relativa ao contador de laço (aL)");
                    }
                    base = std::string(pixel ? "PC" : "VC") + "[" + index + "]";
                    if (a.AbsConstants())
                        base = "abs(" + base + ")";
                }
                if (a.Negate(i))
                    base = "(-" + base + ")";
                return base;
            }

            std::string VectorSource(const Alu& a, int i)
            {
                uint32_t s = a.Swizzle(i);
                std::string sw;
                for (int c = 0; c < 4; c++)
                    sw += XYZW[(Bits(s, 2 * c, 2) + c) & 3];
                std::string base = SourceBase(a, i);
                return sw == "xyzw" ? base : base + "." + sw;
            }

            // operandos escalares saem de src3: "a" = componente do slot w, "b" = componente do slot x
            void ScalarSources(const Alu& a, std::string& sa, std::string& sb)
            {
                uint32_t s = a.Swizzle(2);
                std::string base = SourceBase(a, 2);
                sa = base + "." + XYZW[(Bits(s, 6, 2) + 3) & 3];
                sb = base + "." + XYZW[Bits(s, 0, 2)];
            }

            static std::string Mask(uint32_t m)
            {
                std::string s;
                for (int c = 0; c < 4; c++)
                    if (m >> c & 1)
                        s += XYZW[c];
                return s;
            }

            std::string ExportName(uint32_t d)
            {
                if (!pixel)
                {
                    if (d == 62)
                        return "opos";
                    if (d == 63)
                        return "opts";
                    if (d < Interpolators)
                    {
                        outputs.insert(d);
                        return "v" + std::to_string(d);
                    }
                    Fail("exportação de vértice " + std::to_string(d));
                    return "odummy";
                }
                if (d == 0)
                    return "color0";
                if (d == 61)
                {
                    out.writesDepth = true;
                    return "odepth";
                }
                return "odummy";   // render targets 1-3 ainda não usados
            }

            void EmitAlu(const Alu& a)
            {
                uint32_t vop = a.VectorOp(), sop = a.ScalarOp();
                uint32_t vmask = a.VectorMask(), smask = a.ScalarMask();
                bool exportData = a.Export();

                std::string pre = "{";
                if (a.Predicated())
                    pre = std::string("if (") + (a.PredCondition() ? "p" : "!p") + ") {";
                Line(pre);

                // ---- operação vetorial ----
                static const int VecArgs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 3, 3, 3, 3, 2, 2, 3, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1 };
                bool vectorUsed = vmask != 0 || (vop >= 20 && vop <= 27) || vop == 29;
                if (vectorUsed)
                {
                    int n = vop < 30 ? VecArgs[vop] : 0;
                    if (n >= 1)
                        Line("    vec4 va = " + VectorSource(a, 0) + ";");
                    if (n >= 2)
                        Line("    vec4 vb = " + VectorSource(a, 1) + ";");
                    if (n >= 3)
                        Line("    vec4 vc = " + VectorSource(a, 2) + ";");
                    std::string e;
                    switch (vop)
                    {
                    case 0: e = "va + vb"; break;
                    case 1: e = "va * vb"; break;
                    case 2: e = "max(va, vb)"; break;
                    case 3: e = "min(va, vb)"; break;
                    case 4: e = "vec4(equal(va, vb))"; break;
                    case 5: e = "vec4(greaterThan(va, vb))"; break;
                    case 6: e = "vec4(greaterThanEqual(va, vb))"; break;
                    case 7: e = "vec4(notEqual(va, vb))"; break;
                    case 8: e = "fract(va)"; break;
                    case 9: e = "trunc(va)"; break;
                    case 10: e = "floor(va)"; break;
                    case 11: e = "va * vb + vc"; break;
                    case 12: e = "mix(vc, vb, equal(va, vec4(0.0)))"; break;
                    case 13: e = "mix(vc, vb, greaterThanEqual(va, vec4(0.0)))"; break;
                    case 14: e = "mix(vc, vb, greaterThan(va, vec4(0.0)))"; break;
                    case 15: e = "vec4(dot(va, vb))"; break;
                    case 16: e = "vec4(dot(va.xyz, vb.xyz))"; break;
                    case 17: e = "vec4(va.x * vb.x + va.y * vb.y + vc.x)"; break;
                    case 19: e = "vec4(max(max(va.x, va.y), max(va.z, va.w)))"; break;
                    case 20: Line("    p = va.w == 0.0 && vb.w == 0.0;"); e = "vec4(va.x == 0.0 && vb.x == 0.0 ? 0.0 : va.x + 1.0)"; break;
                    case 21: Line("    p = va.w != 0.0 && vb.w == 0.0;"); e = "vec4(va.x != 0.0 && vb.x == 0.0 ? 0.0 : va.x + 1.0)"; break;
                    case 22: Line("    p = va.w > 0.0 && vb.w == 0.0;"); e = "vec4(va.x > 0.0 && vb.x == 0.0 ? 0.0 : va.x + 1.0)"; break;
                    case 23: Line("    p = va.w >= 0.0 && vb.w == 0.0;"); e = "vec4(va.x >= 0.0 && vb.x == 0.0 ? 0.0 : va.x + 1.0)"; break;
                    case 24: case 25: case 26: case 27:
                    {
                        static const char* Cmp[] = { "equal", "greaterThan", "greaterThanEqual", "notEqual" };
                        std::string c = std::string("any(") + Cmp[vop - 24] + "(va, vb))";
                        if (pixel)
                            Line("    if (" + c + ") discard;");
                        e = "vec4(" + c + " ? 1.0 : 0.0)";
                        break;
                    }
                    case 28: e = "vec4(1.0, va.y * vb.y, va.z, vb.w)"; break;
                    case 29: Line("    addr = int(clamp(floor(va.w + 0.5), -256.0, 255.0));"); e = "vec4(0.0)"; break;
                    default:
                        Fail("operação vetorial " + std::to_string(vop));
                        e = "vec4(0.0)";
                    }
                    if (a.VectorClamp())
                        e = "clamp(" + e + ", 0.0, 1.0)";
                    Line("    vec4 vr = " + e + ";");
                }

                // ---- operação escalar ----
                bool scalarUsed = sop != 50 || smask != 0;
                if (scalarUsed)
                {
                    std::string sa, sb;
                    if (sop >= 42 && sop <= 47)
                    {
                        // constante (src3) vezes/mais/menos um registro temporário codificado no swizzle e no opcode
                        uint32_t s = a.Swizzle(2);
                        uint32_t reg = (sop & 1) | (s & 0x3C) | (uint32_t(a.IsTemp(2)) << 1);
                        std::string c = std::string(pixel ? "PC" : "VC") + "[" + std::to_string(a.Register(2)) + "]";
                        if (a.AbsConstants())
                            c = "abs(" + c + ")";
                        if (a.Negate(2))
                            c = "(-" + c + ")";
                        sa = c + "." + XYZW[(Bits(s, 6, 2) + 3) & 3];
                        sb = Reg(reg) + "." + XYZW[Bits(s, 0, 2)];
                    }
                    else
                    {
                        ScalarSources(a, sa, sb);
                    }
                    Line("    float sa = " + sa + ", sb = " + sb + ";");
                    std::string e;
                    switch (sop)
                    {
                    case 0: e = "sa + sb"; break;
                    case 1: e = "sa + ps"; break;
                    case 2: e = "sa * sb"; break;
                    case 3: e = "sa * ps"; break;
                    case 4: e = "(ps == -3.402823e38 || isinf(ps) || isinf(sb) || sb <= 0.0) ? -3.402823e38 : sa * ps"; break;
                    case 5: e = "max(sa, sb)"; break;
                    case 6: e = "min(sa, sb)"; break;
                    case 7: e = "sa == 0.0 ? 1.0 : 0.0"; break;
                    case 8: e = "sa > 0.0 ? 1.0 : 0.0"; break;
                    case 9: e = "sa >= 0.0 ? 1.0 : 0.0"; break;
                    case 10: e = "sa != 0.0 ? 1.0 : 0.0"; break;
                    case 11: e = "fract(sa)"; break;
                    case 12: e = "trunc(sa)"; break;
                    case 13: e = "floor(sa)"; break;
                    case 14: e = "exp2(sa)"; break;
                    case 15: e = "sa == 0.0 ? -3.402823e38 : log2(sa)"; break;
                    case 16: e = "log2(sa)"; break;
                    case 17: case 18: e = "clamp(1.0 / sa, -3.402823e38, 3.402823e38)"; break;
                    case 19: e = "1.0 / sa"; break;
                    case 20: case 21: e = "clamp(inversesqrt(sa), -3.402823e38, 3.402823e38)"; break;
                    case 22: e = "inversesqrt(sa)"; break;
                    case 23: Line("    addr = int(clamp(floor(sa + 0.5), -256.0, 255.0));"); e = "max(sa, sb)"; break;
                    case 24: Line("    addr = int(clamp(floor(sa), -256.0, 255.0));"); e = "max(sa, sb)"; break;
                    case 25: e = "sa - sb"; break;
                    case 26: e = "sa - ps"; break;
                    case 27: Line("    p = sa == 0.0;"); e = "p ? 0.0 : 1.0"; break;
                    case 28: Line("    p = sa != 0.0;"); e = "p ? 0.0 : 1.0"; break;
                    case 29: Line("    p = sa > 0.0;"); e = "p ? 0.0 : 1.0"; break;
                    case 30: Line("    p = sa >= 0.0;"); e = "p ? 0.0 : 1.0"; break;
                    case 31: Line("    p = sa == 1.0;"); e = "p ? 0.0 : (sa == 0.0 ? 1.0 : sa)"; break;
                    case 32: Line("    p = sa - 1.0 <= 0.0;"); e = "p ? 0.0 : sa - 1.0"; break;
                    case 33: Line("    p = false;"); e = "3.402823e38"; break;
                    case 34: Line("    p = sa == 0.0;"); e = "sa"; break;
                    case 35: case 36: case 37: case 38: case 39:
                    {
                        static const char* Cmp[] = { "sa == 0.0", "sa > 0.0", "sa >= 0.0", "sa != 0.0", "sa == 1.0" };
                        if (pixel)
                            Line(std::string("    if (") + Cmp[sop - 35] + ") discard;");
                        e = std::string("(") + Cmp[sop - 35] + ") ? 1.0 : 0.0";
                        break;
                    }
                    case 40: e = "sqrt(sa)"; break;
                    case 42: case 43: e = "sa * sb"; break;
                    case 44: case 45: e = "sa + sb"; break;
                    case 46: case 47: e = "sa - sb"; break;
                    case 48: e = "sin(sa)"; break;
                    case 49: e = "cos(sa)"; break;
                    case 50: e = "ps"; break;
                    default:
                        Fail("operação escalar " + std::to_string(sop));
                        e = "0.0";
                    }
                    if (a.ScalarClamp())
                        e = "clamp(" + e + ", 0.0, 1.0)";
                    Line("    float sr = " + e + ";");
                }

                // ---- escrita ----
                if (exportData)
                {
                    std::string dest = ExportName(a.VectorDest());
                    uint32_t vOnly = vmask & ~smask, sOnly = smask & ~vmask, ones = vmask & smask;
                    if (vectorUsed && vOnly)
                        Line("    " + dest + "." + Mask(vOnly) + " = vr." + Mask(vOnly) + ";");
                    if (scalarUsed && sOnly)
                        Line("    " + dest + "." + Mask(sOnly) + " = vec4(sr)." + Mask(sOnly) + ";");
                    if (ones)
                        Line("    " + dest + "." + Mask(ones) + " = vec4(1.0)." + Mask(ones) + ";");
                }
                else
                {
                    if (vectorUsed && vmask)
                        Line("    " + Reg(a.VectorDest()) + "." + Mask(vmask) + " = vr." + Mask(vmask) + ";");
                    if (scalarUsed && smask)
                        Line("    " + Reg(a.ScalarDest()) + "." + Mask(smask) + " = vec4(sr)." + Mask(smask) + ";");
                }
                if (scalarUsed)
                    Line("    ps = sr;");
                Line("}");
            }

            // ---- fetch ----
            void EmitDestSwizzle(uint32_t dst, uint32_t dsw, const std::string& value)
            {
                for (int c = 0; c < 4; c++)
                {
                    uint32_t sel = Bits(dsw, 3 * c, 3);
                    std::string target = Reg(dst) + "." + XYZW[c];
                    if (sel < 4)
                        Line("    " + target + " = " + value + "." + XYZW[sel] + ";");
                    else if (sel == 4)
                        Line("    " + target + " = 0.0;");
                    else if (sel == 5)
                        Line("    " + target + " = 1.0;");
                }
            }

            void EmitFetch(uint32_t w0, uint32_t w1, uint32_t w2)
            {
                uint32_t op = Bits(w0, 0, 5);
                uint32_t src = Bits(w0, 5, 6), dst = Bits(w0, 12, 6);
                uint32_t dsw = Bits(w1, 0, 12);
                bool predicated = Bits(w1, 31, 1);   // pred_select
                bool cond = Bits(w2, 31, 1);
                Line(predicated ? std::string("if (") + (cond ? "p" : "!p") + ") {" : "{");
                if (op == 0)
                {
                    if (pixel)
                        Fail("vfetch no shader de pixel");
                    VertexFetch f;
                    bool mini = Bits(w1, 30, 1);
                    f.slot = Bits(w0, 20, 5) * 3 + Bits(w0, 25, 2);
                    f.stride = Bits(w2, 0, 8);
                    int32_t offset = int32_t(Bits(w2, 8, 23) << 9) >> 9;
                    f.offset = uint32_t(offset);
                    f.format = Bits(w1, 16, 6);
                    f.isSigned = Bits(w1, 12, 1);
                    f.isInteger = Bits(w1, 13, 1);
                    f.expAdjust = int32_t(Bits(w1, 24, 6) << 26) >> 26;
                    if (mini && lastFullFetch != ~0u)
                    {
                        f.slot = out.fetches[lastFullFetch].slot;
                        f.stride = out.fetches[lastFullFetch].stride;
                    }
                    uint32_t index = uint32_t(out.fetches.size());
                    if (!mini)
                        lastFullFetch = index;
                    out.fetches.push_back(f);
                    if (index >= 16)
                        Fail("mais de 16 vfetch");
                    EmitDestSwizzle(dst, dsw, "a" + std::to_string(index));
                }
                else if (op == 1)
                {
                    uint32_t slot = Bits(w0, 20, 5);
                    uint32_t ssw = Bits(w0, 26, 6);
                    bool denorm = Bits(w0, 25, 1);
                    uint32_t dimension = Bits(w2, 14, 2);
                    if (dimension > 1)
                        Fail("textura de dimensão " + std::to_string(dimension));
                    out.textureMask |= 1u << slot;
                    std::string t = "T" + std::to_string(slot);
                    std::string coords = std::string("vec2(") + Reg(src) + "." + XYZW[Bits(ssw, 0, 2)] + ", " + Reg(src) + "." +
                        XYZW[Bits(ssw, 2, 2)] + ")";
                    if (denorm)
                        coords = coords + " / vec2(textureSize(" + t + ", 0))";
                    Line("    vec4 tex = texture(" + t + ", " + coords + ");");
                    EmitDestSwizzle(dst, dsw, "tex");
                }
                else
                {
                    Fail("fetch opcode " + std::to_string(op));
                }
                Line("}");
            }

            void EmitExec(uint64_t cf, uint32_t op)
            {
                uint32_t address = Bits(cf, 0, 12), n = Bits(cf, 12, 3), sequence = Bits(cf, 16, 12);
                std::string condition = "!done";
                if (op == 3 || op == 4 || op == 13 || op == 14)
                {
                    uint32_t b = Bits(cf, 34, 8);
                    bool want = Bits(cf, 42, 1);
                    condition += " && ((B[" + std::to_string(b >> 5) + "] >> " + std::to_string(b & 31) + "u) & 1u) == " +
                        (want ? "1u" : "0u");
                }
                else if (op == 5 || op == 6)
                {
                    condition += std::string(" && ") + (Bits(cf, 42, 1) ? "p" : "!p");
                }
                code += "if (" + condition + ") {\n";
                for (uint32_t k = 0; k < n; k++)
                {
                    uint32_t slot = address + k;
                    if (3 * slot + 2 >= count)
                    {
                        Fail("instrução fora do programa");
                        break;
                    }
                    const uint32_t* w = words + 3 * slot;
                    if (sequence >> (2 * k) & 1)
                        EmitFetch(w[0], w[1], w[2]);
                    else
                        EmitAlu(Alu{ w[0], w[1], w[2] });
                }
                if (op == 2 || op == 4 || op == 6 || op == 14)
                    code += "    done = true;\n";
                code += "}\n";
            }

            // cond_jmp: bits 0-12 destino (índice da instrução de controle), 13 incondicional, 14 pelo predicado;
            // senão pela constante booleana dos bits 34-41; bit 42 o valor exigido
            void EmitJump(uint64_t cf)
            {
                uint32_t target = Bits(cf, 0, 13);
                bool want = Bits(cf, 42, 1);
                std::string condition = "!done";
                if (!Bits(cf, 13, 1))
                {
                    if (Bits(cf, 14, 1))
                        condition += std::string(" && ") + (want ? "p" : "!p");
                    else
                    {
                        uint32_t b = Bits(cf, 34, 8);
                        condition += " && ((B[" + std::to_string(b >> 5) + "] >> " + std::to_string(b & 31) + "u) & 1u) == " +
                            (want ? "1u" : "0u");
                    }
                }
                code += "if (" + condition + ") { pc = " + std::to_string(target) + "; continue; }\n";
            }

            void Run()
            {
                // programa de controle: da palavra 0 até o primeiro endereço de exec
                std::vector<uint64_t> cfs;
                uint32_t firstExec = count / 3;
                for (uint32_t slot = 0; slot < firstExec && 3 * slot + 2 < count; slot++)
                {
                    const uint32_t* w = words + 3 * slot;
                    uint64_t pair[2] = { w[0] | (uint64_t(w[1] & 0xFFFF) << 32), (w[1] >> 16) | (uint64_t(w[2]) << 16) };
                    for (uint64_t cf : pair)
                    {
                        cfs.push_back(cf);
                        uint32_t op = Bits(cf, 44, 4);
                        if (op == 1 || op == 2 || (op >= 3 && op <= 6) || op == 13 || op == 14)
                            firstExec = std::min(firstExec, Bits(cf, 0, 12));
                    }
                }
                // com saltos (cond_jmp), o programa vira uma máquina de estados: cada instrução de controle é um case
                // do switch (o endereço do salto é o índice dela) e o salto troca o case; sem saltos, código em sequência
                bool jumps = false;
                for (uint64_t cf : cfs)
                    jumps |= Bits(cf, 44, 4) == 11;
                if (jumps)
                    code += "int pc = 0, steps = 0;\nwhile (pc >= 0 && ++steps < 1024) {\nswitch (pc) {\n";
                for (size_t index = 0; index < cfs.size(); index++)
                {
                    uint64_t cf = cfs[index];
                    uint32_t op = Bits(cf, 44, 4);
                    if (jumps)
                        code += "case " + std::to_string(index) + ":\n";
                    switch (op)
                    {
                    case 0: case 12: case 15: break;   // nop, alloc, mark_vs_fetch_done
                    case 1: case 2: case 3: case 4: case 5: case 6: case 13: case 14: EmitExec(cf, op); break;
                    case 11: EmitJump(cf); break;
                    default: Fail("instrução de controle " + std::to_string(op)); break;
                    }
                }
                if (jumps)
                    code += "break;\n}\npc = -1;\n}\n";   // o GLSL exige um comando depois do último case
            }

            void Assemble()
            {
                std::string h;
                h += "uniform uint B[8];\n";
                if (!pixel)
                {
                    h += "uniform vec4 VC[256];\n";
                    for (size_t i = 0; i < out.fetches.size(); i++)
                        h += "layout(location = " + std::to_string(i) + ") in vec4 a" + std::to_string(i) + ";\n";
                    for (uint32_t i = 0; i < Interpolators; i++)
                        h += "out vec4 v" + std::to_string(i) + ";\n";
                    h += "uniform float wideScale;\n";
                }
                else
                {
                    h += "uniform vec4 PC[256];\n";
                    for (uint32_t i = 0; i < 32; i++)
                        if (out.textureMask >> i & 1)
                            h += "uniform sampler2D T" + std::to_string(i) + ";\n";
                    for (uint32_t i = 0; i < Interpolators; i++)
                        h += "in vec4 v" + std::to_string(i) + ";\n";
                    h += "layout(location = 0) out vec4 color0;\n";
                    h += "uniform int alphaFunc;\nuniform float alphaRef;\n";
                }
                h += "void main()\n{\n";
                h += "    vec4 r[" + std::to_string(maxRegister + 1) + "];\n";
                h += "    for (int i = 0; i < " + std::to_string(maxRegister + 1) + "; i++) r[i] = vec4(0.0);\n";
                h += "    bool p = false, done = false;\n    float ps = 0.0;\n    int addr = 0;\n    vec4 odummy = vec4(0.0);\n";
                if (!pixel)
                {
                    h += "    vec4 opos = vec4(0.0, 0.0, 0.0, 1.0), opts = vec4(0.0);\n";
                    for (uint32_t i = 0; i < Interpolators; i++)
                        h += "    v" + std::to_string(i) + " = vec4(0.0);\n";
                }
                else
                {
                    for (uint32_t i = 0; i < Interpolators && i <= maxRegister; i++)
                        h += "    r[" + std::to_string(i) + "] = v" + std::to_string(i) + ";\n";
                    h += "    color0 = vec4(0.0);\n    vec4 odepth = vec4(gl_FragCoord.z);\n";
                }
                std::string tail;
                if (!pixel)
                {
                    // clip space do D3D -> GL: z de [0, w] para [-w, w]; y invertido para as imagens ficarem com a
                    // linha 0 em cima (como as texturas do console)
                    // widescreen (renderer_gl): wideScale < 1 aperta x nas projeções em perspectiva (w != 1), o que abre o
                    // campo de visão para os lados; o 2D (w = 1: HUD, passes de tela cheia) fica como está
                    tail = "    if (abs(opos.w - 1.0) > 1e-5) opos.x *= wideScale;\n"
                           "    gl_Position = vec4(opos.x, -opos.y, 2.0 * opos.z - opos.w, opos.w);\n";
                    // diagnóstico: DMC3_UNLIT troca a cor iluminada (v1) por cinza neutro, para ver só as texturas
                    static const bool unlit = getenv("DMC3_UNLIT") != nullptr;
                    if (unlit)
                        tail += "    v1 = vec4(0.5);\n";
                }
                else
                {
                    tail =
                        "    bool pass = alphaFunc == 7 ||\n"
                        "        (alphaFunc == 1 && color0.a < alphaRef) || (alphaFunc == 2 && color0.a == alphaRef) ||\n"
                        "        (alphaFunc == 3 && color0.a <= alphaRef) || (alphaFunc == 4 && color0.a > alphaRef) ||\n"
                        "        (alphaFunc == 5 && color0.a != alphaRef) || (alphaFunc == 6 && color0.a >= alphaRef);\n"
                        "    if (!pass) discard;\n";
                    if (out.writesDepth)
                        tail += "    gl_FragDepth = odepth.x;\n";
                }
                out.body = h + code + tail + "}\n";
            }
        };
    }

    Shader Translate(const uint32_t* words, uint32_t count, bool pixel)
    {
        Translator t{ words, count, pixel };
        t.out.pixel = pixel;
        t.Run();
        t.Assemble();
        t.out.ok = t.out.error.empty();
        // constantes que o código usa (VC[n] / PC[n]); um índice que não é número (relativo ao a0) usa todas
        const std::string name = pixel ? "PC[" : "VC[";
        for (size_t at = t.out.body.find(name); at != std::string::npos; at = t.out.body.find(name, at + 1))
        {
            size_t digits = at + name.size();
            if (t.out.body.compare(digits, 4, "256]") == 0)
                continue;   // a própria declaração "uniform vec4 VC[256]"
            if (digits < t.out.body.size() && isdigit(uint8_t(t.out.body[digits])))
            {
                uint32_t index = uint32_t(strtoul(t.out.body.c_str() + digits, nullptr, 10));
                if (index < 256)
                    t.out.constantMask[index / 32] |= 1u << (index % 32);
            }
            else
            {
                t.out.relativeConstants = true;
            }
        }
        return t.out;
    }
}
