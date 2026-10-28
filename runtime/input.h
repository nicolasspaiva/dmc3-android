// Entrada: teclado e controle pelo SDL3, entregues ao jogo no formato do XInput do 360.
#pragma once

#include <cstdint>

union SDL_Event;

namespace input
{
    // botões do XInput (XINPUT_GAMEPAD_*)
    enum : uint16_t
    {
        DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008,
        Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080,
        LeftShoulder = 0x0100, RightShoulder = 0x0200,
        A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
    };

    struct Gamepad
    {
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0, rightTrigger = 0;
        int16_t thumbLX = 0, thumbLY = 0, thumbRX = 0, thumbRY = 0;
    };

    // na thread da janela: trata eventos de conexão de controle e atualiza o retrato do estado
    void HandleEvent(const SDL_Event& e);
    void Update(uint64_t frame);

    // em qualquer thread: último estado do jogador 1 e o número de pacote (muda quando o estado muda)
    Gamepad State(uint32_t* packet);

    // marca um evento do jogo no quadro atual, para entradas do DMC3_INPUT_SCRIPT relativas a ele ("evento+N:...")
    void Mark(const char* event);
}
