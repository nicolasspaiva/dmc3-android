#include "gpu.h"
#include "renderer.h"
#include "xenos_shader.h"

#define NOMINMAX
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

// originais que as substituições ainda chamam (gravam o estado e os comandos que ninguém executa)
GUEST_DECLARE_ORIGINAL(82479038);   // D3DDevice_DrawVertices
GUEST_DECLARE_ORIGINAL(82479450);   // D3DDevice_DrawIndexedVertices
GUEST_DECLARE_ORIGINAL(82478FF0);   // D3DDevice_DrawVerticesUP
GUEST_DECLARE_ORIGINAL(82471D90);   // D3DDevice_Clear
GUEST_DECLARE_ORIGINAL(82478AD0);   // D3DDevice_Resolve
GUEST_DECLARE_ORIGINAL(8247B1B8);   // D3DDevice_Swap

namespace gpu
{
    namespace
    {
        FrameStats stats;
        uint64_t lastReport = 0;   // ns (platform::NowNs)
        uint64_t framesAtReport = 0;
        uint32_t frontBuffer = 0;   // endereço da textura que o último Swap mostrou
    }

    FrameStats& Stats()
    {
        return stats;
    }

    namespace
    {
        thread_local bool renderThread = false;
        uint64_t blocked[3] = {};
        uint64_t lastPresentNs = 0;
    }

    // tempo por quadro (vai no relatório de 2 em 2 s): preparação dos desenhos (leitura de vértices, constantes e
    // estado do console), chamadas ao GL e apresentação; o resto é o código do jogo
    Profile profile;

    void BlockedTime(int kind, uint64_t ns)
    {
        if (renderThread)
            blocked[kind] += ns;
    }

    void Present()
    {
        stats.frame++;
        uint64_t now = platform::NowNs();
        renderThread = true;
        static bool logSync = getenv("DMC3_LOG_VIDEO_SYNC") != nullptr;
        if (logSync && lastPresentNs && now - lastPresentNs > 50000000ull)
            fprintf(stderr, "[gpu] %llu ms entre quadros: leitura %llu ms, espera %llu ms, pausa %llu ms\n",
                (unsigned long long)((now - lastPresentNs) / 1000000), (unsigned long long)(blocked[0] / 1000000),
                (unsigned long long)(blocked[1] / 1000000), (unsigned long long)(blocked[2] / 1000000));
        blocked[0] = blocked[1] = blocked[2] = 0;
        lastPresentNs = now;
        if (lastReport == 0)
            lastReport = now;
        double seconds = double(now - lastReport) / 1e9;
        if (stats.frame <= 3 || seconds >= 2.0)
        {
            double fps = seconds > 0 ? double(stats.frame - framesAtReport) / seconds : 0;
            uint64_t frames = stats.frame - framesAtReport;
            auto ms = [&](uint64_t ns) { return frames ? double(ns) / 1e6 / double(frames) : 0.0; };
            double total = frames ? seconds * 1000.0 / double(frames) : 0;
            double prep = ms(profile.prep), gl = ms(profile.gl), present = ms(profile.present);
            fprintf(stderr, "[gpu] quadro %llu: %u draws (%u indexados, %u UP), %u clears, %u esperas de GPU; %.1f quadros/s; "
                "por quadro: jogo %.1f ms, preparação %.1f ms, GL %.1f ms, apresentação %.1f ms, %llu pulados, %.0f envios (%.0f KB) em %.0f desenhos; GL: texturas %.2f ms, envio %.2f ms\n",
                (unsigned long long)stats.frame, stats.draws, stats.indexedDraws, stats.drawsUP, stats.clears, stats.gpuWaits, fps,
                std::max(0.0, total - prep - gl - present), prep, gl, present, (unsigned long long)profile.skipped,
                frames ? double(profile.uploads) / frames : 0.0, frames ? double(profile.uploadBytes) / 1024 / frames : 0.0,
                frames ? double(profile.draws) / frames : 0.0, ms(profile.textures), ms(profile.submit));
            profile = {};
            lastReport = now;
            framesAtReport = stats.frame;
        }
        stats.draws = stats.indexedDraws = stats.drawsUP = stats.clears = stats.gpuWaits = 0;
        uint64_t presentStart = platform::NowNs();
        renderer::Present(stats.frame, frontBuffer);
        profile.present += platform::NowNs() - presentStart;
    }
}

// ---- sincronização com a GPU ----
// D3D_WaitForGpuProgress(WaitContext*): o D3D chama em laço enquanto espera uma fence ou o swap, e
// sai do laço quando ela devolve 0. Sem GPU, tudo o que foi emitido já está "concluído".
GUEST_OVERRIDE(824799C0)
{
    uint32_t device = kernel::Load32(ctx.r3.u32);
    uint32_t block = kernel::Load32(device + gpu::DeviceGpuBlock);
    if (block)
        kernel::Store32(block, kernel::Load32(device + gpu::DeviceIssuedFence));
    gpu::Stats().gpuWaits++;
    ctx.r3.u64 = 0;
}

namespace
{
    // ---- campos do device (cópia dos registradores da Xenos e objetos atuais) ----
    constexpr uint32_t FetchConstants = 0x480;      // 96 x 2 palavras (vértice) = 32 x 6 palavras (textura)
    constexpr uint32_t VertexConstants = 0x780;     // 256 vec4
    constexpr uint32_t PixelConstants = 0x1780;     // 256 vec4
    constexpr uint32_t BoolConstants = 0x2780;      // 256 bits
    constexpr uint32_t RegDepthControl = 0x2934;    // RB_DEPTHCONTROL
    constexpr uint32_t RegColorControl = 0x293C;    // RB_COLORCONTROL
    constexpr uint32_t RegModeControl = 0x2948;     // PA_SU_SC_MODE_CNTL
    constexpr uint32_t RegVteControl = 0x294C;      // PA_CL_VTE_CNTL
    constexpr uint32_t RegColorMask = 0x28DC;       // RB_COLOR_MASK
    constexpr uint32_t RegBlendColor = 0x28E0;      // RB_BLEND_RED, GREEN, BLUE, ALPHA (floats)
    constexpr uint32_t RegStencilRefMaskBack = 0x28FC;   // RB_STENCILREFMASK_BF
    constexpr uint32_t RegStencilRefMask = 0x2900;  // RB_STENCILREFMASK: referência 0-7, máscara 8-15, escrita 16-23
    constexpr uint32_t RegAlphaRef = 0x2904;        // RB_ALPHA_REF
    constexpr uint32_t RegViewport = 0x2908;        // PA_CL_VPORT_XSCALE, XOFFSET, YSCALE, YOFFSET, ZSCALE, ZOFFSET
    constexpr uint32_t RenderTarget0 = 0x3210, DepthStencil = 0x3220;
    constexpr uint32_t IndexBuffer = 0x320C, PixelShader = 0x330C, VertexShader = 0x3310;

