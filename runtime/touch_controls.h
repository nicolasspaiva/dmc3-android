// Controles na tela (toque): analógico esquerdo, A/B/X/Y, ombros e gatilhos, direcional (troca de estilo),
// START e BACK. Os toques chegam pelo SDL (vários dedos ao mesmo tempo) e viram um Gamepad que input.cpp
// soma ao do controle físico. Um botão discreto no topo (entre BACK e START) esconde ou mostra os controles,
// e a escolha fica gravada; com um controle físico conectado eles somem sozinhos, e um toque os traz de volta.
// A engrenagem embaixo, no meio, abre o menu do port: modo de tela, controles na tela, edição do layout (arrastar
// e redimensionar cada botão; gravado em layout.txt) e restaurar o layout padrão.
// No PC, DMC3_TOUCH=1 liga os controles na tela (o mouse simula o toque) para testar o layout.
#pragma once

#include "input.h"

#include <vector>

namespace touch
{
    void HandleEvent(const SDL_Event& e);
    input::Gamepad State();
    void PhysicalGamepadUsed();   // um controle físico foi usado: esconde os controles na tela
    void GamepadConnected(bool connected);   // controle conectado esconde; desconectado mostra de novo
    bool Visible();
    // modo de tela escolhido no menu: 0 preencher a tela (esticar), 1 16:9 com faixas, 2 zoom (corta em cima e
    // embaixo), 3 widescreen; fica gravado. Vale também sem os controles na tela (no PC, 16:9)
    int DisplayMode();
    // botões de ação do controle físico pela letra impressa (true) ou pela posição do Xbox (false, o padrão); no menu
    bool PadByLabel();

    // triângulos do desenho dos controles em pixels da janela: x, y, r, g, b, a por vértice
    void BuildOverlay(int windowWidth, int windowHeight, std::vector<float>& vertices);
}
