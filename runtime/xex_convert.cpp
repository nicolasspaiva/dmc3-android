// Conversão do XEX2 no PE (ver xex_convert.h).
#include "xex_convert.h"

extern "C"
{
#include "../tools/XenonRecomp/thirdparty/tiny-AES-c/aes.h"
}

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace
{
    // chaves de console de varejo e de kit de desenvolvimento (as mesmas do Xenia e do XenonRecomp)
    const uint8_t RetailKey[16] = { 0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91 };
    const uint8_t DevkitKey[16] = {};

    // o DMC3.pe para o qual recomp/ppc foi gerado: tamanho e FNV-1a de 64 bits
    constexpr size_t ExpectedSize = 12156928;
    constexpr uint64_t ExpectedHash = 0x480F142D85599D43ull;

    uint32_t Be32(const std::vector<uint8_t>& d, size_t at)
    {
        return at + 4 <= d.size() ? uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3] : 0;
    }

    uint64_t Fnv(const std::vector<uint8_t>& data)
    {
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint8_t b : data)
            h = (h ^ b) * 0x100000001B3ull;
        return h;
    }

    bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f)
            return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        out.resize(size > 0 ? size_t(size) : 0);
        bool ok = fread(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        return ok;
    }

    // compressão "basic": blocos de dados seguidos de zeros, descritos no cabeçalho de formato do arquivo
    bool Rebuild(const std::vector<uint8_t>& data, const std::vector<uint8_t>& d, uint32_t ff, uint32_t infoSize,
        uint16_t comp, std::vector<uint8_t>& out)
    {
        if (comp == 0)
        {
            out = data;
            return true;
        }
        if (comp != 1)
            return false;
        out.clear();
        size_t pos = 0;
        for (uint32_t i = 0; i < (infoSize - 8) / 8; i++)
        {
            uint32_t size = Be32(d, ff + 8 + i * 8), zeros = Be32(d, ff + 12 + i * 8);
            if (pos + size > data.size())
                return false;
            out.insert(out.end(), data.begin() + pos, data.begin() + pos + size);
            out.insert(out.end(), zeros, 0);
            pos += size;
        }
        return true;
    }

    bool Expected(const std::vector<uint8_t>& image)
    {
        return image.size() == ExpectedSize && Fnv(image) == ExpectedHash;
    }
}

namespace xex
{
    Result Convert(const std::string& xexPath, const std::string& pePath)
    {
        std::vector<uint8_t> d;
        if (!ReadFile(xexPath, d) || d.size() < 0x200 || memcmp(d.data(), "XEX2", 4) != 0)
            return Result::Unreadable;
        uint32_t peOffset = Be32(d, 8), securityOffset = Be32(d, 0x10), headerCount = Be32(d, 0x14);
        uint32_t ff = 0;
        for (uint32_t i = 0; i < headerCount; i++)
            if (Be32(d, 0x18 + i * 8) == 0x3FF)
                ff = Be32(d, 0x1C + i * 8);
        if (!ff || peOffset >= d.size())
            return Result::Unreadable;
        uint32_t infoSize = Be32(d, ff);
        uint16_t encryption = uint16_t(Be32(d, ff + 4) >> 16), comp = uint16_t(Be32(d, ff + 4));
        std::vector<uint8_t> body(d.begin() + peOffset, d.end()), image;

        bool found = false;
        if (encryption == 1)
        {
            for (const uint8_t* master : { RetailKey, DevkitKey })
            {
                // a chave da sessão vem do cabeçalho de segurança, cifrada com a chave do console (ECB)
                uint8_t session[16];
                memcpy(session, d.data() + securityOffset + 0x150, 16);
                AES_ctx ctx;
                AES_init_ctx(&ctx, master);
                AES_ECB_decrypt(&ctx, session);
                std::vector<uint8_t> plain = body;
                uint8_t iv[16] = {};
                AES_init_ctx_iv(&ctx, session, iv);
                AES_CBC_decrypt_buffer(&ctx, plain.data(), plain.size() & ~size_t(15));
                if (Rebuild(plain, d, ff, infoSize, comp, image) && image.size() > 2 && image[0] == 'M' && image[1] == 'Z')
                {
                    found = true;
                    break;
                }
            }
        }
        else
        {
            found = Rebuild(body, d, ff, infoSize, comp, image) && image.size() > 2 && image[0] == 'M' && image[1] == 'Z';
        }
        if (!found)
            return Result::Unsupported;
        if (!Expected(image))
        {
            fprintf(stderr, "[xex] o PE gerado tem %zu bytes e FNV %016llX: não é a versão recompilada\n", image.size(),
                (unsigned long long)Fnv(image));
            return Result::WrongVersion;
        }
        FILE* f = fopen((pePath + ".tmp").c_str(), "wb");
        if (!f)
            return Result::WriteFailed;
        bool ok = fwrite(image.data(), 1, image.size(), f) == image.size();
        ok = fclose(f) == 0 && ok;
        if (!ok || std::rename((pePath + ".tmp").c_str(), pePath.c_str()) != 0)
            return Result::WriteFailed;
        return Result::Ok;
    }

    bool IsExpectedImage(const std::string& pePath)
    {
        std::vector<uint8_t> image;
        return ReadFile(pePath, image) && Expected(image);
    }
}

// DMC3_XEX_TEST=<xex>;<pe>: converte e sai (para conferir no PC que o resultado é igual ao DMC3.pe)
namespace
{
    struct XexTest
    {
        XexTest()
        {
            const char* spec = getenv("DMC3_XEX_TEST");
            if (!spec)
                return;
            std::string s = spec;
            size_t sep = s.find(';');
            xex::Result r = xex::Convert(s.substr(0, sep), s.substr(sep + 1));
            std::vector<uint8_t> image;
            ReadFile(s.substr(sep + 1), image);
            fprintf(stderr, "[xex] teste: resultado %d, %zu bytes, FNV %016llX\n", int(r), image.size(), (unsigned long long)Fnv(image));
            fflush(stderr);
            std::exit(0);
        }
    } xexTest;
}
