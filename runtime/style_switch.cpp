// Troca de estilo em tempo real do Dante (o mod "Style Switch" do PS2, agora nativo):
// direcional durante a missão: ↑ Trickster, → Swordmaster, ← Gunslinger, ↓ Royal Guard.
//
// A troca repete o que o jogo faz na Divinity Statue, com as funções e campos do próprio jogo
// (equivalências com o PS2 em RELATORIO_REA_DMC3.md, seção 19):
//   - Dante (CPlDante): +0x40DC estilo, +0x40FC nível, +0x4108 EXP (float), +0x2E28 golpes ativos (8 palavras),
//     +0x2E48 golpes comprados (8 palavras); vtable +0x24 (vf9) libera os golpes do nível;
//   - CSceneGame: +0xD8 dados do jogador (+0x58 estilo, +0x5C níveis[6], +0x74 EXP[6]), +0x184 registro da
//     missão (+0x38 estilo, +0x3C nível, +0x60 EXP);
//   - sub_822D2728(personagem, registro): carrega os pacotes de animação do estilo do registro e descarrega os outros;
//   - tabela de animações do Dante em +0x27EC (o jogo só a preenche ao criar o personagem);
//   - ícone do estilo na HUD, refeito como na criação dela (sub_82382148).
//
// A troca tem duas fases: o direcional manda carregar os pacotes do estilo novo (sem tirar os do atual) e, quando
// eles estão no pool e nenhuma ação de estilo está em andamento, o estilo muda de fato. Assim um golpe de estilo
// nunca encontra a tabela de animações vazia (no 360 isso derruba o jogo).
#include "style_switch.h"
#include "gpu.h"
#include "input.h"
#include "renderer.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    // estilos internos (ordem do jogo)
    constexpr uint32_t Swordmaster = 0, Gunslinger = 1, Trickster = 2, RoyalGuard = 3, StyleCount = 6;
    std::atomic<uint32_t> changedStyle{ StyleCount };   // última troca, para o aviso na tela (StyleCount = nenhuma)
    std::atomic<uint64_t> changedNs{ 0 };

    constexpr uint32_t DanteStyle = 0x40DC, DanteLevel = 0x40FC, DanteExp = 0x4108;
    constexpr uint32_t DanteMoves = 0x2E28, DanteMovesBought = 0x2E48;
    constexpr uint32_t ScenePlayerData = 0xD8, SceneRecord = 0x184;
    constexpr uint32_t DataStyle = 0x58, DataLevels = 0x5C, DataExp = 0x74;
    constexpr uint32_t RecordStyle = 0x38, RecordLevel = 0x3C, RecordExp = 0x60;
    constexpr uint32_t LoadStylePacks = 0x822D2728;
    // tabela de animações do Dante: arquivo p do pool de movimentos (categoria 1, índice id*34 + p) em +0x27EC + 4p;
    // os pacotes de estilo são os arquivos 13 a 26. O jogo só a preenche ao criar o personagem.
    constexpr uint32_t DanteMotionFiles = 0x27EC, DanteId = 0x68;
    constexpr uint32_t MotionPool = 0x827BC5E8, FindMotionFile = 0x822AAFC0;
    constexpr uint32_t FirstStylePack = 13, LastStylePack = 26;
    constexpr uint32_t LoadMotionFile = 0x822ABD48;   // (pool, 0, arquivo, 0)
    constexpr uint32_t DanteStyleAction = 0x40E4;     // ação de estilo em andamento (0 = livre)
    // HUD (criada por sub_82382148): +0x44 pacote de modelos, +0x558A ícone do estilo, +0x3100 modelo do ícone,
    // +0x5500 personagem que não é o Dante; tabela estilo -> ícone em 0x82010D30; sub_821654F0(pacote, n) = arquivo n
    constexpr uint32_t HudPack = 0x44, HudIcon = 0x558A, HudIconModel = 0x3100, HudIconExtra = 0x3D0, HudOther = 0x5500;
    constexpr uint32_t StyleIconTable = 0x82010D30, HudFile = 0x821654F0, HudRefresh = 0x8217ACC8;
    constexpr uint32_t ReleaseHandle = 0x824369D8;   // libera um handle de memória do pool (FUN_0013D660 no PS2)
    constexpr uint32_t PendingTimeout = 300;          // quadros esperando o carregamento antes de desistir

    std::atomic<uint32_t> dante{ 0 }, scene{ 0 }, hud{ 0 };
    std::atomic<uint64_t> danteFrame{ 0 };            // último quadro em que o Dante foi atualizado
    std::atomic<int32_t> request{ -1 };               // estilo pedido pelo direcional
    uint32_t refreshFrames = 0;                       // quadros em que a tabela de animações ainda é atualizada
    int32_t pending = -1;                             // estilo cujos pacotes estão carregando
    uint32_t shotDelay = 0;                           // DMC3_STYLE_SHOTS: captura de tela um pouco depois da troca
    uint32_t shotCount = 0;
    uint32_t pendingFrames = 0;
    uint16_t previousDpad = 0;

    bool Log() { static bool on = getenv("DMC3_LOG_STYLE") != nullptr; return on; }

    bool InMission()
    {
        uint64_t frame = gpu::Stats().frame;
        return dante.load() && scene.load() && frame <= danteFrame.load() + 2;
    }

    // pacotes de animação de um estilo, pela mesma regra de sub_822D2728: Swordmaster usa as armas brancas do
    // registro (+0, +1), Gunslinger as de fogo (+2..+4), os outros um pacote fixo
    std::vector<uint32_t> StylePacks(uint32_t record, uint32_t style)
    {
        std::vector<uint32_t> packs;
        if (style == Swordmaster || style == Gunslinger)
        {
            for (uint32_t i = style == Swordmaster ? 0 : 2; i < (style == Swordmaster ? 2u : 5u); i++)
            {
                int8_t w = int8_t(*kernel::Guest<uint8_t>(record + i));
                packs.push_back(w < 0 ? (style == Swordmaster ? 14 : 19) : uint32_t(w) + 13);
            }
        }
        else
        {
            packs.push_back(21 + style);
        }
        return packs;
    }

    uint32_t MotionFile(PPCContext& ctx, uint32_t self, uint32_t pack)
    {
        return kernel::CallGuest(ctx, FindMotionFile, { MotionPool, 1, kernel::Load32(self + DanteId) * 34 + pack });
    }

    void RefreshMotionFiles(PPCContext& ctx, uint32_t self)
    {
        // pacote carregado -> ponteiro; ainda carregando ou descarregado -> 0
        for (uint32_t p = FirstStylePack; p <= LastStylePack; p++)
            kernel::Store32(self + DanteMotionFiles + 4 * p, MotionFile(ctx, self, p));
    }

    void Switch(PPCContext& ctx, uint32_t self, uint32_t style)
    {
        uint32_t current = kernel::Load32(self + DanteStyle);
        uint32_t s = scene.load();
        if (style == current || current >= StyleCount || !s)
            return;
        uint32_t data = s + ScenePlayerData, record = s + SceneRecord;

        // nível e EXP: o estilo antigo guarda os seus, o novo recupera os dele
        kernel::Store32(data + DataLevels + 4 * current, kernel::Load32(self + DanteLevel));
        kernel::Store32(data + DataExp + 4 * current, kernel::Load32(self + DanteExp));
        uint32_t level = kernel::Load32(data + DataLevels + 4 * style), exp = kernel::Load32(data + DataExp + 4 * style);
        kernel::Store32(self + DanteStyle, style);
        kernel::Store32(self + DanteLevel, level);
        kernel::Store32(self + DanteExp, exp);
        kernel::Store32(data + DataStyle, style);
        kernel::Store32(record + RecordStyle, style);
        kernel::Store32(record + RecordLevel, level);
        kernel::Store32(record + RecordExp, exp);

        // golpes ativos voltam a ser os comprados, e o jogo libera os do nível do estilo novo
        for (uint32_t i = 0; i < 8; i++)
            kernel::Store32(self + DanteMoves + 4 * i, kernel::Load32(self + DanteMovesBought + 4 * i));
        uint32_t unlock = kernel::Load32(kernel::Load32(self) + 0x24);
        kernel::CallGuest(ctx, unlock, { self });

        // pacotes: ficam só os do estilo novo (já carregados); a tabela do Dante acompanha o pool por alguns segundos
        kernel::CallGuest(ctx, LoadStylePacks, { 0, record });
        RefreshMotionFiles(ctx, self);
        refreshFrames = 120;

        // ícone do estilo na HUD, como na criação dela
        if (uint32_t h = hud.load(); h && kernel::Load32(h + HudOther) == 0)
        {
            uint8_t icon = *kernel::Guest<uint8_t>(StyleIconTable + style);
            *kernel::Guest<uint8_t>(h + HudIcon) = icon;
            uint32_t pack = kernel::Load32(h + HudPack);
            uint32_t a = kernel::CallGuest(ctx, HudFile, { pack, (icon + 9u) * 2 });
            uint32_t b = kernel::CallGuest(ctx, HudFile, { pack, icon * 2u + 0x13 });
            uint32_t model = h + HudIconModel, vtable = kernel::Load32(model);
            // o modelo já tem memória reservada (handle em +0x1C): libera antes de ligar os arquivos novos
            if (a && b)
                kernel::CallGuest(ctx, ReleaseHandle, { model + 0x1C });
            bool bound = a && b && (kernel::CallGuest(ctx, kernel::Load32(vtable + 0x28), { model, b, a }) & 0xFF);
            if (bound)
            {
                kernel::CallGuest(ctx, kernel::Load32(vtable + 0x30), { model, 1 });
                kernel::CallGuest(ctx, HudRefresh, { h + HudIconExtra });
            }
            if (Log())
                fprintf(stderr, "[style] HUD %08X: ícone %u (arquivos %08X %08X) %s\n", h, icon, a, b, bound ? "trocado" : "falhou");
        }
        else if (Log())
        {
            fprintf(stderr, "[style] HUD não encontrada (%08X)\n", hud.load());
        }

        changedStyle = style;
        changedNs = SDL_GetTicksNS();
        if (Log())
            fprintf(stderr, "[style] Dante %08X: estilo %u -> %u (nível %u)\n", self, current, style, level);
        // DMC3_STYLE_SHOTS=all (todas as trocas) ou =<estilo> (só as trocas para esse estilo)
        if (const char* shots = getenv("DMC3_STYLE_SHOTS"); shots && (strcmp(shots, "all") == 0 || uint32_t(atoi(shots)) == style))
            shotDelay = 40;
    }
}

