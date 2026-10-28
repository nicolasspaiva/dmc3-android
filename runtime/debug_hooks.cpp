// Correções de compatibilidade e diagnóstico.
#include "kernel.h"

#include <algorithm>
#include <atomic>
#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <vector>

GUEST_DECLARE_ORIGINAL(82582580);   // png_error(png_struct*, const char* message)
GUEST_DECLARE_ORIGINAL(824368D0);

namespace
{
    // zlib 1.2.5 do jogo
    constexpr uint32_t InflateInit2 = 0x8258CF68;   // inflateInit2_(z_stream*, windowBits, version, stream_size)
    constexpr uint32_t Inflate = 0x8258D1C8;        // inflate(z_stream*, flush)
    constexpr uint32_t ZlibVersion = 0x8202BDE4;    // "1.2.5"

    // DMC3_ZLIB_TEST=<arquivo>: roda o zlib recompilado sobre casos gerados por tests/make_zlib_vectors.py
    // e compara com o original. Precisa do CRT do jogo inicializado (o zlib aloca pelo malloc dele).
    void RunZlibTest(const PPCContext& ctx, const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (!f)
        {
            fprintf(stderr, "[zlib-test] não abri %s\n", path);
            return;
        }
        std::vector<uint8_t> file;
        uint8_t chunk[65536];
        for (size_t n; (n = fread(chunk, 1, sizeof(chunk), f)) > 0;)
            file.insert(file.end(), chunk, chunk + n);
        fclose(f);

        int cases = 0, ok = 0;
        for (size_t p = 0; p + 8 <= file.size(); cases++)
        {
            uint32_t rawSize, compSize;
            memcpy(&rawSize, &file[p], 4);
            memcpy(&compSize, &file[p + 4], 4);
            const uint8_t* raw = &file[p + 8];
            const uint8_t* comp = raw + rawSize;
            p += 8 + rawSize + compSize;

            uint32_t strm = kernel::AllocPhysical(64, 16);
            uint32_t in = kernel::AllocPhysical(compSize + 16);
            uint32_t out = kernel::AllocPhysical(rawSize + 64);
            memset(kernel::Guest<uint8_t>(strm), 0, 64);
            memcpy(kernel::Guest<uint8_t>(in), comp, compSize);
            memset(kernel::Guest<uint8_t>(out), 0xCD, rawSize + 64);

            // casos pares: uma chamada só; ímpares: em pedaços (entrada de 4 KB, saída de 16 KB), como o leitor de zip
            bool chunked = cases & 1;
            uint32_t init = kernel::CallGuest(ctx, InflateInit2, { strm, uint32_t(-15), ZlibVersion, 56 });
            // z_stream: next_in, avail_in, total_in, next_out, avail_out, total_out, msg, ...
            uint32_t result = 0;
            if (!chunked)
            {
                kernel::Store32(strm + 0, in);
                kernel::Store32(strm + 4, compSize);
                kernel::Store32(strm + 12, out);
                kernel::Store32(strm + 16, rawSize + 64);
                result = kernel::CallGuest(ctx, Inflate, { strm, 4 /* Z_FINISH */ });
            }
            else
            {
                uint32_t fed = 0, room = 0;
                kernel::Store32(strm + 0, in);
                kernel::Store32(strm + 12, out);
                for (int guard = 0; guard < 100000; guard++)
                {
                    if (kernel::Load32(strm + 4) == 0 && fed < compSize)
                    {
                        uint32_t n = std::min<uint32_t>(0x1000, compSize - fed);
                        kernel::Store32(strm + 4, n);
                        fed += n;
                    }
                    if (kernel::Load32(strm + 16) == 0)
                    {
                        uint32_t n = std::min<uint32_t>(0x4000, rawSize + 64 - room);
                        kernel::Store32(strm + 16, n);
                        room += n;
                    }
                    result = kernel::CallGuest(ctx, Inflate, { strm, 0 /* Z_NO_FLUSH */ });
                    if (int32_t(result) != 0)
                        break;
                }
            }
            uint32_t produced = kernel::Load32(strm + 20);
            uint32_t msg = kernel::Load32(strm + 24);

            const uint8_t* got = kernel::Guest<uint8_t>(out);
            size_t mismatch = SIZE_MAX;
            for (size_t i = 0; i < rawSize; i++)
                if (got[i] != raw[i])
                {
                    mismatch = i;
                    break;
                }
            bool pass = init == 0 && int32_t(result) == 1 && produced == rawSize && mismatch == SIZE_MAX;
            ok += pass;
            fprintf(stderr, "[zlib-test] caso %2d (%s): %u -> %u bytes, init %d, inflate %d, saída %u%s", cases,
                chunked ? "em pedaços" : "de uma vez", compSize, rawSize, int32_t(init), int32_t(result), produced, pass ? " ok\n" : "");
            if (!pass)
            {
                fprintf(stderr, " FALHOU");
                if (msg)
                    fprintf(stderr, " (msg: %s)", kernel::Guest<char>(msg));
                if (mismatch != SIZE_MAX)
                    fprintf(stderr, " primeiro byte diferente em %zu: esperado %02X, veio %02X", mismatch, raw[mismatch], got[mismatch]);
                fprintf(stderr, "\n");
            }
        }
        fprintf(stderr, "[zlib-test] %d de %d casos corretos\n", ok, cases);
    }
}
   // alocador de blocos do jogo (pool com bitmap de 1 byte por bloco)

