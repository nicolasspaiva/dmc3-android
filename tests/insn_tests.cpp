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

static void randomize_regs(PPCContext& c, int mode)
{
	c.r0.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r1.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r2.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r3.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r4.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r5.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r6.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r7.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r8.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r9.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r10.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r11.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r12.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r13.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r14.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r15.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r16.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r17.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r18.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r19.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r20.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r21.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r22.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r23.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r24.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r25.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r26.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r27.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r28.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r29.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r30.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.r31.u64 = mode == 1 ? 0x1000 + ((rng() % 0x4000) & ~7ull) : rnd64();
	c.f0.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f1.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f2.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f3.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f4.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f5.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f6.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f7.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f8.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f9.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f10.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f11.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f12.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f13.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f14.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f15.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f16.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f17.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f18.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f19.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f20.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f21.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f22.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f23.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f24.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f25.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f26.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f27.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f28.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f29.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f30.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	c.f31.f64 = rng() % 2 ? double(rndf()) : double(rng() % 100000) / 7.0;
	for (int i = 0; i < 2; i++) c.v0.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v1.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v2.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v3.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v4.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v5.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v6.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v7.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v8.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v9.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v10.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v11.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v12.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v13.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v14.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v15.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v16.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v17.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v18.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v19.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v20.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v21.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v22.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v23.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v24.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v25.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v26.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v27.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v28.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v29.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v30.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v31.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v32.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v33.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v34.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v35.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v36.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v37.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v38.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v39.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v40.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v41.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v42.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v43.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v44.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v45.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v46.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v47.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v48.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v49.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v50.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v51.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v52.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v53.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v54.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v55.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v56.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v57.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v58.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v59.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v60.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v61.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v62.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v63.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v64.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v65.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v66.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v67.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v68.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v69.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v70.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v71.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v72.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v73.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v74.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v75.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v76.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v77.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v78.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v79.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v80.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v81.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v82.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v83.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v84.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v85.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v86.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v87.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v88.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v89.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v90.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v91.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v92.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v93.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v94.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v95.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v96.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v97.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v98.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v99.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v100.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v101.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v102.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v103.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v104.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v105.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v106.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v107.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v108.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v109.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v110.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v111.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v112.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v113.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v114.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v115.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v116.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v117.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v118.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v119.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v120.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v121.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v122.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v123.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v124.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v125.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v126.u64[i] = rnd64();
	for (int i = 0; i < 2; i++) c.v127.u64[i] = rnd64();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v0.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v1.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v2.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v3.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v4.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v5.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v6.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v7.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v8.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v9.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v10.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v11.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v12.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v13.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v14.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v15.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v16.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v17.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v18.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v19.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v20.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v21.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v22.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v23.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v24.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v25.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v26.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v27.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v28.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v29.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v30.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v31.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v32.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v33.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v34.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v35.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v36.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v37.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v38.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v39.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v40.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v41.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v42.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v43.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v44.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v45.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v46.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v47.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v48.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v49.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v50.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v51.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v52.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v53.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v54.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v55.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v56.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v57.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v58.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v59.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v60.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v61.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v62.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v63.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v64.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v65.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v66.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v67.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v68.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v69.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v70.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v71.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v72.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v73.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v74.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v75.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v76.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v77.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v78.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v79.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v80.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v81.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v82.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v83.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v84.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v85.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v86.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v87.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v88.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v89.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v90.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v91.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v92.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v93.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v94.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v95.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v96.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v97.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v98.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v99.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v100.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v101.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v102.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v103.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v104.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v105.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v106.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v107.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v108.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v109.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v110.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v111.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v112.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v113.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v114.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v115.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v116.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v117.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v118.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v119.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v120.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v121.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v122.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v123.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v124.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v125.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v126.f32[i] = rndf();
	if (mode == 2) for (int i = 0; i < 4; i++) c.v127.f32[i] = rndf();
	c.cr0.lt = rng() & 1; c.cr0.gt = rng() & 1; c.cr0.eq = rng() & 1; c.cr0.so = rng() & 1;
	c.cr1.lt = rng() & 1; c.cr1.gt = rng() & 1; c.cr1.eq = rng() & 1; c.cr1.so = rng() & 1;
	c.cr2.lt = rng() & 1; c.cr2.gt = rng() & 1; c.cr2.eq = rng() & 1; c.cr2.so = rng() & 1;
	c.cr3.lt = rng() & 1; c.cr3.gt = rng() & 1; c.cr3.eq = rng() & 1; c.cr3.so = rng() & 1;
	c.cr4.lt = rng() & 1; c.cr4.gt = rng() & 1; c.cr4.eq = rng() & 1; c.cr4.so = rng() & 1;
	c.cr5.lt = rng() & 1; c.cr5.gt = rng() & 1; c.cr5.eq = rng() & 1; c.cr5.so = rng() & 1;
	c.cr6.lt = rng() & 1; c.cr6.gt = rng() & 1; c.cr6.eq = rng() & 1; c.cr6.so = rng() & 1;
	c.cr7.lt = rng() & 1; c.cr7.gt = rng() & 1; c.cr7.eq = rng() & 1; c.cr7.so = rng() & 1;
	c.xer.ca = rng() & 1;
	c.ctr.u64 = rng() % 2 ? rng() % 4 : rnd64();
}

