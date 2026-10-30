// Tradutor de shaders da Xenos (microcódigo da GPU do Xbox 360) para GLSL (OpenGL 3.3 / GLES 3.0).
// O desmontador de referência é scripts/xenos_disasm.py.
//
// Convenções do GLSL gerado:
//   - vértice: atributos "layout(location = i) in vec4 a<i>" (um por vfetch, preparados na CPU), constantes
//     "uniform vec4 VC[256]", saídas "out vec4 v<i>" (interpoladores 0..7) e gl_Position;
//   - pixel: entradas "in vec4 v<i>", constantes "uniform vec4 PC[256]", texturas "uniform sampler2D T<n>"
//     (n = fetch constant de textura), cor em "out vec4 color0";
//   - constantes booleanas em "uniform uint B[8]" (bit n de B[n / 32]).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xenos
{
    constexpr uint32_t Interpolators = 8;

    // um vfetch do shader de vértice; o atributo i do GLSL corresponde a fetches[i]
    struct VertexFetch
    {
        uint32_t slot = 0;       // fetch constant de vértice (0..95)
        uint32_t offset = 0;     // em palavras de 32 bits, dentro do vértice
        uint32_t stride = 0;     // em palavras
        uint32_t format = 0;     // VertexFormat da Xenos (6 = 8_8_8_8, 37 = 32_32_FLOAT, 57 = 32_32_32_FLOAT...)
        bool isSigned = false;
        bool isInteger = false;  // false = normalizado (só para formatos inteiros)
        int32_t expAdjust = 0;
    };

    struct Shader
    {
        bool ok = false;
        bool pixel = false;
        std::string body;                 // GLSL sem a linha #version
        std::string error;
        std::vector<VertexFetch> fetches;
        uint32_t textureMask = 0;         // fetch constants de textura usadas
        bool writesDepth = false;
        uint32_t constantMask[8] = {};    // constantes VC/PC lidas (bit n de [n / 32]), para enviar só essas
        bool relativeConstants = false;   // índice relativo ao a0: qualquer constante pode ser lida
    };

    // words: o programa (instruções de controle a partir da palavra 0), já na ordem de bytes do host
    Shader Translate(const uint32_t* words, uint32_t count, bool pixel);
}
