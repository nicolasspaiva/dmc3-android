// Controles na tela (ver touch_controls.h).
#include "touch_controls.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <string>

// fonte 8x8 de domínio público que o SDL usa no SDL_RenderDebugText (ASCII 33-126 e Latin-1 161-255), para os textos
// do menu
#include "../tools/SDL3-3.4.18/src/render/SDL_render_debug_font.h"

namespace
{
    // posições em frações da tela: x da largura, y e tamanhos da altura (os círculos ficam redondos em qualquer tela)
    struct Button
    {
        float x, y, radius;     // centro e raio padrão (frações: x da largura, y e raio da altura)
        uint16_t bit;           // botão do XInput, ou 0 para os gatilhos
        int trigger;            // 1 = LT, 2 = RT
        float r, g, b;
    };

    const Button Buttons[] = {
        // botões de ação, nas cores do controle do Xbox
        { 0.885f, 0.80f, 0.075f, input::A, 0, 0.30f, 0.75f, 0.25f },
        { 0.955f, 0.64f, 0.075f, input::B, 0, 0.85f, 0.25f, 0.20f },
        { 0.815f, 0.64f, 0.075f, input::X, 0, 0.20f, 0.45f, 0.90f },
        { 0.885f, 0.48f, 0.075f, input::Y, 0, 0.95f, 0.80f, 0.20f },
        // ombros e gatilhos
        { 0.07f, 0.34f, 0.065f, input::LeftShoulder, 0, 0.70f, 0.70f, 0.70f },
        { 0.93f, 0.30f, 0.065f, input::RightShoulder, 0, 0.70f, 0.70f, 0.70f },
        { 0.07f, 0.15f, 0.065f, 0, 1, 0.55f, 0.55f, 0.55f },
        { 0.93f, 0.12f, 0.065f, 0, 2, 0.55f, 0.55f, 0.55f },
        // direcional (troca de estilo): embaixo, entre o analógico e o meio da tela; no alto ele cobria a barra de vida
        // (que vai da esquerda até ~40% da largura)
        { 0.38f, 0.70f, 0.045f, input::DpadUp, 0, 0.90f, 0.90f, 0.90f },
        { 0.38f, 0.90f, 0.045f, input::DpadDown, 0, 0.90f, 0.90f, 0.90f },
        { 0.345f, 0.80f, 0.045f, input::DpadLeft, 0, 0.90f, 0.90f, 0.90f },
        { 0.415f, 0.80f, 0.045f, input::DpadRight, 0, 0.90f, 0.90f, 0.90f },
        // START e BACK
        { 0.56f, 0.07f, 0.04f, input::Start, 0, 0.95f, 0.95f, 0.95f },
        { 0.44f, 0.07f, 0.04f, input::Back, 0, 0.95f, 0.95f, 0.95f },
    };
    constexpr int ButtonCount = int(sizeof(Buttons) / sizeof(Buttons[0]));
    constexpr float StickX = 0.15f, StickY = 0.70f, StickRadius = 0.17f;

    // layout atual (editável no menu): os botões e, no fim, o analógico
    struct Place
    {
        float x, y, radius;
    };
    constexpr int StickIndex = ButtonCount;
    constexpr int PlaceCount = ButtonCount + 1;
    Place places[PlaceCount];

    void DefaultLayout()
    {
        for (int i = 0; i < ButtonCount; i++)
            places[i] = { Buttons[i].x, Buttons[i].y, Buttons[i].radius };
        places[StickIndex] = { StickX, StickY, StickRadius };
    }

    struct Finger
    {
        float x, y;        // posição em frações da tela
        bool stick = false;
    };

    // botão que esconde ou mostra os controles na tela (fica sempre visível, discreto, entre BACK e START)
    constexpr float ToggleX = 0.5f, ToggleY = 0.07f, ToggleRadius = 0.035f;

    std::mutex lock;
    std::map<SDL_FingerID, Finger> fingers;
    // botão do menu (engrenagem, embaixo no meio): modo de tela, controles na tela e edição do layout
    constexpr float MenuX = 0.5f, MenuY = 0.95f, MenuRadius = 0.03f;
#ifdef SDL_PLATFORM_ANDROID
    int displayMode = 0;   // no celular, preencher a tela é o padrão
#else
    int displayMode = 1;
#endif
    const char* const ModeNames[] = { "preencher", "16:9", "zoom", "widescreen" };
    constexpr int ModeCount = 4;