// addc r5,r6,r7
static void t_addc(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.xer.ca = ctx.r6.u32 + ctx.r7.u32 < ctx.r6.u32;
	ctx.r5.u64 = ctx.r6.u64 + ctx.r7.u64;
}

static void c_addc(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r5.u64 == pre.r6.u64 + pre.r7.u64); EXPECT(ctx.xer.ca == ((uint64_t(pre.r6.u32) + pre.r7.u32) >> 32));
}

// addme r7,r8
static void t_addme(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	temp.u8 = (ctx.r8.u32 != 0) | ctx.xer.ca;
	ctx.r7.s64 = ctx.r8.s64 + ctx.xer.ca - 1;
	ctx.xer.ca = temp.u8;
}

static void c_addme(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r7.u64 == pre.r8.u64 + pre.xer.ca - 1); EXPECT(ctx.xer.ca == ((uint64_t(pre.r8.u32) + pre.xer.ca + 0xFFFFFFFFull) >> 32));
}

// bdnzt eq,0x826ac8e4
static void t_bdnzt(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	--ctx.ctr.u64;
	if (ctx.ctr.u32 != 0 && ctx.cr0.eq) taken = true;
}

static void c_bdnzt(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	uint32_t c = uint32_t(pre.ctr.u64 - 1); EXPECT(ctx.ctr.u32 == c); EXPECT(taken == (c != 0 && crbit(pre, 2)));
}

// bdzf 4*cr6+eq,0x821207ec
static void t_bdzf(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	--ctx.ctr.u64;
	if (ctx.ctr.u32 == 0 && !ctx.cr6.eq) taken = true;
}

static void c_bdzf(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	uint32_t c = uint32_t(pre.ctr.u64 - 1); EXPECT(ctx.ctr.u32 == c); EXPECT(taken == (c == 0 && !crbit(pre, 26)));
}

// cror 4*cr1+eq,lt,4*cr6+eq
static void t_cror(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.cr1.eq = ctx.cr0.lt | ctx.cr6.eq;
}

static void c_cror(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(crbit(ctx, 6) == (crbit(pre, 0) | crbit(pre, 26)));
}

// crorc eq,4*cr1+eq,4*cr5+lt
static void t_crorc(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.cr0.eq = ctx.cr1.eq | !ctx.cr5.lt;
}

static void c_crorc(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(crbit(ctx, 2) == (crbit(pre, 6) | !crbit(pre, 20)));
}

// eqv r6,r8,r30
static void t_eqv(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.r6.u64 = ~(ctx.r8.u64 ^ ctx.r30.u64);
}

static void c_eqv(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r6.u64 == ~(pre.r8.u64 ^ pre.r30.u64));
}

// lbzux r7,r9,r31
static void t_lbzux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r9.u32 + ctx.r31.u32;
	ctx.r7.u64 = PPC_LOAD_U8(ea);
	ctx.r9.u32 = ea;
}

static void c_lbzux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r9.u32 + pre.r31.u32;
	EXPECT(ctx.r7.u64 == M[EA]); EXPECT(ctx.r9.u32 == EA);
}

// ldux r9,r3,r4
static void t_ldux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r3.u32 + ctx.r4.u32;
	ctx.r9.u64 = PPC_LOAD_U64(ea);
	ctx.r3.u32 = ea;
}

static void c_ldux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r3.u32 + pre.r4.u32;
	EXPECT(ctx.r9.u64 == be_rd(M, EA, 8)); EXPECT(ctx.r3.u32 == EA);
}

// lfdu f0,8(r10)
static void t_lfdu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 8 + ctx.r10.u32;
	ctx.f0.u64 = PPC_LOAD_U64(ea);
	ctx.r10.u32 = ea;
}

static void c_lfdu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r10.u32 + (8));
	EXPECT(ctx.f0.u64 == be_rd(M, EA, 8)); EXPECT(ctx.r10.u32 == EA);
}

// lfsu f0,4(r11)
static void t_lfsu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 4 + ctx.r11.u32;
	temp.u32 = PPC_LOAD_U32(ea);
	ctx.f0.f64 = double(temp.f32);
	ctx.r11.u32 = ea;
}

static void c_lfsu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r11.u32 + (4));
	uint32_t w = uint32_t(be_rd(M, EA, 4)); float fv; memcpy(&fv, &w, 4); EXPECT(same_f64(ctx.f0.f64, double(fv))); EXPECT(ctx.r11.u32 == EA);
}

// lfsux f0,r10,r11
static void t_lfsux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.fpscr.disableFlushMode();
	ea = ctx.r10.u32 + ctx.r11.u32;
	temp.u32 = PPC_LOAD_U32(ea);
	ctx.f0.f64 = double(temp.f32);
	ctx.r10.u32 = ea;
}

static void c_lfsux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r10.u32 + pre.r11.u32;
	uint32_t w = uint32_t(be_rd(M, EA, 4)); float fv; memcpy(&fv, &w, 4); EXPECT(same_f64(ctx.f0.f64, double(fv))); EXPECT(ctx.r10.u32 == EA);
}

// lhau r7,2(r9)
static void t_lhau(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 2 + ctx.r9.u32;
	ctx.r7.s64 = int16_t(PPC_LOAD_U16(ea));
	ctx.r9.u32 = ea;
}

static void c_lhau(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r9.u32 + (2));
	EXPECT(ctx.r7.s64 == int16_t(be_rd(M, EA, 2))); EXPECT(ctx.r9.u32 == EA);
}

