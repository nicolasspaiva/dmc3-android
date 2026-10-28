// Base dos testes gerados por gen_insn_tests.py.
#include <ppc_context.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

static std::mt19937_64 rng(0xD3C3);
static int failures = 0;
static const char* current = "";

#define EXPECT(cond) do { if (!(cond)) { if (failures++ < 40) printf("FALHOU %s: %s\n", current, #cond); return; } } while (0)

// elemento i na ordem do PowerPC (o registrador guarda o vetor inteiro invertido)
static uint8_t e8(const PPCVRegister& v, int i) { return v.u8[15 - i]; }
static uint16_t e16(const PPCVRegister& v, int i) { return v.u16[7 - i]; }
static uint32_t e32(const PPCVRegister& v, int i) { return v.u32[3 - i]; }
static float f32e(const PPCVRegister& v, int i) { return v.f32[3 - i]; }

static bool crbit(const PPCContext& c, int bit)
{
    const PPCCRRegister* cr[] = { &c.cr0, &c.cr1, &c.cr2, &c.cr3, &c.cr4, &c.cr5, &c.cr6, &c.cr7 };
    const uint8_t* f[] = { &cr[bit / 4]->lt, &cr[bit / 4]->gt, &cr[bit / 4]->eq, &cr[bit / 4]->so };
    return *f[bit % 4] != 0;
}

static uint64_t be_rd(const uint8_t* m, uint32_t ea, int size)
{
    uint64_t v = 0;
    for (int i = 0; i < size; i++)
        v = (v << 8) | m[ea + i];
    return v;
}

template<class T, class V>
static T sat(V v)
{
    if (v < V(std::numeric_limits<T>::min())) return std::numeric_limits<T>::min();
    if (v > V(std::numeric_limits<T>::max())) return std::numeric_limits<T>::max();
    return T(v);
}

// half -> float de referência, calculado por outro caminho (ldexp) que o da implementação
static float ref_half(uint16_t h)
{
    int e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float s = h & 0x8000 ? -1.0f : 1.0f;
    if (e == 0)
        return s * std::ldexp(float(m), -24);
    if (e == 31)
        return m ? NAN : s * INFINITY;
    return s * std::ldexp(float(m + 1024), e - 25);
}

static bool same_f64(double a, double b)
{
    return (std::isnan(a) && std::isnan(b)) || memcmp(&a, &b, 8) == 0;
}

// valores de borda aparecem com frequência para exercitar saturação e carry
static uint64_t rnd64()
{
    static const uint64_t edge[] = { 0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, ~0ull, 0x8000000000000000ull };
    return rng() % 4 == 0 ? edge[rng() % std::size(edge)] : rng();
}

static float rndf()
{
    static const float edge[] = { 0.0f, -0.0f, 0.5f, 1.0f, -1.0f, 4294967295.0f, 4294967296.0f, 1e20f, -1e20f, NAN, INFINITY, -INFINITY };
    return rng() % 3 == 0 ? edge[rng() % std::size(edge)] : float(int64_t(rng() % 20000000000ull) - 5000000000ll) / float(1 + rng() % 1000);
}

static void randomize_regs(PPCContext& c, int mode);

struct Test
{
    const char* name;
    void (*run)(PPCContext& __restrict, uint8_t*, bool&);
    void (*check)(const PPCContext&, const PPCContext&, const uint8_t*, const uint8_t*, bool);
    int mode; // 0 livre, 1 endereços pequenos, 2 floats nos vetores
};

static int run_all(const Test* tests, size_t count)
{
    const size_t memSize = 0x20000;
    std::vector<uint8_t> M(memSize), mem(memSize);
    for (size_t t = 0; t < count; t++)
    {
        current = tests[t].name;
        int before = failures;
        for (int iter = 0; iter < 20000; iter++)
        {
            static PPCContext pre, ctx;
            randomize_regs(pre, tests[t].mode);
            for (auto& b : M) b = uint8_t(rng());
            ctx = pre;
            mem = M;
            bool taken = false;
            tests[t].run(ctx, mem.data(), taken);
            tests[t].check(pre, ctx, M.data(), mem.data(), taken);
            if (failures != before) break;
        }
        printf("%-40s %s\n", tests[t].name, failures == before ? "ok" : "FALHOU");
    }
    printf("%zu instruções testadas, %d falhas\n", count, failures);
    return failures != 0;
}