    bool userHidden = false;   // escondidos pelo botão (gravado em disco)
    // controle físico: false = botões pela posição do Xbox (o de baixo é o A), true = pela letra impressa (nos controles
    // da Nintendo o A fica à direita); gravado em controle.txt
    bool padLabels = false;
    bool autoHidden = false;   // escondidos porque um controle físico está conectado ou foi usado
    bool loaded = false;
    float aspect = 16.0f / 9;   // largura / altura da janela, para medir distâncias em unidades da altura

    // menu e edição do layout
    bool menuOpen = false;
    bool editing = false;
    int selected = -1;          // elemento escolhido na edição (o último tocado)
    SDL_FingerID dragFinger = 0;
    bool dragging = false;
    float dragDx = 0, dragDy = 0;   // do dedo ao centro do elemento

    // retângulo em frações da tela
    struct Rect
    {
        float x0, y0, x1, y1;
        bool Contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };

    // itens do menu, um por linha
    enum MenuItem { ItemMode, ItemControls, ItemPad, ItemEdit, ItemReset, ItemClose, ItemCount };
    constexpr float MenuWidth = 0.72f, RowHeight = 0.12f, RowGap = 0.02f;

    Rect MenuRow(int i)
    {
        float total = ItemCount * RowHeight + (ItemCount - 1) * RowGap;
        float y0 = 0.5f - total / 2 + i * (RowHeight + RowGap);
        return { 0.5f - MenuWidth / 2, y0, 0.5f + MenuWidth / 2, y0 + RowHeight };
    }

    // barra da edição, no meio da tela: diminuir, aumentar e pronto
    enum EditTool { ToolSmaller, ToolBigger, ToolDone, ToolCount };

    Rect EditToolRect(int i)
    {
        constexpr float Widths[ToolCount] = { 0.10f, 0.10f, 0.16f };
        constexpr float Gap = 0.015f, Y0 = 0.40f, Y1 = 0.52f;
        float total = Gap * (ToolCount - 1);
        for (float w : Widths)
            total += w;
        float x = 0.5f - total / 2;
        for (int k = 0; k < i; k++)
            x += Widths[k] + Gap;
        return { x, Y0, x + Widths[i], Y1 };
    }

    std::string MenuText(int i)
    {
        switch (i)
        {
        case ItemMode: return std::string("Tela: ") + ModeNames[displayMode];
        case ItemControls: return userHidden ? "Controles na tela: ocultos" : "Controles na tela: visíveis";
        case ItemPad: return padLabels ? "Botões do controle: letras" : "Botões do controle: posição Xbox";
        case ItemEdit: return "Editar layout dos botões";
        case ItemReset: return "Restaurar layout padrão";
        default: return "Fechar";
        }
    }

    // a escolha do botão fica num arquivo na pasta de preferências do app
    std::string SettingsPath(const char* name = "controles_tela.txt")
    {
        char* pref = SDL_GetPrefPath("dmc3port", "DevilMayCry3");
        std::string path = std::string(pref ? pref : "") + name;
        SDL_free(pref);
        return path;
    }