// lhbrx r8,0,r10
static void t_lhbrx(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.r8.u64 = __builtin_bswap16(PPC_LOAD_U16(ctx.r10.u32));
}

static void c_lhbrx(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r10.u32;
	EXPECT(ctx.r8.u64 == (M[EA] | (M[EA + 1] << 8)));
}

// lhzu r11,4(r28)
static void t_lhzu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 4 + ctx.r28.u32;
	ctx.r11.u64 = PPC_LOAD_U16(ea);
	ctx.r28.u32 = ea;
}

static void c_lhzu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r28.u32 + (4));
	EXPECT(ctx.r11.u64 == be_rd(M, EA, 2)); EXPECT(ctx.r28.u32 == EA);
}

// lhzux r9,r11,r10
static void t_lhzux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r11.u32 + ctx.r10.u32;
	ctx.r9.u64 = PPC_LOAD_U16(ea);
	ctx.r11.u32 = ea;
}

static void c_lhzux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r11.u32 + pre.r10.u32;
	EXPECT(ctx.r9.u64 == be_rd(M, EA, 2)); EXPECT(ctx.r11.u32 == EA);
}

// lwzux r3,r30,r29
static void t_lwzux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r30.u32 + ctx.r29.u32;
	ctx.r3.u64 = PPC_LOAD_U32(ea);
	ctx.r30.u32 = ea;
}

static void c_lwzux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r30.u32 + pre.r29.u32;
	EXPECT(ctx.r3.u64 == be_rd(M, EA, 4)); EXPECT(ctx.r30.u32 == EA);
}

// mulhd r9,r10,r7
static void t_mulhd(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.r9.s64 = int64_t((__int128(ctx.r10.s64) * __int128(ctx.r7.s64)) >> 64);
}

static void c_mulhd(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r9.s64 == int64_t(((__int128)pre.r10.s64 * pre.r7.s64) >> 64));
}

// mulhdu r10,r3,r6
static void t_mulhdu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.r10.u64 = uint64_t(((unsigned __int128)ctx.r3.u64 * (unsigned __int128)ctx.r6.u64) >> 64);
}

static void c_mulhdu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r10.u64 == uint64_t(((unsigned __int128)pre.r3.u64 * pre.r6.u64) >> 64));
}

// stbux r30,r31,r26
static void t_stbux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r31.u32 + ctx.r26.u32;
	PPC_STORE_U8(ea, ctx.r30.u8);
	ctx.r31.u32 = ea;
}

static void c_stbux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r31.u32 + pre.r26.u32;
	EXPECT(mem[EA] == pre.r30.u8); EXPECT(ctx.r31.u32 == EA);
}

// stdux r8,r11,r6
static void t_stdux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r11.u32 + ctx.r6.u32;
	PPC_STORE_U64(ea, ctx.r8.u64);
	ctx.r11.u32 = ea;
}

static void c_stdux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r11.u32 + pre.r6.u32;
	EXPECT(be_rd(mem, EA, 8) == pre.r8.u64); EXPECT(ctx.r11.u32 == EA);
}

// stfdu f10,8(r31)
static void t_stfdu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 8 + ctx.r31.u32;
	PPC_STORE_U64(ea, ctx.f10.u64);
	ctx.r31.u32 = ea;
}

static void c_stfdu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r31.u32 + (8));
	EXPECT(be_rd(mem, EA, 8) == pre.f10.u64); EXPECT(ctx.r31.u32 == EA);
}

// stfsu f13,4(r10)
static void t_stfsu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 4 + ctx.r10.u32;
	temp.f32 = float(ctx.f13.f64);
	PPC_STORE_U32(ea, temp.u32);
	ctx.r10.u32 = ea;
}

static void c_stfsu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r10.u32 + (4));
	float fv = float(pre.f13.f64); uint32_t w; memcpy(&w, &fv, 4); EXPECT(be_rd(mem, EA, 4) == w); EXPECT(ctx.r10.u32 == EA);
}

// stfsux f13,r9,r11
static void t_stfsux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r9.u32 + ctx.r11.u32;
	temp.f32 = float(ctx.f13.f64);
	PPC_STORE_U32(ea, temp.u32);
	ctx.r9.u32 = ea;
}

static void c_stfsux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r9.u32 + pre.r11.u32;
	float fv = float(pre.f13.f64); uint32_t w; memcpy(&w, &fv, 4); EXPECT(be_rd(mem, EA, 4) == w); EXPECT(ctx.r9.u32 == EA);
}

// sthu r9,2(r11)
static void t_sthu(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = 2 + ctx.r11.u32;
	PPC_STORE_U16(ea, ctx.r9.u16);
	ctx.r11.u32 = ea;
}

static void c_sthu(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = uint32_t(pre.r11.u32 + (2));
	EXPECT(be_rd(mem, EA, 2) == pre.r9.u16); EXPECT(ctx.r11.u32 == EA);
}

// sthux r5,r11,r9
static void t_sthux(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ea = ctx.r11.u32 + ctx.r9.u32;
	PPC_STORE_U16(ea, ctx.r5.u16);
	ctx.r11.u32 = ea;
}

static void c_sthux(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	uint32_t EA = pre.r11.u32 + pre.r9.u32;
	EXPECT(be_rd(mem, EA, 2) == pre.r5.u16); EXPECT(ctx.r11.u32 == EA);
}

