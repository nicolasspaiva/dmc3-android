// Teclado e controle pelo SDL3. O estado é lido na thread da janela (Update, chamado a cada quadro)
// e guardado num retrato protegido por mutex, que as funções XamInput* do kernel leem.
//
// Teclado (jogador 1); F12, P ou Print Screen grava uma captura de diagnóstico (renderer_gl.cpp):
//   Enter / Z = A      Backspace / X = B      C = X      V = Y
//   setas = direcional        W A S D = analógico esquerdo      I J K L = analógico direito
//   Q / E = LB / RB           1 / 3 = gatilhos LT / RT          Espaço = Start      Tab = Back
//
// DMC3_INPUT_SCRIPT="quadro:botões:duração,...": botões simulados para testes automáticos
// (ex.: "400:A:10" aperta A do quadro 400 ao 409; botões: A B X Y START BACK UP DOWN LEFT RIGHT LB RB).
// O quadro pode ser relativo a um evento do jogo: "m01_s00+300:A:6" conta a partir do pulo do vídeo m01_s00
// (os eventos são marcados com input::Mark; os vídeos marcam o próprio nome).
#include "input.h"
#include "touch_controls.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace input
{
    namespace
    {
        std::mutex lock;
        Gamepad current;
        uint32_t packet = 0;
        SDL_Gamepad* pad = nullptr;

        struct Scripted
        {
            uint64_t frame, frames;
            uint16_t buttons;
            std::string anchor;   // evento de referência ("" = início do jogo)
        };

        std::mutex anchorLock;
        std::map<std::string, uint64_t> anchors;
        uint64_t currentFrame = 0;

        std::vector<Scripted> LoadScript()
        {
            std::vector<Scripted> out;
            const char* env = getenv("DMC3_INPUT_SCRIPT");
            if (!env)
                return out;
            std::string s = env;
            size_t p = 0;
            while (p < s.size())
            {
                size_t end = s.find(',', p);
                std::string item = s.substr(p, end == std::string::npos ? std::string::npos : end - p);
                p = end == std::string::npos ? s.size() : end + 1;
                Scripted e{ 0, 1, 0, "" };
                char names[64] = {};
                unsigned long long frame = 0, frames = 1;
                size_t plus = item.find('+'), colon = item.find(':');
                if (plus != std::string::npos && plus < colon)
                {
                    e.anchor = item.substr(0, plus);
                    item = item.substr(plus + 1);
                }
                if (sscanf(item.c_str(), "%llu:%63[^:]:%llu", &frame, names, &frames) >= 2)
                {
                    e.frame = frame;
                    e.frames = frames;
                    static const struct { const char* name; uint16_t bit; } map[] = {
                        { "A", A }, { "B", B }, { "X", X }, { "Y", Y }, { "START", Start }, { "BACK", Back },
                        { "UP", DpadUp }, { "DOWN", DpadDown }, { "LEFT", DpadLeft }, { "RIGHT", DpadRight },
                        { "LB", LeftShoulder }, { "RB", RightShoulder } };
                    for (char* t = strtok(names, "+"); t; t = strtok(nullptr, "+"))
                        for (auto& m : map)
                            if (SDL_strcasecmp(t, m.name) == 0)
                                e.buttons |= m.bit;
                    out.push_back(e);
                }
            }
            return out;
        }

        int16_t Axis(const bool* keys, SDL_Scancode negative, SDL_Scancode positive)
        {
            return int16_t((keys[positive] ? 32767 : 0) - (keys[negative] ? 32767 : 0));
        }
    }

    void HandleEvent(const SDL_Event& e)
    {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad)
        {
            pad = SDL_OpenGamepad(e.gdevice.which);
            if (pad)
                fprintf(stderr, "[input] controle conectado: %s (botão de baixo com a letra %d, 1 = A)\n",
                    SDL_GetGamepadName(pad), int(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH)));
            touch::GamepadConnected(pad != nullptr);
        }
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && e.gdevice.which == SDL_GetGamepadID(pad))
        {
            SDL_CloseGamepad(pad);
            pad = nullptr;
            fprintf(stderr, "[input] controle desconectado\n");
            touch::GamepadConnected(false);
        }
        // botões do controle físico escondem os controles na tela; os toques vão para eles
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            touch::PhysicalGamepadUsed();
        touch::HandleEvent(e);
    }

    void Update(uint64_t frame)
    {
        Gamepad g;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        struct { SDL_Scancode key; uint16_t bit; } const keyMap[] = {
            { SDL_SCANCODE_RETURN, A }, { SDL_SCANCODE_Z, A }, { SDL_SCANCODE_BACKSPACE, B }, { SDL_SCANCODE_X, B },
            { SDL_SCANCODE_C, X }, { SDL_SCANCODE_V, Y }, { SDL_SCANCODE_UP, DpadUp }, { SDL_SCANCODE_DOWN, DpadDown },
            { SDL_SCANCODE_LEFT, DpadLeft }, { SDL_SCANCODE_RIGHT, DpadRight }, { SDL_SCANCODE_SPACE, Start },
            { SDL_SCANCODE_TAB, Back }, { SDL_SCANCODE_Q, LeftShoulder }, { SDL_SCANCODE_E, RightShoulder } };
        for (auto& k : keyMap)
            if (keys[k.key])
                g.buttons |= k.bit;
        g.leftTrigger = keys[SDL_SCANCODE_1] ? 255 : 0;
        g.rightTrigger = keys[SDL_SCANCODE_3] ? 255 : 0;
        g.thumbLX = Axis(keys, SDL_SCANCODE_A, SDL_SCANCODE_D);
        g.thumbLY = Axis(keys, SDL_SCANCODE_S, SDL_SCANCODE_W);   // XInput: y positivo = para cima
        g.thumbRX = Axis(keys, SDL_SCANCODE_J, SDL_SCANCODE_L);
        g.thumbRY = Axis(keys, SDL_SCANCODE_K, SDL_SCANCODE_I);

        if (pad)
        {
            // botões de ação: pela posição do Xbox (padrão: o de baixo é o A e pula) ou, escolhido no menu, pela letra
            // impressa (nos controles da Nintendo o A fica à direita e o B embaixo). O SDL diz a letra de cada posição
            bool labels = touch::PadByLabel();
            auto byLabel = [&](SDL_GamepadButtonLabel label, SDL_GamepadButton fallback) {
                for (SDL_GamepadButton b : { SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                         SDL_GAMEPAD_BUTTON_NORTH })
                    if (labels && SDL_GetGamepadButtonLabel(pad, b) == label)
                        return b;
                return fallback;   // sem letras (PlayStation: cruz, círculo...): pela posição do Xbox
            };
            struct { SDL_GamepadButton button; uint16_t bit; } const padMap[] = {
                { byLabel(SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_SOUTH), A },
                { byLabel(SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_EAST), B },
                { byLabel(SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_WEST), X },
                { byLabel(SDL_GAMEPAD_BUTTON_LABEL_Y, SDL_GAMEPAD_BUTTON_NORTH), Y },
                { SDL_GAMEPAD_BUTTON_START, Start }, { SDL_GAMEPAD_BUTTON_BACK, Back },
                { SDL_GAMEPAD_BUTTON_LEFT_STICK, LeftThumb }, { SDL_GAMEPAD_BUTTON_RIGHT_STICK, RightThumb },
                { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, LeftShoulder }, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RightShoulder },
                { SDL_GAMEPAD_BUTTON_DPAD_UP, DpadUp }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, DpadDown },
                { SDL_GAMEPAD_BUTTON_DPAD_LEFT, DpadLeft }, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, DpadRight } };
            for (auto& b : padMap)
                if (SDL_GetGamepadButton(pad, b.button))
                    g.buttons |= b.bit;
            auto stick = [&](SDL_GamepadAxis axis, int16_t keyboard, bool invert) {
                int v = SDL_GetGamepadAxis(pad, axis);
                if (invert)
                    v = -v - 1;   // SDL: y positivo = para baixo
                return keyboard ? keyboard : int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
            };
            g.thumbLX = stick(SDL_GAMEPAD_AXIS_LEFTX, g.thumbLX, false);
            g.thumbLY = stick(SDL_GAMEPAD_AXIS_LEFTY, g.thumbLY, true);
            g.thumbRX = stick(SDL_GAMEPAD_AXIS_RIGHTX, g.thumbRX, false);
            g.thumbRY = stick(SDL_GAMEPAD_AXIS_RIGHTY, g.thumbRY, true);
            auto trigger = [&](SDL_GamepadAxis axis, uint8_t keyboard) {
                return keyboard ? keyboard : uint8_t(SDL_GetGamepadAxis(pad, axis) * 255 / 32767);
            };
            g.leftTrigger = trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, g.leftTrigger);
            g.rightTrigger = trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, g.rightTrigger);
        }

        // controles na tela: somados ao teclado e ao controle físico
        input::Gamepad t = touch::State();
        g.buttons |= t.buttons;
        g.leftTrigger = std::max(g.leftTrigger, t.leftTrigger);
        g.rightTrigger = std::max(g.rightTrigger, t.rightTrigger);
        if (t.thumbLX || t.thumbLY)
        {
            g.thumbLX = t.thumbLX;
            g.thumbLY = t.thumbLY;
        }

        static std::vector<Scripted> script = LoadScript();
        {
            std::lock_guard guard(anchorLock);
            currentFrame = frame;
            for (auto& s : script)
            {
                uint64_t start = s.frame;
                if (!s.anchor.empty())
                {
                    auto a = anchors.find(s.anchor);
                    if (a == anchors.end())
                        continue;
                    start += a->second;
                }
                if (frame >= start && frame < start + s.frames)
                    g.buttons |= s.buttons;
            }
        }

        std::lock_guard guard(lock);
        if (memcmp(&g, &current, sizeof(g)) != 0)
        {
            current = g;
            packet++;
        }
    }

    Gamepad State(uint32_t* packetOut)
    {
        std::lock_guard guard(lock);
        if (packetOut)
            *packetOut = packet;
        return current;
    }

    void Mark(const char* event)
    {
        std::lock_guard guard(anchorLock);
        anchors[event] = currentFrame;
        if (getenv("DMC3_INPUT_SCRIPT"))
            fprintf(stderr, "[input] evento %s no quadro %llu\n", event, (unsigned long long)currentFrame);
    }
}