    float LoadF32(uint32_t address)
    {
        uint32_t bits = kernel::Load32(address);
        float f;
        memcpy(&f, &bits, 4);
        return f;
    }

    void DumpShader(const char* kind, uint32_t object, uint32_t header, uint32_t ucode);

    // endereço físico (como a GPU vê) -> endereço do console
    uint32_t PhysicalToGuest(uint32_t physical) { return 0xA0000000 | (physical & 0x1FFFFFFF); }

    // ---- shaders: o programa fica no microcódigo a partir do deslocamento que o cabeçalho do XDK indica ----
    struct CachedShader
    {
        xenos::Shader shader;
        uint64_t key = 0;
        // constantes literais do shader: o microcódigo antes do programa traz os vec4 das últimas constantes
        // (c[256 - n] .. c255), que o D3D carregaria junto com o shader
        std::vector<float> literals;
    };
    std::unordered_map<uint64_t, CachedShader> shaders;

    // Liga os vfetch do shader de vértice à declaração atual, como o D3D faz ao desenhar (ele manda as instruções
    // corrigidas direto para a GPU; o microcódigo na memória fica com os campos zerados). O cabeçalho lista os
    // vfetch: seção+0x1C = quantidade, a partir de seção+0x28 uma palavra cada (bits 0-11 instrução, 12-15 uso,
    // 16-19 índice do uso). Tipo do elemento (D3DDECLTYPE): bits 0-5 formato da Xenos, 8 com sinal, 9 inteiro,
    // 10-21 swizzle de destino.
    void BindVertexFetches(uint32_t device, uint32_t section, std::vector<uint32_t>& words)
    {
        uint32_t decl = kernel::Load32(device + 0x2FD0);
        if (!decl)
            return;
        uint32_t elements = std::min(kernel::Load32(decl + 0x18), 16u);
        uint32_t fetches = std::min(kernel::Load32(section + 0x1C), 16u);
        for (uint32_t i = 0; i < fetches; i++)
        {
            uint32_t entry = kernel::Load32(section + 0x28 + 4 * i);
            uint32_t slot = entry & 0xFFF, usage = (entry >> 12) & 0xF, usageIndex = (entry >> 16) & 0xF;
            // só os que o compilador deixou sem formato; os outros já vêm completos no microcódigo
            if (3 * slot + 2 >= words.size() || ((words[3 * slot + 1] >> 16) & 0x3F) != 0)
                continue;
            uint32_t e = 0;
            for (uint32_t k = 0; k < elements && !e; k++)
            {
                uint32_t candidate = decl + 0x34 + 12 * k;
                if (*kernel::Guest<uint8_t>(candidate + 9) == usage && *kernel::Guest<uint8_t>(candidate + 10) == usageIndex)
                    e = candidate;
            }
            if (!e)
            {
                static int warned = 0;
                if (warned++ < 8)
                    fprintf(stderr, "[gpu] vfetch de uso %u/%u sem elemento na declaração\n", usage, usageIndex);
                continue;
            }
            uint32_t stream = kernel::Load32(e) >> 16, offset = kernel::Load32(e) & 0xFFFF, type = kernel::Load32(e + 4);
            static const bool logDecl = getenv("DMC3_LOG_DECL") != nullptr;
            if (logDecl)
            {
                static std::unordered_map<uint64_t, bool> logged;
                uint64_t key = (uint64_t(decl) << 32) | (usage << 4) | usageIndex;
                if (!logged[key])
                {
                    logged[key] = true;
                    fprintf(stderr, "[gpu] declaração %08X: uso %u/%u stream %u offset %u tipo %08X (vfetch original %08X %08X)\n",
                        decl, usage, usageIndex, stream, offset, type, words[3 * slot], words[3 * slot + 1]);
                }
            }
            uint32_t stride = *kernel::Guest<uint8_t>(device + 0x3268 + stream);   // em palavras
            uint32_t fetchSlot = 95 - stream;
            uint32_t& w0 = words[3 * slot];
            uint32_t& w1 = words[3 * slot + 1];
            uint32_t& w2 = words[3 * slot + 2];
            // swizzle final: o da instrução escolhe componentes "lógicos", o do tipo diz onde cada um está
            uint32_t typeSwizzle = (type >> 10) & 0xFFF, swizzle = 0;
            // tipos de 16 bits (16_16, 16_16_16_16 e as versões half): a palavra chega com as metades na ordem da
            // memória trocada (buffers 8in32), então x e y (e z e w) trocam de lugar; conferido nos modelos com esqueleto
            // e na tela de título, que transpõem as texturas sem isso
            uint32_t format = type & 0x3F;
            if (format == 25 || format == 26 || format == 31 || format == 32)
            {
                uint32_t swapped = 0;
                for (int c = 0; c < 4; c++)
                {
                    uint32_t sel = (typeSwizzle >> (3 * c)) & 7;
                    if (sel < 4)
                        sel ^= 1;
                    swapped |= sel << (3 * c);
                }
                typeSwizzle = swapped;
            }
            for (int c = 0; c < 4; c++)
            {
                uint32_t sel = (w1 >> (3 * c)) & 7;
                if (sel < 4)
                    sel = (typeSwizzle >> (3 * sel)) & 7;
                swizzle |= sel << (3 * c);
            }
            w0 = (w0 & ~(0x1Fu << 20) & ~(3u << 25)) | ((fetchSlot / 3) << 20) | ((fetchSlot % 3) << 25);
            uint32_t integer = (type >> 9) & 1;
            w1 = (w1 & ~0x3FFFu & ~(0x3Fu << 16) & ~(1u << 30)) | swizzle | (((type >> 8) & 1) << 12) |
                (integer << 13) | ((type & 0x3F) << 16);
            w2 = (w2 & 0x80000000u) | (stride & 0xFF) | ((offset / 4) << 8);
        }
    }