// subfze r3,r4
static void t_subfze(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	temp.u8 = uint32_t(~ctx.r4.u32 + ctx.xer.ca) < ~ctx.r4.u32;
	ctx.r3.u64 = ~ctx.r4.u64 + ctx.xer.ca;
	ctx.xer.ca = temp.u8;
}

static void c_subfze(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r3.u64 == ~pre.r4.u64 + pre.xer.ca); EXPECT(ctx.xer.ca == ((uint64_t(~pre.r4.u32) + pre.xer.ca) >> 32));
}

// subfze. r27,r10
static void t_subfze_(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	temp.u8 = uint32_t(~ctx.r10.u32 + ctx.xer.ca) < ~ctx.r10.u32;
	ctx.r27.u64 = ~ctx.r10.u64 + ctx.xer.ca;
	ctx.xer.ca = temp.u8;
	ctx.cr0.compare<int32_t>(ctx.r27.s32, 0, ctx.xer);
}

static void c_subfze_(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	EXPECT(ctx.r27.u64 == ~pre.r10.u64 + pre.xer.ca); EXPECT(ctx.xer.ca == ((uint64_t(~pre.r10.u32) + pre.xer.ca) >> 32)); EXPECT(ctx.cr0.eq == (ctx.r27.s32 == 0)); EXPECT(ctx.cr0.lt == (ctx.r27.s32 < 0)); EXPECT(ctx.cr0.gt == (ctx.r27.s32 > 0));
}

// vaddsbs v13,v13,v13
static void t_vaddsbs(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v13.s8, simde_mm_adds_epi8(simde_mm_load_si128((simde__m128i*)ctx.v13.s8), simde_mm_load_si128((simde__m128i*)ctx.v13.s8)));
}

static void c_vaddsbs(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 16; i++) EXPECT(int8_t(e8(ctx.v13, i)) == sat<int8_t>(int(int8_t(e8(pre.v13, i))) + int8_t(e8(pre.v13, i))));
}

// vaddsws v4,v7,v11
static void t_vaddsws(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	temp.s64 = int64_t(ctx.v7.s32[0]) + int64_t(ctx.v11.s32[0]);
	ctx.v4.s32[0] = temp.s64 > INT_MAX ? INT_MAX : temp.s64 < INT_MIN ? INT_MIN : temp.s64;
	temp.s64 = int64_t(ctx.v7.s32[1]) + int64_t(ctx.v11.s32[1]);
	ctx.v4.s32[1] = temp.s64 > INT_MAX ? INT_MAX : temp.s64 < INT_MIN ? INT_MIN : temp.s64;
	temp.s64 = int64_t(ctx.v7.s32[2]) + int64_t(ctx.v11.s32[2]);
	ctx.v4.s32[2] = temp.s64 > INT_MAX ? INT_MAX : temp.s64 < INT_MIN ? INT_MIN : temp.s64;
	temp.s64 = int64_t(ctx.v7.s32[3]) + int64_t(ctx.v11.s32[3]);
	ctx.v4.s32[3] = temp.s64 > INT_MAX ? INT_MAX : temp.s64 < INT_MIN ? INT_MIN : temp.s64;
}

static void c_vaddsws(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 4; i++) EXPECT(int32_t(e32(ctx.v4, i)) == sat<int32_t>(int64_t(int32_t(e32(pre.v7, i))) + int32_t(e32(pre.v11, i))));
}

// vavguh v1,v1,v11
static void t_vavguh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v1.u16, simde_mm_avg_epu16(simde_mm_load_si128((simde__m128i*)ctx.v1.u16), simde_mm_load_si128((simde__m128i*)ctx.v11.u16)));
}

static void c_vavguh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v1, i) == (unsigned(e16(pre.v1, i)) + e16(pre.v11, i) + 1) >> 1);
}

// vcfpuxws128 v36,v47,0
static void t_vcfpuxws128(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v36.u32[0] = ppc_vctuxs(ctx.v47.f32[0]);
	ctx.v36.u32[1] = ppc_vctuxs(ctx.v47.f32[1]);
	ctx.v36.u32[2] = ppc_vctuxs(ctx.v47.f32[2]);
	ctx.v36.u32[3] = ppc_vctuxs(ctx.v47.f32[3]);
}

static void c_vcfpuxws128(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 4; i++) { float x = f32e(pre.v47, i) * float(1u << 0); uint32_t want = !(x > 0) ? 0 : x >= 4294967296.0f ? 0xFFFFFFFFu : uint32_t(x); EXPECT(e32(ctx.v36, i) == want); }
}

// vcmpequh v4,v30,v24
static void t_vcmpequh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v4.u16, simde_mm_cmpeq_epi16(simde_mm_load_si128((simde__m128i*)ctx.v30.u16), simde_mm_load_si128((simde__m128i*)ctx.v24.u16)));
}

static void c_vcmpequh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v4, i) == (e16(pre.v30, i) == e16(pre.v24, i) ? 0xFFFF : 0));
}

// vcmpequh. v23,v0,v8
static void t_vcmpequh_(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v23.u16, simde_mm_cmpeq_epi16(simde_mm_load_si128((simde__m128i*)ctx.v0.u16), simde_mm_load_si128((simde__m128i*)ctx.v8.u16)));
	ctx.cr6.setFromMask(simde_mm_load_si128((simde__m128i*)ctx.v23.u8), 0xFFFF);
}

