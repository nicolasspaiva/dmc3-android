// Entrada do XAM (XamInput*). O jogador 1 está sempre conectado: o teclado funciona como controle
// mesmo sem controle físico. Os outros jogadores aparecem desconectados.
#include "input.h"
#include "kernel.h"
#include "style_switch.h"

#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint32_t ERROR_SUCCESS = 0;
    constexpr uint32_t ERROR_DEVICE_NOT_CONNECTED = 0x48F;

    void Store16(uint32_t address, uint16_t value)
    {
        *kernel::Guest<uint16_t>(address) = __builtin_bswap16(value);
    }

    // XINPUT_GAMEPAD { u16 wButtons; u8 bLeftTrigger, bRightTrigger; s16 sThumbLX, sThumbLY, sThumbRX, sThumbRY }
    void StoreGamepad(uint32_t address, const input::Gamepad& g)
    {
        Store16(address + 0, g.buttons);
        *kernel::Guest<uint8_t>(address + 2) = g.leftTrigger;
        *kernel::Guest<uint8_t>(address + 3) = g.rightTrigger;
        Store16(address + 4, uint16_t(g.thumbLX));
        Store16(address + 6, uint16_t(g.thumbLY));
        Store16(address + 8, uint16_t(g.thumbRX));
        Store16(address + 10, uint16_t(g.thumbRY));
    }

    // XINPUT_CAPABILITIES { u8 Type, SubType; u16 Flags; XINPUT_GAMEPAD Gamepad; XINPUT_VIBRATION { u16, u16 } }
    void StoreCapabilities(uint32_t address)
    {
        memset(kernel::Guest<uint8_t>(address), 0, 20);
        *kernel::Guest<uint8_t>(address + 0) = 1;   // XINPUT_DEVTYPE_GAMEPAD
        *kernel::Guest<uint8_t>(address + 1) = 1;   // XINPUT_DEVSUBTYPE_GAMEPAD
        input::Gamepad all;
        all.buttons = 0xF3FF;
        all.leftTrigger = all.rightTrigger = 0xFF;
        all.thumbLX = all.thumbLY = all.thumbRX = all.thumbRY = int16_t(0xFFC0);
        StoreGamepad(address + 4, all);
        Store16(address + 16, 0xFFFF);
        Store16(address + 18, 0xFFFF);
    }
}

// uint32 XamInputGetState(uint32 UserIndex, uint32 Flags, XINPUT_STATE* State)
// XINPUT_STATE { u32 dwPacketNumber; XINPUT_GAMEPAD Gamepad }
KERNEL_IMPL(XamInputGetState)
{
    uint32_t user = ctx.r3.u32, state = ctx.r5.u32;
    static int logged = 0;
    if (getenv("DMC3_LOG_INPUT") && logged++ < 12)
        fprintf(stderr, "[input] XamInputGetState(jogador %u, flags %X, estado %08X), retorno em %08X\n", user, ctx.r4.u32,
            state, uint32_t(ctx.lr));
    if (user != 0 && user != 0xFF)   // 0xFF = qualquer jogador
    {
        ctx.r3.u64 = ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    uint32_t packet;
    input::Gamepad g = input::State(&packet);
    g.buttons = style::FilterButtons(g.buttons);
    if (state)
    {
        kernel::Store32(state, packet);
        StoreGamepad(state + 4, g);
    }
    ctx.r3.u64 = ERROR_SUCCESS;
}

// uint32 XamInputGetCapabilities(uint32 UserIndex, uint32 Flags, XINPUT_CAPABILITIES*)
KERNEL_IMPL(XamInputGetCapabilities)
{
    if (ctx.r3.u32 != 0 && ctx.r3.u32 != 0xFF)
    {
        ctx.r3.u64 = ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    if (ctx.r5.u32)
        StoreCapabilities(ctx.r5.u32);
    ctx.r3.u64 = ERROR_SUCCESS;
}

// uint32 XamInputGetCapabilitiesEx(uint32 Version, uint32 UserIndex, uint32 Flags, XINPUT_CAPABILITIES*)
KERNEL_IMPL(XamInputGetCapabilitiesEx)
{
    if (ctx.r4.u32 != 0 && ctx.r4.u32 != 0xFF)
    {
        ctx.r3.u64 = ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    if (ctx.r6.u32)
        StoreCapabilities(ctx.r6.u32);
    ctx.r3.u64 = ERROR_SUCCESS;
}

// uint32 XamInputSetState(uint32 UserIndex, uint32 Flags, XINPUT_VIBRATION*): vibração ainda ignorada
KERNEL_IMPL(XamInputSetState)
{
    ctx.r3.u64 = ctx.r3.u32 == 0 || ctx.r3.u32 == 0xFF ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