    const CachedShader* GetShader(uint32_t object, bool pixel, uint32_t device)
    {
        if (!object)
            return nullptr;
        uint32_t header = object + (pixel ? 0x28 : 0x368);
        uint32_t ucode = kernel::Load32(object + (pixel ? 0x18 : 0x20));
        uint32_t section = header + kernel::Load32(header + 0x18);
        uint32_t offset = kernel::Load32(section), size = kernel::Load32(section + 4);
        if (!ucode || size == 0 || size > 0x10000)
        {
            static int warned = 0;
            if (warned++ < 8)
                fprintf(stderr, "[gpu] shader de %s %08X sem programa (microcódigo %08X, +%X, %X bytes)\n",
                    pixel ? "pixel" : "vértice", object, ucode, offset, size);
            return nullptr;
        }
        std::vector<uint32_t> words(size / 4);
        uint64_t hash = pixel ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull;
        for (size_t i = 0; i < words.size(); i++)
            words[i] = kernel::Load32(ucode + offset + uint32_t(i) * 4);
        if (!pixel)
            BindVertexFetches(device, section, words);
        for (uint32_t w : words)
            hash = (hash ^ w) * 1099511628211ull;
        CachedShader& c = shaders[hash];
        if (c.key == 0)
        {
            c.key = hash;
            c.shader = xenos::Translate(words.data(), uint32_t(words.size()), pixel);
            for (uint32_t i = 0; i < offset / 4 && i < 1024; i++)
                c.literals.push_back(LoadF32(ucode + 4 * i));
            fprintf(stderr, "[gpu] shader de %s %08X %016llX (%zu palavras) -> %s%s\n", pixel ? "pixel" : "vértice", object,
                (unsigned long long)hash, words.size(), c.shader.ok ? "GLSL" : "ERRO: ", c.shader.error.c_str());
            if (getenv("DMC3_LOG_SHADERS"))
                fprintf(stderr, "%s\n", c.shader.body.c_str());
        }
        return c.shader.ok ? &c : nullptr;
    }