static void c_vcmpequh_(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = e16(pre.v0, i) == e16(pre.v8, i); all &= t; none &= !t; EXPECT(e16(ctx.v23, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);
}

// vcmpgtsh v20,v25,v6
static void t_vcmpgtsh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v20.s16, simde_mm_cmpgt_epi16(simde_mm_load_si128((simde__m128i*)ctx.v25.s16), simde_mm_load_si128((simde__m128i*)ctx.v6.s16)));
}

static void c_vcmpgtsh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v20, i) == (int16_t(e16(pre.v25, i)) > int16_t(e16(pre.v6, i)) ? 0xFFFF : 0));
}

// vcmpgtsh. v29,v3,v26
static void t_vcmpgtsh_(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v29.s16, simde_mm_cmpgt_epi16(simde_mm_load_si128((simde__m128i*)ctx.v3.s16), simde_mm_load_si128((simde__m128i*)ctx.v26.s16)));
	ctx.cr6.setFromMask(simde_mm_load_si128((simde__m128i*)ctx.v29.u8), 0xFFFF);
}

static void c_vcmpgtsh_(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = int16_t(e16(pre.v3, i)) > int16_t(e16(pre.v26, i)); all &= t; none &= !t; EXPECT(e16(ctx.v29, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);
}

// vcmpgtsw. v28,v15,v31
static void t_vcmpgtsw_(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v28.s32, simde_mm_cmpgt_epi32(simde_mm_load_si128((simde__m128i*)ctx.v15.s32), simde_mm_load_si128((simde__m128i*)ctx.v31.s32)));
	ctx.cr6.setFromMask(simde_mm_load_si128((simde__m128i*)ctx.v28.u8), 0xFFFF);
}

static void c_vcmpgtsw_(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	bool all = true, none = true; for (int i = 0; i < 4; i++) { bool t = int32_t(e32(pre.v15, i)) > int32_t(e32(pre.v31, i)); all &= t; none &= !t; EXPECT(e32(ctx.v28, i) == (t ? 0xFFFFFFFFu : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);
}

// vcmpgtuh. v12,v13,v0
static void t_vcmpgtuh_(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v12.u8, simde_mm_cmpgt_epu16(simde_mm_load_si128((simde__m128i*)ctx.v13.u16), simde_mm_load_si128((simde__m128i*)ctx.v0.u16)));
	ctx.cr6.setFromMask(simde_mm_load_si128((simde__m128i*)ctx.v12.u8), 0xFFFF);
}

static void c_vcmpgtuh_(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	bool all = true, none = true; for (int i = 0; i < 8; i++) { bool t = e16(pre.v13, i) > e16(pre.v0, i); all &= t; none &= !t; EXPECT(e16(ctx.v12, i) == (t ? 0xFFFF : 0)); } EXPECT(ctx.cr6.lt == all); EXPECT(ctx.cr6.eq == none);
}

// vmaxsh v4,v9,v15
static void t_vmaxsh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v4.s16, simde_mm_max_epi16(simde_mm_load_si128((simde__m128i*)ctx.v9.s16), simde_mm_load_si128((simde__m128i*)ctx.v15.s16)));
}

static void c_vmaxsh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v4, i)) == std::max(int16_t(e16(pre.v9, i)), int16_t(e16(pre.v15, i))));
}

// vminsh v5,v15,v14
static void t_vminsh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v5.s16, simde_mm_min_epi16(simde_mm_load_si128((simde__m128i*)ctx.v15.s16), simde_mm_load_si128((simde__m128i*)ctx.v14.s16)));
}

static void c_vminsh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v5, i)) == std::min(int16_t(e16(pre.v15, i)), int16_t(e16(pre.v14, i))));
}

// vpkshss v18,v19,v19
static void t_vpkshss(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v18.s8, simde_mm_packs_epi16(simde_mm_load_si128((simde__m128i*)ctx.v19.s16), simde_mm_load_si128((simde__m128i*)ctx.v19.s16)));
}

static void c_vpkshss(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 16; i++) EXPECT(int8_t(e8(ctx.v18, i)) == sat<int8_t>(int16_t(i < 8 ? e16(pre.v19, i) : e16(pre.v19, i - 8))));
}

// vpkswss v23,v13,v3
static void t_vpkswss(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v23.s16, simde_mm_packs_epi32(simde_mm_load_si128((simde__m128i*)ctx.v3.s32), simde_mm_load_si128((simde__m128i*)ctx.v13.s32)));
}

static void c_vpkswss(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v23, i)) == sat<int16_t>(int32_t(i < 4 ? e32(pre.v13, i) : e32(pre.v3, i - 4))));
}

// vpkswss128 v19,v58,v61
static void t_vpkswss128(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v19.s16, simde_mm_packs_epi32(simde_mm_load_si128((simde__m128i*)ctx.v61.s32), simde_mm_load_si128((simde__m128i*)ctx.v58.s32)));
}

static void c_vpkswss128(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v19, i)) == sat<int16_t>(int32_t(i < 4 ? e32(pre.v58, i) : e32(pre.v61, i - 4))));
}

// vpkswus128 v36,v33,v36
static void t_vpkswus128(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v36.u16, simde_mm_packus_epi32(simde_mm_load_si128((simde__m128i*)ctx.v36.s32), simde_mm_load_si128((simde__m128i*)ctx.v33.s32)));
}

