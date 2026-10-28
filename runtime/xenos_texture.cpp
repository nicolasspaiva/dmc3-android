// Decodificação de texturas da Xenos. Cada texel (ou bloco 4x4 nos DXT) é lido como a GPU leria: palavras
// little-endian com a troca do fetch constant (0 nenhuma, 1 bytes em 16 bits, 2 bytes em 32 bits, 3 metades);
// os componentes saem como x, y, z, w (x nos bits baixos) e o swizzle do fetch constant os leva a RGBA.
#include "xenos_texture.h"

#include <cstring>

namespace xenos
{
    namespace
    {
        struct FormatInfo
        {
            uint32_t blockSize;   // 1 ou 4 (DXT)
            uint32_t bytes;       // por bloco
            uint32_t log2Bytes;
        };

        bool Info(uint32_t format, FormatInfo& f)
        {
            switch (format)
            {
            case 2: f = { 1, 1, 0 }; return true;    // 8
            case 4: f = { 1, 2, 1 }; return true;    // 1_5_5_5
            case 5: f = { 1, 2, 1 }; return true;    // 5_6_5
            case 6: f = { 1, 4, 2 }; return true;    // 8_8_8_8
            case 15: f = { 1, 2, 1 }; return true;   // 4_4_4_4
            case 18: f = { 4, 8, 3 }; return true;   // DXT1
            case 19: f = { 4, 16, 4 }; return true;  // DXT2/3
            case 20: f = { 4, 16, 4 }; return true;  // DXT4/5
            default: return false;
            }
        }