    void Load()
    {
        if (loaded)
            return;
        loaded = true;
        DefaultLayout();
        if (FILE* f = fopen(SettingsPath().c_str(), "r"))
        {
            char text[32] = {};
            fgets(text, sizeof(text), f);
            fclose(f);
            userHidden = strncmp(text, "ocultos", 7) == 0;
        }
        if (FILE* f = fopen(SettingsPath("controle.txt").c_str(), "r"))
        {
            char text[32] = {};
            fgets(text, sizeof(text), f);
            fclose(f);
            padLabels = strncmp(text, "letras", 6) == 0;
        }
        if (FILE* f = fopen(SettingsPath("tela.txt").c_str(), "r"))
        {
            char text[32] = {};
            fgets(text, sizeof(text), f);
            fclose(f);
            for (int i = 0; i < ModeCount; i++)
                if (strncmp(text, ModeNames[i], strlen(ModeNames[i])) == 0)
                    displayMode = i;
        }
        // layout.txt: uma linha por elemento (índice, x, y, raio); linhas que faltam ficam no padrão
        if (FILE* f = fopen(SettingsPath("layout.txt").c_str(), "r"))
        {
            int i;
            float x, y, radius;
            while (fscanf(f, "%d %f %f %f", &i, &x, &y, &radius) == 4)
                if (i >= 0 && i < PlaceCount && radius > 0.01f && radius < 0.5f)
                    places[i] = { std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f), radius };
            fclose(f);
        }
        // DMC3_TOUCH_SHOW=menu ou edit: abre o menu ou a edição do layout (para capturas de teste no PC)
        if (const char* show = getenv("DMC3_TOUCH_SHOW"))
        {
            menuOpen = strcmp(show, "menu") == 0;
            editing = strcmp(show, "edit") == 0;
            selected = editing ? 8 : -1;
        }
        // DMC3_DISPLAY_MODE=<nome>: força um modo (testes no PC, onde não há o botão)
        if (const char* forced = getenv("DMC3_DISPLAY_MODE"))
            for (int i = 0; i < ModeCount; i++)
                if (strcmp(forced, ModeNames[i]) == 0)
                    displayMode = i;
    }

    void Save()
    {
        if (FILE* f = fopen(SettingsPath().c_str(), "w"))
        {
            fputs(userHidden ? "ocultos\n" : "visiveis\n", f);
            fclose(f);
        }
    }

    void SaveMode()
    {
        if (FILE* f = fopen(SettingsPath("tela.txt").c_str(), "w"))
        {
            fputs(ModeNames[displayMode], f);
            fclose(f);
        }
    }

    void SaveLayout()
    {
        if (FILE* f = fopen(SettingsPath("layout.txt").c_str(), "w"))
        {
            for (int i = 0; i < PlaceCount; i++)
                fprintf(f, "%d %.4f %.4f %.4f\n", i, places[i].x, places[i].y, places[i].radius);
            fclose(f);
        }
    }

    bool InCircle(float cx, float cy, float radius, float x, float y, float slack)
    {
        float dx = (x - cx) * aspect, dy = y - cy;
        return dx * dx + dy * dy <= radius * radius * slack;
    }

    bool OnMenuButton(float x, float y)
    {
        return InCircle(MenuX, MenuY, MenuRadius, x, y, 2.5f);
    }

    bool OnToggle(float x, float y)
    {
        return InCircle(ToggleX, ToggleY, ToggleRadius, x, y, 2.0f);
    }

    bool Inside(int i, float x, float y)
    {
        return InCircle(places[i].x, places[i].y, places[i].radius, x, y, 1.3f);   // um pouco de folga em volta
    }

    bool InStickArea(float x, float y)
    {
        return InCircle(places[StickIndex].x, places[StickIndex].y, places[StickIndex].radius, x, y, 2.5f);
    }

    // deslocamento do analógico (-1..1) a partir de um dedo
    void StickOffset(const Finger& f, float& sx, float& sy)
    {
        const Place& s = places[StickIndex];
        sx = (f.x - s.x) * aspect / s.radius;
        sy = (f.y - s.y) / s.radius;
        float length = std::sqrt(sx * sx + sy * sy);
        if (length > 1)
        {
            sx /= length;
            sy /= length;
        }
    }

    bool Enabled()
    {
#ifdef SDL_PLATFORM_ANDROID
        return true;
#else
        static bool on = getenv("DMC3_TOUCH") != nullptr;
        return on;
#endif
    }

    // toque no menu aberto
    void MenuTap(float x, float y)
    {
        int item = -1;
        for (int i = 0; i < ItemCount; i++)
            if (MenuRow(i).Contains(x, y))
                item = i;
        switch (item)
        {
        case ItemMode:
            displayMode = (displayMode + 1) % ModeCount;
            SaveMode();
            fprintf(stderr, "[input] modo de tela: %s\n", ModeNames[displayMode]);
            break;
        case ItemControls:
            userHidden = !userHidden;
            autoHidden = false;
            Save();
            break;
        case ItemPad:
            padLabels = !padLabels;
            if (FILE* f = fopen(SettingsPath("controle.txt").c_str(), "w"))
            {
                fputs(padLabels ? "letras\n" : "posicao\n", f);
                fclose(f);
            }
            fprintf(stderr, "[input] botões do controle: %s\n", padLabels ? "pela letra" : "pela posição do Xbox");
            break;
        case ItemEdit:
            menuOpen = false;
            editing = true;
            selected = -1;
            break;
        case ItemReset:
            DefaultLayout();
            SaveLayout();
            fprintf(stderr, "[input] layout dos botões restaurado\n");
            break;
        case ItemClose:
            menuOpen = false;
            break;
        default:   // fora do painel fecha também
            if (!Rect{ 0.5f - MenuWidth / 2, MenuRow(0).y0, 0.5f + MenuWidth / 2, MenuRow(ItemCount - 1).y1 }.Contains(x, y))
                menuOpen = false;
            break;
        }
    }

    // toque na edição do layout: a barra do meio ou um elemento para arrastar
    void EditDown(SDL_FingerID id, float x, float y)
    {
        for (int i = 0; i < ToolCount; i++)
            if (EditToolRect(i).Contains(x, y))
            {
                if (i == ToolDone)
                {
                    editing = false;
                    dragging = false;
                    SaveLayout();
                    fprintf(stderr, "[input] layout dos botões gravado\n");
                }
                else if (selected >= 0)
                {
                    float limit = selected == StickIndex ? 0.30f : 0.15f;
                    float& r = places[selected].radius;
                    r = std::clamp(r * (i == ToolBigger ? 1.1f : 1 / 1.1f), 0.025f, limit);
                }
                return;
            }
        // o mais próximo dentro do alcance (os botões têm prioridade sobre a área grande do analógico)
        int best = -1;
        float bestDistance = 1e9f;
        for (int i = 0; i < PlaceCount; i++)
        {
            float dx = (x - places[i].x) * aspect, dy = y - places[i].y;
            float distance = std::sqrt(dx * dx + dy * dy) / places[i].radius;
            if (distance <= 1.15f && distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        if (best < 0)
            return;
        selected = best;
        dragging = true;
        dragFinger = id;
        dragDx = places[best].x - x;
        dragDy = places[best].y - y;
    }
}

namespace touch
{
    void HandleEvent(const SDL_Event& e)
    {
        if (!Enabled())
            return;
        std::lock_guard guard(lock);
        Load();
        if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            aspect = e.window.data2 ? float(e.window.data1) / float(e.window.data2) : aspect;
        if (editing)
        {
            if (e.type == SDL_EVENT_FINGER_DOWN)
                EditDown(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y);
            else if (e.type == SDL_EVENT_FINGER_MOTION && dragging && e.tfinger.fingerID == dragFinger && selected >= 0)
            {
                places[selected].x = std::clamp(e.tfinger.x + dragDx, 0.0f, 1.0f);
                places[selected].y = std::clamp(e.tfinger.y + dragDy, 0.0f, 1.0f);
            }
            else if ((e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) && e.tfinger.fingerID == dragFinger)
                dragging = false;
            return;
        }
        if (e.type == SDL_EVENT_FINGER_DOWN)
        {
            if (menuOpen)
            {
                MenuTap(e.tfinger.x, e.tfinger.y);
                return;
            }
            // os botões de esconder/mostrar e do menu não viram entrada do jogo
            if (OnToggle(e.tfinger.x, e.tfinger.y))
            {
                userHidden = !userHidden;
                autoHidden = false;
                fingers.clear();
                Save();
                fprintf(stderr, "[input] controles na tela %s\n", userHidden ? "ocultos" : "visíveis");
                return;
            }
            if (OnMenuButton(e.tfinger.x, e.tfinger.y))
            {
                menuOpen = true;
                fingers.clear();
                return;
            }
            if (userHidden)
                return;
            autoHidden = false;   // um toque traz de volta os controles escondidos pelo controle físico
            Finger f{ e.tfinger.x, e.tfinger.y };
            f.stick = InStickArea(f.x, f.y);
            // um botão em cima da área do analógico ganha dele
            for (int i = 0; i < ButtonCount && f.stick; i++)
                if (Inside(i, f.x, f.y))
                    f.stick = false;
            fingers[e.tfinger.fingerID] = f;
        }
        else if (e.type == SDL_EVENT_FINGER_MOTION)
        {
            auto it = fingers.find(e.tfinger.fingerID);
            if (it != fingers.end())
            {
                it->second.x = e.tfinger.x;
                it->second.y = e.tfinger.y;
            }
        }
        else if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED)
        {
            fingers.erase(e.tfinger.fingerID);
        }
    }

    input::Gamepad State()
    {
        input::Gamepad g;
        if (!Visible())
            return g;
        std::lock_guard guard(lock);
        if (menuOpen || editing)
            return g;
        for (auto& [id, f] : fingers)
        {
            if (f.stick)
            {
                float sx, sy;
                StickOffset(f, sx, sy);
                g.thumbLX = int16_t(sx * 32767);
                g.thumbLY = int16_t(-sy * 32767);   // XInput: y positivo = para cima
                continue;
            }
            // os botões seguem o dedo (dá para deslizar de um botão para outro)
            for (int i = 0; i < ButtonCount; i++)
                if (Inside(i, f.x, f.y))
                {
                    g.buttons |= Buttons[i].bit;
                    if (Buttons[i].trigger == 1)
                        g.leftTrigger = 255;
                    if (Buttons[i].trigger == 2)
                        g.rightTrigger = 255;
                }
        }
        // DMC3_LOG_TOUCH=1: registra cada mudança nos botões do toque, com a posição dos dedos
        static bool logTouch = getenv("DMC3_LOG_TOUCH") != nullptr;
        static uint16_t lastButtons = 0;
        if (logTouch && g.buttons != lastButtons)
        {
            fprintf(stderr, "[touch] botões %04X (antes %04X), %zu dedos:", g.buttons, lastButtons, fingers.size());
            for (auto& [id, f] : fingers)
                fprintf(stderr, " (%.3f, %.3f%s)", f.x, f.y, f.stick ? " analógico" : "");
            fprintf(stderr, "\n");
            lastButtons = g.buttons;
        }
        return g;
    }

    void PhysicalGamepadUsed()
    {
        std::lock_guard guard(lock);
        if (fingers.empty() && !menuOpen && !editing)
            autoHidden = true;
    }

    void GamepadConnected(bool connected)
    {
        std::lock_guard guard(lock);
        autoHidden = connected;
        fingers.clear();
    }

    bool PadByLabel()
    {
        std::lock_guard guard(lock);
        Load();
        return padLabels;
    }

    int DisplayMode()
    {
        std::lock_guard guard(lock);
        Load();
        return displayMode;
    }

    bool Visible()
    {
        if (!Enabled())
            return false;
        std::lock_guard guard(lock);
        Load();
        return !userHidden && !autoHidden;
    }

    void BuildOverlay(int windowWidth, int windowHeight, std::vector<float>& vertices)
    {
        vertices.clear();
        if (!Enabled())
            return;
        bool visible = Visible();
        std::lock_guard guard(lock);
        aspect = float(windowWidth) / float(windowHeight);
        float w = float(windowWidth), h = float(windowHeight);
        auto quad = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {   // em pixels
            const float points[6][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y0 }, { x1, y1 }, { x0, y1 } };
            for (auto& p : points)
                vertices.insert(vertices.end(), { p[0], p[1], r, g, b, a });
        };
        auto rect = [&](const Rect& q, float r, float g, float b, float a) {
            quad(q.x0 * w, q.y0 * h, q.x1 * w, q.y1 * h, r, g, b, a);
        };
        auto circle = [&](float cx, float cy, float radius, float r, float g, float b, float a) {
            constexpr int Segments = 28;
            float px = cx * w, py = cy * h, pr = radius * h;
            for (int i = 0; i < Segments; i++)
            {
                float a0 = 6.2831853f * i / Segments, a1 = 6.2831853f * (i + 1) / Segments;
                const float points[3][2] = { { px, py }, { px + std::cos(a0) * pr, py + std::sin(a0) * pr },
                    { px + std::cos(a1) * pr, py + std::sin(a1) * pr } };
                for (auto& p : points)
                    vertices.insert(vertices.end(), { p[0], p[1], r, g, b, a });
            }
        };
        // texto centrado em (cx, cy), com a fonte 8x8 ampliada (pixel = tamanho de um ponto da fonte, em pixels)
        auto text = [&](const std::string& s, float cx, float cy, float pixel, float a) {
            std::vector<uint32_t> codes;
            for (size_t i = 0; i < s.size();)
            {
                uint8_t c = uint8_t(s[i]);
                if (c < 0x80)
                    codes.push_back(c), i += 1;
                else if ((c & 0xE0) == 0xC0 && i + 1 < s.size())
                    codes.push_back(((c & 0x1F) << 6) | (uint8_t(s[i + 1]) & 0x3F)), i += 2;
                else
                    codes.push_back('?'), i += 1;
            }
            float x = cx * w - codes.size() * 8 * pixel / 2, y = cy * h - 4 * pixel;
            for (uint32_t code : codes)
            {
                int glyph = code >= 33 && code <= 126 ? int(code) - 33 : code >= 161 && code <= 255 ? 94 + int(code) - 161 : -1;
                if (glyph >= 0 && glyph < SDL_DEBUG_FONT_NUM_GLYPHS)
                    for (int row = 0; row < 8; row++)
                    {
                        uint8_t bits = SDL_RenderDebugTextFontData[glyph * 8 + row];
                        for (int col = 0; col < 8; col++)
                            if (bits >> col & 1)
                                quad(x + col * pixel, y + row * pixel, x + (col + 1) * pixel, y + (row + 1) * pixel,
                                    1.0f, 1.0f, 1.0f, a);
                    }
                x += 8 * pixel;
            }
        };
        float pixel = std::max(2.0f, std::floor(h * 0.0065f));

        auto drawControls = [&](bool highlight) {
            if (highlight && selected >= 0)
            {
                // anel em volta do escolhido (por baixo dele)
                const Place& p = places[selected];
                circle(p.x, p.y, p.radius * 1.15f, 1.0f, 0.85f, 0.2f, 0.7f);
            }
            for (int i = 0; i < ButtonCount; i++)
            {
                const Button& b = Buttons[i];
                // destaca o botão que algum dedo está apertando
                bool pressed = false;
                for (auto& [id, f] : fingers)
                    pressed |= !f.stick && Inside(i, f.x, f.y);
                circle(places[i].x, places[i].y, places[i].radius, b.r, b.g, b.b, pressed ? 0.75f : 0.35f);
            }
            // analógico: base e manche
            const Place& s = places[StickIndex];
            circle(s.x, s.y, s.radius, 0.6f, 0.6f, 0.6f, 0.22f);
            float kx = s.x, ky = s.y;
            for (auto& [id, f] : fingers)
                if (f.stick)
                {
                    float sx, sy;
                    StickOffset(f, sx, sy);
                    kx = s.x + sx * s.radius / aspect;
                    ky = s.y + sy * s.radius;
                }
            circle(kx, ky, s.radius * 0.45f, 0.85f, 0.85f, 0.85f, 0.5f);
        };

        if (editing)
        {
            rect({ 0, 0, 1, 1 }, 0.0f, 0.0f, 0.0f, 0.35f);
            drawControls(true);
            text("Arraste para mover; toque e use - / + para o tamanho", 0.5f, 0.33f, std::max(2.0f, std::floor(pixel * 0.6f)), 0.95f);
            const char* labels[ToolCount] = { "-", "+", "Pronto" };
            for (int i = 0; i < ToolCount; i++)
            {
                Rect r = EditToolRect(i);
                bool enabled = i == ToolDone || selected >= 0;
                rect(r, 0.15f, 0.15f, 0.18f, 0.85f);
                text(labels[i], (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2, pixel, enabled ? 1.0f : 0.35f);
            }
            return;
        }

        // botão de esconder/mostrar: cheio com os controles à vista, só um anel quando escondidos
        circle(ToggleX, ToggleY, ToggleRadius, 0.9f, 0.9f, 0.9f, visible ? 0.45f : 0.30f);
        circle(ToggleX, ToggleY, ToggleRadius * 0.6f, 0.0f, 0.0f, 0.0f, visible ? 0.0f : 0.55f);
        // botão do menu: engrenagem (anel com dentes)
        for (int i = 0; i < 8; i++)
        {
            float angle = 6.2831853f * i / 8;
            circle(MenuX + std::cos(angle) * MenuRadius / aspect, MenuY + std::sin(angle) * MenuRadius, MenuRadius * 0.3f,
                0.9f, 0.9f, 0.9f, 0.35f);
        }
        circle(MenuX, MenuY, MenuRadius * 0.85f, 0.9f, 0.9f, 0.9f, 0.35f);
        circle(MenuX, MenuY, MenuRadius * 0.4f, 0.0f, 0.0f, 0.0f, 0.55f);

        if (visible && !menuOpen)
            drawControls(false);

        if (menuOpen)
        {
            rect({ 0, 0, 1, 1 }, 0.0f, 0.0f, 0.0f, 0.55f);
            for (int i = 0; i < ItemCount; i++)
            {
                Rect r = MenuRow(i);
                rect(r, 0.12f, 0.12f, 0.15f, 0.9f);
                text(MenuText(i), 0.5f, (r.y0 + r.y1) / 2, pixel, 1.0f);
            }
        }
    }
}