// DMC3_LOG_POOL: registra cada alocação do alocador de blocos
// r3 = estado do alocador, r4 = pool { bitmap, base, nº de blocos, tamanho do bloco, ... }, r5 = tamanho
namespace
{
    constexpr uint32_t GuestMemset = 0x825901E0;   // memset(dest, value, count) do CRT do jogo
    constexpr uint32_t GuestMemcpy = 0x8258FCB0;   // memcpy(dest, src, count)

    // DMC3_CRT_TEST: memset e memcpy do jogo para todos os tamanhos de 0 a 300 e 8 alinhamentos
    void RunCrtTest(const PPCContext& ctx)
    {
        uint32_t area = kernel::AllocPhysical(0x1000, 0x100);
        uint32_t src = kernel::AllocPhysical(0x1000, 0x100);
        uint8_t* a = kernel::Guest<uint8_t>(area);
        uint8_t* s = kernel::Guest<uint8_t>(src);
        for (int i = 0; i < 0x1000; i++)
            s[i] = uint8_t(i * 7 + 3);
        int failures = 0;
        for (int fn = 0; fn < 2; fn++)
            for (uint32_t align = 0; align < 8; align++)
                for (uint32_t n = 0; n <= 300; n++)
                {
                    memset(a, 0xEE, 0x1000);
                    uint32_t dest = area + 64 + align;
                    if (fn == 0)
                        kernel::CallGuest(ctx, GuestMemset, { dest, 1, n });
                    else
                        kernel::CallGuest(ctx, GuestMemcpy, { dest, src + 3 + align, n });
                    for (uint32_t i = 0; i < 0x1000; i++)
                    {
                        uint8_t want = 0xEE;
                        if (i >= 64 + align && i < 64 + align + n)
                            want = fn == 0 ? 1 : s[3 + align + (i - 64 - align)];
                        if (a[i] != want)
                        {
                            if (failures++ < 20)
                                fprintf(stderr, "[crt-test] %s(alinhamento %u, n %u): byte %d = %02X, esperado %02X\n",
                                    fn == 0 ? "memset" : "memcpy", align, n, int(i) - int(64 + align), a[i], want);
                            break;
                        }
                    }
                }
        fprintf(stderr, "[crt-test] %d falhas em %d casos\n", failures, 2 * 8 * 301);
    }
}

GUEST_OVERRIDE(824368D0)
{
    // os testes rodam na primeira alocação do jogo, quando o CRT dele já está inicializado
    if (static bool once = false; !once && (getenv("DMC3_CRT_TEST") || getenv("DMC3_ZLIB_TEST")))
    {
        once = true;
        if (getenv("DMC3_CRT_TEST"))
            RunCrtTest(ctx);
        if (const char* vectors = getenv("DMC3_ZLIB_TEST"))
            RunZlibTest(ctx, vectors);
        fflush(stderr);
        exit(0);
    }
    uint32_t pool = ctx.r4.u32, size = ctx.r5.u32, lr = uint32_t(ctx.lr);
    GUEST_ORIGINAL(824368D0)(ctx, base);
    static bool log = getenv("DMC3_LOG_POOL") != nullptr;
    if (log && pool)
    {
        fprintf(stderr, "[pool] pool %08X (base %08X, %u blocos de %X): %X bytes -> %08X-%08X (de %08X); bitmap:", pool,
            kernel::Load32(pool + 4), kernel::Load32(pool + 8), kernel::Load32(pool + 12), size, ctx.r3.u32,
            ctx.r3.u32 + size, lr);
        const uint8_t* bitmap = kernel::Guest<uint8_t>(kernel::Load32(pool));
        for (int i = 0; i < 16; i++)
            fprintf(stderr, " %d", bitmap[i]);
        fprintf(stderr, "\n");
    }
}
GUEST_DECLARE_ORIGINAL(82446840);   // carregador de PNG do jogo (buffer, tamanho em r5/r6)

// longjmp do CRT (0x82593880). O XenonRecomp troca as chamadas diretas pelo longjmp nativo, e o setjmp
// grava um jmp_buf do host. Mas a libpng 1.5 do jogo recebe o longjmp por ponteiro
// (png_set_longjmp_fn(png, 0x82593880, 0x540)) e chama indiretamente: sem esta substituição, a chamada
// cairia no longjmp recompilado, que lê o jmp_buf no formato do PowerPC e restaura lixo (r1 = 0).
GUEST_OVERRIDE(82593880)
{
    longjmp(*reinterpret_cast<jmp_buf*>(base + ctx.r3.u32), ctx.r4.s32);
}

// DMC3_DUMP_PNG=<pasta>: grava cada PNG que o jogo decodifica, para conferir fora do jogo
GUEST_OVERRIDE(82446840)
{
    if (const char* dir = getenv("DMC3_DUMP_PNG"))
    {
        static std::atomic<int> count{ 0 };
        char path[512];
        snprintf(path, sizeof(path), "%s\\png_%04d.png", dir, count++);
        if (FILE* f = fopen(path, "wb"))
        {
            fwrite(kernel::Guest<uint8_t>(ctx.r5.u32), 1, ctx.r6.u32, f);
            fclose(f);
        }
        fprintf(stderr, "[png] %s: %u bytes de %08X\n", path, ctx.r6.u32, ctx.r5.u32);
    }
    GUEST_ORIGINAL(82446840)(ctx, base);
}

GUEST_OVERRIDE(82582580)
{
    const char* message = ctx.r4.u32 ? kernel::Guest<char>(ctx.r4.u32) : "(nula)";
    fprintf(stderr, "[libpng] erro: %s (png_struct %08X, chamado de %08X)\n", message, ctx.r3.u32, uint32_t(ctx.lr));
    GUEST_ORIGINAL(82582580)(ctx, base);
}