static void c_vpkswus128(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v36, i) == sat<uint16_t>(int32_t(i < 4 ? e32(pre.v33, i) : e32(pre.v36, i - 4))));
}

// vpkuhus128 v54,v55,v36
static void t_vpkuhus128(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v54.u8, simde_mm_packus_epi16(simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*)ctx.v36.u16), simde_mm_set1_epi16(255)), simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*)ctx.v55.u16), simde_mm_set1_epi16(255))));
}

static void c_vpkuhus128(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 16; i++) EXPECT(e8(ctx.v54, i) == std::min<unsigned>(i < 8 ? e16(pre.v55, i) : e16(pre.v36, i - 8), 255));
}

// vrlh v8,v12,v12
static void t_vrlh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v8.u16[0] = uint16_t((ctx.v12.u16[0] << (ctx.v12.u16[0] & 0xF)) | (ctx.v12.u16[0] >> (16 - (ctx.v12.u16[0] & 0xF))));
	ctx.v8.u16[1] = uint16_t((ctx.v12.u16[1] << (ctx.v12.u16[1] & 0xF)) | (ctx.v12.u16[1] >> (16 - (ctx.v12.u16[1] & 0xF))));
	ctx.v8.u16[2] = uint16_t((ctx.v12.u16[2] << (ctx.v12.u16[2] & 0xF)) | (ctx.v12.u16[2] >> (16 - (ctx.v12.u16[2] & 0xF))));
	ctx.v8.u16[3] = uint16_t((ctx.v12.u16[3] << (ctx.v12.u16[3] & 0xF)) | (ctx.v12.u16[3] >> (16 - (ctx.v12.u16[3] & 0xF))));
	ctx.v8.u16[4] = uint16_t((ctx.v12.u16[4] << (ctx.v12.u16[4] & 0xF)) | (ctx.v12.u16[4] >> (16 - (ctx.v12.u16[4] & 0xF))));
	ctx.v8.u16[5] = uint16_t((ctx.v12.u16[5] << (ctx.v12.u16[5] & 0xF)) | (ctx.v12.u16[5] >> (16 - (ctx.v12.u16[5] & 0xF))));
	ctx.v8.u16[6] = uint16_t((ctx.v12.u16[6] << (ctx.v12.u16[6] & 0xF)) | (ctx.v12.u16[6] >> (16 - (ctx.v12.u16[6] & 0xF))));
	ctx.v8.u16[7] = uint16_t((ctx.v12.u16[7] << (ctx.v12.u16[7] & 0xF)) | (ctx.v12.u16[7] >> (16 - (ctx.v12.u16[7] & 0xF))));
}

static void c_vrlh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) { unsigned n = e16(pre.v12, i) & 15, x = e16(pre.v12, i); EXPECT(e16(ctx.v8, i) == uint16_t((x << n) | (x >> ((16 - n) & 15)) * (n != 0))); }
}

// vslh v5,v13,v10
static void t_vslh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v5.u16[0] = ctx.v13.u16[0] << (ctx.v10.u16[0] & 0xF);
	ctx.v5.u16[1] = ctx.v13.u16[1] << (ctx.v10.u16[1] & 0xF);
	ctx.v5.u16[2] = ctx.v13.u16[2] << (ctx.v10.u16[2] & 0xF);
	ctx.v5.u16[3] = ctx.v13.u16[3] << (ctx.v10.u16[3] & 0xF);
	ctx.v5.u16[4] = ctx.v13.u16[4] << (ctx.v10.u16[4] & 0xF);
	ctx.v5.u16[5] = ctx.v13.u16[5] << (ctx.v10.u16[5] & 0xF);
	ctx.v5.u16[6] = ctx.v13.u16[6] << (ctx.v10.u16[6] & 0xF);
	ctx.v5.u16[7] = ctx.v13.u16[7] << (ctx.v10.u16[7] & 0xF);
}

static void c_vslh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v5, i) == uint16_t(e16(pre.v13, i) << (e16(pre.v10, i) & 15)));
}

// vspltish v13,15
static void t_vspltish(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v13.u16, simde_mm_set1_epi16(short(0xF)));
}

static void c_vspltish(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v13, i)) == 15);
}

// vsrab v31,v10,v13
static void t_vsrab(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v31.s8[0] = ctx.v10.s8[0] >> (ctx.v13.u8[0] & 0x7);
	ctx.v31.s8[1] = ctx.v10.s8[1] >> (ctx.v13.u8[1] & 0x7);
	ctx.v31.s8[2] = ctx.v10.s8[2] >> (ctx.v13.u8[2] & 0x7);
	ctx.v31.s8[3] = ctx.v10.s8[3] >> (ctx.v13.u8[3] & 0x7);
	ctx.v31.s8[4] = ctx.v10.s8[4] >> (ctx.v13.u8[4] & 0x7);
	ctx.v31.s8[5] = ctx.v10.s8[5] >> (ctx.v13.u8[5] & 0x7);
	ctx.v31.s8[6] = ctx.v10.s8[6] >> (ctx.v13.u8[6] & 0x7);
	ctx.v31.s8[7] = ctx.v10.s8[7] >> (ctx.v13.u8[7] & 0x7);
	ctx.v31.s8[8] = ctx.v10.s8[8] >> (ctx.v13.u8[8] & 0x7);
	ctx.v31.s8[9] = ctx.v10.s8[9] >> (ctx.v13.u8[9] & 0x7);
	ctx.v31.s8[10] = ctx.v10.s8[10] >> (ctx.v13.u8[10] & 0x7);
	ctx.v31.s8[11] = ctx.v10.s8[11] >> (ctx.v13.u8[11] & 0x7);
	ctx.v31.s8[12] = ctx.v10.s8[12] >> (ctx.v13.u8[12] & 0x7);
	ctx.v31.s8[13] = ctx.v10.s8[13] >> (ctx.v13.u8[13] & 0x7);
	ctx.v31.s8[14] = ctx.v10.s8[14] >> (ctx.v13.u8[14] & 0x7);
	ctx.v31.s8[15] = ctx.v10.s8[15] >> (ctx.v13.u8[15] & 0x7);
}

