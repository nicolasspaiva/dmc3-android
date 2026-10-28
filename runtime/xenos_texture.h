// Conversão de texturas da Xenos (memória do console) para RGBA8 na CPU: formatos de cor e DXT, lineares
// ou em tile, com a troca de bytes e o swizzle do fetch constant. Igual no PC e no Android.
#pragma once

#include "renderer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xenos
{
    bool TextureSupported(uint32_t format);
    // bytes que a textura ocupa na memória (para saber quando ela muda)
    size_t TextureBytes(const renderer::Texture& t);
    // RGBA8, linha 0 em cima; devolve false se o formato não é suportado
    bool DecodeTexture(const renderer::Texture& t, const uint8_t* source, std::vector<uint32_t>& rgba);
}
