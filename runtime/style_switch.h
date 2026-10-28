// Troca de estilo em tempo real do Dante pelo direcional (runtime/style_switch.cpp).
#pragma once

#include "kernel.h"

namespace style
{
    // chamado com o estado do controle do jogador 1 antes de entregá-lo ao jogo: durante a missão com o Dante,
    // o direcional vira pedido de troca de estilo e some dos botões que o jogo vê
    uint16_t FilterButtons(uint16_t buttons);

    // o Dante foi atualizado nos últimos quadros (missão em andamento, fora da pausa e das cenas)
    bool DanteActive();

    // última troca de estilo feita (para o aviso na tela): estilo novo (ordem do jogo: Swordmaster, Gunslinger,
    // Trickster, Royalguard, Quicksilver, Doppelganger) e o instante em ns (SDL_GetTicksNS); false se nenhuma ainda
    bool LastChange(uint32_t& style, uint64_t& whenNs);
}