    // ---- vértices: cada vfetch vira um vec4 por vértice, convertido aqui ----
    float Half(uint16_t h)
    {
        uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
        float v = exponent == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(mantissa | 0x400), int(exponent) - 25);
        return sign ? -v : v;
    }

    float Component(uint32_t raw, int bits, const xenos::VertexFetch& f)
    {
        float v;
        if (f.isSigned)
        {
            int32_t s = int32_t(raw << (32 - bits)) >> (32 - bits);
            v = f.isInteger ? float(s) : std::max(float(s) / float((1u << (bits - 1)) - 1), -1.0f);
        }
        else
        {
            v = f.isInteger ? float(raw) : float(raw) / float((1ull << bits) - 1);
        }
        return f.expAdjust ? std::ldexp(v, f.expAdjust) : v;
    }

    // palavra como a GPU a vê: leitura little-endian e a troca de bytes do fetch constant
    // (0 nenhuma, 1 bytes dentro de cada 16 bits, 2 bytes da palavra, 3 metades de 16 bits)
    uint32_t GpuWord(uint32_t address, uint32_t endian)
    {
        uint32_t w = *kernel::Guest<uint32_t>(address);
        switch (endian)
        {
        case 1: return ((w & 0x00FF00FF) << 8) | ((w >> 8) & 0x00FF00FF);
        case 2: return __builtin_bswap32(w);
        case 3: return (w << 16) | (w >> 16);
        default: return w;
        }
    }

    bool FetchVertex(const xenos::VertexFetch& f, uint32_t address, uint32_t endian, bool fromCpu, float* out)
    {
        auto Load32 = [endian](uint32_t a) { return GpuWord(a, endian); };
        auto LoadF32 = [endian](uint32_t a) { uint32_t b = GpuWord(a, endian); float v; memcpy(&v, &b, 4); return v; };
        out[0] = out[1] = out[2] = 0;
        out[3] = 1;
        switch (f.format)
        {
        case 6:   // 8_8_8_8: x nos bits baixos da palavra
        {
            uint32_t w = Load32(address);
            for (int c = 0; c < 4; c++)
                out[c] = Component((w >> (8 * c)) & 0xFF, 8, f);
            return true;
        }
        case 7:   // 2_10_10_10
        {
            uint32_t w = Load32(address);
            out[0] = Component(w & 0x3FF, 10, f);
            out[1] = Component((w >> 10) & 0x3FF, 10, f);
            out[2] = Component((w >> 20) & 0x3FF, 10, f);
            out[3] = Component(w >> 30, 2, f);
            return true;
        }
        case 25: case 26:   // 16_16, 16_16_16_16
        case 31: case 32:   // as mesmas em half float
        {
            int n = (f.format == 25 || f.format == 31) ? 2 : 4;
            // o primeiro componente é o primeiro na memória: depois da troca 8in16 ele fica na metade baixa da palavra;
            // depois da 8in32 ou da 16in32, na metade alta (conferido na tela de título, que transpõe se for o contrário)
            // a GPU tira x da metade baixa da palavra já trocada (regra do Xenia); os vfetch completados pela declaração
            // do D3D trocam os pares no swizzle (BindVertexFetches)
            bool firstHigh = false;
            static const bool logDecl = getenv("DMC3_LOG_DECL") != nullptr;
            if (logDecl)
            {
                static std::unordered_map<uint32_t, bool> logged;
                uint32_t key = f.format | (endian << 8) | (uint32_t(fromCpu) << 10) | (uint32_t(f.isInteger) << 11) | (f.slot << 12) |
                    (f.stride << 20) | (f.offset << 26);
                if (!logged[key])
                {
                    logged[key] = true;
                    fprintf(stderr, "[gpu] vértice 16 bits: formato %u endian %u %s%s slot %u stride %u offset %u, endereço %08X: %08X %08X\n",
                        f.format, endian, fromCpu ? "UP" : "buffer", f.isInteger ? " inteiro" : "", f.slot, f.stride, f.offset, address,
                        kernel::Load32(address), kernel::Load32(address + 4));
                }
            }
            for (int c = 0; c < n; c++)
            {
                uint32_t w = Load32(address + 4 * (c / 2));
                uint32_t h = (w >> (16 * ((c & 1) ^ uint32_t(firstHigh)))) & 0xFFFF;
                out[c] = f.format >= 31 ? Half(uint16_t(h)) : Component(h, 16, f);
            }
            return true;
        }
        case 33: case 34: case 35:   // 32, 32_32, 32_32_32_32 inteiros
            for (int c = 0; c < f.format - 32; c++)
                out[c] = Component(Load32(address + 4 * c), 32, f);
            return true;
        case 36: case 37: case 38: case 57:   // floats
        {
            int n = f.format == 36 ? 1 : f.format == 37 ? 2 : f.format == 38 ? 4 : 3;
            for (int c = 0; c < n; c++)
                out[c] = LoadF32(address + 4 * c);
            return true;
        }
        default:
            return false;
        }
    }

    // ---- cache de vértices ----
    // Converter cada vértice para vec4 na CPU e reenviar ao GL a cada desenho era a parte mais cara do quadro. Os
    // vértices convertidos ficam guardados por fluxo (endereço, formato, faixa de vértices) junto com um hash dos bytes
    // de origem, conferido uma vez por quadro: só reconverte se o jogo reescreveu a memória.
    uint64_t HashBytes(uint32_t address, size_t bytes)
    {
        const uint8_t* p = kernel::Guest<uint8_t>(address);
        uint64_t h = 1469598103934665603ull ^ bytes;
        size_t i = 0;
        for (; i + 8 <= bytes; i += 8)
        {
            uint64_t w;
            memcpy(&w, p + i, 8);
            h = (h ^ w) * 0x9E3779B97F4A7C15ull;
        }
        for (; i < bytes; i++)
            h = (h ^ p[i]) * 0x100000001B3ull;
        return h ^ (h >> 29);
    }

    struct StreamKey
    {
        uint32_t base, first, count, endian;
        uint32_t slot, offset, stride, format;
        int32_t expAdjust;
        bool isSigned, isInteger;
        bool operator==(const StreamKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
    };
    struct StreamKeyHash
    {
        size_t operator()(const StreamKey& k) const
        {
            uint64_t h = 1469598103934665603ull;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&k);
            for (size_t i = 0; i < sizeof(k); i++)
                h = (h ^ p[i]) * 0x100000001B3ull;
            return size_t(h);
        }
    };
    struct CachedStream
    {
        renderer::VertexStream stream;
        uint64_t hash = 0;
        uint64_t checkedFrame = ~0ull;
        bool valid = false;
    };
    std::unordered_map<StreamKey, CachedStream, StreamKeyHash> vertexCache;
    // hash de cada faixa de memória no quadro atual (vários atributos intercalados leem a mesma faixa)
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> regionHashes;   // chave -> (quadro, hash)

    uint64_t RegionHash(uint32_t address, size_t bytes)
    {
        uint64_t key = (uint64_t(address) << 32) ^ bytes;
        auto& entry = regionHashes[key];
        if (entry.first != gpu::Stats().frame + 1)
            entry = { gpu::Stats().frame + 1, HashBytes(address, bytes) };
        return entry.second;
    }

    // tira da cache o que não é usado há 10 s (e o hash das faixas, a cada quadro novo)
    void TrimVertexCache()
    {
        static uint64_t lastTrim = 0;
        uint64_t frame = gpu::Stats().frame;
        if (frame < lastTrim + 60)
            return;
        lastTrim = frame;
        for (auto it = vertexCache.begin(); it != vertexCache.end();)
            if (it->second.checkedFrame + 60 < frame)
            {
                renderer::Forget(it->second.stream);
                it = vertexCache.erase(it);
            }
            else
            {
                ++it;
            }
        regionHashes.clear();
    }

    bool ConvertStream(const xenos::VertexFetch& f, uint32_t base, uint32_t endian, bool fromCpu, uint32_t first,
        uint32_t count, std::vector<float>& out)
    {
        out.resize(size_t(count) * 4);
        for (uint32_t v = 0; v < count; v++)
        {
            uint32_t address = base + ((first + v) * f.stride + f.offset) * 4;
            if (!FetchVertex(f, address, endian, fromCpu, &out[size_t(v) * 4]))
            {
                static int warned = 0;
                if (warned++ < 8)
                    fprintf(stderr, "[gpu] formato de vértice %u ainda não suportado\n", f.format);
                return false;
            }
        }
        return true;
    }

    // atributos dos vértices first..first+count-1, um fluxo por vfetch do shader; upData != 0 substitui o stream 0
    // (DrawVerticesUP, que muda a cada desenho e não entra na cache)
    bool FetchVertices(uint32_t device, const xenos::Shader& vs, uint32_t first, uint32_t count, uint32_t upData,
        renderer::DrawCall& call)
    {
        // DrawVerticesUP: todos os atributos num fluxo só (um envio ao GL por desenho), um atrás do outro
        static renderer::VertexStream upStream = [] { renderer::VertexStream s; s.transient = true; return s; }();
        static std::vector<float> scratch;
        bool hasUp = false;
        TrimVertexCache();
        for (size_t i = 0; i < vs.fetches.size() && i < 16; i++)
        {
            const xenos::VertexFetch& f = vs.fetches[i];
            uint32_t fetch = device + FetchConstants + f.slot * 8;
            bool fromCpu = upData && f.slot == 95;
            uint32_t base = fromCpu ? upData : PhysicalToGuest(kernel::Load32(fetch) & 0xFFFFFFFC);
            // DrawVerticesUP: os vértices vêm da memória da CPU (big-endian = troca 8in32), não do fetch constant
            uint32_t endian = fromCpu ? 2 : kernel::Load32(fetch + 4) & 3;
            if (fromCpu)
            {
                if (!hasUp)
                    upStream.data.clear();
                hasUp = true;
                if (!ConvertStream(f, base, endian, true, first, count, scratch))
                    return false;
                call.streamOffsets[i] = uint32_t(upStream.data.size() * sizeof(float));
                upStream.data.insert(upStream.data.end(), scratch.begin(), scratch.end());
                call.streams[i] = &upStream;
                continue;
            }
            StreamKey key{ base, first, count, endian, f.slot, f.offset, f.stride, f.format, f.expAdjust, f.isSigned, f.isInteger };
            CachedStream& c = vertexCache[key];
            uint64_t frame = gpu::Stats().frame;
            if (!c.valid || c.checkedFrame != frame)
            {
                uint64_t hash = RegionHash(base + first * f.stride * 4, size_t(count) * f.stride * 4);
                c.checkedFrame = frame;
                if (!c.valid || hash != c.hash)
                {
                    if (!ConvertStream(f, base, endian, false, first, count, c.stream.data))
                        return false;
                    c.hash = hash;
                    c.valid = true;
                    c.stream.version++;
                    // novo ou reescrito: vai pelo buffer circular (muitos são de um quadro só, como partículas)
                    c.stream.transient = true;
                }
                else
                {
                    // igual ao de um quadro anterior: vale a pena ter buffer próprio no GL
                    c.stream.transient = false;
                }
            }
            call.streams[i] = &c.stream;
        }
        if (hasUp)
            upStream.version++;
        return true;
    }

    // ---- estado ----
    void DumpWords(const char* label, uint32_t address, uint32_t count);

    renderer::Target ReadTarget(uint32_t device)
    {
        renderer::Target t;
        t.color = kernel::Load32(device + RenderTarget0);
        t.depth = kernel::Load32(device + DepthStencil);
        if (t.depth)
        {
            // a superfície de profundidade tem o mesmo cabeçalho que a de cor
            uint32_t pitch = kernel::Load32(t.depth + 0x18) & 0x3FFF;
            uint32_t bytes = kernel::Load32(t.depth + 0x2C);
            if (pitch && bytes / (pitch * 4) > 0 && bytes / (pitch * 4) <= 4096)
            {
                t.depthWidth = pitch;
                t.depthHeight = bytes / (pitch * 4);
                if (!t.color)
                {
                    // desenho só de profundidade (sem render target de cor)
                    t.width = t.depthWidth;
                    t.height = t.depthHeight;
                }
            }
        }
        if (t.color)
        {
            // superfície: +0x18 RB_SURFACE_INFO (pitch nos bits 0-13), +0x2C tamanho em bytes (32 bits por pixel)
            uint32_t pitch = kernel::Load32(t.color + 0x18) & 0x3FFF;
            uint32_t bytes = kernel::Load32(t.color + 0x2C);
            if (pitch && bytes / (pitch * 4) > 0 && bytes / (pitch * 4) <= 4096)
            {
                t.width = pitch;
                t.height = bytes / (pitch * 4);
            }
            // diagnóstico: o objeto de cada superfície, uma vez (o formato da cor fica no RB_COLOR_INFO)
            static std::unordered_map<uint32_t, bool> seen;
            if (!seen[t.color])
            {
                seen[t.color] = true;
                DumpWords("superfície", t.color, 16);
            }
        }
        return t;
    }

    renderer::RenderState ReadState(uint32_t device, const renderer::Target& target)
    {
        renderer::RenderState s;
        // blend pelo registrador que a GPU usa (RB_BLENDCONTROL), não pelo estado do D3D em +0x2FF8/+0x2FFC, que pode
        // ficar desatualizado: na composição da câmera próxima ele dizia "mistura por alfa" com o registrador em
        // ONE/ZERO, e a cena sumia. Cor e alfa sempre têm campos próprios; ONE/ADD/ZERO nos dois = sem mistura.
        constexpr uint32_t RegBlendControl = 0x2938;   // RB_BLENDCONTROL0
        s.blend.control = kernel::Load32(device + RegBlendControl);
        s.blend.separateAlpha = true;
        s.blend.enable = s.blend.control != 0x00010001;
        uint32_t colorControl = kernel::Load32(device + RegColorControl);
        s.blend.alphaTest = (colorControl >> 3) & 1;
        s.blend.alphaFunc = colorControl & 7;
        s.blend.alphaRef = LoadF32(device + RegAlphaRef);

        uint32_t depth = kernel::Load32(device + RegDepthControl);
        s.depthTest = (depth >> 1) & 1;
        s.depthWrite = (depth >> 2) & 1;
        s.depthFunc = (depth >> 4) & 7;
        // stencil: bit 0 liga, bit 7 frente e verso separados; frente nos bits 8-19 (função, falha, passa, falha do z),
        // verso nos bits 20-31; referência, máscara e máscara de escrita em RB_STENCILREFMASK (frente) e _BF (verso)
        s.stencil.enable = depth & 1;
        bool twoSided = (depth >> 7) & 1;
        for (int side = 0; side < 2; side++)
        {
            uint32_t shift = side && twoSided ? 20 : 8;
            s.stencil.func[side] = uint8_t((depth >> shift) & 7);
            s.stencil.fail[side] = uint8_t((depth >> (shift + 3)) & 7);
            s.stencil.pass[side] = uint8_t((depth >> (shift + 6)) & 7);
            s.stencil.depthFail[side] = uint8_t((depth >> (shift + 9)) & 7);
            uint32_t refMask = kernel::Load32(device + (side && twoSided ? RegStencilRefMaskBack : RegStencilRefMask));
            s.stencil.ref[side] = uint8_t(refMask);
            s.stencil.mask[side] = uint8_t(refMask >> 8);
            s.stencil.writeMask[side] = uint8_t(refMask >> 16);
        }
        uint32_t mode = kernel::Load32(device + RegModeControl);
        s.cullFront = mode & 1;
        s.cullBack = (mode >> 1) & 1;
        s.frontIsCW = (mode >> 2) & 1;
        s.colorMask = kernel::Load32(device + RegColorMask) & 0xF;
        for (int i = 0; i < 4; i++)
            s.blend.constant[i] = LoadF32(device + RegBlendColor + 4 * i);

        uint32_t vte = kernel::Load32(device + RegVteControl);
        if (vte & 1)
        {
            float xs = LoadF32(device + RegViewport), xo = LoadF32(device + RegViewport + 4);
            float ys = LoadF32(device + RegViewport + 8), yo = LoadF32(device + RegViewport + 12);
            float zs = LoadF32(device + RegViewport + 16), zo = LoadF32(device + RegViewport + 20);
            s.viewport = true;
            s.viewportX = xo - std::fabs(xs);
            s.viewportY = yo - std::fabs(ys);
            s.viewportW = 2 * std::fabs(xs);
            s.viewportH = 2 * std::fabs(ys);
            s.depthMin = zo;
            s.depthMax = zo + zs;
        }
        else
        {
            static int warned = 0;
            if (warned++ < 4)
                fprintf(stderr, "[gpu] desenho com viewport desligado (VTE %08X) ainda não suportado direito\n", vte);
        }
        (void)target;
        return s;
    }

    renderer::Texture ReadTexture(uint32_t fetch)
    {
        renderer::Texture t;
        uint32_t dw0 = kernel::Load32(fetch), dw1 = kernel::Load32(fetch + 4), dw2 = kernel::Load32(fetch + 8);
        t.address = PhysicalToGuest(dw1 & 0xFFFFF000);
        t.format = dw1 & 0x3F;
        t.endian = (dw1 >> 6) & 3;
        t.tiled = dw0 >> 31;
        uint32_t dw3 = kernel::Load32(fetch + 12);
        t.swizzle = (dw3 >> 1) & 0xFFF;
        t.clampX = (dw0 >> 10) & 7;
        t.clampY = (dw0 >> 13) & 7;
        t.magFilter = (dw3 >> 19) & 3;
        t.minFilter = (dw3 >> 21) & 3;
        t.mipFilter = (dw3 >> 23) & 3;
        t.pitch = ((dw0 >> 22) & 0x1FF) * 32;
        t.width = (dw2 & 0x1FFF) + 1;
        t.height = ((dw2 >> 13) & 0x1FFF) + 1;
        if (t.pitch == 0)
            t.pitch = t.width;
        return t;
    }

    std::vector<float> vsConstants(1024), psConstants(1024);

    // só as constantes que os shaders leem (todas, se o shader indexa pelo a0)
    void ReadConstants(uint32_t device, const xenos::Shader& vs, const xenos::Shader& ps)
    {
        auto read = [&](const xenos::Shader& s, uint32_t base, std::vector<float>& out) {
            for (uint32_t c = 0; c < 256; c++)
                if (s.relativeConstants || (s.constantMask[c / 32] >> (c % 32) & 1))
                    for (uint32_t k = 0; k < 4; k++)
                        out[c * 4 + k] = LoadF32(base + 16 * c + 4 * k);
        };
        read(vs, device + VertexConstants, vsConstants);
        read(ps, device + PixelConstants, psConstants);
    }

    // desenho nativo: lê shaders, vértices, constantes e estado do device depois que o D3D original gravou tudo
    void NativeDraw(uint32_t device, uint32_t prim, uint32_t first, uint32_t vertexCount, const renderer::IndexStream* indices,
        uint32_t upData)
    {
        if (renderer::SkippingFrame())
            return;
        uint64_t prepStart = platform::NowNs();
        const CachedShader* vs = GetShader(kernel::Load32(device + VertexShader), false, device);
        uint32_t psObject = kernel::Load32(device + PixelShader);
        const CachedShader* ps = GetShader(psObject, true, device);
        // sem pixel shader: o desenho só grava profundidade e stencil (os volumes de sombra, por exemplo)
        static const CachedShader noPixelShader = [] {
            CachedShader c;
            c.key = 1;
            c.shader.ok = true;
            c.shader.pixel = true;
            c.shader.body = "void main()\n{\n}\n";
            return c;
        }();
        if (!psObject)
            ps = &noPixelShader;
        if (!vs || !ps)
            return;
        renderer::DrawCall call;
        call.vs = &vs->shader;
        call.ps = &ps->shader;
        call.vsKey = vs->key;
        call.psKey = ps->key;
        call.primitive = prim;
        call.vertexCount = vertexCount;
        call.indices = indices;
        if (!FetchVertices(device, vs->shader, first, vertexCount, upData, call))
            return;
        ReadConstants(device, vs->shader, ps->shader);
        std::copy(vs->literals.begin(), vs->literals.end(), vsConstants.end() - vs->literals.size());
        std::copy(ps->literals.begin(), ps->literals.end(), psConstants.end() - ps->literals.size());
        call.vsConstants = vsConstants.data();
        call.psConstants = psConstants.data();
        for (int i = 0; i < 8; i++)
            call.bools[i] = kernel::Load32(device + BoolConstants + 4 * i);
        for (uint32_t i = 0; i < 32; i++)
            if (ps->shader.textureMask >> i & 1)
                call.textures[i] = ReadTexture(device + FetchConstants + i * 0x18);
        // diagnóstico: shaders que leem uma textura de profundidade (formato 24_8) vão para DMC3_CAPTURE_DIR
        static const char* captureDir = getenv("DMC3_CAPTURE_DIR");
        if (captureDir)
            for (uint32_t i = 0; i < 32; i++)
                if ((ps->shader.textureMask >> i & 1) && call.textures[i].format == 22)
                {
                    static std::vector<uint32_t> seen;
                    uint32_t object = kernel::Load32(device + PixelShader);
                    if (std::find(seen.begin(), seen.end(), object) == seen.end())
                    {
                        seen.push_back(object);
                        fprintf(stderr, "[gpu] pixel shader %08X lê profundidade no fetch %u (swizzle %03X)%c", object, i,
                            call.textures[i].swizzle, 10);
                        DumpShader("ps", object, object + 0x28, kernel::Load32(object + 0x18));
                        fprintf(stderr, "%s%c", ps->shader.body.c_str(), 10);
                    }
                }
        call.target = ReadTarget(device);
        call.state = ReadState(device, call.target);
        if (!psObject)
            call.state.colorMask = 0;
        uint64_t glStart = platform::NowNs();
        gpu::profile.prep += glStart - prepStart;
        renderer::Draw(call);
        gpu::profile.gl += platform::NowNs() - glStart;
    }

    // ---- captura de um quadro (DMC3_CAPTURE=<quadro>, DMC3_CAPTURE_DIR para os shaders) ----
    bool Capturing()
    {
        static uint64_t frame = [] { const char* e = getenv("DMC3_CAPTURE"); return e ? strtoull(e, nullptr, 10) : 0ull; }();
        return (frame && gpu::Stats().frame == frame) || renderer::DetailedFrame();
    }

    void DumpWords(const char* label, uint32_t address, uint32_t count)
    {
        fprintf(stderr, "[capture]   %s %08X:", label, address);
        for (uint32_t i = 0; address && i < count; i++)
            fprintf(stderr, " %08X", kernel::Load32(address + 4 * i));
        fprintf(stderr, "\n");
    }

    void DumpShader(const char* kind, uint32_t object, uint32_t header, uint32_t ucode)
    {
        if (!object)
            return;
        DumpWords("objeto", object, 12);
        uint32_t size = kernel::Load32(header + 8);
        const char* dir = getenv("DMC3_CAPTURE_DIR");
        if (!dir || !ucode || size > 0x10000)
            return;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%08X.bin", dir, kind, object);
        if (FILE* f = fopen(path, "wb"))
        {
            fwrite(kernel::Guest<uint8_t>(ucode), 1, size, f);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/%s_%08X.hdr", dir, kind, object);
        if (FILE* f = fopen(path, "wb"))
        {
            fwrite(kernel::Guest<uint8_t>(header), 1, std::min(kernel::Load32(header + 4), 0x4000u), f);
            fclose(f);
        }
    }

    void CaptureDraw(const char* kind, uint32_t device, uint32_t prim, uint32_t a, uint32_t b, uint32_t c)
    {
        uint32_t vs = kernel::Load32(device + VertexShader), ps = kernel::Load32(device + PixelShader);
        fprintf(stderr, "[capture] %s: prim %u, args %u %u %u; VS %08X PS %08X IB %08X RT0 %08X DS %08X\n",
            kind, prim, a, b, c, vs, ps, kernel::Load32(device + IndexBuffer), kernel::Load32(device + RenderTarget0),
            kernel::Load32(device + DepthStencil));
        if (vs)
            DumpShader("vs", vs, vs + 0x368, kernel::Load32(vs + 0x20));
        if (ps)
            DumpShader("ps", ps, ps + 0x28, kernel::Load32(ps + 0x18));
        DumpWords("estado (depth, blend, color, hiz, clip, mode, vte)", device + RegDepthControl, 7);
        DumpWords("viewport", device + RegViewport, 6);
        if (vs)
            DumpWords("vfetch no objeto", vs + 0x28, 0x68);
        for (uint32_t c : { 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 18u, 20u, 21u, 32u, 33u, 34u, 35u, 64u, 65u, 66u,
                 67u, 240u })
            fprintf(stderr, "[capture]   VC%u = %g %g %g %g\n", c, LoadF32(device + VertexConstants + 16 * c),
                LoadF32(device + VertexConstants + 16 * c + 4), LoadF32(device + VertexConstants + 16 * c + 8),
                LoadF32(device + VertexConstants + 16 * c + 12));
        DumpWords("c0-c3 VS", device + VertexConstants, 16);
        DumpWords("c240, c254, c255 VS", device + VertexConstants + 240 * 16, 4);
        DumpWords("", device + VertexConstants + 254 * 16, 8);
    }
}