static void c_vsrab(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 16; i++) EXPECT(e8(ctx.v31, i) == uint8_t(int8_t(e8(pre.v10, i)) >> (e8(pre.v13, i) & 7)));
}

// vsrah v17,v18,v8
static void t_vsrah(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v17.s16[0] = ctx.v18.s16[0] >> (ctx.v8.u16[0] & 0xF);
	ctx.v17.s16[1] = ctx.v18.s16[1] >> (ctx.v8.u16[1] & 0xF);
	ctx.v17.s16[2] = ctx.v18.s16[2] >> (ctx.v8.u16[2] & 0xF);
	ctx.v17.s16[3] = ctx.v18.s16[3] >> (ctx.v8.u16[3] & 0xF);
	ctx.v17.s16[4] = ctx.v18.s16[4] >> (ctx.v8.u16[4] & 0xF);
	ctx.v17.s16[5] = ctx.v18.s16[5] >> (ctx.v8.u16[5] & 0xF);
	ctx.v17.s16[6] = ctx.v18.s16[6] >> (ctx.v8.u16[6] & 0xF);
	ctx.v17.s16[7] = ctx.v18.s16[7] >> (ctx.v8.u16[7] & 0xF);
}

static void c_vsrah(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v17, i) == uint16_t(int16_t(e16(pre.v18, i)) >> (e16(pre.v8, i) & 15)));
}

// vsrh v5,v13,v13
static void t_vsrh(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	ctx.v5.u16[0] = ctx.v13.u16[0] >> (ctx.v13.u16[0] & 0xF);
	ctx.v5.u16[1] = ctx.v13.u16[1] >> (ctx.v13.u16[1] & 0xF);
	ctx.v5.u16[2] = ctx.v13.u16[2] >> (ctx.v13.u16[2] & 0xF);
	ctx.v5.u16[3] = ctx.v13.u16[3] >> (ctx.v13.u16[3] & 0xF);
	ctx.v5.u16[4] = ctx.v13.u16[4] >> (ctx.v13.u16[4] & 0xF);
	ctx.v5.u16[5] = ctx.v13.u16[5] >> (ctx.v13.u16[5] & 0xF);
	ctx.v5.u16[6] = ctx.v13.u16[6] >> (ctx.v13.u16[6] & 0xF);
	ctx.v5.u16[7] = ctx.v13.u16[7] >> (ctx.v13.u16[7] & 0xF);
}

static void c_vsrh(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(e16(ctx.v5, i) == uint16_t(e16(pre.v13, i) >> (e16(pre.v13, i) & 15)));
}

// vsubshs v6,v8,v13
static void t_vsubshs(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v6.s16, simde_mm_subs_epi16(simde_mm_load_si128((simde__m128i*)ctx.v8.s16), simde_mm_load_si128((simde__m128i*)ctx.v13.s16)));
}

static void c_vsubshs(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 8; i++) EXPECT(int16_t(e16(ctx.v6, i)) == sat<int16_t>(int(int16_t(e16(pre.v8, i))) - int16_t(e16(pre.v13, i))));
}

// vsububm v10,v12,v0
static void t_vsububm(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	simde_mm_store_si128((simde__m128i*)ctx.v10.u8, simde_mm_sub_epi8(simde_mm_load_si128((simde__m128i*)ctx.v12.u8), simde_mm_load_si128((simde__m128i*)ctx.v0.u8)));
}

static void c_vsububm(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 16; i++) EXPECT(e8(ctx.v10, i) == uint8_t(e8(pre.v12, i) - e8(pre.v0, i)));
}

// vupkd3d128 v62,v63,20
static void t_vupkd3d128(PPCContext& __restrict ctx, uint8_t* base, bool& taken)
{
	PPCRegister temp{}; PPCVRegister vTemp{}; uint32_t ea{}; (void)temp; (void)vTemp; (void)ea; (void)taken;
	vTemp.f32[0] = ppc_half_to_float(ctx.v63.u16[0]);
	vTemp.f32[1] = ppc_half_to_float(ctx.v63.u16[1]);
	vTemp.f32[2] = ppc_half_to_float(ctx.v63.u16[2]);
	vTemp.f32[3] = ppc_half_to_float(ctx.v63.u16[3]);
	ctx.v62 = vTemp;
}

static void c_vupkd3d128(const PPCContext& pre, const PPCContext& ctx, const uint8_t* M, const uint8_t* mem, bool taken)
{
	(void)M; (void)mem; (void)taken;
	
	for (int i = 0; i < 4; i++) EXPECT(same_f64(f32e(ctx.v62, i), ref_half(e16(pre.v63, 4 + i))));
}