namespace style
{
    bool DanteActive()
    {
        return InMission();
    }

    bool LastChange(uint32_t& style, uint64_t& whenNs)
    {
        style = changedStyle.load();
        whenNs = changedNs.load();
        return style < StyleCount;
    }

    uint16_t FilterButtons(uint16_t buttons)
    {
        uint16_t dpad = buttons & (input::DpadUp | input::DpadDown | input::DpadLeft | input::DpadRight);
        uint16_t pressed = dpad & ~previousDpad;
        previousDpad = dpad;
        if (!InMission())
            return buttons;
        if (pressed && Log())
            fprintf(stderr, "[style] direcional %X (botões %04X) no quadro %llu\n", pressed, buttons,
                (unsigned long long)gpu::Stats().frame);
        if (pressed & input::DpadUp)
            request = int32_t(Trickster);
        else if (pressed & input::DpadRight)
            request = int32_t(Swordmaster);
        else if (pressed & input::DpadLeft)
            request = int32_t(Gunslinger);
        else if (pressed & input::DpadDown)
            request = int32_t(RoyalGuard);
        // na missão o direcional é só da troca de estilo (os atalhos originais ficam desligados)
        return buttons & ~dpad;
    }
}

// criação da HUD da missão: guarda o objeto para trocar o ícone do estilo
GUEST_DECLARE_ORIGINAL(82382148);
GUEST_OVERRIDE(82382148)
{
    hud = ctx.r3.u32;
    GUEST_ORIGINAL(82382148)(ctx, base);
}