// ---- desenho ----
// D3DDevice_DrawVertices(device, PrimitiveType, StartVertex, VertexCount)
GUEST_OVERRIDE(82479038)
{
    gpu::Stats().draws++;
    uint32_t device = ctx.r3.u32, prim = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
    GUEST_ORIGINAL(82479038)(ctx, base);
    if (Capturing())
        CaptureDraw("DrawVertices", device, prim, start, count, 0);
    NativeDraw(device, prim, start, count, nullptr, 0);
}

// D3DDevice_DrawIndexedVertices(device, PrimitiveType, BaseVertexIndex, StartIndex, IndexCount)
GUEST_OVERRIDE(82479450)
{
    gpu::Stats().indexedDraws++;
    uint32_t device = ctx.r3.u32, prim = ctx.r4.u32, baseVertex = ctx.r5.u32, start = ctx.r6.u32, count = ctx.r7.u32;
    GUEST_ORIGINAL(82479450)(ctx, base);
    if (Capturing())
        CaptureDraw("DrawIndexedVertices", device, prim, baseVertex, start, count);

    // índices: objeto +0 bit 31 = 32 bits, +0x18 endereço; big-endian. 0xFFFF/0xFFFFFFFF reiniciam a tira
    uint32_t ib = kernel::Load32(device + IndexBuffer);
    if (!ib || count == 0)
        return;
    bool wide = kernel::Load32(ib) >> 31;
    uint32_t address = kernel::Load32(ib + 0x18);
    // índices convertidos ficam em cache como os vértices (mesma faixa do buffer, mesmos bytes = mesmos índices)
    struct CachedIndices
    {
        renderer::IndexStream stream;
        uint64_t hash = 0, checkedFrame = ~0ull;
        uint32_t lo = 0, hi = 0;
        bool valid = false;
    };
    static std::unordered_map<uint64_t, CachedIndices> indexCache;
    static uint64_t lastTrim = 0;
    uint64_t frame = gpu::Stats().frame;
    if (frame >= lastTrim + 60)
    {
        lastTrim = frame;
        for (auto it = indexCache.begin(); it != indexCache.end();)
            if (it->second.checkedFrame + 60 < frame)
            {
                renderer::Forget(it->second.stream);
                it = indexCache.erase(it);
            }
            else
            {
                ++it;
            }
    }
    uint32_t first = address + (wide ? 4 : 2) * start;
    uint64_t key = (uint64_t(first) << 32) ^ (uint64_t(count) << 1) ^ (uint64_t(baseVertex) * 0x9E3779B97F4A7C15ull) ^ wide;
    CachedIndices& c = indexCache[key];
    if (!c.valid || c.checkedFrame != frame)
    {
        uint64_t hash = HashBytes(first, size_t(count) * (wide ? 4 : 2));
        c.checkedFrame = frame;
        if (!c.valid || hash != c.hash)
        {
            std::vector<uint32_t>& indices = c.stream.data;
            indices.resize(count);
            uint32_t lo = ~0u, hi = 0;
            for (uint32_t i = 0; i < count; i++)
            {
                uint32_t index = wide ? kernel::Load32(address + 4 * (start + i))
                                      : __builtin_bswap16(*kernel::Guest<uint16_t>(address + 2 * (start + i)));
                if (index == (wide ? 0xFFFFFFFFu : 0xFFFFu))
                {
                    indices[i] = ~0u;
                    continue;
                }
                index += baseVertex;
                indices[i] = index;
                lo = std::min(lo, index);
                hi = std::max(hi, index);
            }
            for (uint32_t& index : indices)
                if (index != ~0u)
                    index -= lo;
            c.lo = lo;
            c.hi = hi;
            c.hash = hash;
            c.valid = true;
            c.stream.version++;
        }
    }
    if (c.lo > c.hi || c.hi - c.lo > 0x40000)
        return;
    NativeDraw(device, prim, c.lo, c.hi - c.lo + 1, &c.stream, 0);
}

