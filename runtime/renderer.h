// Renderizador nativo (SDL3 + OpenGL 3.3 no PC; a mesma lógica vai para OpenGL ES 3 no Android).
// Recebe os desenhos que a camada gpu.cpp intercepta, já com o estado lido do device do jogo e os shaders
// traduzidos para GLSL (xenos_shader.h).
//
// Imagens: render targets e texturas resolvidas são guardados com a linha 0 em cima, como na memória do
// console (o shader de vértice gerado inverte o y). Só a apresentação na janela desfaz a inversão.
#pragma once

#include "xenos_shader.h"

#include <cstdint>
#include <vector>

namespace renderer
{
    struct Texture
    {
        uint32_t address = 0;   // endereço no console dos texels (faixa 0xA0000000)
        uint32_t width = 0, height = 0, pitch = 0;   // pitch em texels
        uint32_t format = 0;    // formato da Xenos (6 = 8_8_8_8)
        uint32_t endian = 0;
        uint32_t swizzle = 0x688;   // do fetch constant: 3 bits por canal RGBA (0-3 = x-w, 4 = 0, 5 = 1)
        bool tiled = false;
        // amostragem (fetch constant): repetição 0 wrap, 1 espelho, 2+ clamp; filtros 0 ponto, 1 linear;
        // mip 0 ponto, 1 linear, 2 sem mipmap
        uint8_t clampX = 2, clampY = 2, magFilter = 1, minFilter = 1, mipFilter = 2;
    };

    struct BlendState
    {
        bool enable = false;
        uint32_t control = 0;   // RB_BLENDCONTROL: src 0-4, op 5-7, dest 8-12, e o mesmo para alfa em 16-28
        bool separateAlpha = false;
        bool alphaTest = false;
        uint32_t alphaFunc = 7; // 0 never .. 7 always
        float alphaRef = 0;
        float constant[4] = { 0, 0, 0, 0 };   // cor constante do blend (D3DRS_BLENDFACTOR; RB_BLEND_RED..ALPHA)
    };

    // stencil (RB_DEPTHCONTROL e RB_STENCILREFMASK[_BF]); índice 0 = frente, 1 = verso. Comparações como o depthFunc;
    // operações da Xenos: 0 keep, 1 zero, 2 replace, 3 incr com limite, 4 decr com limite, 5 invert, 6 incr circular,
    // 7 decr circular
    struct StencilState
    {
        bool enable = false;
        uint8_t func[2] = { 7, 7 }, fail[2] = {}, depthFail[2] = {}, pass[2] = {};
        uint8_t ref[2] = {}, mask[2] = { 0xFF, 0xFF }, writeMask[2] = { 0xFF, 0xFF };
    };

    struct RenderState
    {
        BlendState blend;
        StencilState stencil;
        bool depthTest = false, depthWrite = false;
        uint32_t depthFunc = 7;   // como o alphaFunc
        bool cullFront = false, cullBack = false, frontIsCW = true;
        uint32_t colorMask = 0xF; // RGBA
        bool viewport = false;    // senão, o render target inteiro
        float viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0, depthMin = 0, depthMax = 1;
    };

    // render target atual: superfícies do jogo (endereço do objeto) e o tamanho
    struct Target
    {
        uint32_t color = 0, depth = 0;
        uint32_t width = 1280, height = 720;
        uint32_t depthWidth = 0, depthHeight = 0;   // tamanho da superfície de profundidade (0 = o mesmo da cor)
    };

    struct DrawCall
    {
        const xenos::Shader* vs = nullptr;
        const xenos::Shader* ps = nullptr;
        uint64_t vsKey = 0, psKey = 0;               // identificam os shaders (para o cache de programas)
        std::vector<float> attributes;               // atributo i: vertexCount vec4 a partir de i * vertexCount * 4
        uint32_t vertexCount = 0;
        std::vector<uint32_t> indices;               // vazio: desenha os vértices em ordem
        uint32_t primitive = 4;                      // tipo da Xenos: 4 lista, 5 leque, 6 tira de triângulos, 13 quads
        const float* vsConstants = nullptr;          // 256 vec4
        const float* psConstants = nullptr;
        uint32_t bools[8] = {};
        Texture textures[32];                        // pelos fetch constants usados pelo pixel shader
        RenderState state;
        Target target;
    };

    // tudo isto roda na thread que desenha (a thread principal do jogo)
    void EnsureWindow();
    void Draw(const DrawCall& call);
    // flags do D3D do 360: bits 0-3 render targets, 0x10 profundidade, 0x20 stencil; cor RGBA em 0..1
    void Clear(const Target& target, uint32_t flags, const float color[4], float z, uint32_t stencil);
    // copia o render target (ou a profundidade) para a textura do jogo em destAddress (o que o D3DDevice_Resolve faz
    // pela EDRAM)
    void Resolve(const Target& target, uint32_t destAddress, uint32_t destWidth, uint32_t destHeight, bool depth);
    // mostra a textura do front buffer (endereço da última textura resolvida que o Swap recebeu)
    void Present(uint64_t frame, uint32_t frontBuffer);
    // grava <DMC3_SCREENSHOT_DIR>/<nome>.rgba na próxima apresentação (diagnóstico)
    void RequestScreenshot(const char* name);
    // quadro em que o diagnóstico detalhado está ligado (DMC3_DEBUG_FRAME ou um RequestScreenshot)
    bool DetailedFrame();
}
