// Vídeos (XMVVideo@DMC3): o player XMV do jogo não é criado. Abrir um vídeo inicia o player nativo
// (video_player.cpp, FFmpeg), que mostra os quadros por cima do jogo e toca o som; o estado (vf10) fica "tocando"
// até ele acabar ou o jogo parar o vídeo (vf9). Com DMC3_SKIP_VIDEOS, ou nos testes com DMC3_INPUT_SCRIPT, o vídeo
// é pulado: o objeto fica marcado como terminado e o jogo segue como se ele tivesse acabado.
// Campos do objeto: +0x224 flags (bit 2 = laço), +0x240 quadro pronto, +0x258 pausado, +0x259 parado,
// +0x25A terminado, +0x25C, +0x264 player XMV, +0x268 superfície, +0x26C textura.
#include "input.h"
#include "kernel.h"
#include "video_player.h"

#include <cstdlib>
#include <string>

namespace
{
    constexpr uint32_t Player = 0x264;
    constexpr uint32_t StatusEnded = 4, StatusPlaying = 3;   // vf10: 4 terminado, 3 tocando, 2 pausado, 0 parado, 5 sem player
    uint32_t playingObject = 0;   // objeto do vídeo que o player nativo está tocando

    bool SkipVideos()
    {
        // DMC3_PLAY_VIDEOS toca mesmo com roteiro de botões (para testar os vídeos)
        static bool skip = getenv("DMC3_SKIP_VIDEOS") || (getenv("DMC3_INPUT_SCRIPT") && !getenv("DMC3_PLAY_VIDEOS"));
        return skip;
    }

    void Byte(uint32_t address, uint8_t value) { *kernel::Guest<uint8_t>(address) = value; }

    std::string GuestString(uint32_t address)
    {
        std::string out;
        for (uint32_t i = 0; address && i < 260; i++)
        {
            char c = *kernel::Guest<char>(address + i);
            if (!c)
                break;
            out += c;
        }
        return out;
    }
}

// vf3: bool Open(const char* path, uint32 flags) — devolve o tamanho do quadro (0 = falhou)
GUEST_OVERRIDE(8244FAF8)
{
    uint32_t self = ctx.r3.u32, path = ctx.r4.u32;
    if (!path || !*kernel::Guest<char>(path))
    {
        ctx.r3.u64 = 0;
        return;
    }
    std::string guestPath = GuestString(path);
    {
        // evento para os scripts de teste: o nome do arquivo sem pasta nem extensão (ex.: "m01_s00")
        std::string name = GuestString(path);
        name = name.substr(name.find_last_of('\\') + 1);
        input::Mark(name.substr(0, name.find('.')).c_str());
    }
    sub_82120BA0(ctx, base);   // FullMotionVideo::vf3 (estado comum da classe base)
    kernel::Store32(self + 0x25C, 0);
    kernel::Store32(self + Player, 0);
    kernel::Store32(self + 0x268, 0);
    kernel::Store32(self + 0x26C, 0);
    Byte(self + 0x240, 1);   // "primeiro quadro pronto": as cenas de evento esperam por ele (sub_8242BC60)
    Byte(self + 0x258, 0);
    Byte(self + 0x259, 0);
    Byte(self + 0x25A, 1);   // terminado: vf4 (desenho do player XMV) não faz nada; o player nativo desenha
    bool loop = (kernel::Load32(self + 0x224) >> 2) & 1;
    if (!SkipVideos() && videoplayer::Open(kernel::HostPathOf(guestPath), loop))
    {
        playingObject = self;
        fprintf(stderr, "[video] abrindo %s%s\n", guestPath.c_str(), loop ? " (laço)" : "");
    }
    else
    {
        fprintf(stderr, "[video] pulando %s\n", guestPath.c_str());
    }
    ctx.r3.u64 = 1;
}