// D3DDevice_DrawVerticesUP(device, PrimitiveType, VertexCount, const void* data, Stride)
GUEST_OVERRIDE(82478FF0)
{
    gpu::Stats().drawsUP++;
    uint32_t device = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r5.u32, data = ctx.r6.u32;
    GUEST_ORIGINAL(82478FF0)(ctx, base);
    if (Capturing())
        CaptureDraw("DrawVerticesUP", device, prim, count, data, 0);
    NativeDraw(device, prim, 0, count, nullptr, data);
}

// D3DDevice_ClearF(device, Flags, const D3DRECT* Rect, const D3DVECTOR4* Color, float Z (f1), Stencil (r8))
// O Z em f1 também ocupa a vaga de r7 no ABI do PowerPC, então o stencil vem em r8; lido de r7, o stencil era limpo
// com 0 em vez de 0x80, e a passada das sombras (stencil diferente de 0x80) escurecia a cena inteira.
// Flags do 360: bits 0-3 = render targets 0-3, 0x10 = Z, 0x20 = stencil. A cor é um ponteiro para 4 floats (r, g, b, a);
// tratar o ponteiro como cor 0xAARRGGBB pintava a tela de verde (endereço de pilha 0x400FF760 = verde 0F F7 60).
GUEST_OVERRIDE(82471D90)
{
    gpu::Stats().clears++;
    if (Capturing())
        fprintf(stderr, "[capture] Clear: flags %X retângulo %08X cor %08X z %g stencil %X; RT0 %08X DS %08X (r5 %08X r6 %08X r7 %08X r8 %08X)\n",
            ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.f1.f64, ctx.r8.u32, kernel::Load32(ctx.r3.u32 + RenderTarget0),
            kernel::Load32(ctx.r3.u32 + DepthStencil), ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
    float color[4] = { 0, 0, 0, 0 };
    if (ctx.r6.u32)
        for (int i = 0; i < 4; i++)
            color[i] = LoadF32(ctx.r6.u32 + 4 * i);
    if (Capturing())
        fprintf(stderr, "[capture]   cor do Clear: %g %g %g %g\n", color[0], color[1], color[2], color[3]);
    uint64_t glStart = platform::NowNs();
    renderer::Clear(ReadTarget(ctx.r3.u32), ctx.r4.u32, color, float(ctx.f1.f64), ctx.r8.u32);
    gpu::profile.gl += platform::NowNs() - glStart;
    GUEST_ORIGINAL(82471D90)(ctx, base);
}

// D3DDevice_Resolve(device, Flags, D3DRECT* SourceRect, D3DBaseTexture* DestTexture, D3DPOINT* DestPoint, DestLevel,
//                   DestSliceOrFace, D3DVECTOR4* ClearColor, float ClearZ (f1), ClearStencil, D3DRESOLVE_PARAMETERS*)
// Flags: bits 0-1 render target, 4 = profundidade
GUEST_OVERRIDE(82478AD0)
{
    uint32_t device = ctx.r3.u32, flags = ctx.r4.u32, dest = ctx.r6.u32;
    if (Capturing())
        fprintf(stderr, "[capture] Resolve: flags %X rect %08X destino %08X ponto %08X; RT0 %08X\n", flags, ctx.r5.u32, dest,
            ctx.r7.u32, kernel::Load32(device + RenderTarget0));
    if (dest)
    {
        if (ctx.r5.u32 || ctx.r7.u32)
        {
            static int warned = 0;
            if (warned++ < 4)
                fprintf(stderr, "[gpu] resolve com retângulo/ponto ainda copia a imagem inteira\n");
        }
        // textura: fetch constant em +0x1C
        renderer::Texture t = ReadTexture(dest + 0x1C);
        uint64_t glStart = platform::NowNs();
        renderer::Resolve(ReadTarget(device), t.address, t.width, t.height, (flags & 4) != 0);
        gpu::profile.gl += platform::NowNs() - glStart;
    }
    GUEST_ORIGINAL(82478AD0)(ctx, base);
}

// D3DDevice_Swap(device, D3DBaseTexture* FrontBuffer, D3DVIDEO_SCALER_PARAMETERS*)
GUEST_OVERRIDE(8247B1B8)
{
    if (ctx.r4.u32)
        gpu::frontBuffer = ReadTexture(ctx.r4.u32 + 0x1C).address;
    // DMC3_SHOW_SHADERS="ps:401D8000,vs:401D85D0": no quadro 300 traduz e imprime esses shaders (os objetos são
    // criados no boot e ficam no mesmo endereço), sem precisar chegar na cena que os usa
    static uint32_t swaps = 0;
    if (++swaps % 1200 == 0 && getenv("DMC3_LOG_GAMMA"))
    {
        // tabela de gama do D3DDevice_SetGammaRamp: 3 x 256 valores de 16 bits em device+0x3C30
        fprintf(stderr, "[gpu] gama:");
        for (uint32_t i = 0; i < 256; i += 32)
            fprintf(stderr, " %u:%04X/%04X/%04X", i, *kernel::Guest<uint16_t>(ctx.r3.u32 + 0x3C30 + 2 * i),
                *kernel::Guest<uint16_t>(ctx.r3.u32 + 0x3C30 + 0x200 + 2 * i), *kernel::Guest<uint16_t>(ctx.r3.u32 + 0x3C30 + 0x400 + 2 * i));
        fprintf(stderr, "\n");
    }
    if (swaps == 300)
        if (const char* list = getenv("DMC3_SHOW_SHADERS"))
            for (const char* p = list; *p;)
            {
                bool pixel = strncmp(p, "ps:", 3) == 0;
                char* end;
                uint32_t object = uint32_t(strtoul(p + 3, &end, 16));
                if (const CachedShader* s = GetShader(object, pixel, ctx.r3.u32))
                {
                    fprintf(stderr, "[gpu] shader %s %08X:\n%s\n", pixel ? "ps" : "vs", object, s->shader.body.c_str());
                    // literais: os últimos vec4 do banco de constantes (c[256 - n] .. c255)
                    for (const xenos::VertexFetch& f : s->shader.fetches)
                        fprintf(stderr, "[gpu]   vfetch slot %u stride %u offset %u formato %u%s%s exp %d\n", f.slot, f.stride, f.offset,
                            f.format, f.isSigned ? " com sinal" : "", f.isInteger ? " inteiro" : " normalizado", f.expAdjust);
                    size_t n = s->literals.size() / 4;
                    for (size_t k = 0; k < n; k++)
                        fprintf(stderr, "[gpu]   literal c%zu = %g %g %g %g\n", 256 - n + k, s->literals[4 * k],
                            s->literals[4 * k + 1], s->literals[4 * k + 2], s->literals[4 * k + 3]);
                }
                p = *end ? end + 1 : end;
            }
    GUEST_ORIGINAL(8247B1B8)(ctx, base);
}