// CSceneGame::vf3 (atualização da cena de jogo): guarda a cena para achar os dados do jogador
GUEST_DECLARE_ORIGINAL(8233A2B8);
GUEST_OVERRIDE(8233A2B8)
{
    scene = ctx.r3.u32;
    GUEST_ORIGINAL(8233A2B8)(ctx, base);
}

// CPlDante::vf21 (lógica do Dante, chamada a cada quadro na missão): aplica a troca pedida antes dela
GUEST_DECLARE_ORIGINAL(822F7A18);
GUEST_OVERRIDE(822F7A18)
{
    uint32_t self = ctx.r3.u32;
    if (!InMission())
        input::Mark("missao");   // evento para os scripts de teste: o Dante começou a agir
    dante = self;
    danteFrame = gpu::Stats().frame;
    // fase 1: o pedido manda carregar os pacotes do estilo novo
    int32_t want = request.exchange(-1);
    uint32_t record = scene.load() + SceneRecord;
    if (want >= 0 && uint32_t(want) != kernel::Load32(self + DanteStyle) && scene.load())
    {
        pending = want;
        pendingFrames = 0;
        for (uint32_t pack : StylePacks(record, uint32_t(want)))
            if (!MotionFile(ctx, self, pack))
                kernel::CallGuest(ctx, LoadMotionFile, { MotionPool, 0, pack, 0 });
    }
    // fase 2: troca quando os pacotes estão prontos e o Dante não está no meio de uma ação de estilo
    if (pending >= 0)
    {
        bool ready = kernel::Load32(self + DanteStyleAction) == 0;
        for (uint32_t pack : StylePacks(record, uint32_t(pending)))
            ready = ready && MotionFile(ctx, self, pack) != 0;
        if (ready)
        {
            Switch(ctx, self, uint32_t(pending));
            pending = -1;
        }
        else if (++pendingFrames > PendingTimeout)
        {
            if (Log())
                fprintf(stderr, "[style] desistindo do estilo %d: pacotes não carregaram\n", pending);
            pending = -1;
        }
    }
    if (refreshFrames)
    {
        refreshFrames--;
        RefreshMotionFiles(ctx, self);
    }
    if (shotDelay && --shotDelay == 0)
    {
        char name[64];
        snprintf(name, sizeof(name), "estilo_%02u_%u", shotCount++, kernel::Load32(self + DanteStyle));
        renderer::RequestScreenshot(name);
    }
    static uint64_t lastLog = 0;
    if (Log() && danteFrame >= lastLog + 60)
    {
        lastLog = danteFrame;
        fprintf(stderr, "[style] quadro %llu: Dante %08X cena %08X campos 40DC..4108:", (unsigned long long)danteFrame.load(),
            self, scene.load());
        for (uint32_t o = 0x40DC; o <= 0x4108; o += 4)
            fprintf(stderr, " %X", kernel::Load32(self + o));
        fprintf(stderr, "; pacotes 13-26:");
        for (uint32_t p = 13; p <= 26; p++)
            fprintf(stderr, " %X", kernel::Load32(self + DanteMotionFiles + 4 * p));
        fprintf(stderr, "; id %X", kernel::Load32(self + 0x68));
        if (uint32_t s = scene.load())
        {
            // dados do jogador: níveis e EXP dos 6 estilos (o que deve ir para o save)
            fprintf(stderr, "; dados: estilo %u, níveis", kernel::Load32(s + ScenePlayerData + DataStyle));
            for (uint32_t i = 0; i < StyleCount; i++)
                fprintf(stderr, " %u", kernel::Load32(s + ScenePlayerData + DataLevels + 4 * i));
            fprintf(stderr, ", EXP");
            for (uint32_t i = 0; i < StyleCount; i++)
            {
                uint32_t bits = kernel::Load32(s + ScenePlayerData + DataExp + 4 * i);
                float f;
                memcpy(&f, &bits, 4);
                fprintf(stderr, " %.1f", f);
            }
        }
        fprintf(stderr, "\n");
    }
    GUEST_ORIGINAL(822F7A18)(ctx, base);
}

// sub_822E69D8(Dante, pacote, movimento, ...) toca um movimento do Dante. Se o pacote de estilo não estiver na
// tabela (descarregado durante uma troca), o movimento não é tocado: no PS2 o golpe só saía sem animação, mas no
// 360 o interpretador de movimentos lê o ponteiro nulo e derruba o jogo.
GUEST_DECLARE_ORIGINAL(822E69D8);
GUEST_OVERRIDE(822E69D8)
{
    uint32_t self = ctx.r3.u32, pack = ctx.r4.u32;
    if (self == dante.load() && pack >= FirstStylePack && pack <= LastStylePack &&
        kernel::Load32(self + DanteMotionFiles + 4 * pack) == 0)
    {
        if (Log())
            fprintf(stderr, "[style] movimento %u do pacote %u ignorado: pacote não carregado (de %08X)\n", ctx.r5.u32, pack,
                uint32_t(ctx.lr));
        return;
    }
    GUEST_ORIGINAL(822E69D8)(ctx, base);
}