static const Test TESTS[] = {
	{ "addc r5,r6,r7", t_addc, c_addc, 0 },
	{ "addme r7,r8", t_addme, c_addme, 0 },
	{ "bdnzt eq,0x826ac8e4", t_bdnzt, c_bdnzt, 0 },
	{ "bdzf 4*cr6+eq,0x821207ec", t_bdzf, c_bdzf, 0 },
	{ "cror 4*cr1+eq,lt,4*cr6+eq", t_cror, c_cror, 0 },
	{ "crorc eq,4*cr1+eq,4*cr5+lt", t_crorc, c_crorc, 0 },
	{ "eqv r6,r8,r30", t_eqv, c_eqv, 0 },
	{ "lbzux r7,r9,r31", t_lbzux, c_lbzux, 1 },
	{ "ldux r9,r3,r4", t_ldux, c_ldux, 1 },
	{ "lfdu f0,8(r10)", t_lfdu, c_lfdu, 1 },
	{ "lfsu f0,4(r11)", t_lfsu, c_lfsu, 1 },
	{ "lfsux f0,r10,r11", t_lfsux, c_lfsux, 1 },
	{ "lhau r7,2(r9)", t_lhau, c_lhau, 1 },
	{ "lhbrx r8,0,r10", t_lhbrx, c_lhbrx, 1 },
	{ "lhzu r11,4(r28)", t_lhzu, c_lhzu, 1 },
	{ "lhzux r9,r11,r10", t_lhzux, c_lhzux, 1 },
	{ "lwzux r3,r30,r29", t_lwzux, c_lwzux, 1 },
	{ "mulhd r9,r10,r7", t_mulhd, c_mulhd, 0 },
	{ "mulhdu r10,r3,r6", t_mulhdu, c_mulhdu, 0 },
	{ "stbux r30,r31,r26", t_stbux, c_stbux, 1 },
	{ "stdux r8,r11,r6", t_stdux, c_stdux, 1 },
	{ "stfdu f10,8(r31)", t_stfdu, c_stfdu, 1 },
	{ "stfsu f13,4(r10)", t_stfsu, c_stfsu, 1 },
	{ "stfsux f13,r9,r11", t_stfsux, c_stfsux, 1 },
	{ "sthu r9,2(r11)", t_sthu, c_sthu, 1 },
	{ "sthux r5,r11,r9", t_sthux, c_sthux, 1 },
	{ "subfze r3,r4", t_subfze, c_subfze, 0 },
	{ "subfze. r27,r10", t_subfze_, c_subfze_, 0 },
	{ "vaddsbs v13,v13,v13", t_vaddsbs, c_vaddsbs, 0 },
	{ "vaddsws v4,v7,v11", t_vaddsws, c_vaddsws, 0 },
	{ "vavguh v1,v1,v11", t_vavguh, c_vavguh, 0 },
	{ "vcfpuxws128 v36,v47,0", t_vcfpuxws128, c_vcfpuxws128, 2 },
	{ "vcmpequh v4,v30,v24", t_vcmpequh, c_vcmpequh, 0 },
	{ "vcmpequh. v23,v0,v8", t_vcmpequh_, c_vcmpequh_, 0 },
	{ "vcmpgtsh v20,v25,v6", t_vcmpgtsh, c_vcmpgtsh, 0 },
	{ "vcmpgtsh. v29,v3,v26", t_vcmpgtsh_, c_vcmpgtsh_, 0 },
	{ "vcmpgtsw. v28,v15,v31", t_vcmpgtsw_, c_vcmpgtsw_, 0 },
	{ "vcmpgtuh. v12,v13,v0", t_vcmpgtuh_, c_vcmpgtuh_, 0 },
	{ "vmaxsh v4,v9,v15", t_vmaxsh, c_vmaxsh, 0 },
	{ "vminsh v5,v15,v14", t_vminsh, c_vminsh, 0 },
	{ "vpkshss v18,v19,v19", t_vpkshss, c_vpkshss, 0 },
	{ "vpkswss v23,v13,v3", t_vpkswss, c_vpkswss, 0 },
	{ "vpkswss128 v19,v58,v61", t_vpkswss128, c_vpkswss128, 0 },
	{ "vpkswus128 v36,v33,v36", t_vpkswus128, c_vpkswus128, 0 },
	{ "vpkuhus128 v54,v55,v36", t_vpkuhus128, c_vpkuhus128, 0 },
	{ "vrlh v8,v12,v12", t_vrlh, c_vrlh, 0 },
	{ "vslh v5,v13,v10", t_vslh, c_vslh, 0 },
	{ "vspltish v13,15", t_vspltish, c_vspltish, 0 },
	{ "vsrab v31,v10,v13", t_vsrab, c_vsrab, 0 },
	{ "vsrah v17,v18,v8", t_vsrah, c_vsrah, 0 },
	{ "vsrh v5,v13,v13", t_vsrh, c_vsrh, 0 },
	{ "vsubshs v6,v8,v13", t_vsubshs, c_vsubshs, 0 },
	{ "vsububm v10,v12,v0", t_vsububm, c_vsububm, 0 },
	{ "vupkd3d128 v62,v63,20", t_vupkd3d128, c_vupkd3d128, 0 },
};

int main() { return run_all(TESTS, sizeof(TESTS) / sizeof(TESTS[0])); }
