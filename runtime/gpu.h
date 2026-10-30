// Camada que substitui o D3D do jogo (ver x360/d3d/D3D_MAP.md).
//
// Divisão:
//   - Setters de estado, criação e Lock de recursos do D3D original continuam rodando: só gravam na
//     memória do console (registradores-sombra do device, cabeçalhos de recursos).
//   - O que falaria com a GPU Xenos é substituído: espera por fences e pela GPU, apresentação do quadro
//     e, aos poucos, os Draw*/Clear/Resolve, que passam a ler o estado direto do device.
//   - Os comandos que o D3D original ainda grava para a Xenos não são executados por ninguém.
#pragma once

#include "kernel.h"

namespace gpu
{
    // endereços do D3D do XDK dentro do DMC3.xex (nomes em x360/d3d/d3d_names.tsv)
    constexpr uint32_t DeviceGlobal = 0x82A2ABA0;   // IDirect3DDevice9* do jogo

    // campos do device usados aqui
    constexpr uint32_t DeviceIssuedFence = 0x2B1C;  // última fence emitida pela CPU
    constexpr uint32_t DeviceGpuBlock = 0x2B10;     // ponteiro para o bloco que a GPU escreveria: +0 fence concluída, +4 swap

    struct FrameStats
    {
        uint64_t frame = 0;
        uint32_t draws = 0;
        uint32_t indexedDraws = 0;
        uint32_t drawsUP = 0;
        uint32_t clears = 0;
        uint32_t gpuWaits = 0;
    };

    FrameStats& Stats();

    // tempo gasto em cada parte do quadro desde o último relatório (ns), e quadros pulados (renderer, pular quadros)
    struct Profile
    {
        uint64_t prep = 0, gl = 0, present = 0, skipped = 0;
        uint64_t uploads = 0, uploadBytes = 0, draws = 0, textures = 0, submit = 0;   // envios de vértices e índices ao GL
    };
    extern Profile profile;
    void Present();   // fim de quadro (chamado pelo VdSwap)

    // tempo que a thread que apresenta os quadros passa bloqueada no kernel (0 leitura, 1 espera, 2 pausa);
    // com DMC3_LOG_VIDEO_SYNC, os intervalos longos entre quadros mostram onde o tempo foi
    void BlockedTime(int kind, uint64_t ns);
}
