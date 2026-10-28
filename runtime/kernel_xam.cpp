// XAM: notificações do sistema e estado de login. O jogador 1 já aparece logado (perfil local), e as
// telas do sistema (login) "abrem e fecham" na hora, com as notificações que o console mandaria. As caixas de
// mensagem do sistema aparecem como diálogos nativos.
#include "kernel.h"

#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

namespace
{
    // IDs de notificação do XAM
    constexpr uint32_t XN_SYS_UI = 0x00000009;
    constexpr uint32_t XN_SYS_SIGNINCHANGED = 0x0000000A;
    constexpr uint32_t XN_SYS_STORAGEDEVICESCHANGED = 0x0000000B;
    constexpr uint32_t XN_SYS_INPUTDEVICESCHANGED = 0x00000012;

    std::mutex lock;
    std::map<uint32_t, std::deque<std::pair<uint32_t, uint32_t>>> listeners;   // handle -> (id, parâmetro)
    uint32_t nextListener = 0xF9000004;

    void Broadcast(uint32_t id, uint32_t param)
    {
        std::lock_guard guard(lock);
        for (auto& [handle, queue] : listeners)
            queue.emplace_back(id, param);
        if (getenv("DMC3_LOG_INPUT"))
            fprintf(stderr, "[xam] notificação %X(%X) para %zu ouvintes\n", id, param, listeners.size());
    }
}

// HANDLE XamNotifyCreateListener(uint64 Areas, uint32 Unknown)
KERNEL_IMPL(XamNotifyCreateListener)
{
    std::lock_guard guard(lock);
    uint32_t handle = nextListener;
    nextListener += 4;
    // o console avisa logo de entrada os dispositivos e o login atuais
    auto& queue = listeners[handle];
    queue.emplace_back(XN_SYS_SIGNINCHANGED, 1);
    queue.emplace_back(XN_SYS_INPUTDEVICESCHANGED, 0);
    queue.emplace_back(XN_SYS_STORAGEDEVICESCHANGED, 0);
    ctx.r3.u64 = handle;
}

// BOOL XNotifyGetNext(HANDLE Listener, uint32 MsgFilter, uint32* Id, uint32* Param)
KERNEL_IMPL(XNotifyGetNext)
{
    uint32_t handle = ctx.r3.u32, filter = ctx.r4.u32, pId = ctx.r5.u32, pParam = ctx.r6.u32;
    std::lock_guard guard(lock);
    auto it = listeners.find(handle);
    if (it != listeners.end())
    {
        auto& queue = it->second;
        for (auto q = queue.begin(); q != queue.end(); ++q)
        {
            if (filter && q->first != filter)
                continue;
            if (pId)
                kernel::Store32(pId, q->first);
            if (pParam)
                kernel::Store32(pParam, q->second);
            queue.erase(q);
            ctx.r3.u64 = 1;
            return;
        }
    }
    ctx.r3.u64 = 0;
}

// uint32 XamUserGetSigninState(uint32 UserIndex): 1 = logado localmente
KERNEL_IMPL(XamUserGetSigninState)
{
    ctx.r3.u64 = ctx.r3.u32 == 0 ? 1 : 0;
}

// conclui uma operação assíncrona do XAM: XOVERLAPPED { u32 InternalLow (resultado), InternalHigh, InternalContext,
// hEvent, pCompletionRoutine, dwCompletionContext, dwExtendedError }
void kernel::CompleteOverlapped(uint32_t overlapped, uint32_t result)
{
    if (!overlapped)
        return;
    kernel::Store32(overlapped + 0, result);
    kernel::Store32(overlapped + 4, 0);
    kernel::Store32(overlapped + 24, result);
    if (uint32_t event = kernel::Load32(overlapped + 12))
        kernel::SignalEventHandle(event);
}

// uint32 XamShowDeviceSelectorUI(uint32 UserIndex, uint32 ContentType, uint32 ContentFlags, uint64 TotalRequested,
//                                uint32* DeviceID, XOVERLAPPED*): um console com um disco rígido só, escolhido na hora
KERNEL_IMPL(XamShowDeviceSelectorUI)
{
    constexpr uint32_t HardDrive = 1;
    uint32_t pDevice = ctx.r7.u32, overlapped = ctx.r8.u32;
    Broadcast(XN_SYS_UI, 1);
    if (pDevice)
        kernel::Store32(pDevice, HardDrive);
    Broadcast(XN_SYS_UI, 0);
    if (overlapped)
    {
        kernel::CompleteOverlapped(overlapped, 0);
        ctx.r3.u64 = 997;   // ERROR_IO_PENDING: o resultado vai pelo XOVERLAPPED
    }
    else
    {
        ctx.r3.u64 = 0;
    }
}