// vf9 (Stop) e vf11 usam o player sem checar
GUEST_DECLARE_ORIGINAL(8244FE50);
GUEST_OVERRIDE(8244FE50)
{
    if (kernel::Load32(ctx.r3.u32 + Player))
        return GUEST_ORIGINAL(8244FE50)(ctx, base);
    if (ctx.r3.u32 == playingObject)
    {
        fprintf(stderr, "[video] o jogo parou o vídeo (pulou)\n");
        videoplayer::Stop();
        playingObject = 0;
    }
    Byte(ctx.r3.u32 + 0x259, 1);
    Byte(ctx.r3.u32 + 0x258, 0);
}

GUEST_DECLARE_ORIGINAL(8244FD30);
GUEST_OVERRIDE(8244FD30)
{
    if (kernel::Load32(ctx.r3.u32 + Player))   // Seek(this, double posição em f1)
        GUEST_ORIGINAL(8244FD30)(ctx, base);
}

// vf10: estado do vídeo; sem player, terminado
GUEST_DECLARE_ORIGINAL(8244FE70);
GUEST_OVERRIDE(8244FE70)
{
    uint32_t lr = uint32_t(ctx.lr), self = ctx.r3.u32;
    if (kernel::Load32(ctx.r3.u32 + Player))
        GUEST_ORIGINAL(8244FE70)(ctx, base);
    else if (self == playingObject && videoplayer::Playing())
        ctx.r3.u64 = StatusPlaying;
    else
    {
        if (self == playingObject)
        {
            videoplayer::Stop();
            playingObject = 0;
        }
        ctx.r3.u64 = StatusEnded;
    }
    static int count = 0;
    if (getenv("DMC3_LOG_VIDEO") && count++ < 50)
        fprintf(stderr, "[video] vf10 estado(%08X) de %08X -> %u\n", self, lr, ctx.r3.u32);
}

// DMC3_LOG_VIDEO=1: registra as chamadas aos métodos do vídeo (quem chama e o que recebe)
namespace
{
    bool LogVideo()
    {
        static bool on = getenv("DMC3_LOG_VIDEO") != nullptr;
        return on;
    }

    void Log(const char* method, PPCContext& ctx, uint32_t result)
    {
        fprintf(stderr, "[video] %s(%08X) de %08X -> %X; pilha:", method, ctx.r3.u32, uint32_t(ctx.lr), result);
        uint32_t sp = ctx.r1.u32;
        for (int i = 0; i < 8 && sp >= 0x40000000 && sp < 0x80000000; i++)
        {
            uint32_t previous = kernel::Load32(sp);
            if (previous <= sp || previous >= 0x80000000)
                break;
            fprintf(stderr, " %08X", kernel::Load32(previous - 8));
            sp = previous;
        }
        fprintf(stderr, "\n");
    }
}

#define LOGGED_METHOD(address, name)                                     \
    GUEST_DECLARE_ORIGINAL(address);                                     \
    GUEST_OVERRIDE(address)                                              \
    {                                                                    \
        uint32_t self = ctx.r3.u32;                                      \
        uint64_t lr = ctx.lr;                                            \
        GUEST_ORIGINAL(address)(ctx, base);                              \
        static int count = 0;                                            \
        if (LogVideo() && count++ < 30)                                  \
        {                                                                \
            PPCContext c = ctx;                                          \
            c.r3.u64 = self;                                             \
            c.lr = lr;                                                   \
            Log(name, c, ctx.r3.u32);                                    \
        }                                                                \
    }

LOGGED_METHOD(8244FD90, "vf7")
LOGGED_METHOD(8244FDF0, "vf8")
LOGGED_METHOD(82120C38, "vf14")
LOGGED_METHOD(82120B28, "vf12")
LOGGED_METHOD(82120B70, "vf2")
LOGGED_METHOD(824493D0, "manager.vf1 (criar)")
LOGGED_METHOD(82120F28, "manager.vf10 (ativos)")
LOGGED_METHOD(82120CB8, "manager.vf4")
LOGGED_METHOD(82120D30, "manager.vf5")
LOGGED_METHOD(82120DA8, "manager.vf6")
LOGGED_METHOD(82121A60, "manager.vf7")
LOGGED_METHOD(82120E20, "manager.vf8")