        // deslocamento em bytes de um bloco numa textura em tile (pitch em blocos)
        uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2Bpp)
        {
            pitch = (pitch + 31) & ~31u;
            uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2Bpp + 7);
            uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2Bpp;
            uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
            return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
                ((((y & 8) >> 2) + (x >> 3)) & 3) * 64 + (offset & 0x3F);
        }

        uint32_t Swap(uint32_t w, uint32_t endian)
        {
            switch (endian)
            {
            case 1: return ((w & 0x00FF00FF) << 8) | ((w >> 8) & 0x00FF00FF);
            case 2: return __builtin_bswap32(w);
            case 3: return (w << 16) | (w >> 16);
            default: return w;
            }
        }

        uint32_t Word(const uint8_t* p, size_t offset, uint32_t endian)
        {
            uint32_t w;
            memcpy(&w, p + (offset & ~size_t(3)), 4);
            return Swap(w, endian);
        }

        uint8_t Expand(uint32_t v, int bits) { return uint8_t(bits == 8 ? v : (v * 255 + ((1 << bits) - 1) / 2) / ((1 << bits) - 1)); }

        // componentes x, y, z, w de um texel (formatos não comprimidos)
        void Texel(uint32_t format, uint32_t v, uint8_t c[4])
        {
            switch (format)
            {
            case 2: c[0] = c[1] = c[2] = c[3] = uint8_t(v); break;
            case 4: c[0] = Expand(v & 31, 5); c[1] = Expand((v >> 5) & 31, 5); c[2] = Expand((v >> 10) & 31, 5); c[3] = (v >> 15) & 1 ? 255 : 0; break;
            case 5: c[0] = Expand(v & 31, 5); c[1] = Expand((v >> 5) & 63, 6); c[2] = Expand((v >> 11) & 31, 5); c[3] = 255; break;
            case 6: c[0] = uint8_t(v); c[1] = uint8_t(v >> 8); c[2] = uint8_t(v >> 16); c[3] = uint8_t(v >> 24); break;
            case 15: c[0] = Expand(v & 15, 4); c[1] = Expand((v >> 4) & 15, 4); c[2] = Expand((v >> 8) & 15, 4); c[3] = Expand(v >> 12, 4); break;
            }
        }

        // cores de um bloco DXT (componentes x, y, z = vermelho, verde, azul do 5_6_5 com o vermelho nos bits altos)
        void DxtColors(uint32_t word, bool fourColors, uint8_t colors[4][4])
        {
            uint32_t c0 = word & 0xFFFF, c1 = word >> 16;
            for (int i = 0; i < 2; i++)
            {
                uint32_t c = i ? c1 : c0;
                colors[i][0] = Expand(c >> 11, 5);
                colors[i][1] = Expand((c >> 5) & 63, 6);
                colors[i][2] = Expand(c & 31, 5);
                colors[i][3] = 255;
            }
            for (int k = 0; k < 3; k++)
            {
                if (fourColors || c0 > c1)
                {
                    colors[2][k] = uint8_t((2 * colors[0][k] + colors[1][k] + 1) / 3);
                    colors[3][k] = uint8_t((colors[0][k] + 2 * colors[1][k] + 1) / 3);
                }
                else
                {
                    colors[2][k] = uint8_t((colors[0][k] + colors[1][k]) / 2);
                    colors[3][k] = 0;
                }
            }
            colors[2][3] = 255;
            colors[3][3] = (fourColors || c0 > c1) ? 255 : 0;
        }

        uint32_t Pack(const uint8_t c[4], uint32_t swizzle)
        {
            uint32_t out = 0;
            for (int i = 0; i < 4; i++)
            {
                uint32_t sel = (swizzle >> (3 * i)) & 7;
                uint8_t v = sel < 4 ? c[sel] : sel == 4 ? 0 : 255;
                out |= uint32_t(v) << (8 * i);
            }
            return out;
        }
    }

    bool TextureSupported(uint32_t format)
    {
        FormatInfo f;
        return Info(format, f);
    }

    size_t TextureBytes(const renderer::Texture& t)
    {
        FormatInfo f;
        if (!Info(t.format, f))
            return 0;
        uint32_t pitch = (t.pitch + f.blockSize - 1) / f.blockSize;
        uint32_t rows = (t.height + f.blockSize - 1) / f.blockSize;
        if (t.tiled)
        {
            pitch = (pitch + 31) & ~31u;
            rows = (rows + 31) & ~31u;
        }
        return size_t(pitch) * rows * f.bytes;
    }

    bool DecodeTexture(const renderer::Texture& t, const uint8_t* src, std::vector<uint32_t>& rgba)
    {
        FormatInfo f;
        if (!Info(t.format, f))
            return false;
        rgba.assign(size_t(t.width) * t.height, 0);
        uint32_t pitch = (t.pitch + f.blockSize - 1) / f.blockSize;   // em blocos
        uint32_t blocksX = (t.width + f.blockSize - 1) / f.blockSize, blocksY = (t.height + f.blockSize - 1) / f.blockSize;
        for (uint32_t by = 0; by < blocksY; by++)
            for (uint32_t bx = 0; bx < blocksX; bx++)
            {
                size_t offset = t.tiled ? TiledOffset(bx, by, pitch, f.log2Bytes) : (size_t(by) * pitch + bx) * f.bytes;
                if (f.blockSize == 1)
                {
                    uint32_t w = Word(src, offset, t.endian);
                    uint32_t v = f.bytes == 4 ? w : f.bytes == 2 ? (w >> ((offset & 2) * 8)) & 0xFFFF : (w >> ((offset & 3) * 8)) & 0xFF;
                    uint8_t c[4];
                    Texel(t.format, v, c);
                    rgba[size_t(by) * t.width + bx] = Pack(c, t.swizzle);
                    continue;
                }
                // DXT: a parte de cor fica nos últimos 8 bytes do bloco (DXT3/5) ou no bloco inteiro (DXT1)
                size_t colorOffset = offset + (f.bytes == 16 ? 8 : 0);
                uint8_t colors[4][4];
                DxtColors(Word(src, colorOffset, t.endian), t.format != 18, colors);
                uint32_t indices = Word(src, colorOffset + 4, t.endian);
                uint64_t alpha = 0;
                uint8_t alphas[8] = {};
                if (t.format == 19)
                    alpha = Word(src, offset, t.endian) | (uint64_t(Word(src, offset + 4, t.endian)) << 32);
                else if (t.format == 20)
                {
                    uint32_t a0 = Word(src, offset, t.endian), a1 = Word(src, offset + 4, t.endian);
                    alphas[0] = uint8_t(a0);
                    alphas[1] = uint8_t(a0 >> 8);
                    alpha = (a0 >> 16) | (uint64_t(a1) << 16);   // 48 bits de índices de 3 bits
                    for (int i = 2; i < 8; i++)
                        alphas[i] = alphas[0] > alphas[1]
                            ? uint8_t(((8 - i) * alphas[0] + (i - 1) * alphas[1]) / 7)
                            : i < 6 ? uint8_t(((6 - i) * alphas[0] + (i - 1) * alphas[1]) / 5) : (i == 6 ? 0 : 255);
                }
                for (uint32_t py = 0; py < 4; py++)
                    for (uint32_t px = 0; px < 4; px++)
                    {
                        uint32_t x = bx * 4 + px, y = by * 4 + py;
                        if (x >= t.width || y >= t.height)
                            continue;
                        uint32_t i = py * 4 + px;
                        uint8_t c[4];
                        memcpy(c, colors[(indices >> (2 * i)) & 3], 4);
                        if (t.format == 19)
                            c[3] = uint8_t(((alpha >> (4 * i)) & 15) * 17);
                        else if (t.format == 20)
                            c[3] = alphas[(alpha >> (3 * i)) & 7];
                        rgba[size_t(y) * t.width + x] = Pack(c, t.swizzle);
                    }
            }
        return true;
    }
}