namespace
{
    // texto UTF-16 big-endian do console -> UTF-8
    std::string Utf16Guest(uint32_t address)
    {
        std::string out;
        for (uint32_t i = 0; address && i < 1024; i++)
        {
            uint32_t c = __builtin_bswap16(*kernel::Guest<uint16_t>(address + 2 * i));
            if (!c)
                break;
            if (c >= 0xD800 && c < 0xDC00)   // par substituto
            {
                uint32_t low = __builtin_bswap16(*kernel::Guest<uint16_t>(address + 2 * (i + 1)));
                if (low >= 0xDC00 && low < 0xE000)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    i++;
                }
            }
            if (c < 0x80)
                out += char(c);
            else if (c < 0x800)
                out += { char(0xC0 | (c >> 6)), char(0x80 | (c & 0x3F)) };
            else if (c < 0x10000)
                out += { char(0xE0 | (c >> 12)), char(0x80 | ((c >> 6) & 0x3F)), char(0x80 | (c & 0x3F)) };
            else
                out += { char(0xF0 | (c >> 18)), char(0x80 | ((c >> 12) & 0x3F)), char(0x80 | ((c >> 6) & 0x3F)),
                         char(0x80 | (c & 0x3F)) };
        }
        return out;
    }

    // Caixa de mensagem do sistema. No console o jogador escolhe; aqui ela aparece como diálogo nativo (SDL, que
    // também existe no Android). Em testes automáticos (DMC3_INPUT_SCRIPT) ou com DMC3_MSGBOX_BUTTON, a resposta é
    // automática: o botão indicado ou o padrão do jogo.
    uint32_t AskPlayer(const std::string& title, const std::string& text, const std::vector<std::string>& labels, uint32_t active)
    {
        if (const char* forced = getenv("DMC3_MSGBOX_BUTTON"))
            return uint32_t(atoi(forced));
        if (getenv("DMC3_INPUT_SCRIPT") || labels.empty())
            return active;
        std::vector<SDL_MessageBoxButtonData> buttons;
        for (size_t i = 0; i < labels.size(); i++)
        {
            SDL_MessageBoxButtonData b{};
            b.buttonID = int(i);
            b.text = labels[i].c_str();
            if (i == active)
                b.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
            buttons.push_back(b);
        }
        SDL_MessageBoxData data{};
        data.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
        data.title = title.c_str();
        data.message = text.c_str();
        data.numbuttons = int(buttons.size());
        data.buttons = buttons.data();
        int chosen = -1;
        if (!SDL_ShowMessageBox(&data, &chosen) || chosen < 0)
        {
            fprintf(stderr, "[xam] diálogo indisponível (%s): usando o botão padrão\n", SDL_GetError());
            return active;
        }
        return uint32_t(chosen);
    }

    uint32_t MessageBox(uint32_t title, uint32_t text, uint32_t count, uint32_t buttons, uint32_t active,
        uint32_t pResult, uint32_t overlapped)
    {
        std::string titleText = Utf16Guest(title), bodyText = Utf16Guest(text);
        std::vector<std::string> labels;
        fprintf(stderr, "[xam] caixa de mensagem: \"%s\" — %s\n", titleText.c_str(), bodyText.c_str());
        for (uint32_t i = 0; i < count && i < 3; i++)
        {
            labels.push_back(Utf16Guest(kernel::Load32(buttons + 4 * i)));
            fprintf(stderr, "[xam]   botão %u: %s%s\n", i, labels.back().c_str(), i == active ? " (padrão)" : "");
        }
        Broadcast(XN_SYS_UI, 1);
        uint32_t choice = AskPlayer(titleText, bodyText, labels, active);
        fprintf(stderr, "[xam]   resposta: botão %u\n", choice);
        if (pResult)
            kernel::Store32(pResult, choice);
        Broadcast(XN_SYS_UI, 0);
        if (overlapped)
        {
            kernel::CompleteOverlapped(overlapped, 0);
            return 997;   // ERROR_IO_PENDING
        }
        return 0;
    }
}

// uint32 XamShowMessageBoxUI(uint32 UserIndex, const wchar16* Title, const wchar16* Text, uint32 ButtonCount,
//                            const wchar16** Buttons, uint32 FocusButton, uint32 Flags, uint32* Result, XOVERLAPPED*)
KERNEL_IMPL(XamShowMessageBoxUI)
{
    uint32_t overlapped = kernel::Load32(ctx.r1.u32 + 0x54);   // 9º argumento, na pilha
    ctx.r3.u64 = MessageBox(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r10.u32, overlapped);
}

// uint32 XamShowMessageBoxUIEx(UserIndex, Title, Text, ButtonCount, Buttons, FocusButton, Flags, Unknown,
//                              uint32* Result, XOVERLAPPED*): resultado e XOVERLAPPED na pilha
KERNEL_IMPL(XamShowMessageBoxUIEx)
{
    uint32_t pResult = kernel::Load32(ctx.r1.u32 + 0x54), overlapped = kernel::Load32(ctx.r1.u32 + 0x5C);
    ctx.r3.u64 = MessageBox(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, pResult, overlapped);
}

// uint32 XamShowSigninUI(uint32 Panes, uint32 Flags): a tela do sistema abre e fecha na hora
KERNEL_IMPL(XamShowSigninUI)
{
    Broadcast(XN_SYS_UI, 1);
    Broadcast(XN_SYS_SIGNINCHANGED, 1);
    Broadcast(XN_SYS_UI, 0);
    ctx.r3.u64 = 0;
}
